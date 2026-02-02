/*
 * QCOM GENI SPI driver for baremetal
 *
 * This driver implements SPI communication using GENI Serial Engine
 * in FIFO mode (no DMA).
 *
 * Reference: Linux kernel drivers/spi/spi-geni-qcom.c
 */

#include <type.h>
#include <spi_geni.h>
#include <geni_se.h>
#include <gpio.h>
#include <clock.h>
#include <clock_qcs6490.h>

#define SPI_TIMEOUT		1000000
#define SPI_DEFAULT_SPEED_HZ	1000000		/* 1 MHz default */
#define SPI_DEFAULT_BPW		8		/* 8 bits per word */

/* Global SPI12 device context */
static struct geni_spi_dev spi12_dev = {
	.base = SPI12_BASE,
	.tx_fifo_depth = 0,
	.fifo_width_bits = 0,
	.tx_wm = 1,
	.cur_speed_hz = 0,
	.cur_bits_per_word = 0,
	.cur_cs = 0,
	.cur_mode = 0,
	.oversampling = 1,
};

static int geni_spi_set_clock(struct geni_spi_dev *spi, u32 speed_hz)
{
	u32 clk_sel, m_clk_cfg, clk_div;
	u32 sclk_freq;

	if (speed_hz == spi->cur_speed_hz)
		return 0;

	/*
	 * Use 100MHz source clock from GPLL0_OUT_MAIN for high-speed support.
	 * The SE divider further divides this to get the desired SPI clock.
	 * clk_freq_out = source_clk / clk_div
	 */
	sclk_freq = 100000000;  /* 100 MHz from GPLL0_OUT_MAIN */
	clk_div = (sclk_freq + speed_hz - 1) / speed_hz;
	if (clk_div < 1)
		clk_div = 1;
	if (clk_div > 0xFFF)
		clk_div = 0xFFF;

	spi->cur_speed_hz = sclk_freq / clk_div;

	/* Select clock source 1 (GPLL0_OUT_MAIN via RCG) */
	clk_sel = 0;
	geni_write32(spi->base, SE_GENI_CLK_SEL, clk_sel);

	/* Configure clock divider */
	m_clk_cfg = (clk_div << CLK_DIV_SHFT) | SER_CLK_EN;
	geni_write32(spi->base, GENI_SER_M_CLK_CFG, m_clk_cfg);

	return 0;
}

static void geni_spi_setup_word_len(struct geni_spi_dev *spi, u8 mode,
				    u8 bits_per_word)
{
	u32 word_len;
	u32 pack_words;
	bool msb_first = !(mode & SPI_LSB_FIRST);

	/* Configure packing */
	if (!(spi->fifo_width_bits % bits_per_word))
		pack_words = spi->fifo_width_bits / bits_per_word;
	else
		pack_words = 1;

	geni_se_config_packing(spi->base, bits_per_word, pack_words,
			       msb_first, true, true);

	/* Set word length register */
	word_len = (bits_per_word - MIN_WORD_LEN) & WORD_LEN_MSK;
	geni_write32(spi->base, SE_SPI_WORD_LEN, word_len);

	spi->cur_bits_per_word = bits_per_word;
}

static void geni_spi_setup_mode(struct geni_spi_dev *spi, u8 cs, u8 mode)
{
	if (spi->cur_cs != cs) {
		geni_write32(spi->base, SE_SPI_DEMUX_SEL, cs & CS_DEMUX_OUTPUT_SEL);
		spi->cur_cs = cs;
	}

	if (spi->cur_mode != mode) {
		/* CPOL */
		geni_write32(spi->base, SE_SPI_CPOL,
			     (mode & SPI_CPOL) ? CPOL : 0);
		/* CPHA */
		geni_write32(spi->base, SE_SPI_CPHA,
			     (mode & SPI_CPHA) ? CPHA : 0);
		/* CS active high */
		geni_write32(spi->base, SE_SPI_DEMUX_OUTPUT_INV,
			     (mode & SPI_CS_HIGH) ? BIT(cs) : 0);
		/* Loopback */
		geni_write32(spi->base, SE_SPI_LOOPBACK,
			     (mode & SPI_LOOP) ? LOOPBACK_ENABLE : NORMAL_MODE);

		spi->cur_mode = mode;
	}
}

