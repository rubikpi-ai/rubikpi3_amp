#include <type.h>
#include <io.h>
#include <uart_geni.h>
#include <gcc_sc7280.h>
#include <asm/gpio.h>

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
#define SE_GENI_CLK_SEL             0x7c
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

static inline u32 ceil_div_u32(u32 a, u32 b) { return (a + b - 1U) / b; }

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
	w32(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_CANCEL);
	w32(SE_GENI_M_CMD_CTRL_REG, M_GENI_CMD_ABORT);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
}

static int uart2_check_uart_proto(void)
{
	u32 proto = (r32(GENI_FW_REVISION_RO) & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
	return (proto == GENI_SE_UART) ? 0 : -1;
}

static void uart2_set_8n1_no_fc(void)
{
	/* ignore CTS, no parity, 8bit, 1 stop */
	u32 txcfg = UART_CTS_MASK;
	txcfg &= ~UART_TX_PAR_EN;

	w32(SE_UART_TX_TRANS_CFG, txcfg);
	w32(SE_UART_TX_PARITY_CFG, 0);
	w32(SE_UART_RX_TRANS_CFG, 0 & ~UART_RX_PAR_EN);
	w32(SE_UART_RX_PARITY_CFG, 0);

	w32(SE_UART_TX_WORD_LEN, 8);
	w32(SE_UART_RX_WORD_LEN, 8);
	w32(SE_UART_TX_STOP_BIT_LEN, TX_STOP_BIT_LEN_1);

	/* stale cnt: optional */
	w32(SE_UART_RX_STALE_CNT, 0);
}

static void uart2_set_clk(u32 clk_sel, u32 src_hz, u32 baud, u32 sampling)
{
	u32 div = ceil_div_u32(src_hz, baud * sampling);
	if (!div) div = 1;

	u32 cfg = SER_CLK_EN | (div << CLK_DIV_SHFT);

	w32(SE_GENI_CLK_SEL, clk_sel);
	w32(GENI_SER_M_CLK_CFG, cfg);
	w32(GENI_SER_S_CLK_CFG, cfg);
	w32(SE_GENI_CFG_SEQ_START, START_TRIGGER);
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

	/* clear irq */
	u32 mis = r32(SE_GENI_M_IRQ_STATUS);
	if (mis) w32(SE_GENI_M_IRQ_CLEAR, mis);

	/* start tx */
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

/*
 * Core: exhaustive scan to lock correct tuple:
 *  - clk_sel: 0..15
 *  - sampling: 32 or 16
 *  - src_hz: common candidates
 *
 * You watch host terminal: only one tuple will produce readable ASCII.
 */
static void uart2_scan_all(void)
{
	static const u32 src_list[] = {
	//	19200000,
	//	14745600,
	//	7372800,
		32000000,
	//	50000000,
	//	75000000,
	//	100000000
	};
	static const u32 sampling_list[] = {
		//32,
		16,
	};

	const u32 baud = 115200;

	uart2_set_clk(2, 32000000, 115200, 16);
	udelay_local(300);

	uart2_puts("\n--- UART2 GENI Scan Start ---\n");
	return;


	for (u32 si = 0; si < (u32)(sizeof(sampling_list)/sizeof(sampling_list[0])); si++) {
		u32 sampling = sampling_list[si];

		for (u32 hi = 0; hi < (u32)(sizeof(src_list)/sizeof(src_list[0])); hi++) {
			u32 src_hz = src_list[hi];

			for (u32 sel = 0; sel < 8; sel++) {
				uart2_set_clk(sel, src_hz, baud, sampling);

				/* send a distinctive pattern:
				 * 0x55 helps measure baud; and "SEL=.. SRC=.. SMP=.."
				 */
				//uart2_puts("\n[BM] SEL=");
				uart2_putc('0' + (sel / 10));
				uart2_putc('0' + (sel % 10));
				//uart2_puts(" SRC=");
				/* print src_hz in a very simple way: just index */
				uart2_putc('0' + (char)hi);
				//uart2_puts(" SMP=");
				//uart2_putc((sampling == 32) ? '3' : '1');
				//uart2_putc('6');
				//uart2_puts(" 55=");
				//for (int k = 0; k < 32; k++) uart2_putc(0x55);
				//uart2_puts(" END\n");

				udelay_local(300);
			}
		}
	}
}

int uart2_init(unsigned int baud, unsigned long src_clk_hz, unsigned int clk_sel)
{
	(void)baud; (void)src_clk_hz; (void)clk_sel;

	gpio_pinmux_set(10, mux_qup02);
	gpio_pinmux_set(11, mux_qup02);

	gcc_enable_uart2_clocks();

	uart2_cancel_abort();
	uart2_force_cfg_trigger();

	uart2_set_8n1_no_fc();

	/* scan to find correct tuple */
	uart2_scan_all();

	/* After you find the correct tuple from terminal output,
	   replace uart2_scan_all() with a single uart2_set_clk(sel, src_hz, 115200, sampling); */

	return 0;
}
