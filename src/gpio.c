#include <array_size.h>
#include <asm/gpio.h>

#define GPIO(id, f1, f2, f3, f4, f5, f6, f7, f8, f9)	\
	{					        \
		.funcs = (int[]){			\
			msm_mux_gpio, /* gpio mode */	\
			msm_mux_##f1,			\
			msm_mux_##f2,			\
			msm_mux_##f3,			\
			msm_mux_##f4,			\
			msm_mux_##f5,			\
			msm_mux_##f6,			\
			msm_mux_##f7,			\
			msm_mux_##f8,			\
			msm_mux_##f9			\
		},				        \
		.nfuncs = 10,				\
		.ctl_reg = 0x1000 * id,			\
		.io_reg = 0x1000 * id + 0x4,		\
		.intr_cfg_reg = 0x1000 * id + 0x8,	\
		.intr_status_reg = 0x1000 * id + 0xc,	\
		.intr_target_reg = 0x1000 * id + 0x8,	\
		.mux_bit = 2,			\
		.pull_bit = 0,			\
		.drv_bit = 6,			\
		.egpio_enable = 12,		\
		.egpio_present = 11,		\
		.oe_bit = 9,			\
		.in_bit = 0,			\
		.out_bit = 1,			\
		.intr_enable_bit = 0,		\
		.intr_status_bit = 0,		\
		.intr_target_bit = 5,		\
		.intr_target_kpss_val = 3,	\
		.intr_raw_status_bit = 4,	\
		.intr_polarity_bit = 1,		\
		.intr_detection_bit = 2,	\
		.intr_detection_width = 2,	\
	}

