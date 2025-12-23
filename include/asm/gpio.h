#ifndef GPIO_H
#define	GPIO_H

#include <type.h>

struct gpio_tlmm
{
	unsigned *funcs;
	unsigned nfuncs;

	u32 ctl_reg;
	u32 io_reg;
	u32 intr_cfg_reg;
	u32 intr_status_reg;
	u32 intr_target_reg;

	unsigned int tile:2;

	unsigned mux_bit:5;

	unsigned pull_bit:5;
	unsigned drv_bit:5;
	unsigned i2c_pull_bit:5;

	unsigned od_bit:5;
	unsigned egpio_enable:5;
	unsigned egpio_present:5;
	unsigned oe_bit:5;
	unsigned in_bit:5;
	unsigned out_bit:5;

	unsigned intr_enable_bit:5;
	unsigned intr_status_bit:5;
	unsigned intr_ack_high:1;

	unsigned intr_wakeup_present_bit:5;
	unsigned intr_wakeup_enable_bit:5;
	unsigned intr_target_bit:5;
	unsigned intr_target_width:5;
	unsigned intr_target_kpss_val:5;
	unsigned intr_raw_status_bit:5;
	unsigned intr_polarity_bit:5;
	unsigned intr_detection_bit:5;
	unsigned intr_detection_width:5;
};

#endif

