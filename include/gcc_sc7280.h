#ifndef GCC_SC7280_BM_H
#define GCC_SC7280_BM_H

#include <type.h>

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


/* GCC base */
#ifndef GCC_BASE
#define GCC_BASE 0x00100000UL
#endif

/* QUPV3 wrap0 wrapper base (you provided) */
#ifndef QUPV3_WRAP0_BASE
#define QUPV3_WRAP0_BASE 0x009c0000UL
#endif

/* QUP HW version register offset (common for QUPv3 wrapper) */
#ifndef QUPV3_HW_VER_REG
#define QUPV3_HW_VER_REG 0x0004
#endif

/* ===== RCG2 register layout (common Qualcomm) ===== */
#define RCG2_CMD_RCGR_OFF      0x0
#define RCG2_CFG_RCGR_OFF      0x4
#define RCG2_M_OFF             0x8
#define RCG2_N_OFF             0xC
#define RCG2_D_OFF             0x10

/* CMD bits */
#define RCG2_CMD_UPDATE        (1U << 0)

/* CFG fields (typical) */
#define RCG2_CFG_SRC_SEL_SHFT  0
#define RCG2_CFG_SRC_SEL_MSK   (0x7U << RCG2_CFG_SRC_SEL_SHFT)

/* Many RCG2 use "hid" divider encoded as (div-1). We'll do that. */
#define RCG2_CFG_SRC_DIV_SHFT  8
#define RCG2_CFG_SRC_DIV_MSK   (0x1FU << RCG2_CFG_SRC_DIV_SHFT)

/* Branch clock enable (you provided) */
#define GCC_QUPV3_WRAP0_S2_CLK_ENABLE_REG   0x52008
#define GCC_QUPV3_WRAP0_S2_CLK_ENABLE_MASK  (1U << 12)

/* RCG for wrap0_s2_clk_src (you provided) */
#define GCC_QUPV3_WRAP0_S2_CLK_SRC_CMD_RCGR 0x17270
#define GCC_QUPV3_WRAP0_S2_CLK_HALT_REG     0x1726c

/* Optional: halt bit meaning depends on BRANCH_HALT_VOTED; we will not poll for now. */

u32 qup_get_hw_version(void);

/* Program wrap0_s2_clk_src to an entry in freq table (index) */
int gcc_qupv3_wrap0_s2_clk_set_rate_idx(u32 idx);

/* Enable wrap0_s2 branch gate */
void gcc_qupv3_wrap0_s2_clk_enable(void);


#endif
