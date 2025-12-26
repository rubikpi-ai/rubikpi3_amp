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

enum gpio_functions {
	mux_atest_char,
	mux_atest_char0,
	mux_atest_char1,
	mux_atest_char2,
	mux_atest_char3,
	mux_atest_usb0,
	mux_atest_usb00,
	mux_atest_usb01,
	mux_atest_usb02,
	mux_atest_usb03,
	mux_atest_usb1,
	mux_atest_usb10,
	mux_atest_usb11,
	mux_atest_usb12,
	mux_atest_usb13,
	mux_audio_ref,
	mux_cam_mclk,
	mux_cci_async,
	mux_cci_i2c,
	mux_cci_timer0,
	mux_cci_timer1,
	mux_cci_timer2,
	mux_cci_timer3,
	mux_cci_timer4,
	mux_cmu_rng0,
	mux_cmu_rng1,
	mux_cmu_rng2,
	mux_cmu_rng3,
	mux_coex_uart1,
	mux_cri_trng,
	mux_cri_trng0,
	mux_cri_trng1,
	mux_dbg_out,
	mux_ddr_bist,
	mux_ddr_pxi0,
	mux_ddr_pxi1,
	mux_dp_hot,
	mux_dp_lcd,
	mux_edp_hot,
	mux_edp_lcd,
	mux_egpio,
	mux_gcc_gp1,
	mux_gcc_gp2,
	mux_gcc_gp3,
	mux_gpio,
	mux_host2wlan_sol,
	mux_ibi_i3c,
	mux_jitter_bist,
	mux_lpass_slimbus,
	mux_mdp_vsync,
	mux_mdp_vsync0,
	mux_mdp_vsync1,
	mux_mdp_vsync2,
	mux_mdp_vsync3,
	mux_mdp_vsync4,
	mux_mdp_vsync5,
	mux_mi2s0_data0,
	mux_mi2s0_data1,
	mux_mi2s0_sck,
	mux_mi2s0_ws,
	mux_mi2s1_data0,
	mux_mi2s1_data1,
	mux_mi2s1_sck,
	mux_mi2s1_ws,
	mux_mi2s2_data0,
	mux_mi2s2_data1,
	mux_mi2s2_sck,
	mux_mi2s2_ws,
	mux_mss_grfc0,
	mux_mss_grfc1,
	mux_mss_grfc10,
	mux_mss_grfc11,
	mux_mss_grfc12,
	mux_mss_grfc2,
	mux_mss_grfc3,
	mux_mss_grfc4,
	mux_mss_grfc5,
	mux_mss_grfc6,
	mux_mss_grfc7,
	mux_mss_grfc8,
	mux_mss_grfc9,
	mux_nav_gpio0,
	mux_nav_gpio1,
	mux_nav_gpio2,
	mux_pa_indicator,
	mux_pcie0_clkreqn,
	mux_pcie1_clkreqn,
	mux_phase_flag,
	mux_pll_bist,
	mux_pll_bypassnl,
	mux_pll_clk,
	mux_pll_reset,
	mux_pri_mi2s,
	mux_prng_rosc,
	mux_qdss,
	mux_qdss_cti,
	mux_qlink0_enable,
	mux_qlink0_request,
	mux_qlink0_wmss,
	mux_qlink1_enable,
	mux_qlink1_request,
	mux_qlink1_wmss,
	mux_qspi_clk,
	mux_qspi_cs,
	mux_qspi_data,
	mux_qup00,
	mux_qup01,
	mux_qup02,
	mux_qup03,
	mux_qup04,
	mux_qup05,
	mux_qup06,
	mux_qup07,
	mux_qup10,
	mux_qup11,
	mux_qup12,
	mux_qup13,
	mux_qup14,
	mux_qup15,
	mux_qup16,
	mux_qup17,
	mux_sd_write,
	mux_sdc40,
	mux_sdc41,
	mux_sdc42,
	mux_sdc43,
	mux_sdc4_clk,
	mux_sdc4_cmd,
	mux_sec_mi2s,
	mux_tb_trig,
	mux_tgu_ch0,
	mux_tgu_ch1,
	mux_tsense_pwm1,
	mux_tsense_pwm2,
	mux_uim0_clk,
	mux_uim0_data,
	mux_uim0_present,
	mux_uim0_reset,
	mux_uim1_clk,
	mux_uim1_data,
	mux_uim1_present,
	mux_uim1_reset,
	mux_usb2phy_ac,
	mux_usb_phy,
	mux_vfr_0,
	mux_vfr_1,
	mux_vsense_trigger,
	mux__,
};

#endif
