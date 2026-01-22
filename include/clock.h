#ifndef __CLOCK_H__
#define __CLOCK_H__

#define CMD_REG			0x0
#define CMD_UPDATE		BIT(0)
#define CMD_ROOT_EN		BIT(1)
#define CMD_DIRTY_CFG		BIT(4)
#define CMD_DIRTY_N		BIT(5)
#define CMD_DIRTY_M		BIT(6)
#define CMD_DIRTY_D		BIT(7)
#define CMD_ROOT_OFF		BIT(31)

#define CFG_REG			0x4
#define CFG_SRC_DIV_SHIFT	0
#define CFG_SRC_DIV_LENGTH	8
#define CFG_SRC_SEL_SHIFT	8
#define CFG_SRC_SEL_MASK	(0x7 << CFG_SRC_SEL_SHIFT)
#define CFG_MODE_SHIFT		12
#define CFG_MODE_MASK		(0x3 << CFG_MODE_SHIFT)
#define CFG_MODE_DUAL_EDGE	(0x2 << CFG_MODE_SHIFT)
#define CFG_HW_CLK_CTRL_MASK	BIT(20)

#define M_REG			0x8
#define N_REG			0xc
#define D_REG			0x10

#define RCG_CFG_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + CFG_REG)
#define RCG_M_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + M_REG)
#define RCG_N_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + N_REG)
#define RCG_D_OFFSET(rcg)	((rcg)->cmd_rcgr + (rcg)->cfg_off + D_REG)

/* Dynamic Frequency Scaling */
#define MAX_PERF_LEVEL		8
#define SE_CMD_DFSR_OFFSET	0x14
#define SE_CMD_DFS_EN		BIT(0)
#define SE_PERF_DFSR(level)	(0x1c + 0x4 * (level))
#define SE_PERF_M_DFSR(level)	(0x5c + 0x4 * (level))
#define SE_PERF_N_DFSR(level)	(0x9c + 0x4 * (level))

#define F(f, s, h, m, n) { (f), (s), (2 * (h) - 1), (m), (n) }

struct clk;

struct parent_map {
	u8 src;
	u8 cfg;
};

struct freq_tbl {
	unsigned long freq;
	u8 src;
	u8 pre_div;
	u16 m;
	u16 n;
};

struct clk_ops {
	void (*enable)(struct clk *clk);
	void (*disable)(struct clk *clk);
	int (*set_rate)(struct clk *clk, u32 rate);
	u32 (*get_rate)(struct clk *clk);
	u32 (*set_parent)(struct clk *clk, int index);
};

struct clk {
	const char *name;
	unsigned int enable_reg;
	unsigned int enable_mask;
	struct clk_ops *ops;
	struct clk *src;
	struct freq_tbl *ftbl;
	struct parent_map *parent_map;
	int num_parents;
	u8 mnd_width;
	u8 hid_width;
};

extern struct clk *clock_resource[];

int clk_enable(int id);
int clk_set_rate(int id, u32 rate);
int clk_disable(int id);

#endif
