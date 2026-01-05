#ifndef _GIC_V3_H_
#define _GIC_V3_H_

void gicv3_init_for_cpu(void);
void gicv3_enable_ppi(u32 intid, u8 prio);
u32 gicv3_iar1(void);
void gicv3_eoi1(u32 iar);

/* New helper functions for PPI management */
void disable_all_ppis(u64 gicr_base);
void enable_ppi_mask(u64 gicr_base, u32 mask, u8 prio);
void enable_only_ppi(u64 gicr_base, u32 intid, u8 prio);

#endif // _GIC_V3_H_
