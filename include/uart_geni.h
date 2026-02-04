/*
 * QCOM GENI UART driver definitions for baremetal
 *
 * This header contains UART-specific register definitions and APIs.
 * Common GENI SE definitions are in geni_se.h.
 */

#ifndef __UART_GENI_H
#define __UART_GENI_H

#include <type.h>
#include <geni_se.h>

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
#define UART2_BASE		QUPV3_SE2_BASE

/* ============================================================
 * UART specific registers (offset from SE base)
 * ============================================================ */
#define SE_UART_TX_TRANS_CFG		0x25c
#define SE_UART_TX_WORD_LEN		0x268
#define SE_UART_TX_STOP_BIT_LEN		0x26c
#define SE_UART_TX_TRANS_LEN		0x270
#define SE_UART_TX_PARITY_CFG		0x2a4
#define SE_UART_RX_TRANS_CFG		0x280
#define SE_UART_RX_WORD_LEN		0x28c
#define SE_UART_RX_STALE_CNT		0x294
#define SE_UART_RX_PARITY_CFG		0x2a8
#define SE_UART_MANUAL_RFR		0x2ac

/* ============================================================
 * UART specific fields
 * ============================================================ */

/* SE_UART_TX_TRANS_CFG fields */
#define UART_TX_PAR_EN			BIT(0)
#define UART_CTS_MASK			BIT(1)

/* SE_UART_RX_TRANS_CFG fields */
#define UART_RX_INSERT_STATUS		BIT(2)
#define UART_RX_PAR_EN			BIT(3)

/* SE_UART_TX_STOP_BIT_LEN values */
#define TX_STOP_BIT_LEN_1		0
#define TX_STOP_BIT_LEN_1_5		1
#define TX_STOP_BIT_LEN_2		2

/* SE_UART_MANUAL_RFR fields */
#define UART_MANUAL_RFR_EN		BIT(31)
#define UART_RFR_NOT_READY		BIT(1)
#define UART_RFR_READY			BIT(0)

/* UART M_CMD opcodes */
#define UART_START_TX			0x1
#define UART_START_BREAK		0x4
#define UART_STOP_BREAK			0x5

/* UART S_CMD opcodes */
#define UART_START_READ			0x1
#define UART_PARAM			0x1

/* UART M_CMD0 param bits */
#define UART_TX_CMD_PARAM_MSK		GENMASK(7, 0)

/* UART timing constants */
#define STALE_TIMEOUT			16
#define DEFAULT_BITS_PER_CHAR		10

/* Default FIFO depth (words) if HW param reads 0 */
#define UART_TX_FIFO_DEPTH_WORDS_DEFAULT	16
#define UART_RX_WM			2

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * uart2_init() - Initialize UART2 for polled TX/RX
 *
 * Configures GPIO, clocks, and UART settings for 115200 8N1
 */
void uart2_init(void);

/**
 * uart2_putc() - Output a single character
 * @c: Character to output
 */
void uart2_putc(char c);

/**
 * uart2_puts() - Output a null-terminated string
 * @s: String to output
 */
void uart2_puts(const char *s);

/**
 * uart2_write() - Write a buffer to UART
 * @buf: Buffer to write
 * @len: Number of bytes to write
 *
 * Return: 0 on success, negative error code on failure
 */
int uart2_write(const void *buf, u32 len);

/**
 * uart2_getc_nonblock() - Non-blocking character read
 *
 * Return: Character read (0-255) or -1 if no data available
 */
int uart2_getc_nonblock(void);

/**
 * uart2_getc() - Blocking character read
 *
 * Waits until a character is available and returns it.
 *
 * Return: Character read (0-255)
 */
int uart2_getc(void);

#endif /* __UART_GENI_H */