enum gpio_functions {
	msm_mux_atest_char,
	msm_mux_atest_char0,
	msm_mux_atest_char1,
	msm_mux_atest_char2,
	msm_mux_atest_char3,
	msm_mux_atest_usb0,
	msm_mux_atest_usb00,
	msm_mux_atest_usb01,
	msm_mux_atest_usb02,
	msm_mux_atest_usb03,
	msm_mux_atest_usb1,
	msm_mux_atest_usb10,
	msm_mux_atest_usb11,
	msm_mux_atest_usb12,
	msm_mux_atest_usb13,
	msm_mux_audio_ref,
	msm_mux_cam_mclk,
	msm_mux_cci_async,
	msm_mux_cci_i2c,
	msm_mux_cci_timer0,
	msm_mux_cci_timer1,
	msm_mux_cci_timer2,
	msm_mux_cci_timer3,
	msm_mux_cci_timer4,
	msm_mux_cmu_rng0,
	msm_mux_cmu_rng1,
	msm_mux_cmu_rng2,
	msm_mux_cmu_rng3,
	msm_mux_coex_uart1,
	msm_mux_cri_trng,
	msm_mux_cri_trng0,
	msm_mux_cri_trng1,
	msm_mux_dbg_out,
	msm_mux_ddr_bist,
	msm_mux_ddr_pxi0,
	msm_mux_ddr_pxi1,
	msm_mux_dp_hot,
	msm_mux_dp_lcd,
	msm_mux_edp_hot,
	msm_mux_edp_lcd,
	msm_mux_egpio,
	msm_mux_gcc_gp1,
	msm_mux_gcc_gp2,
	msm_mux_gcc_gp3,
	msm_mux_gpio,
	msm_mux_host2wlan_sol,
	msm_mux_ibi_i3c,
	msm_mux_jitter_bist,
	msm_mux_lpass_slimbus,
	msm_mux_mdp_vsync,
	msm_mux_mdp_vsync0,
	msm_mux_mdp_vsync1,
	msm_mux_mdp_vsync2,
	msm_mux_mdp_vsync3,
	msm_mux_mdp_vsync4,
	msm_mux_mdp_vsync5,
	msm_mux_mi2s0_data0,
	msm_mux_mi2s0_data1,
	msm_mux_mi2s0_sck,
	msm_mux_mi2s0_ws,
	msm_mux_mi2s1_data0,
	msm_mux_mi2s1_data1,
	msm_mux_mi2s1_sck,
	msm_mux_mi2s1_ws,
	msm_mux_mi2s2_data0,
	msm_mux_mi2s2_data1,
	msm_mux_mi2s2_sck,
	msm_mux_mi2s2_ws,
	msm_mux_mss_grfc0,
	msm_mux_mss_grfc1,
	msm_mux_mss_grfc10,
	msm_mux_mss_grfc11,
	msm_mux_mss_grfc12,
	msm_mux_mss_grfc2,
	msm_mux_mss_grfc3,
	msm_mux_mss_grfc4,
	msm_mux_mss_grfc5,
	msm_mux_mss_grfc6,
	msm_mux_mss_grfc7,
	msm_mux_mss_grfc8,
	msm_mux_mss_grfc9,
	msm_mux_nav_gpio0,
	msm_mux_nav_gpio1,
	msm_mux_nav_gpio2,
	msm_mux_pa_indicator,
	msm_mux_pcie0_clkreqn,
	msm_mux_pcie1_clkreqn,
	msm_mux_phase_flag,
	msm_mux_pll_bist,
	msm_mux_pll_bypassnl,
	msm_mux_pll_clk,
	msm_mux_pll_reset,
	msm_mux_pri_mi2s,
	msm_mux_prng_rosc,
	msm_mux_qdss,
	msm_mux_qdss_cti,
	msm_mux_qlink0_enable,
	msm_mux_qlink0_request,
	msm_mux_qlink0_wmss,
	msm_mux_qlink1_enable,
	msm_mux_qlink1_request,
	msm_mux_qlink1_wmss,
	msm_mux_qspi_clk,
	msm_mux_qspi_cs,
	msm_mux_qspi_data,
	msm_mux_qup00,
	msm_mux_qup01,
	msm_mux_qup02,
	msm_mux_qup03,
	msm_mux_qup04,
	msm_mux_qup05,
	msm_mux_qup06,
	msm_mux_qup07,
	msm_mux_qup10,
	msm_mux_qup11,
	msm_mux_qup12,
	msm_mux_qup13,
	msm_mux_qup14,
	msm_mux_qup15,
	msm_mux_qup16,
	msm_mux_qup17,
	msm_mux_sd_write,
	msm_mux_sdc40,
	msm_mux_sdc41,
	msm_mux_sdc42,
	msm_mux_sdc43,
	msm_mux_sdc4_clk,
	msm_mux_sdc4_cmd,
	msm_mux_sec_mi2s,
	msm_mux_tb_trig,
	msm_mux_tgu_ch0,
	msm_mux_tgu_ch1,
	msm_mux_tsense_pwm1,
	msm_mux_tsense_pwm2,
	msm_mux_uim0_clk,
	msm_mux_uim0_data,
	msm_mux_uim0_present,
	msm_mux_uim0_reset,
	msm_mux_uim1_clk,
	msm_mux_uim1_data,
	msm_mux_uim1_present,
	msm_mux_uim1_reset,
	msm_mux_usb2phy_ac,
	msm_mux_usb_phy,
	msm_mux_vfr_0,
	msm_mux_vfr_1,
	msm_mux_vsense_trigger,
	msm_mux__,
};

