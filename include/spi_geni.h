/*
 * QCOM GENI SPI driver definitions for baremetal
 *
 * This header contains SPI-specific register definitions and APIs.
 * Common GENI SE definitions are in geni_se.h.
 */

#ifndef __SPI_GENI_H
#define __SPI_GENI_H

#include <type.h>
#include <geni_se.h>

/*
 * SPI12: spi@a90000
 * reg = <0 0x00a90000 0 0x4000>
 * GPIO: 48 (MOSI), 49 (MISO), 50 (CLK), 51 (CS) - function qup14
 * Clock: GCC_QUPV3_WRAP1_S4_CLK
 * Interrupt: GIC_SPI 357
 *
 * This driver implements polled FIFO mode SPI.
 */
#define SPI12_BASE		QUPV3_SE12_BASE

/* ============================================================
 * SPI specific registers (offset from SE base)
 * ============================================================ */
#define SE_SPI_CPHA			0x224
#define SE_SPI_LOOPBACK			0x22c
#define SE_SPI_CPOL			0x230
#define SE_SPI_DEMUX_OUTPUT_INV		0x24c
#define SE_SPI_DEMUX_SEL		0x250
#define SE_SPI_TRANS_CFG		0x25c
#define SE_SPI_WORD_LEN			0x268
#define SE_SPI_TX_TRANS_LEN		0x26c
#define SE_SPI_RX_TRANS_LEN		0x270
#define SE_SPI_PRE_POST_CMD_DLY		0x274
#define SE_SPI_DELAY_COUNTERS		0x278
#define SE_SPI_SLAVE_EN			0x2BC

/* ============================================================
 * SPI specific fields
 * ============================================================ */

/* SE_SPI_CPHA fields */
#define CPHA				BIT(0)

/* SE_SPI_LOOPBACK fields */
#define LOOPBACK_ENABLE			0x1
#define NORMAL_MODE			0x0
#define LOOPBACK_MSK			GENMASK(1, 0)

/* SE_SPI_CPOL fields */
#define CPOL				BIT(2)

/* SE_SPI_DEMUX_OUTPUT_INV fields */
#define CS_DEMUX_OUTPUT_INV_MSK		GENMASK(3, 0)

/* SE_SPI_DEMUX_SEL fields */
#define CS_DEMUX_OUTPUT_SEL		GENMASK(3, 0)

/* SE_SPI_TRANS_CFG fields */
#define CS_TOGGLE			BIT(1)

/* SE_SPI_WORD_LEN fields */
#define WORD_LEN_MSK			GENMASK(9, 0)
#define MIN_WORD_LEN			4

/* SE_SPI_TX_TRANS_LEN / SE_SPI_RX_TRANS_LEN fields */
#define TRANS_LEN_MSK			GENMASK(23, 0)

/* SE_SPI_DELAY_COUNTERS fields */
#define SPI_INTER_WORDS_DELAY_MSK	GENMASK(9, 0)
#define SPI_CS_CLK_DELAY_MSK		GENMASK(19, 10)
#define SPI_CS_CLK_DELAY_SHFT		10

/* SE_SPI_SLAVE_EN fields */
#define SPI_SLAVE_EN			BIT(0)

/* ============================================================
 * SPI M_CMD opcodes
 * ============================================================ */
#define SPI_TX_ONLY			1
#define SPI_RX_ONLY			2
#define SPI_TX_RX			7
#define SPI_CS_ASSERT			8
#define SPI_CS_DEASSERT			9
#define SPI_SCK_ONLY			10

/* M_CMD params for SPI */
#define SPI_PRE_CMD_DELAY		BIT(0)
#define SPI_TIMESTAMP_BEFORE		BIT(1)
#define SPI_FRAGMENTATION		BIT(2)
#define SPI_TIMESTAMP_AFTER		BIT(3)
#define SPI_POST_CMD_DELAY		BIT(4)

/* ============================================================
 * SPI Mode definitions (compatible with Linux SPI)
 * ============================================================ */
#define SPI_CPHA			0x01	/* Clock phase */
#define SPI_CPOL			0x02	/* Clock polarity */
#define SPI_MODE_0			(0|0)			/* CPOL=0, CPHA=0 */
#define SPI_MODE_1			(0|SPI_CPHA)		/* CPOL=0, CPHA=1 */
#define SPI_MODE_2			(SPI_CPOL|0)		/* CPOL=1, CPHA=0 */
#define SPI_MODE_3			(SPI_CPOL|SPI_CPHA)	/* CPOL=1, CPHA=1 */
#define SPI_CS_HIGH			0x04	/* CS active high */
#define SPI_LSB_FIRST			0x08	/* LSB first */
#define SPI_LOOP			0x20	/* Loopback mode */

