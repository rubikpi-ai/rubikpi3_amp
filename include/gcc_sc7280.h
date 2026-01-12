#ifndef GCC_SC7280_BM_H
#define GCC_SC7280_BM_H

#include <type.h>

/*
 * GCC base for SC7280/QCS6490: 0x00100000
 * From DT: gcc: clock-controller@100000 { reg = <0 0x00100000 0 0x1f0000>; }
 */
#define GCC_BASE 0x00100000ULL

/*
 * Minimal clocks we likely need for QUPv3 wrap0 SE2 (uart2 @ 0x00988000):
 * - GCC_QUPV3_WRAP_0_M_AHB_CLK
 * - GCC_QUPV3_WRAP_0_S_AHB_CLK
 * - GCC_QUPV3_WRAP0_S2_CLK   (the "se" clock used by uart2)
 *
 * NOTE: Exact CBCR offsets are SoC-specific. We will fill these in Step 1.2
 */
struct gcc_clk_desc {
	const char *name;
	u32 cbcr_off; /* offset from GCC_BASE */
	u32 enable_mask; /* usually BIT(0) */
};

int gcc_enable_clock_by_desc(const struct gcc_clk_desc *c);

/* convenience: enable the 3 clocks needed for uart2 */
int gcc_enable_uart2_clocks(void);

#endif
