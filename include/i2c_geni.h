#ifndef __I2C_GENI_H
#define __I2C_GENI_H

#include <type.h>
#include <geni_se.h>

/*
 * QCOM GENI I2C driver for baremetal
 *
 * I2C1: i2c@984000
 *   reg = <0 0x00984000 0 0x4000>
 *   GPIO: 4 (SDA), 5 (SCL) - function qup01
 *   Clock: GCC_QUPV3_WRAP0_S1_CLK
 *   Interrupt: GIC_SPI 602
 */

#define I2C1_BASE		QUPV3_SE1_BASE

/* I2C specific registers (offset from SE base) */
#define SE_I2C_TX_TRANS_LEN	0x26c
#define SE_I2C_RX_TRANS_LEN	0x270
#define SE_I2C_SCL_COUNTERS	0x278

/* M_CMD OP codes for I2C */
#define I2C_WRITE		0x1
#define I2C_READ		0x2
#define I2C_WRITE_READ		0x3
#define I2C_ADDR_ONLY		0x4
#define I2C_BUS_CLEAR		0x6
#define I2C_STOP_ON_BUS		0x7

/* M_CMD params for I2C */
#define PRE_CMD_DELAY		BIT(0)
#define TIMESTAMP_BEFORE	BIT(1)
#define STOP_STRETCH		BIT(2)
#define TIMESTAMP_AFTER		BIT(3)
#define POST_COMMAND_DELAY	BIT(4)
#define IGNORE_ADD_NACK		BIT(6)
#define READ_FINISHED_WITH_ACK	BIT(7)
#define BYPASS_ADDR_PHASE	BIT(8)
#define SLV_ADDR_MSK		GENMASK(15, 9)
#define SLV_ADDR_SHFT		9

/* I2C SCL COUNTER fields */
#define HIGH_COUNTER_MSK	GENMASK(29, 20)
#define HIGH_COUNTER_SHFT	20
#define LOW_COUNTER_MSK		GENMASK(19, 10)
#define LOW_COUNTER_SHFT	10
#define CYCLE_COUNTER_MSK	GENMASK(9, 0)

/* I2C Packing config */
#define I2C_PACK_TX		BIT(0)
#define I2C_PACK_RX		BIT(1)

/* GENI Protocol types */
#define GENI_SE_I2C		3

/* I2C error codes */
enum geni_i2c_err_code {
	I2C_GP_IRQ0 = 0,
	I2C_NACK,
	I2C_GP_IRQ2,
	I2C_BUS_PROTO,
	I2C_ARB_LOST,
	I2C_GP_IRQ5,
	I2C_GENI_OVERRUN,
	I2C_GENI_ILLEGAL_CMD,
	I2C_GENI_ABORT_DONE,
	I2C_GENI_TIMEOUT,
};

/*
 * I2C clock timing configuration
 *
 * Hardware uses the formula:
 *   t_high = (t_high_cnt * clk_div) / source_clock
 *   t_low = (t_low_cnt * clk_div) / source_clock
 *   t_cycle = (t_cycle_cnt * clk_div) / source_clock
 *   clk_freq_out = 1 / t_cycle
 *
 * source_clock = 19.2 MHz (default SE clock)
 */
struct geni_i2c_clk_fld {
	u32 clk_freq_out;
	u8 clk_div;
	u8 t_high_cnt;
	u8 t_low_cnt;
	u8 t_cycle_cnt;
};

/* I2C message flags */
#define I2C_M_RD		0x0001	/* read data, from slave to master */
#define I2C_M_TEN		0x0010	/* 10-bit slave address */
#define I2C_M_STOP		0x8000	/* send STOP after this message */

/* I2C message structure */
struct i2c_msg {
	u16 addr;	/* slave address */
	u16 flags;	/* I2C_M_* flags */
	u16 len;	/* msg length */
	u8 *buf;	/* pointer to msg data */
};

/* Standard I2C speeds */
#define I2C_STANDARD_MODE_FREQ	100000	/* 100 kHz */
#define I2C_FAST_MODE_FREQ	400000	/* 400 kHz */
#define I2C_FAST_MODE_PLUS_FREQ	1000000	/* 1 MHz */

/* I2C controller structure */
struct geni_i2c_dev {
	u64 base;
	u32 clk_freq_out;
	const struct geni_i2c_clk_fld *clk_fld;
	int err;
};

/* Public API */
int i2c1_init(u32 clk_freq);
int i2c1_write(u8 addr, const u8 *buf, u32 len);
int i2c1_read(u8 addr, u8 *buf, u32 len);
int i2c1_write_read(u8 addr, const u8 *tx_buf, u32 tx_len, u8 *rx_buf, u32 rx_len);
int i2c1_transfer(struct i2c_msg *msgs, int num);

/* Register a byte to/from a device */
int i2c1_reg_write(u8 addr, u8 reg, u8 val);
int i2c1_reg_read(u8 addr, u8 reg, u8 *val);

/* Write/read multiple bytes to/from a register */
int i2c1_reg_write_buf(u8 addr, u8 reg, const u8 *buf, u32 len);
int i2c1_reg_read_buf(u8 addr, u8 reg, u8 *buf, u32 len);

#endif /* __I2C_GENI_H */
