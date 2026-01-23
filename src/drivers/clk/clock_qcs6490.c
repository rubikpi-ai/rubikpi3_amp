#include <type.h>
#include <io.h>
#include <clock.h>
#include <clock_qcs6490.h>

enum {
	P_BI_TCXO,
	P_GCC_GPLL0_OUT_EVEN,
	P_GCC_GPLL0_OUT_MAIN,
	P_GCC_GPLL0_OUT_ODD,
	P_GCC_GPLL10_OUT_MAIN,
	P_GCC_GPLL4_OUT_MAIN,
	P_GCC_GPLL9_OUT_MAIN,
	P_PCIE_0_PIPE_CLK,
	P_PCIE_1_PIPE_CLK,
	P_SLEEP_CLK,
	P_UFS_PHY_RX_SYMBOL_0_CLK,
	P_UFS_PHY_RX_SYMBOL_1_CLK,
	P_UFS_PHY_TX_SYMBOL_0_CLK,
	P_USB3_PHY_WRAPPER_GCC_USB30_PIPE_CLK,
	P_GCC_MSS_GPLL0_MAIN_DIV_CLK,
};

struct clk gcc_qupv3_wrap0_s2_clk;

struct clk *clock_resource[]  = {
	[UART2_CLK] = &gcc_qupv3_wrap0_s2_clk,
};

static inline u32 clock_r32(u32 off) { return readl((u32)(GCC_BASE + off)); }
static inline void clock_w32(u32 off, u32 v) { writel(v, (u32)(GCC_BASE + off)); }

static const struct freq_tbl ftbl_gcc_qupv3_wrap0_s2_clk_src[] = {
	F(7372800, P_GCC_GPLL0_OUT_EVEN, 1, 384, 15625),
	F(14745600, P_GCC_GPLL0_OUT_EVEN, 1, 768, 15625),
	F(19200000, P_BI_TCXO, 1, 0, 0),
	F(29491200, P_GCC_GPLL0_OUT_EVEN, 1, 1536, 15625),
	F(32000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 75),
	F(48000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 25),
	F(52174000, P_GCC_GPLL0_OUT_MAIN, 1, 2, 23),
	F(64000000, P_GCC_GPLL0_OUT_EVEN, 1, 16, 75),
	F(75000000, P_GCC_GPLL0_OUT_EVEN, 4, 0, 0),
	F(80000000, P_GCC_GPLL0_OUT_EVEN, 1, 4, 15),
	F(96000000, P_GCC_GPLL0_OUT_EVEN, 1, 8, 25),
	F(100000000, P_GCC_GPLL0_OUT_MAIN, 6, 0, 0),
	{ },
};

static struct freq_tbl *qcom_find_freq(const struct freq_tbl *f, unsigned long rate)
{
	if (!f)
		return NULL;

	if (!f->freq)
		return f;

	for (; f->freq; f++)
		if (rate <= f->freq)
			return f;

	/* Default to our fastest rate */
	return f - 1;
}

static int update_config(struct clk *clk)
{
	int count, ret, v;
	u32 cmd;

	v = clock_r32(clk->enable_reg + CMD_REG);
	v |= CMD_UPDATE;

	clock_w32(clk->enable_reg + CMD_REG, v);

	for (count = 500; count > 0; count--) {
		v = clock_r32(clk->enable_reg + CMD_REG);
		if (!(v & CMD_UPDATE))
			return 0;
		delay(0xffff);
	}

	return -EBUSY;
}

void gcc_qupv3_generic_clk_enable(struct clk *clk)
{
	u32 v;

	if (!clk)
		return;

	v = clock_r32(clk->enable_reg);
	v |= clk->enable_mask;
	clock_w32(clk->enable_reg, v);
}

void gcc_qupv3_generic_clk_disable(struct clk *clk)
{
	u32 v;

	if (!clk)
		return;

	v = clock_r32(clk->enable_reg);
	v &= ~clk->enable_mask;
//	clock_w32(clk->enable_reg, v);
}

int qcom_find_src_index(struct clk *clk, const struct parent_map *map, u8 src)
{
	int i, num_parents = clk->num_parents;

	for (i = 0; i < num_parents; i++) {
		if (src == map[i].src)
			return i;
	}

	return -ENOENT;
}

static __clk_rcg2_configure_parent(struct clk *clk, u8 src, u32 *_cfg)
{
	int index, ret;

	index = qcom_find_src_index(clk, clk->parent_map, src);
	if (index < 0)
		return index;

	*_cfg &= ~CFG_SRC_SEL_MASK;
	*_cfg |= clk->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;

	return 0;
}

