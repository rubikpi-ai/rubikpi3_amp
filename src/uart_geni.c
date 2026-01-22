#include <type.h>
#include <uart_geni.h>
#include <asm/gpio.h>
#include <clock.h>
#include <clock_qcs6490.h>

#define BITS_PER_BYTE		8
#define BYTES_PER_FIFO_WORD		4U

#define GENMASK GENMASK_ULL
/* ---------------- MMIO helpers ---------------- */
static inline u32 r32(u64 off) { return *(volatile u32 *)(UART2_BASE + off); }
static inline void w32(u64 off, u32 v) { *(volatile u32 *)(UART2_BASE + off) = v; }

static inline void writel_relaxed(u32 val, u64 addr)
{
	*(volatile u32 *)addr = val;
}

static u32 readl_relaxed(u64 addr)
{
	return *(volatile u32 *)addr;
}

static inline u32 pack4(const u8 *p, u32 n)
{
	u32 w = 0;
	for (u32 i = 0; i < n; i++)
		w |= ((u32)p[i]) << (8U * i);
	return w;
}

/*
 * Read TX fifo depth (words) from HW param if possible.
 * If it returns 0, fall back to a safe default.
 *
 * Note: field layout depends on QUP hw version; but in your dump HP0 looked valid.
 * We use the common mask GENMASK(21,16) shifted by 16 (good for many versions).
 */
static u32 uart2_tx_fifo_depth_words(void)
{
	u32 hp0 = r32(SE_HW_PARAM_0);
	u32 depth = (hp0 >> 16) & 0x3F; /* GENMASK(21,16) */
	if (depth == 0)
		depth = UART_TX_FIFO_DEPTH_WORDS_DEFAULT;
	return depth;
}

static void uart2_clear_m_irqs(void)
{
	u32 st = r32(SE_GENI_M_IRQ_STATUS);
	if (st)
		w32(SE_GENI_M_IRQ_CLEAR, st);
}

static void uart2_force_cfg_trigger(void)
{
	/* Force default to kick hardware into a sane state */
	w32(GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);

	/* Ensure output mux enabled (often required to drive pins) */
	u32 out = r32(GENI_OUTPUT_CTRL);
	out |= GENI_IO_MUX_0_EN;
	w32(GENI_OUTPUT_CTRL, out);

	/* Trigger config sequence */
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
}

static void uart2_cancel_abort(void)
{
	/*
	 * IMPORTANT:
	 * Do NOT write SE_GENI_{M,S}_CMD_CTRL_REG here.
	 * We previously observed that writing wrong bits can leave MCC at 0x28000000
	 * and break TX state machine (timeouts + garbled output).
	 *
	 * Safe recovery sequence:
	 *  - clear IRQ status
	 *  - FORCE_DEFAULT pulse
	 *  - trigger config sequence
	 */

	/* clear any pending irqs */
	w32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFF);
	w32(SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFF);

	/* reset internal state machines */
	w32(GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
	/* small delay */
	for (volatile u32 i = 0; i < 1000; i++) __asm__ volatile("nop");
	w32(GENI_FORCE_DEFAULT_REG, 0);
	for (volatile u32 i = 0; i < 1000; i++) __asm__ volatile("nop");

	/* apply config */
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
}

/* ---------------- public API ---------------- */