static const struct gpio_tlmm gpio_desc[] = {
	[0] = GPIO(0, qup00, ibi_i3c, _, _, _, _, _, _, _),
	[1] = GPIO(1, qup00, ibi_i3c, _, _, _, _, _, _, _),
	[2] = GPIO(2, qup00, qup07, _, qdss, _, _, _, _, _),
	[3] = GPIO(3, qup00, qup07, _, qdss, _, _, _, _, _),
	[4] = GPIO(4, qup01, ibi_i3c, _, _, _, _, _, _, _),
	[5] = GPIO(5, qup01, ibi_i3c, _, _, _, _, _, _, _),
	[6] = GPIO(6, qup01, qup07, _, _, _, _, _, _, _),
	[7] = GPIO(7, qup01, _, _, _, _, _, _, _, _),
	[8] = GPIO(8, qup02, _, qdss, _, _, _, _, _, _),
	[9] = GPIO(9, qup02, _, qdss, _, _, _, _, _, _),
	[10] = GPIO(10, qup02, _, qdss, _, _, _, _, _, _),
	[11] = GPIO(11, qup02, _, qdss, _, _, _, _, _, _),
	[12] = GPIO(12, qup03, qspi_data, sdc40, tb_trig, phase_flag, qdss, ddr_pxi1, _, _),
	[13] = GPIO(13, qup03, qspi_data, sdc41, tb_trig, phase_flag, qdss, ddr_pxi1, _, _),
	[14] = GPIO(14, qup03, qspi_clk, sdc4_clk, mdp_vsync, phase_flag, ddr_pxi0, _, _, _),
	[15] = GPIO(15, qup03, qspi_cs, tb_trig, phase_flag, qdss_cti, ddr_pxi0, _, _, _),
	[16] = GPIO(16, qup04, qspi_data, sdc42, mdp_vsync, phase_flag, qdss_cti, _, _, _),
	[17] = GPIO(17, qup04, qspi_data, sdc43, _, phase_flag, _, _, _, _),
	[18] = GPIO(18, qup04, _, phase_flag, qdss_cti, _, _, _, _, _),
	[19] = GPIO(19, qup04, qspi_cs, sdc4_cmd, _, phase_flag, qdss_cti, _, _, _),
	[20] = GPIO(20, qup05, cci_timer0, _, qdss, _, _, _, _, _),
	[21] = GPIO(21, qup05, cci_timer1, _, qdss, _, _, _, _, _),
	[22] = GPIO(22, qup05, _, qdss, _, _, _, _, _, _),
	[23] = GPIO(23, qup05, _, qdss, _, _, _, _, _, _),
	[24] = GPIO(24, qup06, _, qdss, _, _, _, _, _, _),
	[25] = GPIO(25, qup06, _, qdss, _, _, _, _, _, _),
	[26] = GPIO(26, qup06, host2wlan_sol, _, qdss, _, _, _, _, _),
	[27] = GPIO(27, qup06, _, qdss, _, _, _, _, _, _),
	[28] = GPIO(28, qup07, _, qdss, _, _, _, _, _, _),
	[29] = GPIO(29, qup07, qdss, _, _, _, _, _, _, _),
	[30] = GPIO(30, qup07, _, _, _, _, _, _, _, _),
	[31] = GPIO(31, qup07, _, _, _, _, _, _, _, _),
	[32] = GPIO(32, qup10, _, _, _, _, _, _, _, _),
	[33] = GPIO(33, qup10, _, _, _, _, _, _, _, _),
	[34] = GPIO(34, qup10, _, _, _, _, _, _, _, _),
	[35] = GPIO(35, qup10, _, _, _, _, _, _, _, _),
	[36] = GPIO(36, qup11, ibi_i3c, _, _, _, _, _, _, _),
	[37] = GPIO(37, qup11, ibi_i3c, _, _, _, _, _, _, _),
	[38] = GPIO(38, qup11, qup14, dbg_out, _, _, _, _, _, _),
	[39] = GPIO(39, qup11, _, _, _, _, _, _, _, _),
	[40] = GPIO(40, qup12, _, _, _, _, _, _, _, _),
	[41] = GPIO(41, qup12, _, _, _, _, _, _, _, _),
	[42] = GPIO(42, qup12, _, _, _, _, _, _, _, _),
	[43] = GPIO(43, qup12, _, _, _, _, _, _, _, _),
	[44] = GPIO(44, qup13, _, _, _, _, _, _, _, _),
	[45] = GPIO(45, qup13, _, _, _, _, _, _, _, _),
	[46] = GPIO(46, qup13, edp_lcd, _, _, _, _, _, _, _),
	[47] = GPIO(47, qup13, dp_hot, _, _, _, _, _, _, _),
	[48] = GPIO(48, qup14, _, _, _, _, _, _, _, _),
	[49] = GPIO(49, qup14, _, _, _, _, _, _, _, _),
	[50] = GPIO(50, qup14, qup16, _, _, _, _, _, _, _),
	[51] = GPIO(51, qup14, _, _, _, _, _, _, _, _),
	[52] = GPIO(52, qup15, _, _, _, _, _, _, _, _),
	[53] = GPIO(53, qup15, _, _, _, _, _, _, _, _),
	[54] = GPIO(54, qup15, qup14, _, _, _, _, _, _, _),
	[55] = GPIO(55, qup15, qup14, _, _, _, _, _, _, _),
	[56] = GPIO(56, qup16, ddr_bist, phase_flag, _, _, _, _, _, _),
	[57] = GPIO(57, qup16, ddr_bist, phase_flag, _, _, _, _, _, _),
	[58] = GPIO(58, qup16, ddr_bist, phase_flag, qdss, _, _, _, _, _),
	[59] = GPIO(59, qup16, ddr_bist, phase_flag, qdss, _, _, _, _, _),
	[60] = GPIO(60, qup17, edp_hot, _, phase_flag, _, _, _, _, _),
	[61] = GPIO(61, qup17, sd_write, phase_flag, tsense_pwm1, tsense_pwm2, _, _, _, _),
	[62] = GPIO(62, qup17, qup16, phase_flag, _, _, _, _, _, _),
	[63] = GPIO(63, qup17, qup16, phase_flag, _, _, _, _, _, _),
	[64] = GPIO(64, cam_mclk, _, _, _, _, _, _, _, _),
	[65] = GPIO(65, cam_mclk, tgu_ch0, _, _, _, _, _, _, _),
	[66] = GPIO(66, cam_mclk, pll_bypassnl, tgu_ch1, _, _, _, _, _, _),
	[67] = GPIO(67, cam_mclk, pll_reset, _, _, _, _, _, _, _),
	[68] = GPIO(68, cam_mclk, _, _, _, _, _, _, _, _),
	[69] = GPIO(69, cci_i2c, _, _, _, _, _, _, _, _),
	[70] = GPIO(70, cci_i2c, _, _, _, _, _, _, _, _),
	[71] = GPIO(71, cci_i2c, _, _, _, _, _, _, _, _),
	[72] = GPIO(72, cci_i2c, _, _, _, _, _, _, _, _),
	[73] = GPIO(73, cci_i2c, _, _, _, _, _, _, _, _),
	[74] = GPIO(74, cci_i2c, _, _, _, _, _, _, _, _),
	[75] = GPIO(75, cci_i2c, _, _, _, _, _, _, _, _),
	[76] = GPIO(76, cci_i2c, gcc_gp1, _, _, _, _, _, _, _),
	[77] = GPIO(77, cci_timer2, gcc_gp2, _, atest_usb13, atest_char0, _, _, _, _),
	[78] = GPIO(78, cci_timer3, cci_async, gcc_gp3, _, atest_usb12, atest_char1, _, _, _),
	[79] = GPIO(79, cci_timer4, cci_async, pcie1_clkreqn, mdp_vsync, jitter_bist, atest_usb11, atest_char2, _, _),
	[80] = GPIO(80, mdp_vsync, vfr_0, mdp_vsync0, mdp_vsync1, mdp_vsync4, pll_bist, atest_usb10, atest_char3, _),
	[81] = GPIO(81, mdp_vsync, dp_lcd, mdp_vsync2, mdp_vsync3, mdp_vsync5, atest_usb1, atest_char, _, _),
	[82] = GPIO(82, _, _, _, _, _, _, _, _, _),
	[83] = GPIO(83, _, _, _, _, _, _, _, _, _),
	[84] = GPIO(84, usb2phy_ac, _, _, _, _, _, _, _, _),
	[85] = GPIO(85, usb2phy_ac, _, _, _, _, _, _, _, _),
	[86] = GPIO(86, _, _, _, _, _, _, _, _, _),
	[87] = GPIO(87, _, _, _, _, _, _, _, _, _),
	[88] = GPIO(88, pcie0_clkreqn, _, _, _, _, _, _, _, _),
	[89] = GPIO(89, _, _, _, _, _, _, _, _, _),
	[90] = GPIO(90, _, _, _, _, _, _, _, _, _),
	[91] = GPIO(91, _, _, _, _, _, _, _, _, _),
	[92] = GPIO(92, _, _, _, _, _, _, _, _, _),
	[93] = GPIO(93, cam_mclk, cci_async, _, _, _, _, _, _, _),
	[94] = GPIO(94, lpass_slimbus, _, _, _, _, _, _, _, _),
	[95] = GPIO(95, lpass_slimbus, _, _, _, _, _, _, _, _),
	[96] = GPIO(96, pri_mi2s, _, _, _, _, _, _, _, _),
	[97] = GPIO(97, mi2s0_sck, _, _, _, _, _, _, _, _),
	[98] = GPIO(98, mi2s0_data0, _, _, _, _, _, _, _, _),
	[99] = GPIO(99, mi2s0_data1, _, _, _, _, _, _, _, _),
	[100] = GPIO(100, mi2s0_ws, _, vsense_trigger, _, _, _, _, _, _),
	[101] = GPIO(101, mi2s2_sck, _, qdss, _, _, _, _, _, _),
	[102] = GPIO(102, mi2s2_data0, _, _, qdss, _, _, _, _, _),
	[103] = GPIO(103, mi2s2_ws, vfr_1, _, _, qdss, _, atest_usb03, _, _),
	[104] = GPIO(104, mi2s2_data1, _, _, qdss, _, atest_usb02, _, _, _),
	[105] = GPIO(105, sec_mi2s, mi2s1_data1, audio_ref, gcc_gp1, _, qdss, atest_usb01, _, _),
	[106] = GPIO(106, mi2s1_sck, gcc_gp2, _, qdss, atest_usb00, _, _, _, _),
	[107] = GPIO(107, mi2s1_data0, gcc_gp3, _, qdss, atest_usb0, _, _, _, _),
	[108] = GPIO(108, mi2s1_ws, _, qdss, _, _, _, _, _, _),
	[109] = GPIO(109, uim1_data, _, _, _, _, _, _, _, _),
	[110] = GPIO(110, uim1_clk, _, _, _, _, _, _, _, _),
	[111] = GPIO(111, uim1_reset, _, _, _, _, _, _, _, _),
	[112] = GPIO(112, uim1_present, _, _, _, _, _, _, _, _),
	[113] = GPIO(113, uim0_data, _, _, _, _, _, _, _, _),
	[114] = GPIO(114, uim0_clk, _, _, _, _, _, _, _, _),
	[115] = GPIO(115, uim0_reset, _, _, _, _, _, _, _, _),
	[116] = GPIO(116, uim0_present, _, _, _, _, _, _, _, _),
	[117] = GPIO(117, _, mss_grfc0, cmu_rng3, phase_flag, _, _, _, _, _),
	[118] = GPIO(118, _, mss_grfc1, cmu_rng2, phase_flag, _, _, _, _, _),
	[119] = GPIO(119, _, mss_grfc2, cmu_rng1, phase_flag, _, _, _, _, _),
	[120] = GPIO(120, _, mss_grfc3, cmu_rng0, phase_flag, _, _, _, _, _),
	[121] = GPIO(121, _, mss_grfc4, cri_trng0, phase_flag, _, _, _, _, _),
	[122] = GPIO(122, _, mss_grfc5, cri_trng1, phase_flag, _, _, _, _, _),
	[123] = GPIO(123, _, mss_grfc6, prng_rosc, phase_flag, _, _, _, _, _),
	[124] = GPIO(124, _, mss_grfc7, cri_trng, phase_flag, _, _, _, _, _),
	[125] = GPIO(125, _, mss_grfc8, phase_flag, _, _, _, _, _, _),
	[126] = GPIO(126, _, mss_grfc9, phase_flag, _, _, _, _, _, _),
	[127] = GPIO(127, coex_uart1, mss_grfc10, phase_flag, _, _, _, _, _, _),
	[128] = GPIO(128, coex_uart1, mss_grfc11, phase_flag, _, _, _, _, _, _),
	[129] = GPIO(129, nav_gpio0, phase_flag, _, _, _, _, _, _, _),
	[130] = GPIO(130, nav_gpio1, phase_flag, _, _, _, _, _, _, _),
	[131] = GPIO(131, mss_grfc12, nav_gpio2, pa_indicator, phase_flag, _, _, _, _, _),
	[132] = GPIO(132, mss_grfc0, phase_flag, _, _, _, _, _, _, _),
	[133] = GPIO(133, qlink0_request, _, _, _, _, _, _, _, _),
	[134] = GPIO(134, qlink0_enable, _, _, _, _, _, _, _, _),
	[135] = GPIO(135, qlink0_wmss, _, _, _, _, _, _, _, _),
	[136] = GPIO(136, qlink1_request, _, _, _, _, _, _, _, _),
	[137] = GPIO(137, qlink1_enable, _, _, _, _, _, _, _, _),
	[138] = GPIO(138, qlink1_wmss, _, _, _, _, _, _, _, _),
	[139] = GPIO(139, _, _, _, _, _, _, _, _, _),
	[140] = GPIO(140, usb_phy, pll_clk, _, _, _, _, _, _, _),
	[141] = GPIO(141, _, _, _, _, _, _, _, _, _),
	[142] = GPIO(142, _, _, _, _, _, _, _, _, _),
	[143] = GPIO(143, _, _, _, _, _, _, _, _, _),
	[144] = GPIO(144, _, _, _, _, _, _, _, _, egpio),
	[145] = GPIO(145, _, _, _, _, _, _, _, _, egpio),
	[146] = GPIO(146, _, _, _, _, _, _, _, _, egpio),
	[147] = GPIO(147, _, _, _, _, _, _, _, _, egpio),
	[148] = GPIO(148, _, _, _, _, _, _, _, _, egpio),
	[149] = GPIO(149, _, _, _, _, _, _, _, _, egpio),
	[150] = GPIO(150, qdss, _, _, _, _, _, _, _, egpio),
	[151] = GPIO(151, qdss, _, _, _, _, _, _, _, egpio),
	[152] = GPIO(152, qdss, _, _, _, _, _, _, _, egpio),
	[153] = GPIO(153, qdss, _, _, _, _, _, _, _, egpio),
	[154] = GPIO(154, _, _, _, _, _, _, _, _, egpio),
	[155] = GPIO(155, _, _, _, _, _, _, _, _, egpio),
	[156] = GPIO(156, qdss_cti, _, _, _, _, _, _, _, egpio),
	[157] = GPIO(157, qdss_cti, _, _, _, _, _, _, _, egpio),
	[158] = GPIO(158, _, _, _, _, _, _, _, _, egpio),
	[159] = GPIO(159, _, _, _, _, _, _, _, _, egpio),
	[160] = GPIO(160, _, _, _, _, _, _, _, _, egpio),
	[161] = GPIO(161, _, _, _, _, _, _, _, _, egpio),
	[162] = GPIO(162, _, _, _, _, _, _, _, _, egpio),
	[163] = GPIO(163, _, _, _, _, _, _, _, _, egpio),
	[164] = GPIO(164, _, _, _, _, _, _, _, _, egpio),
	[165] = GPIO(165, qdss_cti, _, _, _, _, _, _, _, egpio),
	[166] = GPIO(166, qdss_cti, _, _, _, _, _, _, _, egpio),
	[167] = GPIO(167, _, _, _, _, _, _, _, _, egpio),
	[168] = GPIO(168, _, _, _, _, _, _, _, _, egpio),
	[169] = GPIO(169, _, _, _, _, _, _, _, _, egpio),
	[170] = GPIO(170, _, _, _, _, _, _, _, _, egpio),
	[171] = GPIO(171, qdss, _, _, _, _, _, _, _, egpio),
	[172] = GPIO(172, qdss, _, _, _, _, _, _, _, egpio),
	[173] = GPIO(173, qdss, _, _, _, _, _, _, _, egpio),
	[174] = GPIO(174, qdss, _, _, _, _, _, _, _, egpio),
};


