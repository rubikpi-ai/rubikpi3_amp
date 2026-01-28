/*
 * QCOM GENI I2C driver for baremetal
 *
 * This driver implements I2C communication using GENI Serial Engine
 * in FIFO mode (no DMA).
 *
 * Reference: Linux kernel drivers/i2c/busses/i2c-qcom-geni.c
 */

#include <type.h>
#include <i2c_geni.h>
#include <uart_geni.h>	/* For common GENI SE register definitions */
#include <gpio.h>
#include <clock.h>
#include <clock_qcs6490.h>

#define BITS_PER_BYTE		8
#define BYTES_PER_FIFO_WORD	4U
#define I2C_TIMEOUT		1000000

/* MMIO helpers */
static inline u32 i2c_r32(u64 base, u32 off)
{
	return *(volatile u32 *)(base + off);
}

static inline void i2c_w32(u64 base, u32 off, u32 v)
{
	*(volatile u32 *)(base + off) = v;
}

/*
 * I2C clock timing configuration for 19.2 MHz source clock
 *
 * t_high = (t_high_cnt * clk_div) / 19.2 MHz
 * t_low  = (t_low_cnt * clk_div) / 19.2 MHz
 * t_cycle = (t_cycle_cnt * clk_div) / 19.2 MHz
 */
static const struct geni_i2c_clk_fld geni_i2c_clk_map[] = {
	{ I2C_STANDARD_MODE_FREQ,    7, 10, 12, 26 },	/* 100 kHz */
	{ I2C_FAST_MODE_FREQ,        2,  5, 11, 22 },	/* 400 kHz */
	{ I2C_FAST_MODE_PLUS_FREQ,   1,  2,  8, 18 },	/* 1 MHz */
	{ 0 },
};

/* Global I2C1 device context */
static struct geni_i2c_dev i2c1_dev = {
	.base = I2C1_BASE,
	.clk_freq_out = I2C_STANDARD_MODE_FREQ,
	.clk_fld = NULL,
	.err = 0,
};

static const struct geni_i2c_clk_fld *geni_i2c_find_clk_fld(u32 clk_freq)
{
	const struct geni_i2c_clk_fld *itr = geni_i2c_clk_map;

	while (itr->clk_freq_out) {
		if (itr->clk_freq_out == clk_freq)
			return itr;
		itr++;
	}
	return NULL;
}

static void geni_i2c_se_irq_clear(u64 base)
{
	i2c_w32(base, SE_GSI_EVENT_EN, 0);
	i2c_w32(base, SE_GENI_M_IRQ_CLEAR, 0xffffffff);
	i2c_w32(base, SE_GENI_S_IRQ_CLEAR, 0xffffffff);
	i2c_w32(base, SE_DMA_TX_IRQ_CLR, 0xffffffff);
	i2c_w32(base, SE_DMA_RX_IRQ_CLR, 0xffffffff);
}

static void geni_i2c_se_io_init(u64 base)
{
	u32 val;

	/* Enable clock gating */
	val = i2c_r32(base, SE_GENI_CGC_CTRL);
	val |= DEFAULT_CGC_EN;
	i2c_w32(base, SE_GENI_CGC_CTRL, val);

	/* Enable DMA clocks */
	val = i2c_r32(base, SE_DMA_GENERAL_CFG);
	val |= AHB_SEC_SLV_CLK_CGC_ON | DMA_AHB_SLV_CLK_CGC_ON;
	val |= DMA_TX_CLK_CGC_ON | DMA_RX_CLK_CGC_ON;
	i2c_w32(base, SE_DMA_GENERAL_CFG, val);

	/* Set default IO output control */
	i2c_w32(base, GENI_OUTPUT_CTRL, DEFAULT_IO_OUTPUT_CTRL_MSK);
	i2c_w32(base, GENI_FORCE_DEFAULT_REG, FORCE_DEFAULT);
}

