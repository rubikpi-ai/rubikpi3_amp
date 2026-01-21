#include <type.h>
#include <io.h>
#include <bm_qcom_clk.h>

/* RCG2 register layout (common Qualcomm) */
#define RCG2_CMD_RCGR_OFF      0x0
#define RCG2_CFG_RCGR_OFF      0x4
#define RCG2_M_OFF             0x8
#define RCG2_N_OFF             0xC
#define RCG2_D_OFF             0x10

#define RCG2_CMD_UPDATE        (1U << 0)

#define RCG2_CFG_SRC_SEL_SHFT  8
#define RCG2_CFG_SRC_SEL_MSK   (0x7U << RCG2_CFG_SRC_SEL_SHFT)

#define RCG2_CFG_SRC_DIV_SHFT  8
#define RCG2_CFG_SRC_DIV_MSK   (0x1FU << RCG2_CFG_SRC_DIV_SHFT)

/* Toggle if needed */
//#ifndef BM_RCG2_D_INVERT
//#define BM_RCG2_D_INVERT 1
//#endif

static inline u32 gcc_r32(u32 off) { return readl((u32)(GCC_BASE + off)); }
static inline void gcc_w32(u32 off, u32 v) { writel(v, (u32)(GCC_BASE + off)); }
static inline u32 div_round_up_u32(u32 a, u32 b) { return (a + b - 1U) / b; }

static int pmap_lookup(const struct bm_rcg2 *r, u32 parent_id, u32 *src_sel)
{
	for (u32 i = 0; i < r->pmap_cnt; i++) {
		if (r->pmap[i].parent_id == parent_id) {
			*src_sel = r->pmap[i].src_sel;
			return 0;
		}
	}
	return -1;
}

int bm_clk_set_rate_idx(const struct bm_clk_rcg2_branch *c, u32 idx)
{
	if (!c || !c->rcg2.ftbl || idx >= c->rcg2.ftbl_cnt)
		return -1;

	const struct bm_rcg2 *r = &c->rcg2;
	const struct bm_freq_tbl *t = &r->ftbl[idx];
	if (t->freq_hz == 0)
		return -1;

	u32 src_sel = 0;
	if (pmap_lookup(r, t->parent_id, &src_sel) != 0)
		return -2;

	if (t->pre_div == 0)
		return -3;

	u32 hid = t->pre_div - 1U;
	u32 cfg = gcc_r32(r->cmd_rcgr_off + RCG2_CFG_RCGR_OFF);
	cfg &= ~RCG2_CFG_SRC_SEL_MSK;

	cfg |= src_sel << RCG2_CFG_SRC_SEL_SHFT;

	cfg |= ((hid << RCG2_CFG_SRC_DIV_SHFT) & RCG2_CFG_SRC_DIV_MSK);

	u32 m = t->m;
	u32 n = t->n;
	u32 d = 0;

	if (m == 0 && n == 0) {
		m = 0; n = 0; d = 0;
	} else {
		u32 diff = (n > m) ? (n - m) : 0;
#if BM_RCG2_D_INVERT
		d = ~diff;
#else
		d = diff;
#endif
	}

	u32 base = r->cmd_rcgr_off;

	//gcc_w32(base + RCG2_CFG_RCGR_OFF, cfg);
	//gcc_w32(base + RCG2_M_OFF, m);
	//gcc_w32(base + RCG2_N_OFF, n);
	//gcc_w32(base + RCG2_D_OFF, d);
	//gcc_w32(base + RCG2_CMD_RCGR_OFF, RCG2_CMD_UPDATE);
	printk("bm_clk_set_rate_idx: cfg=0x%x, m=%x, n=%x, d=%x\n", cfg, m, n, d);

	(void)gcc_r32(base + RCG2_CMD_RCGR_OFF);
	return 0;
}

void bm_clk_enable(const struct bm_clk_rcg2_branch *c)
{
	if (!c) return;
	u32 v = gcc_r32(c->branch.enable_reg_off);
	v |= c->branch.enable_mask;
	//gcc_w32(c->branch.enable_reg_off, v);
}

int bm_clk_freq_match(const struct bm_clk_rcg2_branch *c,
		      u32 req_hz, u32 *idx_out, u32 *src_hz_out, int exact)
{
	if (!c || !c->rcg2.ftbl || c->rcg2.ftbl_cnt == 0 || !idx_out || !src_hz_out)
		return -1;
	if (req_hz == 0)
		return -2;

	u32 best_delta = 0xFFFFFFFFU;
	u32 best_idx = 0;
	u32 best_src = 0;

	for (u32 i = 0; i < c->rcg2.ftbl_cnt; i++) {
		u32 src = c->rcg2.ftbl[i].freq_hz;
		if (!src) continue;

		u32 div = div_round_up_u32(src, req_hz);

		u32 delta = req_hz - src / div;

		if (delta < best_delta) {
			best_delta = delta;
			best_idx = i;
			best_src = src;
			if (delta == 0)
				break;
		}
	}

	if (exact && best_delta != 0)
		return -3;

	*idx_out = best_idx;
	*src_hz_out = best_src;
	return 0;
}

int bm_clk_config_for_req(const struct bm_clk_rcg2_branch *c,
			  u32 req_hz, u32 *idx_out, u32 *src_hz_out)
{
	u32 idx = 0, src = 0;
	int ret = bm_clk_freq_match(c, req_hz, &idx, &src, 0);
	if (ret) return ret;

	ret = bm_clk_set_rate_idx(c, idx);
	if (ret) return ret;

	//bm_clk_enable(c);

	if (idx_out) *idx_out = idx;
	if (src_hz_out) *src_hz_out = src;
	return 0;
}
