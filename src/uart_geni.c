#include <type.h>
#include <uart_geni.h>
#include <gcc_sc7280.h>
#include <asm/gpio.h>
#include <gcc_uart2_clk.h>

#define BITS_PER_BYTE		8
#define BYTES_PER_FIFO_WORD		4U

#define GENMASK GENMASK_ULL

/*
 * QUPv3 GENI UART2 baremetal helper (reuse Linux init).
 *
 * Assumptions:
 *  - Linux already probed/enabled serial@988000 (clocks/pd/icc/pinmux active).
 *  - Baremetal runs on CPU7 and only does polled TX/RX for logging.
 *
 * This file provides:
 *  - uart2_putc / uart2_puts / uart2_getc_nonblock
 *  - uart2_debug_dump_and_try_tx(): dumps key regs to SHM and tries to TX.
 */

/* ---------------- MMIO helpers ---------------- */
static inline u32 r32(u64 off) { return *(volatile u32 *)(UART2_BASE + off); }
static inline void w32(u64 off, u32 v) { *(volatile u32 *)(UART2_BASE + off) = v; }

static writel_relaxed(u32 val, u64 addr)
{
	*(volatile u32 *)addr = val;
}

static u32 readl_relaxed(u64 addr)
{
	return *(volatile u32 *)addr;
}

/* ---------------- Offsets/macros (from Linux geni-se.h + qcom_geni_serial.c) ---------------- */
/* Common SE registers */
#define GENI_FORCE_DEFAULT_REG      0x20
#define GENI_OUTPUT_CTRL            0x24
#define SE_GENI_STATUS              0x40
#define GENI_SER_M_CLK_CFG          0x48
#define GENI_SER_S_CLK_CFG          0x4c
#define GENI_IF_DISABLE_RO          0x64
#define GENI_FW_REVISION_RO         0x68
#define SE_GENI_CLK_SEL             0x7c
#define SE_GENI_CFG_SEQ_START       0x84
#define SE_GENI_DMA_MODE_EN         0x258

#define SE_GENI_M_CMD0              0x600
#define SE_GENI_M_CMD_CTRL_REG      0x604
#define SE_GENI_M_IRQ_STATUS        0x610
#define SE_GENI_M_IRQ_CLEAR         0x618
#define SE_GENI_M_IRQ_EN            0x614

#define SE_GENI_S_CMD0              0x630
#define SE_GENI_S_CMD_CTRL_REG      0x634
#define SE_GENI_S_IRQ_STATUS        0x640
#define SE_GENI_S_IRQ_CLEAR         0x648
#define SE_GENI_S_IRQ_EN            0x644

#define SE_GENI_TX_FIFOn            0x700
#define SE_GENI_RX_FIFOn            0x780
#define SE_GENI_TX_FIFO_STATUS      0x800
#define SE_GENI_RX_FIFO_STATUS      0x804

#define SE_HW_PARAM_0               0xe24
#define SE_HW_PARAM_1               0xe28

/* UART specific regs */
#define SE_UART_TX_TRANS_CFG        0x25c
#define SE_UART_TX_WORD_LEN         0x268
#define SE_UART_TX_STOP_BIT_LEN     0x26c
#define SE_UART_TX_TRANS_LEN        0x270
#define SE_UART_TX_PARITY_CFG       0x2a4

#define SE_UART_RX_TRANS_CFG        0x280
#define SE_UART_RX_WORD_LEN         0x28c
#define SE_UART_RX_STALE_CNT        0x294
#define SE_UART_RX_PARITY_CFG       0x2a8

/* Fields */
#define FORCE_DEFAULT               (1U << 0)
#define GENI_IO_MUX_0_EN            (1U << 0)

#define START_TRIGGER               (1U << 0)

#define FW_REV_PROTOCOL_MSK         (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT        8
#define GENI_SE_UART                2

#define M_GENI_CMD_ACTIVE           (1U << 0)
#define S_GENI_CMD_ACTIVE           (1U << 12)

#define SER_CLK_EN                  (1U << 0)

/* M_CMD_CTRL bits */
#define M_GENI_DISABLE              (1U << 0)
#define M_GENI_CMD_ABORT            (1U << 1)
#define M_GENI_CMD_CANCEL           (1U << 2)