static void geni_i2c_se_set_mode(u64 base)
{
	u32 val;

	/* Enable GENI M/S IRQs and DMA IRQs */
	val = i2c_r32(base, SE_IRQ_EN);
	val |= GENI_M_IRQ_EN | GENI_S_IRQ_EN;
	val |= DMA_TX_IRQ_EN | DMA_RX_IRQ_EN;
	i2c_w32(base, SE_IRQ_EN, val);

	/* Disable DMA mode, use FIFO */
	val = i2c_r32(base, SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	i2c_w32(base, SE_GENI_DMA_MODE_EN, val);

	/* Disable GSI events */
	i2c_w32(base, SE_GSI_EVENT_EN, 0);
}

static u32 geni_i2c_get_tx_fifo_depth(u64 base)
{
	u32 val = i2c_r32(base, SE_HW_PARAM_0);
	return (val >> 16) & 0x3F;
}

static void geni_i2c_se_init(u64 base, u32 tx_wm, u32 rx_wm)
{
	u32 val;

	geni_i2c_se_irq_clear(base);
	geni_i2c_se_io_init(base);
	geni_i2c_se_set_mode(base);

	/* Set watermarks */
	i2c_w32(base, SE_GENI_TX_WATERMARK_REG, tx_wm);
	i2c_w32(base, SE_GENI_RX_WATERMARK_REG, rx_wm);

	/* Enable M_IRQs for I2C */
	val = i2c_r32(base, SE_GENI_M_IRQ_EN);
	val |= M_COMMON_GENI_M_IRQ_EN;
	val |= M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN;
	val |= M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
	i2c_w32(base, SE_GENI_M_IRQ_EN, val);
}

static void geni_i2c_config_packing(u64 base, int bpw, int pack_words,
				    bool msb_to_lsb, bool tx_cfg, bool rx_cfg)
{
	u32 cfg0, cfg1, cfg[4] = {0};
	int len, idx, idx_start, idx_delta;
	int ceil_bpw, iter, temp_bpw;
	int i;

	idx_start = msb_to_lsb ? bpw - 1 : 0;
	idx = idx_start;
	idx_delta = msb_to_lsb ? -BITS_PER_BYTE : BITS_PER_BYTE;
	ceil_bpw = ALIGN(bpw, BITS_PER_BYTE);
	iter = (ceil_bpw * pack_words) / BITS_PER_BYTE;
	temp_bpw = bpw;

	if (iter <= 0 || iter > 4)
		return;

	for (i = 0; i < iter; i++) {
		len = (temp_bpw < BITS_PER_BYTE ? temp_bpw : BITS_PER_BYTE) - 1;
		cfg[i] = idx << 5;			/* PACKING_START_SHIFT */
		cfg[i] |= msb_to_lsb << 4;		/* PACKING_DIR_SHIFT */
		cfg[i] |= len << 1;			/* PACKING_LEN_SHIFT */

		if (temp_bpw <= BITS_PER_BYTE) {
			idx = ((i + 1) * BITS_PER_BYTE) + idx_start;
			temp_bpw = bpw;
		} else {
			idx = idx + idx_delta;
			temp_bpw = temp_bpw - BITS_PER_BYTE;
		}
	}
	cfg[iter - 1] |= 1;  /* PACKING_STOP_BIT */

	cfg0 = cfg[0] | (cfg[1] << 10);
	cfg1 = cfg[2] | (cfg[3] << 10);

	if (tx_cfg) {
		i2c_w32(base, SE_GENI_TX_PACKING_CFG0, cfg0);
		i2c_w32(base, SE_GENI_TX_PACKING_CFG1, cfg1);
	}
	if (rx_cfg) {
		i2c_w32(base, SE_GENI_RX_PACKING_CFG0, cfg0);
		i2c_w32(base, SE_GENI_RX_PACKING_CFG1, cfg1);
	}

	/* BYTE_GRAN: 0 = 4x8 (four words, max 8 bits each) */
	if (pack_words || bpw == 32)
		i2c_w32(base, SE_GENI_BYTE_GRAN, bpw / 16);
}

static void geni_i2c_conf(struct geni_i2c_dev *gi2c)
{
	const struct geni_i2c_clk_fld *itr = gi2c->clk_fld;
	u32 val;

	if (!itr)
		return;

	/* Select clock source 0 */
	i2c_w32(gi2c->base, SE_GENI_CLK_SEL, 0);

	/* Configure clock divider */
	val = (itr->clk_div << 4) | SER_CLK_EN;
	i2c_w32(gi2c->base, GENI_SER_M_CLK_CFG, val);

	/* Configure SCL timing counters */
	val = itr->t_high_cnt << HIGH_COUNTER_SHFT;
	val |= itr->t_low_cnt << LOW_COUNTER_SHFT;
	val |= itr->t_cycle_cnt;
	i2c_w32(gi2c->base, SE_I2C_SCL_COUNTERS, val);
}

static int geni_i2c_wait_cmd_done(struct geni_i2c_dev *gi2c)
{
	u32 m_stat, timeout = I2C_TIMEOUT;

	while (timeout--) {
		m_stat = i2c_r32(gi2c->base, SE_GENI_M_IRQ_STATUS);

		/* Check for errors */
		if (m_stat & M_GP_IRQ_1_EN) {
			gi2c->err = -I2C_NACK;
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return gi2c->err;
		}
		if (m_stat & M_GP_IRQ_3_EN) {
			gi2c->err = -I2C_BUS_PROTO;
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return gi2c->err;
		}
		if (m_stat & M_GP_IRQ_4_EN) {
			gi2c->err = -I2C_ARB_LOST;
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return gi2c->err;
		}
		if (m_stat & M_CMD_OVERRUN_EN) {
			gi2c->err = -I2C_GENI_OVERRUN;
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return gi2c->err;
		}
		if (m_stat & M_ILLEGAL_CMD_EN) {
			gi2c->err = -I2C_GENI_ILLEGAL_CMD;
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return gi2c->err;
		}

		/* Check for command done */
		if (m_stat & M_CMD_DONE_EN) {
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			return 0;
		}
	}

	gi2c->err = -I2C_GENI_TIMEOUT;
	return gi2c->err;
}

static int geni_i2c_tx_one_msg(struct geni_i2c_dev *gi2c, struct i2c_msg *msg,
			       u32 m_param)
{
	u32 i, j, val;
	const u8 *buf = msg->buf;
	u32 len = msg->len;
	u32 fifo_depth = geni_i2c_get_tx_fifo_depth(gi2c->base);
	u32 tx_wm = fifo_depth > 1 ? fifo_depth - 1 : 1;
	u32 cur_wr = 0;

	/* Set TX length */
	i2c_w32(gi2c->base, SE_I2C_TX_TRANS_LEN, len);

	/* Issue I2C WRITE command */
	val = (I2C_WRITE << 27) | m_param;
	i2c_w32(gi2c->base, SE_GENI_M_CMD0, val);

	/* Write data to FIFO */
	while (cur_wr < len) {
		u32 m_stat = i2c_r32(gi2c->base, SE_GENI_M_IRQ_STATUS);

		/* Check for TX FIFO watermark or any error */
		if (m_stat & (M_TX_FIFO_WATERMARK_EN | M_CMD_DONE_EN |
			      M_GP_IRQ_1_EN | M_GP_IRQ_3_EN | M_GP_IRQ_4_EN)) {

			/* Check for errors first */
			if (m_stat & (M_GP_IRQ_1_EN | M_GP_IRQ_3_EN | M_GP_IRQ_4_EN)) {
				i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
				gi2c->err = -I2C_NACK;
				return gi2c->err;
			}

			/* Clear watermark IRQ */
			if (m_stat & M_TX_FIFO_WATERMARK_EN)
				i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, M_TX_FIFO_WATERMARK_EN);

			/* Write words to FIFO */
			for (j = 0; j < tx_wm && cur_wr < len; j++) {
				val = 0;
				for (i = 0; i < 4 && cur_wr < len; i++) {
					val |= ((u32)buf[cur_wr++]) << (i * 8);
				}
				i2c_w32(gi2c->base, SE_GENI_TX_FIFOn, val);
			}

			/* Disable TX watermark if all data written */
			if (cur_wr >= len) {
				i2c_w32(gi2c->base, SE_GENI_TX_WATERMARK_REG, 0);
			}
		}

		/* Check for command done */
		if (m_stat & M_CMD_DONE_EN) {
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
			break;
		}
	}

	/* Wait for command completion if not already done */
	return geni_i2c_wait_cmd_done(gi2c);
}

