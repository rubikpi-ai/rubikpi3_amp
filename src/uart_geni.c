#include <type.h>
#include <uart_geni.h>

static inline u32 uart_mmio_read32(u64 off)
{
	return *(volatile u32 *)(UART2_BASE + off);
}

static inline void uart_mmio_write32(u64 off, u32 v)
{
	*(volatile u32 *)(UART2_BASE + off) = v;
}

/* Wait for TX FIFO not-full then write one byte */
void uart2_putc(char c)
{
	/* Optional CRLF handling is in uart2_puts */
	u32 wc;

	/* TX_FIFO_STATUS.TX_FIFO_WC is number of words in FIFO */
	do {
		wc = uart_mmio_read32(SE_GENI_TX_FIFO_STATUS) & TX_FIFO_WC;
	} while (wc >= UART_TX_FIFO_DEPTH_WORDS_DEFAULT);

	/* GENI FIFO is 32-bit; write byte in low 8 bits */
	uart_mmio_write32(SE_GENI_TX_FIFOn, (u32)(u8)c);
}

// void uart2_puts(const char *s)
// {
// 	for (; *s; s++) {
// 		if (*s == '\n')
// 			uart2_putc('\r');
// 		uart2_putc(*s);
// 	}
// }

/* Return -1 if no data */
int uart2_getc_nonblock(void)
{
	u32 wc = uart_mmio_read32(SE_GENI_RX_FIFO_STATUS) & RX_FIFO_WC_MSK;
	if (!wc)
		return -1;

	u32 w = uart_mmio_read32(SE_GENI_RX_FIFOn);
	return (int)(w & 0xFF);
}

static inline u32 r32(u64 off){ return *(volatile u32 *)(UART2_BASE + off); }
static inline void w32(u64 off, u32 v){ *(volatile u32 *)(UART2_BASE + off) = v; }

/* optional: pack 4 chars into one word for fewer MMIO writes */
static inline u32 pack4(const u8 *p, u32 n)
{
    u32 w = 0;
    for (u32 i = 0; i < n; i++)
        w |= ((u32)p[i]) << (8*i);
    return w;
}

