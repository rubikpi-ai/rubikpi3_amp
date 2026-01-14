#include <type.h>
#include <io.h>
#include <uart_geni.h>
#include <gcc_sc7280.h>
#include <asm/gpio.h>
#include <gcc_uart2_clk.h>

/* fallback */
#ifndef UART_TX_FIFO_DEPTH_WORDS_DEFAULT
#define UART_TX_FIFO_DEPTH_WORDS_DEFAULT 16U
#endif

static inline u32 r32(u64 off) { return readl((u32)(UART2_BASE + off)); }
static inline void w32(u64 off, u32 v) { writel(v, (u32)(UART2_BASE + off)); }

/* --- GENI/SE regs --- */
#define GENI_FORCE_DEFAULT_REG      0x20
#define GENI_OUTPUT_CTRL            0x24
#define SE_GENI_STATUS              0x40
#define GENI_SER_M_CLK_CFG          0x48
#define GENI_SER_S_CLK_CFG          0x4c
#define GENI_IF_DISABLE_RO          0x64
#define GENI_FW_REVISION_RO         0x68
#define SE_GENI_CLK_SEL             0x80
#define SE_GENI_CFG_SEQ_START       0x84
#define SE_GENI_DMA_MODE_EN         0x258

#define SE_GENI_M_CMD0              0x600
#define SE_GENI_M_CMD_CTRL_REG      0x604
#define SE_GENI_M_IRQ_STATUS        0x610
#define SE_GENI_M_IRQ_CLEAR         0x618

#define SE_GENI_TX_FIFOn            0x700
#define SE_GENI_TX_FIFO_STATUS      0x800
#define TX_FIFO_WC_MASK             0x0FFFFFFF

#define SE_HW_PARAM_0               0xe24
#define SE_HW_PARAM_1               0xe28

/* UART regs */
#define SE_UART_TX_TRANS_CFG        0x25c
#define SE_UART_TX_WORD_LEN         0x268
#define SE_UART_TX_STOP_BIT_LEN     0x26c
#define SE_UART_TX_TRANS_LEN        0x270
#define SE_UART_TX_PARITY_CFG       0x2a4
#define SE_UART_RX_TRANS_CFG        0x280
#define SE_UART_RX_WORD_LEN         0x28c
#define SE_UART_RX_STALE_CNT        0x294
#define SE_UART_RX_PARITY_CFG       0x2a8

/* bits */
#define FORCE_DEFAULT               (1U << 0)
#define GENI_IO_MUX_0_EN            (1U << 0)
#define START_TRIGGER               (1U << 0)

#define FW_REV_PROTOCOL_MSK         (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT        8
#define GENI_SE_UART                2

#define SER_CLK_EN                  (1U << 0)
#define CLK_DIV_SHFT                4

#define M_GENI_CMD_ABORT            (1U << 1)
#define M_GENI_CMD_CANCEL           (1U << 2)

#define M_CMD_DONE_EN               (1U << 0)

#define UART_START_TX               0x1
#define M_OPCODE_SHFT               27

/* UART cfg bits from Linux */
#define UART_TX_PAR_EN              (1U << 0)
#define UART_CTS_MASK               (1U << 1)
#define UART_RX_PAR_EN              (1U << 3)
#define TX_STOP_BIT_LEN_1           0

/* Packing + WM regs (from linux geni-se.h, common for QUPv3) */
#define SE_GENI_TX_PACKING_CFG0     0x260
#define SE_GENI_TX_PACKING_CFG1     0x264
#define SE_GENI_RX_PACKING_CFG0     0x284
#define SE_GENI_RX_PACKING_CFG1     0x288

#define SE_GENI_TX_WATERMARK_REG    0x80c
#define SE_GENI_RX_WATERMARK_REG    0x810

/* Common packing fields: we want 8-bit, 4 bytes per word packing */
#define PACK_EN                     (1U << 0)
#define BYTE_SWAP                   (1U << 2)

/* ---------- delay/poll ---------- */
static void udelay_local(u32 us)
{
	for (volatile u32 i = 0; i < us * 200; i++)
		__asm__ volatile("nop");
}