/* S_CMD_CTRL bits */
#define S_GENI_DISABLE              (1U << 0)
#define S_GENI_CMD_ABORT            (1U << 1)
#define S_GENI_CMD_CANCEL           (1U << 2)

/* IRQ bits */
#define M_CMD_DONE_EN               (1U << 0)

/* FIFO status fields */
#define TX_FIFO_WC_MASK             0x0FFFFFFF  /* GENMASK(27,0) from geni-se.h */
#define RX_FIFO_WC_MASK             0x01FFFFFF  /* GENMASK(24,0) from geni-se.h */

/* UART M_CMD opcode */
#define UART_START_TX               0x1
#define M_OPCODE_SHFT               27

#define FORCE_DEFAULT               (1U << 0)
#define GENI_IO_MUX_0_EN            (1U << 0)
#define START_TRIGGER               (1U << 0)

#define FW_REV_PROTOCOL_MSK         (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT        8
#define GENI_SE_UART                2

#define SER_CLK_EN                  (1U << 0)
#define CLK_DIV_SHFT                4

/* UART cfg bits from Linux */
#define UART_TX_PAR_EN              (1U << 0)
#define UART_CTS_MASK               (1U << 1)
#define UART_RX_PAR_EN              (1U << 3)
#define TX_STOP_BIT_LEN_1           0

#define SER_CLK_EN                  (1U << 0)
#define CLK_DIV_SHFT                4
#define CLK_DIV_MSK                 (0xFFFU << CLK_DIV_SHFT)  /* conservative; Linux uses div field */
#define CLK_SEL_MSK                 0x7

#define QUP_SE_VERSION_2_5 0x2500

#define DEF_FIFO_DEPTH_WORDS		16
#define UART_RX_WM			2


/* Common QUPV3 registers */
#define QUPV3_HW_VER_REG		0x4
#define QUPV3_SE_AHB_M_CFG		0x118
#define QUPV3_COMMON_CFG		0x120
#define QUPV3_COMMON_CGC_CTRL		0x21c

/* QUPV3_COMMON_CFG fields */
#define FAST_SWITCH_TO_HIGH_DISABLE	BIT(0)

/* QUPV3_SE_AHB_M_CFG fields */
#define AHB_M_CLK_CGC_ON		BIT(0)

/* QUPV3_COMMON_CGC_CTRL fields */
#define COMMON_CSR_SLV_CLK_CGC_ON	BIT(0)

