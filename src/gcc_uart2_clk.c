#include <type.h>
#include <io.h>
#include <gcc_uart2_clk.h>

/* Your parent IDs */
enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
};

static const struct bm_parent_map gcc_parent_map_0_bm[] = {
	{ P_BI_TCXO,            0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

/* Your ftbl for gcc_qupv3_wrap0_s2_clk_src */
static const struct bm_freq_tbl ftbl_wrap0_s2[] = {
	{  7372800,  P_GCC_GPLL0_OUT_EVEN, 1,  384, 15625 },
	{ 14745600, P_GCC_GPLL0_OUT_EVEN, 1,  768, 15625 },
	{ 19200000, P_BI_TCXO,            1,    0,     0 },
	{ 29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625 },
	{ 32000000, P_GCC_GPLL0_OUT_EVEN, 1,    8,    75 },
	{ 48000000, P_GCC_GPLL0_OUT_EVEN, 1,    4,    25 },
	{ 52174000, P_GCC_GPLL0_OUT_MAIN, 1,    2,    23 },
	{ 64000000, P_GCC_GPLL0_OUT_EVEN, 1,   16,    75 },
	{ 75000000, P_GCC_GPLL0_OUT_EVEN, 4,    0,     0 },
	{ 80000000, P_GCC_GPLL0_OUT_EVEN, 1,    4,    15 },
	{ 96000000, P_GCC_GPLL0_OUT_EVEN, 1,    8,    25 },
	{100000000, P_GCC_GPLL0_OUT_MAIN, 6,    0,     0 },
};

static const struct bm_clk_rcg2_branch uart2_se_clk = {
	.name = "gcc_qupv3_wrap0_s2_clk",
	.rcg2 = {
		.cmd_rcgr_off = 0x17270, /* gcc_qupv3_wrap0_s2_clk_src.cmd_rcgr */
		.hid_width = 5,
		.mnd_width = 16,
		.pmap = gcc_parent_map_0_bm,
		.pmap_cnt = (u32)(sizeof(gcc_parent_map_0_bm)/sizeof(gcc_parent_map_0_bm[0])),
		.ftbl = ftbl_wrap0_s2,
		.ftbl_cnt = (u32)(sizeof(ftbl_wrap0_s2)/sizeof(ftbl_wrap0_s2[0])),
	},
	.branch = {
		.enable_reg_off = 0x52008,
		.enable_mask = (1U << 12),
		.halt_reg_off = 0x1726c, /* optional */
		.halt_mask = 0,          /* not used */
	},
};

u32 qup_wrap0_hw_version(void)
{
	return readl((u32)(QUPV3_WRAP0_BASE + QUPV3_HW_VER_REG));
}

int gcc_uart2_se_clk_config_for_req(u32 req_hz, u32 *idx_out, u32 *src_hz_out)
{
	return bm_clk_config_for_req(&uart2_se_clk, req_hz, idx_out, src_hz_out);
}
