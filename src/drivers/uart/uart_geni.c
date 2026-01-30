/*
 * QCOM GENI UART driver for baremetal
 *
 * This driver implements UART communication using GENI Serial Engine
 * in FIFO mode (no DMA).
 *
 * Reference: Linux kernel drivers/tty/serial/qcom_geni_serial.c
 */

#include <type.h>
#include <uart_geni.h>
#include <geni_se.h>
#include <gpio.h>
#include <clock.h>
#include <clock_qcs6490.h>

/* MMIO helpers for UART2 */
static inline u32 r32(u32 off) { return geni_read32(UART2_BASE, off); }
static inline void w32(u32 off, u32 v) { geni_write32(UART2_BASE, off, v); }

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
 */
static u32 uart2_tx_fifo_depth_words(void)
{
	u32 depth = geni_se_get_tx_fifo_depth(UART2_BASE);
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
	u32 wc = r32(SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK;
	if (!wc)
		return -1;
	return (int)(r32(SE_GENI_RX_FIFOn) & 0xFF);
}

int uart2_write(const void *buf, u32 len)
{
	const u8 *p = (const u8 *)buf;
	u32 proto = geni_se_read_proto(UART2_BASE);

	/* If protocol isn't UART, don't try to send (will never work) */
	if (proto != GENI_SE_UART)
		return -10;

	/* Make sure FIFO interface isn't disabled */
	if (r32(GENI_IF_DISABLE_RO) & FIFO_IF_DISABLE)
		return -11;

	/* Program length and kick START_TX */
	w32(SE_UART_TX_TRANS_LEN, len);
	w32(SE_GENI_M_CMD0, (UART_START_TX << M_OPCODE_SHFT));

	/* Push data with FIFO-depth based flow control */
	u32 depth = uart2_tx_fifo_depth_words();
	u32 left = len;

	for (u32 guard = 0; left && guard < 2000000; guard++) {
		u32 wc = r32(SE_GENI_TX_FIFO_STATUS) & TX_FIFO_WC;

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
			tmp[n] = s[n];
			n++;
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

static void uart2_set_8n1_no_fc(void)
{
	/* Truly disable flow-control: do NOT set UART_CTS_MASK */
	w32(SE_UART_TX_TRANS_CFG, 0);        /* no CTS */
	w32(SE_UART_TX_PARITY_CFG, 0);       /* no parity */

	w32(SE_UART_RX_TRANS_CFG, 0);        /* no parity */
	w32(SE_UART_RX_PARITY_CFG, 0);

	w32(SE_UART_TX_WORD_LEN, BITS_PER_BYTE);  /* 8 bits */
	w32(SE_UART_RX_WORD_LEN, BITS_PER_BYTE);  /* 8 bits */

	w32(SE_UART_TX_STOP_BIT_LEN, TX_STOP_BIT_LEN_1);  /* 1 stop bit */

	/* stale timeout: keep as before */
	w32(SE_UART_RX_STALE_CNT, DEFAULT_BITS_PER_CHAR * STALE_TIMEOUT);

	w32(SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);

	/* Set the UART's internal divider. */
	w32(GENI_SER_M_CLK_CFG, 0x41);
}

void uart2_init(void)
{
	gpio_pinmux_set(10, mux_qup02);
	gpio_pinmux_set(11, mux_qup02);

	clk_enable(UART2_CLK);
	clk_set_rate(UART2_CLK, 115200 * 16); /* 16x oversampling */

	geni_se_config_packing(UART2_BASE, BITS_PER_BYTE, BYTES_PER_FIFO_WORD,
			       false, true, true);
	geni_se_init(UART2_BASE, UART_RX_WM, DEF_FIFO_DEPTH_WORDS - 2);
	geni_se_select_mode(UART2_BASE, GENI_SE_FIFO);

	uart2_cancel_abort();
	uart2_force_cfg_trigger();
	uart2_clear_m_irqs();

	w32(SE_GENI_DMA_MODE_EN, 0);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);

	uart2_set_8n1_no_fc();
}