/* Common SE registers */
#define SE_GENI_INIT_CFG_REVISION	0x0
#define SE_GENI_S_INIT_CFG_REVISION	0x4
#define SE_GENI_CGC_CTRL		0x28
#define SE_GENI_CLK_CTRL_RO		0x60
#define SE_GENI_FW_S_REVISION_RO	0x6c
#define SE_GENI_CFG_REG0		0x100
#define SE_GENI_BYTE_GRAN		0x254
#define SE_GENI_TX_PACKING_CFG0		0x260
#define SE_GENI_TX_PACKING_CFG1		0x264
#define SE_GENI_RX_PACKING_CFG0		0x284
#define SE_GENI_RX_PACKING_CFG1		0x288
#define SE_GENI_S_IRQ_ENABLE		0x644
#define SE_DMA_TX_PTR_L			0xc30
#define SE_DMA_TX_PTR_H			0xc34
#define SE_DMA_TX_ATTR			0xc38
#define SE_DMA_TX_LEN			0xc3c
#define SE_DMA_TX_IRQ_EN		0xc48
#define SE_DMA_TX_IRQ_EN_SET		0xc4c
#define SE_DMA_TX_IRQ_EN_CLR		0xc50
#define SE_DMA_TX_LEN_IN		0xc54
#define SE_DMA_TX_MAX_BURST		0xc5c
#define SE_DMA_RX_PTR_L			0xd30
#define SE_DMA_RX_PTR_H			0xd34
#define SE_DMA_RX_ATTR			0xd38
#define SE_DMA_RX_LEN			0xd3c
#define SE_DMA_RX_IRQ_EN		0xd48
#define SE_DMA_RX_IRQ_EN_SET		0xd4c
#define SE_DMA_RX_IRQ_EN_CLR		0xd50
#define SE_DMA_RX_MAX_BURST		0xd5c
#define SE_DMA_RX_FLUSH			0xd60
#define SE_GSI_EVENT_EN			0xe18
#define SE_IRQ_EN			0xe1c
#define SE_DMA_GENERAL_CFG		0xe30
#define SE_GENI_FW_REVISION		0x1000
#define SE_GENI_FW_REVISION		0x1000
#define SE_GENI_S_FW_REVISION		0x1004
#define SE_GENI_CFG_RAMN		0x1010
#define SE_GENI_CLK_CTRL		0x2000
#define SE_DMA_IF_EN			0x2004
#define SE_FIFO_IF_DISABLE		0x2008
#define GENI_FORCE_DEFAULT_REG		0x20
#define GENI_OUTPUT_CTRL		0x24
#define SE_GENI_STATUS			0x40
#define GENI_SER_M_CLK_CFG		0x48
#define GENI_SER_S_CLK_CFG		0x4c
#define GENI_IF_DISABLE_RO		0x64
#define GENI_FW_REVISION_RO		0x68
#define SE_GENI_CLK_SEL			0x7c
#define SE_GENI_CFG_SEQ_START		0x84
#define SE_GENI_DMA_MODE_EN		0x258
#define SE_GENI_M_CMD0			0x600
#define SE_GENI_M_CMD_CTRL_REG		0x604
#define SE_GENI_M_IRQ_STATUS		0x610
#define SE_GENI_M_IRQ_EN		0x614
#define SE_GENI_M_IRQ_CLEAR		0x618
#define SE_GENI_M_IRQ_EN_SET		0x61c
#define SE_GENI_M_IRQ_EN_CLEAR		0x620
#define SE_GENI_S_CMD0			0x630
#define SE_GENI_S_CMD_CTRL_REG		0x634
#define SE_GENI_S_IRQ_STATUS		0x640
#define SE_GENI_S_IRQ_EN		0x644
#define SE_GENI_S_IRQ_CLEAR		0x648
#define SE_GENI_S_IRQ_EN_SET		0x64c
#define SE_GENI_S_IRQ_EN_CLEAR		0x650
#define SE_GENI_TX_FIFOn		0x700
#define SE_GENI_RX_FIFOn		0x780
#define SE_GENI_TX_FIFO_STATUS		0x800
#define SE_GENI_RX_FIFO_STATUS		0x804
#define SE_GENI_TX_WATERMARK_REG	0x80c
#define SE_GENI_RX_WATERMARK_REG	0x810
#define SE_GENI_RX_RFR_WATERMARK_REG	0x814
#define SE_GENI_IOS			0x908
#define SE_GENI_M_GP_LENGTH		0x910
#define SE_GENI_S_GP_LENGTH		0x914
#define SE_DMA_TX_IRQ_STAT		0xc40
#define SE_DMA_TX_IRQ_CLR		0xc44
#define SE_DMA_TX_FSM_RST		0xc58
#define SE_DMA_RX_IRQ_STAT		0xd40
#define SE_DMA_RX_IRQ_CLR		0xd44
#define SE_DMA_RX_LEN_IN		0xd54
#define SE_DMA_RX_FSM_RST		0xd58
#define SE_HW_PARAM_0			0xe24
#define SE_HW_PARAM_1			0xe28


/* GENI_FW_REVISION_RO fields */
#define FW_REV_VERSION_MSK		GENMASK(7, 0)

/* GENI_OUTPUT_CTRL fields */
#define DEFAULT_IO_OUTPUT_CTRL_MSK	GENMASK(6, 0)

/* GENI_CGC_CTRL fields */
#define CFG_AHB_CLK_CGC_ON		BIT(0)
#define CFG_AHB_WR_ACLK_CGC_ON		BIT(1)
#define DATA_AHB_CLK_CGC_ON		BIT(2)
#define SCLK_CGC_ON			BIT(3)
#define TX_CLK_CGC_ON			BIT(4)
#define RX_CLK_CGC_ON			BIT(5)
#define EXT_CLK_CGC_ON			BIT(6)
#define PROG_RAM_HCLK_OFF		BIT(8)
#define PROG_RAM_SCLK_OFF		BIT(9)
#define DEFAULT_CGC_EN			GENMASK(6, 0)