static int geni_i2c_rx_one_msg(struct geni_i2c_dev *gi2c, struct i2c_msg *msg,
			       u32 m_param)
{
	u32 i, val;
	u8 *buf = msg->buf;
	u32 len = msg->len;
	u32 cur_rd = 0;
	u32 timeout = I2C_TIMEOUT;

	/* Set RX length */
	i2c_w32(gi2c->base, SE_I2C_RX_TRANS_LEN, len);

	/* Issue I2C READ command */
	val = (I2C_READ << 27) | m_param;
	i2c_w32(gi2c->base, SE_GENI_M_CMD0, val);

	/* Read data from FIFO */
	while (cur_rd < len && timeout--) {
		u32 m_stat = i2c_r32(gi2c->base, SE_GENI_M_IRQ_STATUS);
		u32 rx_stat = i2c_r32(gi2c->base, SE_GENI_RX_FIFO_STATUS);
		u32 rx_wc = rx_stat & 0x1FFFFFF;  /* RX_FIFO_WC_MSK */

		/* Check for errors */
		if (m_stat & (M_GP_IRQ_1_EN | M_GP_IRQ_3_EN | M_GP_IRQ_4_EN)) {
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, m_stat);
			gi2c->err = -I2C_NACK;
			return gi2c->err;
		}

		/* Read available data */
		if (rx_wc > 0) {
			for (i = 0; i < rx_wc && cur_rd < len; i++) {
				val = i2c_r32(gi2c->base, SE_GENI_RX_FIFOn);
				u32 j;
				for (j = 0; j < 4 && cur_rd < len; j++) {
					buf[cur_rd++] = (val >> (j * 8)) & 0xFF;
				}
			}
		}

		/* Clear RX watermark and last IRQs */
		if (m_stat & (M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN))
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR,
				M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN);

		/* Check for command done */
		if (m_stat & M_CMD_DONE_EN) {
			i2c_w32(gi2c->base, SE_GENI_M_IRQ_CLEAR, M_CMD_DONE_EN);
			break;
		}
	}

	if (timeout == 0) {
		gi2c->err = -I2C_GENI_TIMEOUT;
		return gi2c->err;
	}

	return 0;
}

