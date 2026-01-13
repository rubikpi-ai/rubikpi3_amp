#ifndef GCC_UART2_CLK_H
#define GCC_UART2_CLK_H

#include <type.h>
#include <bm_qcom_clk.h>

/* From your info */
#ifndef QUPV3_WRAP0_BASE
#define QUPV3_WRAP0_BASE 0x009c0000UL
#endif
#ifndef QUPV3_HW_VER_REG
#define QUPV3_HW_VER_REG 0x0004
#endif

u32 qup_wrap0_hw_version(void);

/* Configure and enable gcc_qupv3_wrap0_s2_clk for requested freq (Hz) */
int gcc_uart2_se_clk_config_for_req(u32 req_hz, u32 *idx_out, u32 *src_hz_out);

#endif