int uart2_getc_nonblock(void)
{
	u32 wc = r32(SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MASK;
	if (!wc)
		return -1;
	return (int)(r32(SE_GENI_RX_FIFOn) & 0xFF);
}

int uart2_write(const void *buf, u32 len)
{
	const u8 *p = (const u8 *)buf;
	u32 proto = (r32(GENI_FW_REVISION_RO) & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;

	/* If protocol isn't UART, don't try to send (will never work) */
	if (proto != GENI_SE_UART)
		return -10;

	/* Make sure FIFO interface isn't disabled */
	if (r32(GENI_IF_DISABLE_RO) & 0x1) /* FIFO_IF_DISABLE */
		return -11;

	/* Program length and kick START_TX */
	w32(SE_UART_TX_TRANS_LEN, len);
	w32(SE_GENI_M_CMD0, (UART_START_TX << M_OPCODE_SHFT));

	/* Push data with FIFO-depth based flow control */
	u32 depth = uart2_tx_fifo_depth_words();
	u32 left = len;

	for (u32 guard = 0; left && guard < 2000000; guard++) {
		u32 wc = r32(SE_GENI_TX_FIFO_STATUS) & TX_FIFO_WC_MASK;

		/* Keep at least 1 word headroom */
		if (wc >= (depth ? (depth - 1) : UART_TX_FIFO_DEPTH_WORDS_DEFAULT))
			continue;

		u32 chunk = (left >= 4) ? 4 : left;
		w32(SE_GENI_TX_FIFOn, pack4(p, chunk));
		p += chunk;
		left -= chunk;
	}

	if (left) {
		/* couldn't drain all bytes into FIFO */
		return -12;
	}

	/* Wait for completion: prefer CMD_DONE; also watch ACTIVE */
	for (u32 guard = 0; guard < 3000000; guard++) {
		u32 irq = r32(SE_GENI_M_IRQ_STATUS);
		if (irq & M_CMD_DONE_EN) {
			w32(SE_GENI_M_IRQ_CLEAR, irq);
			return 0;
		}

		/* If engine never becomes active, it likely ignored the command */
		u32 st = r32(SE_GENI_STATUS);
		if (!(st & M_GENI_CMD_ACTIVE)) {
			/* Not active and not done -> treat as failure */
			/* (Some HW may be very fast; but then CMD_DONE should have latched) */
			if (guard > 1000)
				return -13;
		}
	}

	return -14; /* timeout */
}

void uart2_putc(char c)
{
	(void)uart2_write(&c, 1);
}

void uart2_puts(const char *s)
{
	/* Send in chunks to keep TX_TRANS_LEN reasonable */
	while (*s) {
		char tmp[64];
		u32 n = 0;

		while (n < sizeof(tmp) && s[n]) {
			if (s[n] == '\n')
				break;
			tmp[n++] = s[n];
		}

		if (n) {
			(void)uart2_write(tmp, n);
			s += n;
		}

		if (*s == '\n') {
			const char crlf[2] = {'\r','\n'};
			(void)uart2_write(crlf, 2);
			s++;
		}
	}
}

/* ---------------- debug dump (to SHM) ---------------- */

static void shm_put(volatile u64 *shm, u32 *idx, const char *tag, u32 off, u32 val)
{
	u32 t = 0;
	for (int i = 0; i < 4; i++) {
		char c = tag[i] ? tag[i] : ' ';
		t |= ((u32)(u8)c) << (8 * i);
	}
	shm[(*idx)++] = ((u64)t << 32) | (u64)off;
	shm[(*idx)++] = (u64)val;
}

static void uart2_dump_regs(volatile u64 *shm, u32 *idx)
{
	shm_put(shm, idx, "FW  ", GENI_FW_REVISION_RO,    r32(GENI_FW_REVISION_RO));
	shm_put(shm, idx, "IFD ", GENI_IF_DISABLE_RO,     r32(GENI_IF_DISABLE_RO));
	shm_put(shm, idx, "OUT ", GENI_OUTPUT_CTRL,       r32(GENI_OUTPUT_CTRL));
	shm_put(shm, idx, "CFG ", SE_GENI_CFG_SEQ_START,  r32(SE_GENI_CFG_SEQ_START));
	shm_put(shm, idx, "STA ", SE_GENI_STATUS,         r32(SE_GENI_STATUS));
	shm_put(shm, idx, "CLKM", GENI_SER_M_CLK_CFG,     r32(GENI_SER_M_CLK_CFG));
	shm_put(shm, idx, "CLKS", GENI_SER_S_CLK_CFG,     r32(GENI_SER_S_CLK_CFG));
	shm_put(shm, idx, "SEL ", SE_GENI_CLK_SEL,        r32(SE_GENI_CLK_SEL));
	shm_put(shm, idx, "DMA ", SE_GENI_DMA_MODE_EN,    r32(SE_GENI_DMA_MODE_EN));

	shm_put(shm, idx, "TXFS", SE_GENI_TX_FIFO_STATUS, r32(SE_GENI_TX_FIFO_STATUS));
	shm_put(shm, idx, "RXFS", SE_GENI_RX_FIFO_STATUS, r32(SE_GENI_RX_FIFO_STATUS));

	shm_put(shm, idx, "MC0 ", SE_GENI_M_CMD0,         r32(SE_GENI_M_CMD0));
	shm_put(shm, idx, "MCC ", SE_GENI_M_CMD_CTRL_REG, r32(SE_GENI_M_CMD_CTRL_REG));
	shm_put(shm, idx, "MEN ", SE_GENI_M_IRQ_EN,       r32(SE_GENI_M_IRQ_EN));
	shm_put(shm, idx, "MIS ", SE_GENI_M_IRQ_STATUS,   r32(SE_GENI_M_IRQ_STATUS));

	shm_put(shm, idx, "SC0 ", SE_GENI_S_CMD0,         r32(SE_GENI_S_CMD0));
	shm_put(shm, idx, "SCC ", SE_GENI_S_CMD_CTRL_REG, r32(SE_GENI_S_CMD_CTRL_REG));
	shm_put(shm, idx, "SEN ", SE_GENI_S_IRQ_EN,       r32(SE_GENI_S_IRQ_EN));
	shm_put(shm, idx, "SIS ", SE_GENI_S_IRQ_STATUS,   r32(SE_GENI_S_IRQ_STATUS));

	shm_put(shm, idx, "HP0 ", SE_HW_PARAM_0,          r32(SE_HW_PARAM_0));
	shm_put(shm, idx, "HP1 ", SE_HW_PARAM_1,          r32(SE_HW_PARAM_1));

	shm_put(shm, idx, "TXCF", SE_UART_TX_TRANS_CFG,   r32(SE_UART_TX_TRANS_CFG));
	shm_put(shm, idx, "TXWL", SE_UART_TX_WORD_LEN,    r32(SE_UART_TX_WORD_LEN));
	shm_put(shm, idx, "TXSB", SE_UART_TX_STOP_BIT_LEN,r32(SE_UART_TX_STOP_BIT_LEN));
	shm_put(shm, idx, "TXPC", SE_UART_TX_PARITY_CFG,  r32(SE_UART_TX_PARITY_CFG));
	shm_put(shm, idx, "TXLN", SE_UART_TX_TRANS_LEN,   r32(SE_UART_TX_TRANS_LEN));

	shm_put(shm, idx, "RXCF", SE_UART_RX_TRANS_CFG,   r32(SE_UART_RX_TRANS_CFG));
	shm_put(shm, idx, "RXWL", SE_UART_RX_WORD_LEN,    r32(SE_UART_RX_WORD_LEN));
	shm_put(shm, idx, "RXPC", SE_UART_RX_PARITY_CFG,  r32(SE_UART_RX_PARITY_CFG));
	shm_put(shm, idx, "RXST", SE_UART_RX_STALE_CNT,   r32(SE_UART_RX_STALE_CNT));

	shm_put(shm, idx, "CFG0", SE_GENI_TX_PACKING_CFG0,   r32(SE_GENI_TX_PACKING_CFG0));
	shm_put(shm, idx, "GRAN", SE_GENI_BYTE_GRAN,   r32(SE_GENI_BYTE_GRAN));

	shm_put(shm, idx, "CGC ", SE_GENI_CGC_CTRL, r32(SE_GENI_CGC_CTRL));
	shm_put(shm, idx, "IOS ", SE_GENI_IOS,      r32(SE_GENI_IOS));

	shm_put(shm, idx, "QVER", QUPV3_HW_VER_REG, readl_relaxed(QUPV3_WRAP0_BASE + QUPV3_HW_VER_REG));
}

#define STALE_TIMEOUT			16
#define DEFAULT_BITS_PER_CHAR		10

static void uart2_set_8n1_no_fc(void)
{
	/* Truly disable flow-control: do NOT set UART_CTS_MASK */
	w32(SE_UART_TX_TRANS_CFG, 0);        /* no parity, no CTS */
	w32(SE_UART_TX_PARITY_CFG, 0);

	w32(SE_UART_RX_TRANS_CFG, 0);        /* no parity */
	w32(SE_UART_RX_PARITY_CFG, 0);

	w32(SE_UART_TX_WORD_LEN, 8);
	w32(SE_UART_RX_WORD_LEN, 8);

	w32(SE_UART_TX_STOP_BIT_LEN, TX_STOP_BIT_LEN_1);

	/* stale timeout: keep as before */
	w32(SE_UART_RX_STALE_CNT, DEFAULT_BITS_PER_CHAR * STALE_TIMEOUT);
}

static void geni_se_io_set_mode(u32 base)
{
	u32 val;

	val = readl_relaxed(base + SE_IRQ_EN);
	val |= GENI_M_IRQ_EN | GENI_S_IRQ_EN;
	val |= DMA_TX_IRQ_EN | DMA_RX_IRQ_EN;
	writel_relaxed(val, base + SE_IRQ_EN);

	val = readl_relaxed(base + SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	writel_relaxed(val, base + SE_GENI_DMA_MODE_EN);

	writel_relaxed(0, base + SE_GSI_EVENT_EN);
}

static void geni_se_io_init(u32 base)
{
	u32 val;

	val = readl_relaxed(base + SE_GENI_CGC_CTRL);
	val |= DEFAULT_CGC_EN;
	writel_relaxed(val, base + SE_GENI_CGC_CTRL);

	val = readl_relaxed(base + SE_DMA_GENERAL_CFG);
	val |= AHB_SEC_SLV_CLK_CGC_ON | DMA_AHB_SLV_CLK_CGC_ON;
	val |= DMA_TX_CLK_CGC_ON | DMA_RX_CLK_CGC_ON;
	writel_relaxed(val, base + SE_DMA_GENERAL_CFG);

	writel_relaxed(DEFAULT_IO_OUTPUT_CTRL_MSK, base + GENI_OUTPUT_CTRL);
	writel_relaxed(FORCE_DEFAULT, base + GENI_FORCE_DEFAULT_REG);
}

static void geni_se_irq_clear(u32 base)
{
	writel_relaxed(0, base + SE_GSI_EVENT_EN);
	writel_relaxed(0xffffffff, base + SE_GENI_M_IRQ_CLEAR);
	writel_relaxed(0xffffffff, base + SE_GENI_S_IRQ_CLEAR);
	writel_relaxed(0xffffffff, base + SE_DMA_TX_IRQ_CLR);
	writel_relaxed(0xffffffff, base + SE_DMA_RX_IRQ_CLR);
	writel_relaxed(0xffffffff, base + SE_IRQ_EN);
}

void geni_se_init(u32 rx_wm, u32 rx_rfr)
{
	u32 val;
	u32 base = UART2_BASE;

	geni_se_irq_clear(base);
	geni_se_io_init(base);
	geni_se_io_set_mode(base);

	writel_relaxed(rx_wm, base + SE_GENI_RX_WATERMARK_REG);
	writel_relaxed(rx_rfr, base + SE_GENI_RX_RFR_WATERMARK_REG);

	val = readl_relaxed(base + SE_GENI_M_IRQ_EN);
	val |= M_COMMON_GENI_M_IRQ_EN;
	writel_relaxed(val, base + SE_GENI_M_IRQ_EN);

	val = readl_relaxed(base + SE_GENI_S_IRQ_EN);
	val |= S_COMMON_GENI_S_IRQ_EN;
	writel_relaxed(val, base + SE_GENI_S_IRQ_EN);
}

static inline u32 geni_se_read_proto(u32 base)
{
	u32 val;

	val = readl_relaxed(base + GENI_FW_REVISION_RO);

	return (val & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
}

static void geni_se_select_fifo_mode(u32 base)
{
	u32 proto = geni_se_read_proto(base);
	u32 val, val_old;

	geni_se_irq_clear(base);

	/* UART driver manages enabling / disabling interrupts internally */
	if (proto != GENI_SE_UART) {
		/* Non-UART use only primary sequencer so dont bother about S_IRQ */
		val_old = val = readl_relaxed(base + SE_GENI_M_IRQ_EN);
		val |= M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN;
		val |= M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
		if (val != val_old)
			writel_relaxed(val, base + SE_GENI_M_IRQ_EN);
	}

	val_old = val = readl_relaxed(base + SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	if (val != val_old)
		writel_relaxed(val, base + SE_GENI_DMA_MODE_EN);
}

#define NUM_PACKING_VECTORS 4
#define PACKING_START_SHIFT 5
#define PACKING_DIR_SHIFT 4
#define PACKING_LEN_SHIFT 1
#define PACKING_STOP_BIT BIT(0)
#define PACKING_VECTOR_SHIFT 10
void geni_se_config_packing(u32 base, int bpw, int pack_words,
			    bool msb_to_lsb, bool tx_cfg, bool rx_cfg)
{
	u32 cfg0, cfg1, cfg[NUM_PACKING_VECTORS] = {0};
	int len;
	int temp_bpw = bpw;
	int idx_start = msb_to_lsb ? bpw - 1 : 0;
	int idx = idx_start;
	int idx_delta = msb_to_lsb ? -BITS_PER_BYTE : BITS_PER_BYTE;
	int ceil_bpw = ALIGN(bpw, BITS_PER_BYTE);
	int iter = (ceil_bpw * pack_words) / BITS_PER_BYTE;
	int i;

	if (iter <= 0 || iter > NUM_PACKING_VECTORS)
		return;

	for (i = 0; i < iter; i++) {
		len = min(temp_bpw, BITS_PER_BYTE) - 1;
		cfg[i] = idx << PACKING_START_SHIFT;
		cfg[i] |= msb_to_lsb << PACKING_DIR_SHIFT;
		cfg[i] |= len << PACKING_LEN_SHIFT;

		if (temp_bpw <= BITS_PER_BYTE) {
			idx = ((i + 1) * BITS_PER_BYTE) + idx_start;
			temp_bpw = bpw;
		} else {
			idx = idx + idx_delta;
			temp_bpw = temp_bpw - BITS_PER_BYTE;
		}
	}
	cfg[iter - 1] |= PACKING_STOP_BIT;
	cfg0 = cfg[0] | (cfg[1] << PACKING_VECTOR_SHIFT);
	cfg1 = cfg[2] | (cfg[3] << PACKING_VECTOR_SHIFT);

	if (tx_cfg) {
		writel_relaxed(cfg0, base + SE_GENI_TX_PACKING_CFG0);
		writel_relaxed(cfg1, base + SE_GENI_TX_PACKING_CFG1);
	}
	if (rx_cfg) {
		writel_relaxed(cfg0, base + SE_GENI_RX_PACKING_CFG0);
		writel_relaxed(cfg1, base + SE_GENI_RX_PACKING_CFG1);
	}

	/*
	 * Number of protocol words in each FIFO entry
	 * 0 - 4x8, four words in each entry, max word size of 8 bits
	 * 1 - 2x16, two words in each entry, max word size of 16 bits
	 * 2 - 1x32, one word in each entry, max word size of 32 bits
	 * 3 - undefined
	 */
	if (pack_words || bpw == 32)
		writel_relaxed(bpw / 16, base + SE_GENI_BYTE_GRAN);

}

void uart2_init(void)
{
	volatile u64 *shm = (volatile u64 *)0xD7C00000;
	gpio_pinmux_set(10, mux_qup02);
	gpio_pinmux_set(11, mux_qup02);

	clk_enable(UART2_CLK);

	geni_se_config_packing(UART2_BASE, BITS_PER_BYTE, BYTES_PER_FIFO_WORD,
			       false, true, true);
	geni_se_init(UART_RX_WM, DEF_FIFO_DEPTH_WORDS - 2);
	geni_se_select_fifo_mode(UART2_BASE);

	uart2_cancel_abort();
	uart2_force_cfg_trigger();
	uart2_clear_m_irqs();

	w32(SE_GENI_DMA_MODE_EN, 0);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);

	//w32(SE_GENI_S_IRQ_EN, 0);
	//w32(SE_GENI_M_IRQ_EN, 0);

	uart2_set_8n1_no_fc();

	u32 tx_trans_cfg;
	u32 tx_parity_cfg = 0;	/* Disable Tx Parity */
	u32 rx_trans_cfg = 0;
	u32 rx_parity_cfg = 0;	/* Disable Rx Parity */
	u32 stop_bit_len = 0;	/* Default stop bit length - 1 bit */
	u32 bits_per_char;
	tx_trans_cfg = UART_CTS_MASK;
	bits_per_char = BITS_PER_BYTE;

	w32(SE_UART_TX_TRANS_CFG, tx_trans_cfg);
	w32(SE_UART_TX_PARITY_CFG, tx_parity_cfg);
	w32(SE_UART_RX_TRANS_CFG, rx_trans_cfg);
	w32(SE_UART_RX_PARITY_CFG, rx_parity_cfg);
	w32(SE_UART_TX_WORD_LEN, bits_per_char);
	w32(SE_UART_RX_WORD_LEN, bits_per_char);
	w32(SE_UART_TX_STOP_BIT_LEN, stop_bit_len);

	w32(SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);

	/*
	 * Hardcode the "golden" register values discovered from dumping the
	 * state after Linux configures the clock. This bypasses the incomplete
	 * baremetal clock library and guarantees the correct baud rate.
	 */
	const u32 RCG_BASE = GCC_BASE + 0x17270;
	writel_relaxed(0x2601, RCG_BASE + 0x4); /* RCG CFG: SRC=GPLL0_MAIN, PRE_DIV=7, MODE=? */
	writel_relaxed(0x180,  RCG_BASE + 0x8); /* RCG M */
	writel_relaxed(0xc476, RCG_BASE + 0xc); /* RCG N */
	writel_relaxed(0xc2f6, RCG_BASE + 0x10);/* RCG D */
	writel_relaxed(0x1,    RCG_BASE + 0x0); /* RCG CMD: UPDATE */

	/* Set the UART's internal divider. */
	w32(GENI_SER_M_CLK_CFG, 0x41);

	/* Force-disable HW flow control (CTS). */
	w32(SE_UART_TX_TRANS_CFG, 0);
	w32(SE_UART_RX_TRANS_CFG, 0);

	/* optional: ensure parity cfg still 0 */
	w32(SE_UART_TX_PARITY_CFG, 0);
	w32(SE_UART_RX_PARITY_CFG, 0);
}

void uart2_dump_baud_regs(volatile u64 *shm, u32 *idx)
{
	shm[(*idx)++] = 0xBADDEADBEEFULL;
	/* RCG registers for qupv3_wrap0_s2_clk_src at offset 0x17270 */
	shm_put(shm, idx, "RCG_CFG", 0x17274, readl_relaxed(GCC_BASE + 0x17274));
	shm_put(shm, idx, "RCG_M",   0x17278, readl_relaxed(GCC_BASE + 0x17278));
	shm_put(shm, idx, "RCG_N",   0x1727C, readl_relaxed(GCC_BASE + 0x1727C));
	shm_put(shm, idx, "RCG_D",   0x17280, readl_relaxed(GCC_BASE + 0x17280));
	/* UART's internal clock divider register */
	shm_put(shm, idx, "UART_CLK", 0x48, r32(GENI_SER_M_CLK_CFG));
	shm[(*idx)++] = 0xBADDEADBEEFULL;
}
