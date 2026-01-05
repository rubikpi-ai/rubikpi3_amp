#ifndef _GIC_V3_H_
#define _GIC_V3_H_

/* GICD offsets */
#define GICD_CTLR        0x0000
#define GICD_IGROUPR(n)  (0x0080 + 4*(n))
#define GICD_ISENABLER(n) (0x0100 + 4*(n))
#define GICD_IPRIORITYR(n) (0x0400 + (n))
#define GICD_IROUTER(n)  (0x6100 + 8*(n))

#define GICR_SGI_BASE   0x10000
#define GICR_IGROUPR0   (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI_BASE + 0x0100)
#define GICR_IPRIORITYR (GICR_SGI_BASE + 0x0400)
#define GICR_ICFGR1     (GICR_SGI_BASE + 0x0C04)

#define GICR_TYPER              0x0008
#define GICR_WAKER              0x0014

/* QCS6490 DTS: GICR region size = 0x100000 for 8 CPUs */
#define GICR_REGION_SIZE        0x100000UL
#define GICR_RD_STRIDE          0x20000UL
#define GICR_RD_FRAMES          (GICR_REGION_SIZE / GICR_RD_STRIDE)

void gicv3_init_for_cpu(void);
void enable_all_ppis(u64 gicr_base);
void enable_ppi(u64 gicr_base, u8 ppi_num, u8 prio);
void disable_all_ppis(u64 gicr_base);
void write_cntp_ctl_el0(u64 v);
void write_cntv_ctl_el0(u64 v);

#endif // _GIC_V3_H_