/*
 * Public API
 */

int i2c1_init(u32 clk_freq)
{
	u32 tx_depth, tx_wm;
	u32 proto;

	/* Default to 100 kHz if not specified */
	if (clk_freq == 0)
		clk_freq = I2C_STANDARD_MODE_FREQ;

	/* Find clock configuration */
	i2c1_dev.clk_freq_out = clk_freq;
	i2c1_dev.clk_fld = geni_i2c_find_clk_fld(clk_freq);
	if (!i2c1_dev.clk_fld)
		return -1;

	/* Configure GPIO pins for I2C1: GPIO 4 (SDA), GPIO 5 (SCL) */
	gpio_pinmux_set(4, mux_qup01);
	gpio_pinmux_set(5, mux_qup01);

	/* Enable I2C1 clock */
	clk_enable(I2C1_CLK);

	/* Set clock rate to 19.2 MHz (source clock for I2C timing) */
	clk_set_rate(I2C1_CLK, 19200000);

	/* Check protocol type */
	proto = i2c_r32(i2c1_dev.base, GENI_FW_REVISION_RO);
	proto = (proto >> 8) & 0xFF;
	if (proto != GENI_SE_I2C) {
		/* Protocol not configured for I2C */
		return -2;
	}

	/* Get FIFO depth */
	tx_depth = geni_i2c_get_tx_fifo_depth(i2c1_dev.base);
	if (tx_depth == 0)
		tx_depth = 16;  /* Default */
	tx_wm = tx_depth - 1;

	/* Initialize SE */
	geni_i2c_se_init(i2c1_dev.base, tx_wm, tx_depth);

	/* Configure packing: 8 bits, 4 words per FIFO entry */
	geni_i2c_config_packing(i2c1_dev.base, BITS_PER_BYTE,
				BYTES_PER_FIFO_WORD, true, true, true);

	/* Configure I2C timing */
	geni_i2c_conf(&i2c1_dev);

	i2c1_dev.err = 0;
	return 0;
}

