#ifndef BM_QCOM_CLK_H
#define BM_QCOM_CLK_H

#include <type.h>

#ifndef GCC_BASE
#define GCC_BASE 0x00100000UL
#endif

struct bm_parent_map {
	u32 parent_id;
	u32 src_sel;
};

struct bm_freq_tbl {
	u32 freq_hz;
	u32 parent_id;
	u32 pre_div;
	u32 m;
	u32 n;
};

struct bm_rcg2 {
	u32 cmd_rcgr_off;
	u32 hid_width;
	u32 mnd_width;
	const struct bm_parent_map *pmap;
	u32 pmap_cnt;
	const struct bm_freq_tbl *ftbl;
	u32 ftbl_cnt;
};

struct bm_branch {
	u32 enable_reg_off;
	u32 enable_mask;
	u32 halt_reg_off;
	u32 halt_mask;
};

struct bm_clk_rcg2_branch {
	const char *name;
	struct bm_rcg2 rcg2;
	struct bm_branch branch;
};

int bm_clk_freq_match(const struct bm_clk_rcg2_branch *c,
		      u32 req_hz, u32 *idx_out, u32 *src_hz_out, int exact);

int bm_clk_set_rate_idx(const struct bm_clk_rcg2_branch *c, u32 idx);
void bm_clk_enable(const struct bm_clk_rcg2_branch *c);

int bm_clk_config_for_req(const struct bm_clk_rcg2_branch *c,
			  u32 req_hz, u32 *idx_out, u32 *src_hz_out);

#endif