/* SE_GSI_EVENT_EN fields */
#define DMA_RX_EVENT_EN			BIT(0)
#define DMA_TX_EVENT_EN			BIT(1)
#define GENI_M_EVENT_EN			BIT(2)
#define GENI_S_EVENT_EN			BIT(3)

/* SE_IRQ_EN fields */
#define DMA_RX_IRQ_EN			BIT(0)
#define DMA_TX_IRQ_EN			BIT(1)
#define GENI_M_IRQ_EN			BIT(2)
#define GENI_S_IRQ_EN			BIT(3)

/* SE_DMA_GENERAL_CFG */
#define DMA_RX_CLK_CGC_ON		BIT(0)
#define DMA_TX_CLK_CGC_ON		BIT(1)
#define DMA_AHB_SLV_CLK_CGC_ON		BIT(2)
#define AHB_SEC_SLV_CLK_CGC_ON		BIT(3)
#define DUMMY_RX_NON_BUFFERABLE		BIT(4)
#define RX_DMA_ZERO_PADDING_EN		BIT(5)
#define RX_DMA_IRQ_DELAY_MSK		GENMASK(8, 6)
#define RX_DMA_IRQ_DELAY_SHFT		6

/* GENI_CLK_CTRL fields */
#define SER_CLK_SEL			BIT(0)
#define DMA_IF_EN			BIT(0)

#define GENI_DMA_MODE_EN		BIT(0)

/* GENI_M_IRQ_EN fields */
#define M_CMD_DONE_EN			BIT(0)
#define M_CMD_OVERRUN_EN		BIT(1)
#define M_ILLEGAL_CMD_EN		BIT(2)
#define M_CMD_FAILURE_EN		BIT(3)
#define M_CMD_CANCEL_EN			BIT(4)
#define M_CMD_ABORT_EN			BIT(5)
#define M_TIMESTAMP_EN			BIT(6)
#define M_RX_IRQ_EN			BIT(7)
#define M_GP_SYNC_IRQ_0_EN		BIT(8)
#define M_GP_IRQ_0_EN			BIT(9)
#define M_GP_IRQ_1_EN			BIT(10)
#define M_GP_IRQ_2_EN			BIT(11)
#define M_GP_IRQ_3_EN			BIT(12)
#define M_GP_IRQ_4_EN			BIT(13)
#define M_GP_IRQ_5_EN			BIT(14)
#define M_TX_FIFO_NOT_EMPTY_EN		BIT(21)
#define M_IO_DATA_DEASSERT_EN		BIT(22)
#define M_IO_DATA_ASSERT_EN		BIT(23)
#define M_RX_FIFO_RD_ERR_EN		BIT(24)
#define M_RX_FIFO_WR_ERR_EN		BIT(25)
#define M_RX_FIFO_WATERMARK_EN		BIT(26)
#define M_RX_FIFO_LAST_EN		BIT(27)
#define M_TX_FIFO_RD_ERR_EN		BIT(28)
#define M_TX_FIFO_WR_ERR_EN		BIT(29)
#define M_TX_FIFO_WATERMARK_EN		BIT(30)
#define M_SEC_IRQ_EN			BIT(31)
#define M_COMMON_GENI_M_IRQ_EN	(GENMASK(6, 1) | \
				M_IO_DATA_DEASSERT_EN | \
				M_IO_DATA_ASSERT_EN | M_RX_FIFO_RD_ERR_EN | \
				M_RX_FIFO_WR_ERR_EN | M_TX_FIFO_RD_ERR_EN | \
				M_TX_FIFO_WR_ERR_EN)

