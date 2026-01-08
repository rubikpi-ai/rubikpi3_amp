#ifndef __UART_GENI_H
#define __UART_GENI_H
#include <type.h>

/*
 * UART2: serial@988000
 * reg = <0 0x00988000 0 0x4000>
 *
 * This driver assumes Linux already:
 *  - enabled clocks / power domain / interconnect votes
 *  - configured pinmux (gpio10/gpio11 to qup02)
 *  - set up GENI UART protocol engine
 *
 * Baremetal only does polled FIFO TX/RX.
 */
#define UART2_BASE 0x00988000ULL

/* From Linux include/linux/soc/qcom/geni-se.h */
#define SE_GENI_TX_FIFOn        0x700
#define SE_GENI_RX_FIFOn        0x780
#define SE_GENI_TX_FIFO_STATUS  0x800
#define SE_GENI_RX_FIFO_STATUS  0x804
#define TX_FIFO_WC              0x0FFFFFFF     /* GENMASK(27,0) */
#define RX_FIFO_WC_MSK          0x01FFFFFF     /* GENMASK(24,0) */

#define SE_GENI_M_CMD0           0x600
#define SE_GENI_M_IRQ_STATUS     0x610
#define SE_GENI_M_IRQ_CLEAR      0x618
#define SE_GENI_STATUS           0x40
#define GENI_FW_REVISION_RO      0x68

#define SE_UART_TX_TRANS_LEN     0x270

#define UART_START_TX            0x1
#define M_OPCODE_SHFT            27
#define M_CMD_DONE_EN            (1U << 0)
#define M_GENI_CMD_ACTIVE        (1U << 0)

#define FW_REV_PROTOCOL_MSK      (0xFFU << 8)
#define FW_REV_PROTOCOL_SHFT     8
#define GENI_SE_UART             2

/* Optionally read HW fifo depth if you want; simplest is fixed threshold */
#define UART_TX_FIFO_DEPTH_WORDS_DEFAULT 16

void uart2_putc(char c);
void uart2_puts(const char *s);
int uart2_getc_nonblock(void);

void uart2_debug_dump_and_try_tx(volatile u64 *shm, u32 shm_base_idx,
				const char *msg);
#endif
