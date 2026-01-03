#include <type.h>
#include <asm/irq.h>

/* From DT */
#define ARCH_MEM_TIMER_FRAME0_BASE 0x17C21000ULL
#define ARCH_MEM_TIMER_SPI_ID      6

/* Registers (match drivers/clocksource/arm_arch_timer_mmio.c) */
#define CNTPCT_LO   0x00
#define CNTFRQ      0x10
#define CNTP_CVAL_LO 0x20
#define CNTP_CTL    0x2c

/* CNTP_CTL bits */
#define ARCH_TIMER_CTRL_ENABLE  (1U << 0)
#define ARCH_TIMER_CTRL_IT_MASK (1U << 1)
#define ARCH_TIMER_CTRL_IT_STAT (1U << 2)

static inline u32 mmio_read32(u64 a){ return *(volatile u32 *)a; }
static inline void mmio_write32(u64 a, u32 v){ *(volatile u32 *)a = v; }

static inline u64 mmio_read64_cntpct(u64 base)
{
    /* lo/hi non-atomic safe read */
    u32 hi1, lo, hi2;
    do {
        hi1 = mmio_read32(base + CNTPCT_LO + 4);
        lo  = mmio_read32(base + CNTPCT_LO);
        hi2 = mmio_read32(base + CNTPCT_LO + 4);
    } while (hi1 != hi2);
    return ((u64)hi1 << 32) | lo;
}

static inline void mmio_write64_cval(u64 base, u64 v)
{
    /* timer must be disabled before programming CVAL */
    *(volatile u32 *)(base + CNTP_CVAL_LO)     = (u32)(v & 0xffffffffU);
    *(volatile u32 *)(base + CNTP_CVAL_LO + 4) = (u32)(v >> 32);
}

extern void gicv3_enable_spi(u32 intid, u8 prio, u64 mpidr);
extern void gicv3_route_spi_to_self(u32 intid, u64 mpidr);

static inline u64 read_mpidr_el1(void)
{
    u64 v; __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v)); return v;
}

void arch_mem_timer_start_hz(u32 hz)
{
    u64 mpidr = read_mpidr_el1();

    /* 1) route and enable SPI in GICD */
    gicv3_route_spi_to_self(ARCH_MEM_TIMER_SPI_ID, mpidr);
    gicv3_enable_spi(ARCH_MEM_TIMER_SPI_ID, 0x80, mpidr);

    /* 2) program timer */
    u64 base = ARCH_MEM_TIMER_FRAME0_BASE;
    u32 frq = mmio_read32(base + CNTFRQ);
    u64 now = mmio_read64_cntpct(base);
    u64 delta = (u64)frq / hz;

    /* disable */
    mmio_write32(base + CNTP_CTL, 0);

    /* set compare */
    mmio_write64_cval(base, now + delta);

    /* enable + unmask */
    mmio_write32(base + CNTP_CTL, ARCH_TIMER_CTRL_ENABLE); /* IT_MASK=0 */
}

void arch_mem_timer_ack_and_rearm_hz(u32 hz)
{
    u64 base = ARCH_MEM_TIMER_FRAME0_BASE;

    /* mask to clear status in this mmio timer model */
    u32 ctl = mmio_read32(base + CNTP_CTL);
    ctl |= ARCH_TIMER_CTRL_IT_MASK;
    mmio_write32(base + CNTP_CTL, ctl);

    /* re-arm */
    arch_mem_timer_start_hz(hz);
}