static int poll_m_done(u32 timeout_us)
{
	while (timeout_us--) {
		if (r32(SE_GENI_M_IRQ_STATUS) & M_CMD_DONE_EN)
			return 0;
		udelay_local(1);
	}
	return -1;
}

static u32 uart2_tx_fifo_depth_words(void)
{
	u32 hp0 = r32(SE_HW_PARAM_0);
	u32 depth = (hp0 >> 16) & 0x3F;
	if (!depth) depth = UART_TX_FIFO_DEPTH_WORDS_DEFAULT;
	return depth;
}

/* ---------- low-level setup ---------- */
static void uart2_force_cfg_trigger(void)
{
	w32(GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
	u32 out = r32(GENI_OUTPUT_CTRL);
	out |= GENI_IO_MUX_0_EN;
	w32(GENI_OUTPUT_CTRL, out);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
}

static void uart2_cancel_abort(void)
{
	/*
	 * IMPORTANT:
	 * Current MCC/SCC bit definitions are not confirmed. Writing wrong bits
	 * leaves MCC at 0x28000000 (observed) and breaks TX state machine.
	 *
	 * Until SE_GENI_*_CMD_CTRL_REG bitfields are verified, avoid writing them.
	 */

	/* Clear IRQ status (safe) */
	w32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFF);
	w32(SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFF);

	/* Force default resets internal state machines (safe) */
	w32(GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
	udelay_local(10);
	w32(GENI_FORCE_DEFAULT_REG, 0);
	udelay_local(10);

	/* Trigger cfg seq */
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
	udelay_local(10);
}

static int uart2_check_uart_proto(void)
{
	u32 proto = (r32(GENI_FW_REVISION_RO) & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
	return (proto == GENI_SE_UART) ? 0 : -1;
}

static void uart2_set_8n1_no_fc(void)
{
	u32 txcfg = UART_CTS_MASK;
	txcfg &= ~UART_TX_PAR_EN;

	w32(SE_UART_TX_TRANS_CFG, txcfg);
	w32(SE_UART_TX_PARITY_CFG, 0);
	w32(SE_UART_RX_TRANS_CFG, 0 & ~UART_RX_PAR_EN);
	w32(SE_UART_RX_PARITY_CFG, 0);

	w32(SE_UART_TX_WORD_LEN, 8);
	w32(SE_UART_RX_WORD_LEN, 8);
	w32(SE_UART_TX_STOP_BIT_LEN, TX_STOP_BIT_LEN_1);
	w32(SE_UART_RX_STALE_CNT, 0);
}

/* -------- Linux-style set_rate (关键修复) -------- */
#ifndef QUP_SE_VERSION_2_5
#define QUP_SE_VERSION_2_5 0x2500
#endif

//static int uart2_set_baud_linux_style(u32 baud)
//{
//	u32 sampling = UART_OVERSAMPLING_DEFAULT; /* 32 */
//	u32 ver = qup_wrap0_hw_version();
//	if (ver >= QUP_SE_VERSION_2_5)
//		sampling >>= 1; /* 16 */
//
//	u32 req = baud * sampling;
//
//	u32 clk_idx = 0, clk_rate = 0;
//	if (gcc_uart2_se_clk_config_for_req(req, &clk_idx, &clk_rate) != 0)
//		return -1;
//
//	u32 clk_div = (clk_rate + req - 1U) / req;
//	if (!clk_div) clk_div = 1;
//
//	u32 ser_clk_cfg = SER_CLK_EN | (clk_div << CLK_DIV_SHFT);
//
//	w32(GENI_SER_M_CLK_CFG, ser_clk_cfg);
//	w32(GENI_SER_S_CLK_CFG, ser_clk_cfg);
//	w32(SE_GENI_CLK_SEL, clk_idx & CLK_SEL_MSK);
//	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
//
//	return 0;
//}
//
int gcc_uart2_se_clk_set_rate_idx(u32 idx);
static int uart2_set_baud_linux_style(u32 baud)
{
	/* force sampling=32 for now to match 7.3728 -> /2 exact */
	u32 sampling = 32;
	u32 req = baud * sampling;

	/* Force GCC clock to 7.3728MHz entry (idx=0 in your ftbl) */
	/* 你需要在 gcc_uart2_clk.c 里暴露一个 set_rate_idx(0) 的函数，
	   如果目前没有，就先临时在 uart_geni.c 里直接写 RCG2（不推荐长期） */
	extern int gcc_uart2_se_clk_set_rate_idx(u32 idx);
	gcc_uart2_se_clk_set_rate_idx(0);

	/* Ensure branch enabled (你现在用 gcc_enable_uart2_clocks 也行，但更推荐在 gcc_uart2 内部做) */
	gcc_enable_uart2_clocks();

	u32 clk_rate = 7372800;
	u32 clk_idx = 0;

	u32 clk_div = (clk_rate + req - 1U) / req; /* should be 2 */
	u32 ser_clk_cfg = SER_CLK_EN | (clk_div << CLK_DIV_SHFT); /* expect 0x21 */

	w32(GENI_SER_M_CLK_CFG, ser_clk_cfg);
	w32(GENI_SER_S_CLK_CFG, ser_clk_cfg);

	/* IMPORTANT: CLK_SEL now at 0x80 */
	w32(SE_GENI_CLK_SEL, clk_idx & CLK_SEL_MSK);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);

	return 0;
}

/* ---------- TX ---------- */
static int uart2_tx_bytes(const u8 *p, u32 len)
{
	if (uart2_check_uart_proto() != 0) return -10;
	if (r32(GENI_IF_DISABLE_RO) & 0x1) return -11;

	/* force FIFO (DMA off) */
	if (r32(SE_GENI_DMA_MODE_EN) != 0) {
		w32(SE_GENI_DMA_MODE_EN, 0);
		w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
	}

	u32 mis = r32(SE_GENI_M_IRQ_STATUS);
	if (mis) w32(SE_GENI_M_IRQ_CLEAR, mis);

	w32(SE_UART_TX_TRANS_LEN, len);
	w32(SE_GENI_M_CMD0, (UART_START_TX << M_OPCODE_SHFT));

	u32 depth = uart2_tx_fifo_depth_words();
	u32 left = len;

	while (left) {
		u32 wc = r32(SE_GENI_TX_FIFO_STATUS) & TX_FIFO_WC_MASK;
		if (wc >= (depth - 1))
			continue;

		w32(SE_GENI_TX_FIFOn, *p++);
		left--;
	}

	if (poll_m_done(200000) != 0)
		return -14;

	w32(SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
	return 0;
}

void uart2_putc(char c)
{
	(void)uart2_tx_bytes((const u8 *)&c, 1);
}

void uart2_puts(const char *s)
{
	while (s && *s) {
		if (*s == '\n') uart2_putc('\r');
		uart2_putc(*s++);
	}
}

int uart2_write(const void *buf, u32 len)
{
	return uart2_tx_bytes((const u8*)buf, len);
}

static void uart2_geni_full_reset(void)
{
	/* Clear IRQs */
	w32(SE_GENI_M_IRQ_CLEAR, 0xFFFFFFFF);
	w32(SE_GENI_S_IRQ_CLEAR, 0xFFFFFFFF);

	/* Do NOT write SE_GENI_{M,S}_CMD_CTRL_REG until bitfields are confirmed */

	/* Force default */
	w32(GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
	udelay_local(10);
	w32(GENI_FORCE_DEFAULT_REG, 0);
	udelay_local(10);

	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
	udelay_local(10);
}

static void uart2_geni_packing_init(void)
{
	/* Only set watermarks; packing cfg offsets not yet verified */
	w32(SE_GENI_TX_WATERMARK_REG, 1);
	w32(SE_GENI_RX_WATERMARK_REG, 1);
}



/* Add near top with other macros */
#define GCC_BASE_LOCAL 0x00100000UL
#define GCC_S2_ENABLE_REG 0x52008
#define GCC_S2_CLK_SRC_CMD_RCGR 0x17270

static inline u32 gcc_r32_local(u32 off)
{
	return readl((u32)(GCC_BASE_LOCAL + off));
}

int uart2_init(unsigned int baud, unsigned long src_clk_hz, unsigned int clk_sel)
{
	(void)src_clk_hz;
	(void)clk_sel;

	gpio_pinmux_set(10, mux_qup02);
	gpio_pinmux_set(11, mux_qup02);

	gcc_enable_uart2_clocks();

	uart2_geni_full_reset();
	uart2_geni_packing_init();

	uart2_set_8n1_no_fc();

	/* 4) 最后设波特率（会调用 RCG2 set_rate + enable branch + GENI divider/clk_sel） */
	if (uart2_set_baud_linux_style(baud ? baud : 115200) != 0)
		return -1;

	return 0;
}

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

static void uart2_dump_window_0x70_0x90(volatile u64 *shm, u32 *idx)
{
	for (u32 off = 0x70; off <= 0x90; off += 4) {
		shm_put(shm, idx, "W70 ", off, r32(off));
	}
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

		/* ---- GCC side sanity ---- */
	shm_put(shm, idx, "GEn ", GCC_S2_ENABLE_REG,         gcc_r32_local(GCC_S2_ENABLE_REG));
	shm_put(shm, idx, "GCc ", GCC_S2_CLK_SRC_CMD_RCGR+0, gcc_r32_local(GCC_S2_CLK_SRC_CMD_RCGR+0));
	shm_put(shm, idx, "GCf ", GCC_S2_CLK_SRC_CMD_RCGR+4, gcc_r32_local(GCC_S2_CLK_SRC_CMD_RCGR+4));
	shm_put(shm, idx, "GCm ", GCC_S2_CLK_SRC_CMD_RCGR+8, gcc_r32_local(GCC_S2_CLK_SRC_CMD_RCGR+8));
	shm_put(shm, idx, "GCn ", GCC_S2_CLK_SRC_CMD_RCGR+12,gcc_r32_local(GCC_S2_CLK_SRC_CMD_RCGR+12));
	shm_put(shm, idx, "GCd ", GCC_S2_CLK_SRC_CMD_RCGR+16,gcc_r32_local(GCC_S2_CLK_SRC_CMD_RCGR+16));

	uart2_dump_window_0x70_0x90(shm, idx);
}

static void uart2_probe_clk_sel_offsets(volatile u64 *shm, u32 *idx, u32 clk_idx)
{
	static const u32 cand[] = { 0x7c, 0x80, 0x88, 0x8c, 0x90 };
	for (u32 i = 0; i < sizeof(cand)/sizeof(cand[0]); i++) {
		u32 off = cand[i];
		w32(off, clk_idx & 0x7);
		w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
		shm_put(shm, idx, "PSL ", off, r32(off));
	}
}

void uart2_debug_dump_and_try_tx(volatile u64 *shm, u32 shm_base_idx, const char *msg)
{
	u32 idx = shm_base_idx;

	shm[idx++] = 0x554152543247454EULL; /* "UART2GEN" */
	shm[idx++] = UART2_BASE;

	shm[idx++] = 0x4245464F52450001ULL; /* BEFORE */
	uart2_dump_regs(shm, &idx);

	/* Apply recovery + cfg trigger before TX */
	//uart2_cancel_abort();
//	uart2_force_cfg_trigger();

	shm[idx++] = 0x5052455000000000ULL; /* "PREP" */
	uart2_probe_clk_sel_offsets(shm, &idx, 3); // 用一个明显的 idx 值
	uart2_dump_regs(shm, &idx);

	/* Try transmit */
	u32 len = 0;
	while (msg[len] && len < 256) len++;
	int rc = uart2_write(msg, len);

	shm[idx++] = 0x5243000000000000ULL | (u32)(rc & 0xFFFF); /* "RC" */
	shm[idx++] = (u64)(u32)rc;

	shm[idx++] = 0x4146544552000002ULL; /* AFTER */
	uart2_dump_regs(shm, &idx);

	shm[idx++] = 0x454e440000000000ULL; /* END */
}