int i2c1_transfer(struct i2c_msg *msgs, int num)
{
	int i, ret;
	u32 m_param;

	if (!msgs || num <= 0)
		return -1;

	i2c1_dev.err = 0;

	/* Configure I2C timing before transfer */
	geni_i2c_conf(&i2c1_dev);

	for (i = 0; i < num; i++) {
		/* Build M_CMD parameter */
		m_param = (i < (num - 1)) ? STOP_STRETCH : 0;
		m_param |= (msgs[i].addr << SLV_ADDR_SHFT) & SLV_ADDR_MSK;

		if (msgs[i].flags & I2C_M_RD)
			ret = geni_i2c_rx_one_msg(&i2c1_dev, &msgs[i], m_param);
		else
			ret = geni_i2c_tx_one_msg(&i2c1_dev, &msgs[i], m_param);

		if (ret)
			return ret;
	}

	return num;
}

int i2c1_write(u8 addr, const u8 *buf, u32 len)
{
	struct i2c_msg msg = {
		.addr = addr,
		.flags = 0,
		.len = len,
		.buf = (u8 *)buf,
	};

	return i2c1_transfer(&msg, 1);
}

int i2c1_read(u8 addr, u8 *buf, u32 len)
{
	struct i2c_msg msg = {
		.addr = addr,
		.flags = I2C_M_RD,
		.len = len,
		.buf = buf,
	};

	return i2c1_transfer(&msg, 1);
}

int i2c1_write_read(u8 addr, const u8 *tx_buf, u32 tx_len, u8 *rx_buf, u32 rx_len)
{
	struct i2c_msg msgs[2] = {
		{
			.addr = addr,
			.flags = 0,
			.len = tx_len,
			.buf = (u8 *)tx_buf,
		},
		{
			.addr = addr,
			.flags = I2C_M_RD,
			.len = rx_len,
			.buf = rx_buf,
		},
	};

	return i2c1_transfer(msgs, 2);
}

int i2c1_reg_write(u8 addr, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	return i2c1_write(addr, buf, 2);
}

int i2c1_reg_read(u8 addr, u8 reg, u8 *val)
{
	return i2c1_write_read(addr, &reg, 1, val, 1);
}

int i2c1_reg_write_buf(u8 addr, u8 reg, const u8 *buf, u32 len)
{
	u8 tx_buf[len + 1];
	tx_buf[0] = reg;
	for (u32 i = 0; i < len; i++)
		tx_buf[i + 1] = buf[i];
	return i2c1_write(addr, tx_buf, len + 1);
}

int i2c1_reg_read_buf(u8 addr, u8 reg, u8 *buf, u32 len)
{
	return i2c1_write_read(addr, &reg, 1, buf, len);
}
