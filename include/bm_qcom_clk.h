#ifndef BM_QCOM_CLK_H
#define BM_QCOM_CLK_H

#include <type.h>

/* GCC base */
#ifndef GCC_BASE
#define GCC_BASE 0x00100000UL
#endif

/* -------- Parent mapping (Linux gcc_parent_map_* equivalent) -------- */
struct bm_parent_map {
	u32 parent_id; /* your enum P_* */
	u32 src_sel;   /* value written into CFG_RCGR[SRC_SEL] */
};

/* -------- Frequency table entry (Linux freq_tbl equivalent) -------- */
struct bm_freq_tbl {
	u32 freq_hz;
	u32 parent_id;  /* your enum P_* */
	u32 pre_div;    /* hid divider (1..32), encoded as (pre_div-1) */
	u32 m;          /* mnd */
	u32 n;          /* mnd */
};

/* -------- RCG2 descriptor -------- */
struct bm_rcg2 {
	u32 cmd_rcgr_off;               /* offset from GCC_BASE */
	u32 hid_width;                  /* usually 5 */
	u32 mnd_width;                  /* usually 16 */
	const struct bm_parent_map *pmap;
	u32 pmap_cnt;
	const struct bm_freq_tbl *ftbl; /* array */
	u32 ftbl_cnt;
};

/* -------- Branch clock descriptor -------- */
struct bm_branch {
	u32 enable_reg_off; /* offset from GCC_BASE */
	u32 enable_mask;
	u32 halt_reg_off;   /* optional, can be 0 */
	u32 halt_mask;      /* optional, can be 0 */
};

/* -------- Combined clock (rcg2 + branch) -------- */
struct bm_clk_rcg2_branch {
	const char *name;
	struct bm_rcg2 rcg2;
	struct bm_branch branch;
};

/* Find best freq table index for requested frequency (Hz) */
int bm_clk_freq_match(const struct bm_clk_rcg2_branch *c,
		      u32 req_hz, u32 *idx_out, u32 *src_hz_out, int exact);

/* Program RCG2 to a given freq table index */
int bm_clk_set_rate_idx(const struct bm_clk_rcg2_branch *c, u32 idx);

/* Enable branch gate */
void bm_clk_enable(const struct bm_clk_rcg2_branch *c);

/* Convenience: match->set_rate_idx->enable */
int bm_clk_config_for_req(const struct bm_clk_rcg2_branch *c,
			  u32 req_hz, u32 *idx_out, u32 *src_hz_out);

#endif