#if 0
static int msm_pinmux_set_mux(struct pinctrl_dev *pctldev,
			      unsigned function,
			      unsigned group)
{
	struct msm_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	struct gpio_chip *gc = &pctrl->chip;
	unsigned int irq = irq_find_mapping(gc->irq.domain, group);
	struct irq_data *d = irq_get_irq_data(irq);
	unsigned int gpio_func = pctrl->soc->gpio_func;
	unsigned int egpio_func = pctrl->soc->egpio_func;
	const struct msm_pingroup *g;
	unsigned long flags;
	u32 val, mask;
	int i;

	g = &pctrl->soc->groups[group];
	mask = GENMASK(g->mux_bit + order_base_2(g->nfuncs) - 1, g->mux_bit);

	for (i = 0; i < g->nfuncs; i++) {
		if (g->funcs[i] == function)
			break;
	}

	if (WARN_ON(i == g->nfuncs))
		return -EINVAL;

	/*
	 * If an GPIO interrupt is setup on this pin then we need special
	 * handling.  Specifically interrupt detection logic will still see
	 * the pin twiddle even when we're muxed away.
	 *
	 * When we see a pin with an interrupt setup on it then we'll disable
	 * (mask) interrupts on it when we mux away until we mux back.  Note
	 * that disable_irq() refcounts and interrupts are disabled as long as
	 * at least one disable_irq() has been called.
	 */
	if (d && i != gpio_func &&
	    !test_and_set_bit(d->hwirq, pctrl->disabled_for_mux))
		disable_irq_nosync(irq);

	raw_spin_lock_irqsave(&pctrl->lock, flags);

	val = msm_readl_ctl(pctrl, g);

	/*
	 * If this is the first time muxing to GPIO and the direction is
	 * output, make sure that we're not going to be glitching the pin
	 * by reading the current state of the pin and setting it as the
	 * output.
	 */
	if (i == gpio_func && (val & BIT(g->oe_bit)) &&
	    !test_and_set_bit(group, pctrl->ever_gpio)) {
		u32 io_val = msm_readl_io(pctrl, g);

		if (io_val & BIT(g->in_bit)) {
			if (!(io_val & BIT(g->out_bit)))
				msm_writel_io(io_val | BIT(g->out_bit), pctrl, g);
		} else {
			if (io_val & BIT(g->out_bit))
				msm_writel_io(io_val & ~BIT(g->out_bit), pctrl, g);
		}
	}

	if (egpio_func && i == egpio_func) {
		if (val & BIT(g->egpio_present))
			val &= ~BIT(g->egpio_enable);
	} else {
		val &= ~mask;
		val |= i << g->mux_bit;
		/* Claim ownership of pin if egpio capable */
		if (egpio_func && val & BIT(g->egpio_present))
			val |= BIT(g->egpio_enable);
	}

	msm_writel_ctl(val, pctrl, g);

	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	if (d && i == gpio_func &&
	    test_and_clear_bit(d->hwirq, pctrl->disabled_for_mux)) {
		/*
		 * Clear interrupts detected while not GPIO since we only
		 * masked things.
		 */
		if (d->parent_data && test_bit(d->hwirq, pctrl->skip_wake_irqs))
			irq_chip_set_parent_state(d, IRQCHIP_STATE_PENDING, false);
		else
			msm_ack_intr_status(pctrl, g);

		enable_irq(irq);
	}

	return 0;
}

int gpio_set_mux(unsigned gpio, unsigned function)
{

}
#endif