static int geni_spi_wait_cmd_done(struct geni_spi_dev *spi)
{
	u32 m_stat, timeout = SPI_TIMEOUT;

	while (timeout--) {
		m_stat = geni_read32(spi->base, SE_GENI_M_IRQ_STATUS);

		/* Check for errors */
		if (m_stat & M_CMD_OVERRUN_EN) {
			geni_write32(spi->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return -1;
		}
		if (m_stat & M_ILLEGAL_CMD_EN) {
			geni_write32(spi->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return -2;
		}
		if (m_stat & M_CMD_FAILURE_EN) {
			geni_write32(spi->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return -3;
		}

		/* Check for command done */
		if (m_stat & M_CMD_DONE_EN) {
			geni_write32(spi->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return 0;
		}
	}

	return -4; /* timeout */
}

static int geni_spi_xfer_fifo(struct geni_spi_dev *spi,
			      const u8 *tx_buf, u8 *rx_buf, u32 len)
{
	u32 m_cmd = 0;
	u32 tx_rem = 0, rx_rem = 0;
	u32 fifo_word;
	u32 bytes_per_word = (spi->cur_bits_per_word + 7) / 8;
	u32 words_per_fifo = spi->fifo_width_bits / 8;
	u32 xfer_len;
	u32 timeout;
	u32 m_stat;
	u32 i, j;

	/* Calculate transfer length in words */
	if (spi->cur_bits_per_word < 8)
		xfer_len = len;
	else
		xfer_len = (len * 8 + spi->cur_bits_per_word - 1) / spi->cur_bits_per_word;

	/* Determine command type */
	if (tx_buf && rx_buf) {
		m_cmd = SPI_TX_RX;
		tx_rem = len;
		rx_rem = len;
		geni_write32(spi->base, SE_SPI_TX_TRANS_LEN, xfer_len);
		geni_write32(spi->base, SE_SPI_RX_TRANS_LEN, xfer_len);
	} else if (tx_buf) {
		m_cmd = SPI_TX_ONLY;
		tx_rem = len;
		geni_write32(spi->base, SE_SPI_TX_TRANS_LEN, xfer_len);
	} else if (rx_buf) {
		m_cmd = SPI_RX_ONLY;
		rx_rem = len;
		geni_write32(spi->base, SE_SPI_RX_TRANS_LEN, xfer_len);
	} else {
		return -1;
	}

	/* Issue command */
	geni_write32(spi->base, SE_GENI_M_CMD0, m_cmd << M_OPCODE_SHFT);

	/* TX loop */
	timeout = SPI_TIMEOUT;
	while (tx_rem > 0 && timeout--) {
		m_stat = geni_read32(spi->base, SE_GENI_M_IRQ_STATUS);

		if (m_stat & M_TX_FIFO_WATERMARK_EN) {
			geni_write32(spi->base, SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);

			/* Write data to FIFO */
			for (j = 0; j < spi->tx_fifo_depth - spi->tx_wm && tx_rem > 0; j++) {
				fifo_word = 0;
				for (i = 0; i < words_per_fifo && tx_rem > 0; i++) {
					fifo_word |= ((u32)*tx_buf++) << (i * 8);
					tx_rem--;
				}
				geni_write32(spi->base, SE_GENI_TX_FIFOn, fifo_word);
			}

			if (tx_rem == 0) {
				/* Disable TX watermark */
				geni_write32(spi->base, SE_GENI_TX_WATERMARK_REG, 0);
			}
		}

		if (m_stat & M_CMD_DONE_EN)
			break;
	}

	/* RX loop */
	timeout = SPI_TIMEOUT;
	while (rx_rem > 0 && timeout--) {
		m_stat = geni_read32(spi->base, SE_GENI_M_IRQ_STATUS);
		u32 rx_stat = geni_read32(spi->base, SE_GENI_RX_FIFO_STATUS);
		u32 rx_wc = rx_stat & RX_FIFO_WC_MSK;

		if (rx_wc > 0) {
			for (j = 0; j < rx_wc && rx_rem > 0; j++) {
				fifo_word = geni_read32(spi->base, SE_GENI_RX_FIFOn);
				for (i = 0; i < words_per_fifo && rx_rem > 0; i++) {
					*rx_buf++ = (fifo_word >> (i * 8)) & 0xFF;
					rx_rem--;
				}
			}
		}

		if (m_stat & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN))
			geni_write32(spi->base, SE_GENI_M_IRQ_CLEAR,
				     M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);

		if (m_stat & M_CMD_DONE_EN)
			break;
	}

	/* Wait for completion */
	return geni_spi_wait_cmd_done(spi);
}

/*
 * Public API
 */

int spi12_init(u32 max_speed_hz)
{
	u32 tx_depth, proto;
	u32 ver, major;

	if (max_speed_hz == 0)
		max_speed_hz = SPI_DEFAULT_SPEED_HZ;

	/* Configure GPIO pins for SPI12: GPIO 48-51, function qup14 */
	gpio_pinmux_set(48, mux_qup14);  /* MOSI */
	gpio_pinmux_set(49, mux_qup14);  /* MISO */
	gpio_pinmux_set(50, mux_qup14);  /* CLK */
	gpio_pinmux_set(51, mux_qup14);  /* CS */

	/* Enable SPI12 clock */
	clk_enable(SPI12_CLK);

	/* Set clock rate to 100MHz for high-speed SPI support */
	clk_set_rate(SPI12_CLK, 100000000);

	/* Check protocol type */
	proto = geni_se_read_proto(spi12_dev.base);
	if (proto != GENI_SE_SPI) {
		/* Protocol not configured for SPI */
		return -1;
	}

	/* Get FIFO parameters */
	tx_depth = geni_se_get_tx_fifo_depth(spi12_dev.base);
	if (tx_depth == 0)
		tx_depth = 16;  /* Default */

	spi12_dev.tx_fifo_depth = tx_depth;
	spi12_dev.fifo_width_bits = 32;  /* Fixed for QUP */
	spi12_dev.tx_wm = 1;

	/* Check version for oversampling */
	ver = geni_se_get_qup_hw_version(QUPV3_WRAP1_BASE);
	major = GENI_SE_VERSION_MAJOR(ver);
	if (major == 1)
		spi12_dev.oversampling = 2;
	else
		spi12_dev.oversampling = 1;

	/* Initialize SE */
	geni_se_init(spi12_dev.base, tx_depth - 3, tx_depth - 2);

	/* Set TX watermark */
	geni_write32(spi12_dev.base, SE_GENI_TX_WATERMARK_REG, spi12_dev.tx_wm);

	/* Select FIFO mode */
	geni_se_select_mode(spi12_dev.base, GENI_SE_FIFO);

	/* Initialize SPI registers to default values */
	geni_write32(spi12_dev.base, SE_SPI_LOOPBACK, 0);
	geni_write32(spi12_dev.base, SE_SPI_DEMUX_SEL, 0);
	geni_write32(spi12_dev.base, SE_SPI_CPHA, 0);
	geni_write32(spi12_dev.base, SE_SPI_CPOL, 0);
	geni_write32(spi12_dev.base, SE_SPI_DEMUX_OUTPUT_INV, 0);

	/* Disable CS toggle (controller manages CS) */
	u32 spi_tx_cfg = geni_read32(spi12_dev.base, SE_SPI_TRANS_CFG);
	spi_tx_cfg &= ~CS_TOGGLE;
	geni_write32(spi12_dev.base, SE_SPI_TRANS_CFG, spi_tx_cfg);

	/* Set default speed and word length */
	geni_spi_set_clock(&spi12_dev, max_speed_hz);
	geni_spi_setup_word_len(&spi12_dev, 0, SPI_DEFAULT_BPW);

	return 0;
}

int spi12_set_mode(u8 cs, u8 mode)
{
	if (cs > 3)
		return -1;

	geni_spi_setup_mode(&spi12_dev, cs, mode);
	return 0;
}

int spi12_set_speed(u32 speed_hz)
{
	return geni_spi_set_clock(&spi12_dev, speed_hz);
}

int spi12_transfer(u8 cs, struct spi_transfer *xfer)
{
	int ret;
	u8 bpw;
	u32 speed;

	if (!xfer)
		return -1;
	if (cs > 3)
		return -1;

	/* Setup CS and mode */
	geni_spi_setup_mode(&spi12_dev, cs, spi12_dev.cur_mode);

	/* Setup bits per word */
	bpw = xfer->bits_per_word ? xfer->bits_per_word : SPI_DEFAULT_BPW;
	if (bpw != spi12_dev.cur_bits_per_word)
		geni_spi_setup_word_len(&spi12_dev, spi12_dev.cur_mode, bpw);

	/* Setup speed */
	speed = xfer->speed_hz ? xfer->speed_hz : spi12_dev.cur_speed_hz;
	if (speed != spi12_dev.cur_speed_hz)
		geni_spi_set_clock(&spi12_dev, speed);

	/* Perform transfer */
	ret = geni_spi_xfer_fifo(&spi12_dev, xfer->tx_buf, xfer->rx_buf, xfer->len);
	if (ret)
		return ret;

	return xfer->len;
}

int spi12_write(u8 cs, const u8 *tx_buf, u32 len)
{
	struct spi_transfer xfer = {
		.tx_buf = tx_buf,
		.rx_buf = NULL,
		.len = len,
		.speed_hz = 0,
		.bits_per_word = 0,
		.cs_change = 0,
	};

	return spi12_transfer(cs, &xfer);
}

int spi12_read(u8 cs, u8 *rx_buf, u32 len)
{
	struct spi_transfer xfer = {
		.tx_buf = NULL,
		.rx_buf = rx_buf,
		.len = len,
		.speed_hz = 0,
		.bits_per_word = 0,
		.cs_change = 0,
	};

	return spi12_transfer(cs, &xfer);
}

int spi12_write_read(u8 cs, const u8 *tx_buf, u8 *rx_buf, u32 len)
{
	struct spi_transfer xfer = {
		.tx_buf = tx_buf,
		.rx_buf = rx_buf,
		.len = len,
		.speed_hz = 0,
		.bits_per_word = 0,
		.cs_change = 0,
	};

	return spi12_transfer(cs, &xfer);
}

int spi12_write_then_read(u8 cs, const u8 *tx_buf, u32 tx_len,
			  u8 *rx_buf, u32 rx_len)
{
	int ret;

	/* First transfer: TX only */
	ret = spi12_write(cs, tx_buf, tx_len);
	if (ret < 0)
		return ret;

	/* Second transfer: RX only */
	ret = spi12_read(cs, rx_buf, rx_len);
	if (ret < 0)
		return ret;

	return tx_len + rx_len;
}