/* ============================================================
 * SPI transfer structure
 * ============================================================ */
struct spi_transfer {
	const u8 *tx_buf;	/* TX buffer (NULL if RX only) */
	u8 *rx_buf;		/* RX buffer (NULL if TX only) */
	u32 len;		/* Transfer length in bytes */
	u32 speed_hz;		/* Transfer speed (0 = use default) */
	u8 bits_per_word;	/* Bits per word (0 = use default) */
	u8 cs_change;		/* Deselect CS after transfer */
};

/* ============================================================
 * SPI device configuration
 * ============================================================ */
struct spi_device {
	u8 chip_select;		/* CS line (0-3) */
	u8 mode;		/* SPI mode (SPI_MODE_0..3) */
	u8 bits_per_word;	/* Default bits per word (8) */
	u32 max_speed_hz;	/* Maximum SPI clock speed */
};

/* ============================================================
 * SPI controller context
 * ============================================================ */
struct geni_spi_dev {
	u64 base;
	u32 tx_fifo_depth;
	u32 fifo_width_bits;
	u32 tx_wm;
	u32 cur_speed_hz;
	u32 cur_bits_per_word;
	u8 cur_cs;
	u8 cur_mode;
	int oversampling;
};

/* ============================================================
 * Standard SPI speeds
 * ============================================================ */
#define SPI_1MHZ			1000000
#define SPI_5MHZ			5000000
#define SPI_10MHZ			10000000
#define SPI_25MHZ			25000000
#define SPI_50MHZ			50000000

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * spi12_init() - Initialize SPI12 controller
 * @max_speed_hz: Maximum SPI clock speed
 *
 * Return: 0 on success, negative error code on failure
 */
int spi12_init(u32 max_speed_hz);

/**
 * spi12_set_mode() - Set SPI mode for a chip select
 * @cs: Chip select (0-3)
 * @mode: SPI mode (SPI_MODE_0..3)
 *
 * Return: 0 on success, negative error code on failure
 */
int spi12_set_mode(u8 cs, u8 mode);

/**
 * spi12_set_speed() - Set SPI clock speed
 * @speed_hz: Clock speed in Hz
 *
 * Return: 0 on success, negative error code on failure
 */
int spi12_set_speed(u32 speed_hz);

/**
 * spi12_transfer() - Perform a SPI transfer
 * @cs: Chip select (0-3)
 * @xfer: Transfer descriptor
 *
 * Return: Number of bytes transferred, or negative error code
 */
int spi12_transfer(u8 cs, struct spi_transfer *xfer);

/**
 * spi12_write() - Write data to SPI (TX only)
 * @cs: Chip select (0-3)
 * @tx_buf: Data to transmit
 * @len: Number of bytes to transmit
 *
 * Return: Number of bytes written, or negative error code
 */
int spi12_write(u8 cs, const u8 *tx_buf, u32 len);

/**
 * spi12_read() - Read data from SPI (RX only, sends zeros)
 * @cs: Chip select (0-3)
 * @rx_buf: Buffer to receive data
 * @len: Number of bytes to read
 *
 * Return: Number of bytes read, or negative error code
 */
int spi12_read(u8 cs, u8 *rx_buf, u32 len);

/**
 * spi12_write_read() - Full duplex SPI transfer
 * @cs: Chip select (0-3)
 * @tx_buf: Data to transmit
 * @rx_buf: Buffer to receive data
 * @len: Number of bytes to transfer
 *
 * Return: Number of bytes transferred, or negative error code
 */
int spi12_write_read(u8 cs, const u8 *tx_buf, u8 *rx_buf, u32 len);

/**
 * spi12_write_then_read() - Write then read (half duplex)
 * @cs: Chip select (0-3)
 * @tx_buf: Data to transmit
 * @tx_len: Number of bytes to transmit
 * @rx_buf: Buffer to receive data
 * @rx_len: Number of bytes to read
 *
 * Return: Total bytes transferred, or negative error code
 */
int spi12_write_then_read(u8 cs, const u8 *tx_buf, u32 tx_len,
			  u8 *rx_buf, u32 rx_len);

#endif /* __SPI_GENI_H */