static int __clk_rcg2_configure_mnd(struct clk *rcg, const struct freq_tbl *f,
				u32 *_cfg)
{
	u32 cfg, mask, d_val, not2d_val, n_minus_m, v = 0;
	int ret;

	if (rcg->mnd_width && f->n) {
		mask = BIT(rcg->mnd_width) - 1;

		v = clock_r32(rcg->enable_reg + M_REG);
		v &= ~mask;
		v |= f->m;
		clock_w32(rcg->enable_reg + M_REG, v);

		v = clock_r32(rcg->enable_reg + N_REG);
		v &= ~mask;
		v |= ~(f->n - f->m);
		clock_w32(rcg->enable_reg + N_REG, v);

		/* Calculate 2d value */
		d_val = f->n;

		n_minus_m = f->n - f->m;
		n_minus_m *= 2;

		d_val = clamp(d_val, f->m, n_minus_m);
		not2d_val = ~d_val & mask;

		v = clock_r32(rcg->enable_reg + D_REG);
		v &= ~mask;
		v |= not2d_val;
		clock_w32(rcg->enable_reg + D_REG, v);
	}

	mask = BIT(rcg->hid_width) - 1;
	mask |= CFG_MODE_MASK | CFG_HW_CLK_CTRL_MASK;

	cfg = f->pre_div << CFG_SRC_DIV_SHIFT;
	if (rcg->mnd_width && f->n && (f->m != f->n))
		cfg |= CFG_MODE_DUAL_EDGE;

	*_cfg &= ~mask;
	*_cfg |= cfg;

	return 0;
}

static int __clk_rcg2_configure(struct clk *rcg, const struct freq_tbl *f,
				u32 *_cfg)
{
	int ret;

	ret = __clk_rcg2_configure_parent(rcg, f->src, _cfg);
	if (ret)
		return ret;

	ret = __clk_rcg2_configure_mnd(rcg, f, _cfg);
	if (ret)
		return ret;

	return 0;
}

void gcc_qupv3_wrap0_s2_clk_src_set_rate(struct clk *clk, u32 rate)
{
	struct freq_tbl *f;
	u32 cfg;
	int ret;

	if (!clk || !clk->ftbl)
		return;

	f = qcom_find_freq(clk->ftbl, rate);
	if (!f)
		return;

	cfg = clock_r32(clk->enable_reg + CFG_REG);

	ret = __clk_rcg2_configure(clk, f, &cfg);
	if (ret)
		return;

	clock_w32(clk->enable_reg + CFG_REG, cfg);

	update_config(clk);
}

void gcc_qupv3_set_rate_call_parent(struct clk *clk, u32 rate)
{
	if (!clk || !clk->src || !clk->src->ops || !clk->src->ops->set_rate)
		return;

	clk->src->ops->set_rate(clk->src, rate);
}

void gcc_qupv3_get_rate_call_parent(struct clk *clk)
{
	if (!clk || !clk->src || !clk->src->ops || !clk->src->ops->get_rate)
		return;

	clk->src->ops->get_rate(clk->src);
}

u8 gcc_qupv3_wrap0_s2_clk_src_set_parent(struct clk *clk, int index)
{
	int ret;
	u32 cfg = clk->parent_map[index].cfg << CFG_SRC_SEL_SHIFT;
	u32 v;

	if (!clk)
		return -1;

	v = clock_r32(clk->enable_reg + CFG_REG);
	v &= ~CFG_SRC_SEL_MASK;
	v |= cfg;

	clock_w32(clk->enable_reg, v);

	return update_config(clk);
}

static const struct parent_map gcc_parent_map_0[] = {
	{ P_BI_TCXO, 0 },
	{ P_GCC_GPLL0_OUT_MAIN, 1 },
	{ P_GCC_GPLL0_OUT_EVEN, 6 },
};

struct clk_ops gcc_qupv3_wrap0_s2_clk_src_ops = {
	.set_parent = gcc_qupv3_wrap0_s2_clk_src_set_parent,
	.set_rate = gcc_qupv3_wrap0_s2_clk_src_set_rate,
};

struct clk_ops gcc_qupv3_wrap0_s2_clk_ops = {
	.enable = gcc_qupv3_generic_clk_enable,
	.disable = gcc_qupv3_generic_clk_disable,
	.set_rate = gcc_qupv3_set_rate_call_parent,
};

struct clk gcc_qupv3_wrap0_s2_clk_src = {
	.name = "gcc_qupv3_wrap0_s2_clk_src",
	.enable_reg = 0x17270,
	.enable_mask = 0,
	.mnd_width = 16,
	.hid_width = 5,
	.ops = &gcc_qupv3_wrap0_s2_clk_src_ops,
	.src = NULL,
	.ftbl = (struct freq_tbl *)ftbl_gcc_qupv3_wrap0_s2_clk_src,
	.parent_map = (struct parent_map *)gcc_parent_map_0,
	.num_parents = ARRAY_SIZE(gcc_parent_map_0),
};

struct clk gcc_qupv3_wrap0_s2_clk = {
	.enable_reg = 0x52008,
	.enable_mask = BIT(12),
	.name = "gcc_qupv3_wrap0_s2_clk",
	.src = &gcc_qupv3_wrap0_s2_clk_src,
	.ops = &gcc_qupv3_wrap0_s2_clk_ops,
	.ftbl = NULL,
};