static void uart2_write(const void *buf, u32 len)
{
    const u8 *p = (const u8 *)buf;

    /* check protocol (debug) */
    u32 fw = r32(GENI_FW_REVISION_RO);
    u32 proto = (fw & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
    /* 你可以把 proto/fw 写到 SHM 里看 */
    (void)proto;

    /* clear latched status */
    u32 st = r32(SE_GENI_M_IRQ_STATUS);
    if (st)
        w32(SE_GENI_M_IRQ_CLEAR, st);

    /* program length and start TX */
    w32(SE_UART_TX_TRANS_LEN, len);
    w32(SE_GENI_M_CMD0, (UART_START_TX << M_OPCODE_SHFT));

    /* push data into FIFO, 4 bytes at a time */
    while (len) {
        u32 chunk = (len >= 4) ? 4 : len;
        w32(SE_GENI_TX_FIFOn, pack4(p, chunk));
        p += chunk;
        len -= chunk;
    }

    /* wait for completion (poll CMD_DONE or ACTIVE clears) */
    for (u32 i = 0; i < 1000000; i++) {
        u32 irq = r32(SE_GENI_M_IRQ_STATUS);
        if (irq & M_CMD_DONE_EN) {
            w32(SE_GENI_M_IRQ_CLEAR, irq);
            break;
        }
        if (!(r32(SE_GENI_STATUS) & M_GENI_CMD_ACTIVE))
            break;
    }
}

void uart2_puts(const char *s)
{
    /* send in small chunks so we can set correct len */
    while (*s) {
        char tmp[64];
        u32 n = 0;
        while (n < sizeof(tmp) && s[n]) {
            char c = s[n];
            if (c == '\n') {
                /* flush what we have, then send CRLF */
                break;
            }
            tmp[n++] = c;
        }

        if (n) {
            uart2_write(tmp, n);
            s += n;
        }

        if (*s == '\n') {
            char crlf[2] = {'\r','\n'};
            uart2_write(crlf, 2);
            s++;
        }
    }
}






/* ---- Offsets/macros from your pasted Linux geni-se.h ---- */
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

#define SE_GENI_S_CMD0              0x630
#define SE_GENI_S_CMD_CTRL_REG      0x634
#define SE_GENI_S_IRQ_STATUS        0x640
#define SE_GENI_S_IRQ_CLEAR         0x648

#define SE_GENI_TX_FIFOn            0x700
#define SE_GENI_RX_FIFOn            0x780
#define SE_GENI_TX_FIFO_STATUS      0x800
#define SE_GENI_RX_FIFO_STATUS      0x804

#define SE_HW_PARAM_0               0xe24
#define SE_HW_PARAM_1               0xe28

/* Fields */
#define FW_REV_PROTOCOL_MSK         (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT        8

#define M_GENI_CMD_ACTIVE           (1U << 0)
#define S_GENI_CMD_ACTIVE           (1U << 12)

#define SER_CLK_EN                  (1U << 0)

#define M_CMD_DONE_EN               (1U << 0)
#define M_TX_FIFO_WATERMARK_EN      (1U << 30)

/* UART specific regs from qcom_geni_serial.c */
#define SE_UART_TX_TRANS_CFG        0x25c
#define SE_UART_TX_WORD_LEN         0x268
#define SE_UART_TX_TRANS_LEN        0x270
#define SE_UART_RX_TRANS_CFG        0x280
#define SE_UART_RX_WORD_LEN         0x28c
#define SE_UART_RX_STALE_CNT        0x294

/* UART M_CMD opcode */
#define UART_START_TX               0x1
#define M_OPCODE_SHFT               27

static void shm_put(volatile u64 *shm, u32 *idx, const char *tag, u32 off, u32 val)
{
	/* Layout per entry: [tag4][off][val] (tag stored as 4 ASCII bytes) */
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
	/* Basic identity/state */
	shm_put(shm, idx, "FW  ", GENI_FW_REVISION_RO,    r32(GENI_FW_REVISION_RO));
	shm_put(shm, idx, "IFD ", GENI_IF_DISABLE_RO,     r32(GENI_IF_DISABLE_RO));
	shm_put(shm, idx, "STA ", SE_GENI_STATUS,         r32(SE_GENI_STATUS));
	shm_put(shm, idx, "CLKM", GENI_SER_M_CLK_CFG,     r32(GENI_SER_M_CLK_CFG));
	shm_put(shm, idx, "CLKS", GENI_SER_S_CLK_CFG,     r32(GENI_SER_S_CLK_CFG));
	shm_put(shm, idx, "CLKS", SE_GENI_CLK_SEL,        r32(SE_GENI_CLK_SEL));
	shm_put(shm, idx, "DMA ", SE_GENI_DMA_MODE_EN,    r32(SE_GENI_DMA_MODE_EN));

	/* FIFO status */
	shm_put(shm, idx, "TXFS", SE_GENI_TX_FIFO_STATUS, r32(SE_GENI_TX_FIFO_STATUS));
	shm_put(shm, idx, "RXFS", SE_GENI_RX_FIFO_STATUS, r32(SE_GENI_RX_FIFO_STATUS));

	/* Command/IRQ */
	shm_put(shm, idx, "MC0 ", SE_GENI_M_CMD0,         r32(SE_GENI_M_CMD0));
	shm_put(shm, idx, "MCC ", SE_GENI_M_CMD_CTRL_REG, r32(SE_GENI_M_CMD_CTRL_REG));
	shm_put(shm, idx, "MIS ", SE_GENI_M_IRQ_STATUS,   r32(SE_GENI_M_IRQ_STATUS));

	shm_put(shm, idx, "SC0 ", SE_GENI_S_CMD0,         r32(SE_GENI_S_CMD0));
	shm_put(shm, idx, "SCC ", SE_GENI_S_CMD_CTRL_REG, r32(SE_GENI_S_CMD_CTRL_REG));
	shm_put(shm, idx, "SIS ", SE_GENI_S_IRQ_STATUS,   r32(SE_GENI_S_IRQ_STATUS));

	/* HW params (fifo depth/width) */
	shm_put(shm, idx, "HP0 ", SE_HW_PARAM_0,          r32(SE_HW_PARAM_0));
	shm_put(shm, idx, "HP1 ", SE_HW_PARAM_1,          r32(SE_HW_PARAM_1));

	/* UART protocol regs */
	shm_put(shm, idx, "TXCF", SE_UART_TX_TRANS_CFG,   r32(SE_UART_TX_TRANS_CFG));
	shm_put(shm, idx, "TXWL", SE_UART_TX_WORD_LEN,    r32(SE_UART_TX_WORD_LEN));
	shm_put(shm, idx, "TXLN", SE_UART_TX_TRANS_LEN,   r32(SE_UART_TX_TRANS_LEN));

	shm_put(shm, idx, "RXCF", SE_UART_RX_TRANS_CFG,   r32(SE_UART_RX_TRANS_CFG));
	shm_put(shm, idx, "RXWL", SE_UART_RX_WORD_LEN,    r32(SE_UART_RX_WORD_LEN));
	shm_put(shm, idx, "RXST", SE_UART_RX_STALE_CNT,   r32(SE_UART_RX_STALE_CNT));
}

/*
 * Try send: non-blocking with timeout, and always dump state.
 * shm_base_idx:建议用 200+，避开你 timer/irq 现有字段。
 */
void uart2_debug_dump_and_try_tx(volatile u64 *shm, u32 shm_base_idx,
				const char *msg)
{
	u32 idx = shm_base_idx;

	/* Header */
	shm[idx++] = 0x554152543247454EULL; /* "UART2GEN" magic-ish */
	shm[idx++] = UART2_BASE;

	/* Dump before */
	shm[idx++] = 0x4245464F52450001ULL; /* "BEFORE"+1 */
	uart2_dump_regs(shm, &idx);

	/* Decode protocol */
	u32 fw = r32(GENI_FW_REVISION_RO);
	u32 proto = (fw & FW_REV_PROTOCOL_MSK) >> FW_REV_PROTOCOL_SHFT;
	shm[idx++] = 0x50524F544F000000ULL | proto; /* "PROTO"+proto */

	/* Clear any latched M IRQ */
	u32 mis = r32(SE_GENI_M_IRQ_STATUS);
	if (mis)
		w32(SE_GENI_M_IRQ_CLEAR, mis);

	/* Program TX length */
	u32 len = 0;
	while (msg[len] && len < 256) len++; /* cap */
	w32(SE_UART_TX_TRANS_LEN, len);

	/* Kick START_TX */
	w32(SE_GENI_M_CMD0, (UART_START_TX << M_OPCODE_SHFT));

	/* Push bytes (packed) with timeout to avoid hang */
	const u8 *p = (const u8 *)msg;
	u32 left = len;
	u32 writes = 0;

	for (u32 guard = 0; left && guard < 1000000; guard++) {
		/* If TX FIFO status looks dead, still try a few writes then bail */
		u32 chunk = (left >= 4) ? 4 : left;
		w32(SE_GENI_TX_FIFOn, pack4(p, chunk));
		p += chunk;
		left -= chunk;
		writes++;
	}

	shm[idx++] = 0x5752495445530000ULL | (u64)writes; /* "WRITES" */

	/* Wait for done/idle (bounded) */
	u32 done = 0;
	for (u32 guard = 0; guard < 2000000; guard++) {
		u32 irq = r32(SE_GENI_M_IRQ_STATUS);
		if (irq & M_CMD_DONE_EN) {
			done = 1;
			w32(SE_GENI_M_IRQ_CLEAR, irq);
			break;
		}
		u32 st = r32(SE_GENI_STATUS);
		if (!(st & M_GENI_CMD_ACTIVE)) {
			/* became idle without DONE */
			break;
		}
	}
	shm[idx++] = 0x444F4E4500000000ULL | done; /* "DONE" */

	/* Dump after */
	shm[idx++] = 0x4146544552000002ULL; /* "AFTER"+2 */
	uart2_dump_regs(shm, &idx);

	/* End marker */
	shm[idx++] = 0x454E440000000000ULL; /* "END" */
}
