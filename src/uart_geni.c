#include <type.h>
#include <uart_geni.h>
#include <gcc_sc7280.h>
#include <asm/gpio.h>

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

void uart2_init(void)
{
	gpio_pinmux_set(10, mux_qup02);
	gpio_pinmux_set(11, mux_qup02);

	gcc_enable_uart2_clocks();

	uart2_cancel_abort();
	uart2_force_cfg_trigger();
	uart2_clear_m_irqs();

	w32(SE_GENI_DMA_MODE_EN, 0);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
}