/* GENI_S_IRQ_EN fields */
#define S_CMD_DONE_EN			BIT(0)
#define S_CMD_OVERRUN_EN		BIT(1)
#define S_ILLEGAL_CMD_EN		BIT(2)
#define S_CMD_FAILURE_EN		BIT(3)
#define S_CMD_CANCEL_EN			BIT(4)
#define S_CMD_ABORT_EN			BIT(5)
#define S_GP_SYNC_IRQ_0_EN		BIT(8)
#define S_GP_IRQ_0_EN			BIT(9)
#define S_GP_IRQ_1_EN			BIT(10)
#define S_GP_IRQ_2_EN			BIT(11)
#define S_GP_IRQ_3_EN			BIT(12)
#define S_GP_IRQ_4_EN			BIT(13)
#define S_GP_IRQ_5_EN			BIT(14)
#define S_IO_DATA_DEASSERT_EN		BIT(22)
#define S_IO_DATA_ASSERT_EN		BIT(23)
#define S_RX_FIFO_RD_ERR_EN		BIT(24)
#define S_RX_FIFO_WR_ERR_EN		BIT(25)
#define S_RX_FIFO_WATERMARK_EN		BIT(26)
#define S_RX_FIFO_LAST_EN		BIT(27)
#define S_COMMON_GENI_S_IRQ_EN	(GENMASK(5, 1) | GENMASK(13, 9) | \
				 S_RX_FIFO_RD_ERR_EN | S_RX_FIFO_WR_ERR_EN)


/* ---------------- small utils ---------------- */
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

/*
 * Try to unwind stuck command engine.
 * This is safe to call even if engine is idle.
 */
static void uart2_cancel_abort(void)
{
	/* Cancel/abort main + secondary */
	w32(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_CANCEL);
	w32(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
	w32(SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_CANCEL);
	w32(SE_GENI_S_CMD_CTRL_REG, S_GENI_CMD_ABORT);

	/* Trigger cfg after abort */
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

/*
 * "Raw" write with proper START_TX.
 * Returns 0 on "likely sent", <0 on failure/timeout.
 */
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

	/* Attempt to recover from stuck engine */
	//uart2_cancel_abort();
	//uart2_force_cfg_trigger();
	//uart2_clear_m_irqs();

	/* If DMA mode is enabled, turn it off for simple FIFO TX */
	// w32(SE_GENI_DMA_MODE_EN, 0);
	// w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);

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


#define STALE_TIMEOUT			16
#define DEFAULT_BITS_PER_CHAR		10
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
	w32(SE_UART_RX_STALE_CNT, DEFAULT_BITS_PER_CHAR * STALE_TIMEOUT);
}

static int uart2_set_baud_linux_style(u32 baud)
{
	u32 sampling = 32; /* 32 */
	u32 ver = qup_wrap0_hw_version();
	if (ver >= QUP_SE_VERSION_2_5)
		sampling >>= 1; /* 16 */

	u32 req = baud * sampling;

	u32 clk_idx = 0, clk_rate = 0;
	if (gcc_uart2_se_clk_config_for_req(req, &clk_idx, &clk_rate) != 0)
		return -1;

	u32 clk_div = (clk_rate + req - 1U) / req;
	if (!clk_div) clk_div = 1;

	u32 ser_clk_cfg = SER_CLK_EN | (clk_div << CLK_DIV_SHFT);

	w32(GENI_SER_M_CLK_CFG, ser_clk_cfg);
	w32(GENI_SER_S_CLK_CFG, ser_clk_cfg);
	w32(SE_GENI_CLK_SEL, clk_idx & CLK_SEL_MSK);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);

	return 0;
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
	u32 base = QUPV3_WRAP0_BASE;

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


	gcc_enable_uart2_clocks();

	geni_se_config_packing(QUPV3_WRAP0_BASE, BITS_PER_BYTE, BYTES_PER_FIFO_WORD,
			       false, true, true);
	geni_se_init(UART_RX_WM, DEF_FIFO_DEPTH_WORDS - 2);
	shm[21] = 0xaaaaaa;
	geni_se_select_fifo_mode(QUPV3_WRAP0_BASE);
	shm[22] = 0xbbbbbb;

	uart2_cancel_abort();
	uart2_force_cfg_trigger();
	uart2_clear_m_irqs();

	w32(SE_GENI_DMA_MODE_EN, 0);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);

	w32(SE_GENI_S_IRQ_EN, 0);
	w32(SE_GENI_M_IRQ_EN, 0);

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

	w32(SE_GENI_TX_WATERMARK_REG, 2);
	w32(SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);

	uart2_set_baud_linux_style(115200);
}
