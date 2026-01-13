#include <type.h>
#include <io.h>
#include <gcc_sc7280.h>

static inline u32 gcc_r32(u32 off) { return readl((u32)(GCC_BASE + off)); }
static inline void gcc_w32(u32 off, u32 v) { writel(v, (u32)(GCC_BASE + off)); }

/*
 * Generic GCC branch clock enable:
 * - set enable bit
 * - optional: poll halt bit if known (先不做 halt polling，等我们确认寄存器格式)
 */
int gcc_enable_clock_by_desc(const struct gcc_clk_desc *c)
{
	u32 v;

	if (!c || !c->name)
		return -EINVAL;

	v = gcc_r32(c->cbcr_off);
	v |= c->enable_mask;
	gcc_w32(c->cbcr_off, v);

	/* TODO Step 1.2: add halt polling once halt bit position confirmed */
	return 0;
}

/*
 * Step 1: placeholder offsets (TO BE FILLED).
 * 你需要用 Linux /dev/mem 把这些 CBCR offset 对上。
 */
static const struct gcc_clk_desc uart2_clks[] = {
//	{ "GCC_QUPV3_WRAP_0_M_AHB_CLK", 0xFFFFFFFF, BIT(0) },
//	{ "GCC_QUPV3_WRAP_0_S_AHB_CLK", 0xFFFFFFFF, BIT(0) },
	{ "GCC_QUPV3_WRAP0_S2_CLK",     0x52008, BIT(12) },
};

int gcc_enable_uart2_clocks(void)
{
	for (u32 i = 0; i < ARRAY_SIZE(uart2_clks); i++) {
		int ret = gcc_enable_clock_by_desc(&uart2_clks[i]);
		if (ret)
			return ret;
	}
	return 0;
}
