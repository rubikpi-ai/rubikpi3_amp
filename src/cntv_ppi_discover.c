#include <type.h>
#include <asm/irq.h>
#include <asm/gic_v3.h>
#include <asm/barrier.h>

/* Shared memory base for debug output */
#define SHM_BASE 0xD7C00000ULL

/* GICR offsets */
#define GICR_SGI_BASE   0x10000
#define GICR_ISPENDR0   (GICR_SGI_BASE + 0x0200)
#define GICR_ISACTIVER0 (GICR_SGI_BASE + 0x0300)

static inline u32 mmio_read32(u64 a) { return *(volatile u32 *)a; }

/* CNTV timer control functions */
static inline void write_cntv_tval_el0(u64 v) {
    __asm__ volatile ("msr cntv_tval_el0, %0" :: "r"(v));
}
static inline void write_cntv_ctl_el0(u64 v) {
    __asm__ volatile ("msr cntv_ctl_el0, %0" :: "r"(v));
}
static inline u64 read_cntfrq_el0(void) {
    u64 v; __asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(v)); return v;
}

#define CNTV_CTL_ENABLE (1U << 0)

/*
 * Discover which PPI corresponds to CNTV virtual timer.
 * 
 * Strategy:
 * 1. Disable all PPIs to clear any spurious state
 * 2. Start CNTV timer at low frequency (10Hz)
 * 3. Poll GICR_ISPENDR0 to detect which PPI bit goes pending
 * 4. Record result to shared memory
 * 
 * Returns: PPI intid (16..31), or 0 if not found
 * 
 * Shared memory layout (starting at SHM_BASE + offset 100):
 *  [100] = magic value 0x434E545644495343 ("CNTV DISC")
 *  [101] = gicr_base used
 *  [102] = discovered PPI intid (or 0 if not found)
 *  [103] = GICR_ISPENDR0 value when PPI was detected
 *  [104] = GICR_ISACTIVER0 value when PPI was detected
 *  [105] = iteration count when detected
 *  [106] = final GICR_ISPENDR0
 *  [107] = final GICR_ISACTIVER0
 */
u32 cntv_ppi_discover(u64 gicr_base)
{
    volatile u64 *shm = (volatile u64 *)SHM_BASE;
    
    /* Mark start of discovery */
    shm[100] = 0x434E545644495343ULL; /* "CNTVDISC" */
    shm[101] = gicr_base;
    shm[102] = 0; /* discovered intid */
    
    /* Step 1: Disable all PPIs to start clean */
    disable_all_ppis(gicr_base);
    dsb(sy);
    isb();
    
    /* Small delay to let things settle */
    for (volatile u64 i = 0; i < 100000; i++);
    
    /* Step 2: Start CNTV timer at 10Hz */
    u64 frq = read_cntfrq_el0();
    u64 ticks = frq / 10; /* 10 Hz */
    write_cntv_tval_el0(ticks);
    write_cntv_ctl_el0(CNTV_CTL_ENABLE);
    dsb(sy);
    isb();
    
    /* Step 3: Poll GICR_ISPENDR0 for pending PPI */
    const u32 max_iter = 500000; /* should be enough for 10Hz */
    u32 discovered_ppi = 0;
    u32 ispendr0 = 0;
    u32 isactiver0 = 0;
    u32 iter = 0;
    
    for (iter = 0; iter < max_iter; iter++) {
        ispendr0 = mmio_read32(gicr_base + GICR_ISPENDR0);
        
        /* Check for any pending PPI (bits 16..31) */
        u32 ppi_pending = ispendr0 & 0xFFFF0000;
        if (ppi_pending) {
            /* Find which bit is set */
            for (u32 bit = 16; bit < 32; bit++) {
                if (ispendr0 & (1U << bit)) {
                    discovered_ppi = bit;
                    isactiver0 = mmio_read32(gicr_base + GICR_ISACTIVER0);
                    break;
                }
            }
            break;
        }
        
        /* Periodic ISB for coherency */
        if ((iter & 0xFFF) == 0) {
            isb();
        }
    }
    
    /* Stop CNTV timer */
    write_cntv_ctl_el0(0);
    dsb(sy);
    isb();
    
    /* Step 4: Record results to shared memory */
    shm[102] = discovered_ppi;
    shm[103] = ispendr0;
    shm[104] = isactiver0;
    shm[105] = iter;
    shm[106] = mmio_read32(gicr_base + GICR_ISPENDR0);
    shm[107] = mmio_read32(gicr_base + GICR_ISACTIVER0);
    
    dsb(sy);
    
    return discovered_ppi;
}
