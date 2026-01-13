#ifndef __UART_GENI_H
#define __UART_GENI_H
#include <type.h>

#define UART2_BASE 0x00988000ULL

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

#define SE_GENI_M_CMD0              0x600
#define SE_GENI_M_CMD_CTRL_REG      0x604
#define SE_GENI_M_IRQ_STATUS        0x610
#define SE_GENI_M_IRQ_EN            0x614
#define SE_GENI_M_IRQ_CLEAR         0x618

#define SE_GENI_S_CMD0              0x630
#define SE_GENI_S_CMD_CTRL_REG      0x634
#define SE_GENI_S_IRQ_STATUS        0x640
#define SE_GENI_S_IRQ_EN            0x644
#define SE_GENI_S_IRQ_CLEAR         0x648

#define SE_GENI_TX_FIFOn            0x700
#define SE_GENI_RX_FIFOn            0x780
#define SE_GENI_TX_FIFO_STATUS      0x800
#define SE_GENI_RX_FIFO_STATUS      0x804

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

/* Bits/fields */
#define FORCE_DEFAULT               (1U << 0)

#define FW_REV_PROTOCOL_MSK         (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT        8
#define GENI_SE_UART                2

#define M_GENI_CMD_ACTIVE           (1U << 0)
#define S_GENI_CMD_ACTIVE           (1U << 12)

#define SER_CLK_EN                  (1U << 0)
#define CLK_DIV_SHFT                4
#define CLK_DIV_MSK                 (0xFFFU << CLK_DIV_SHFT)  /* conservative; Linux uses div field */
#define CLK_SEL_MSK                 0x7

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
#define TX_FIFO_WC_MASK             0x0FFFFFFF

/* UART protocol opcodes */
#define UART_START_TX               0x1
#define M_OPCODE_SHFT               27

/* UART config defaults */
#define UART_OVERSAMPLING_DEFAULT   32

/* Public API */
int  uart2_init(unsigned int baud, unsigned long src_clk_hz, unsigned int clk_sel);
void uart2_putc(char c);
void uart2_puts(const char *s);
int  uart2_write(const void *buf, u32 len);

#endif
