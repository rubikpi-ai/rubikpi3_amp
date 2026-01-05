#include <type.h>
#include <asm/irq.h>
#include <asm/timer.h>

/*
 * ARM arch timer MMIO frames (from DT):
 * frame0: base 0x17C21000, phys SPI6 (virt SPI8)
 * frame1..6: base 0x17C23000..0x17C2D000, SPI9..14
 */

/* Registers (match drivers/clocksource/arm_arch_timer_mmio.c) */
#define CNTPCT_LO     0x00
#define CNTFRQ        0x10
#define CNTP_CVAL_LO  0x20
#define CNTP_CTL      0x2c

/* CNTP_CTL bits */
#define ARCH_TIMER_CTRL_ENABLE  (1U << 0)
#define ARCH_TIMER_CTRL_IT_MASK (1U << 1)
#define ARCH_TIMER_CTRL_IT_STAT (1U << 2)

/* GICD pending bits for SPIs */
#define GICD_ISPENDR(n) (0x0200 + 4*(n))

// GICD basic regs (GICv3)
#define GICD_IGROUPR(n)    (0x0080 + 4*(n))
#define GICD_ISENABLER(n)  (0x0100 + 4*(n))
#define GICD_ICENABLER(n)  (0x0180 + 4*(n))
#define GICD_IPRIORITYR(n) (0x0400 + (n))      /* byte addressing */
#define GICD_IROUTER(n)    (0x6100 + 8*(n))

static inline u32 mmio_read32(u64 a){ return *(volatile u32 *)a; }
static inline void mmio_write32(u64 a, u32 v){ *(volatile u32 *)a = v; }
static inline u64 mmio_read64(u64 a){ return *(volatile u64 *)a; }
static inline void mmio_write64(u64 a, u64 v){ *(volatile u64 *)a = v; }

static inline u32 gicd_read_isenabler(u32 intid)
{
	u32 n = intid / 32;
	return mmio_read32(GICD_BASE + GICD_ISENABLER(n));
}

static inline u32 gicd_read_igroupr(u32 intid)
{
	u32 n = intid / 32;
	return mmio_read32(GICD_BASE + GICD_IGROUPR(n));
}

static inline u8 gicd_read_ipriority(u32 intid)
{
	return *(volatile u8 *)(GICD_BASE + GICD_IPRIORITYR(intid));
}

static inline u64 gicd_read_irouter(u32 intid)
{
	return mmio_read64(GICD_BASE + GICD_IROUTER(intid));
}

static inline void dsb_sy(void){ __asm__ volatile("dsb sy" ::: "memory"); }
static inline void isb_(void){ __asm__ volatile("isb" ::: "memory"); }

/* CNTCTLBase regs */
#define CNTCTLBASE   0x17C20000ULL
#define CNTACR(n)    (0x40 + ((n) * 4))

#define CNTACR_RPCT  (1U << 0)
#define CNTACR_RVCT  (1U << 1)
#define CNTACR_RFRQ  (1U << 2)
#define CNTACR_RVOFF (1U << 3)
#define CNTACR_RWVT  (1U << 4)
#define CNTACR_RWPT  (1U << 5)

void arch_mem_timer_cntacr_enable_all(volatile u64 *shm)
{
    u32 want = CNTACR_RFRQ | CNTACR_RWPT | CNTACR_RPCT |
               CNTACR_RWVT | CNTACR_RVOFF | CNTACR_RVCT;

    for (u32 n = 0; n < 7; n++) {
        u64 reg = CNTCTLBASE + CNTACR(n);
        mmio_write32(reg, want);
        dsb_sy();
        u32 got = mmio_read32(reg);

        /* record for debug */
        if (shm) {
            shm[200 + n*2 + 0] = reg;
            shm[200 + n*2 + 1] = ((u64)want << 32) | got;
        }
    }
}

/* safe non-atomic read for CNTPCT */
static inline u64 mmio_read64_cntpct(u64 base)
{
	u32 hi1, lo, hi2;
	do {
		hi1 = mmio_read32(base + CNTPCT_LO + 4);
		lo  = mmio_read32(base + CNTPCT_LO);
		hi2 = mmio_read32(base + CNTPCT_LO + 4);
	} while (hi1 != hi2);
	return ((u64)hi1 << 32) | lo;
}

/* safe write for CVAL (must be disabled before calling) */
static inline void mmio_write64_cval(u64 base, u64 v)
{
	*(volatile u32 *)(base + CNTP_CVAL_LO)     = (u32)(v & 0xffffffffU);
	*(volatile u32 *)(base + CNTP_CVAL_LO + 4) = (u32)(v >> 32);
}

/* Provided by your gic_v3.c */
extern void gicv3_enable_spi(u32 intid, u8 prio, u64 mpidr);
extern void gicv3_route_spi_to_self(u32 intid, u64 mpidr);

static inline u64 read_mpidr_el1(void)
{
	u64 v; __asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v)); return v;
}

/* -------------------- frame selection -------------------- */

struct arch_mem_timer_frame {
	u64 base;
	u32 spi;
	u32 frame_no;
};

static const struct arch_mem_timer_frame frames[] = {
	/* frame0 has two IRQs in DT; we use phys (SPI6) here */
	{ 0x17C21000ULL,  6, 0 },
	{ 0x17C23000ULL,  9, 1 },
	{ 0x17C25000ULL, 10, 2 },
	{ 0x17C27000ULL, 11, 3 },
	{ 0x17C29000ULL, 12, 4 },
	{ 0x17C2B000ULL, 13, 5 },
	{ 0x17C2D000ULL, 14, 6 },
};

static u64 g_active_base = 0;
static u32 g_active_spi  = 0;
static u32 g_active_hz   = 0;

int arch_mem_timer_select_frame(u32 frame_no)
{
	for (u32 i = 0; i < (u32)(sizeof(frames)/sizeof(frames[0])); i++) {
		if (frames[i].frame_no == frame_no) {
			g_active_base = frames[i].base;
			g_active_spi  = frames[i].spi;
			return 0;
		}
	}
	return -1;
}

u32 arch_mem_timer_active_spi(void)
{
	return g_active_spi;
}

u64 arch_mem_timer_active_base(void)
{
	return g_active_base;
}

/* -------------------- programming -------------------- */

static void arch_mem_timer_route_and_enable_spi(u32 spi)
{
	u64 mpidr = read_mpidr_el1();
	gicv3_route_spi_to_self(spi, mpidr);
	gicv3_enable_spi(spi, 0x80, mpidr);
}

/* Program oneshot event on selected frame, do NOT ack here */
static void arch_mem_timer_program_oneshot(u64 base, u32 hz)
{
	u32 frq = mmio_read32(base + CNTFRQ);
	u64 now = mmio_read64_cntpct(base);
	u64 delta = (u64)frq / hz;

	/* disable before CVAL */
	mmio_write32(base + CNTP_CTL, 0);
	dsb_sy();

	/* set compare */
	mmio_write64_cval(base, now + delta);
	dsb_sy();

	/* enable + unmask */
	mmio_write32(base + CNTP_CTL, ARCH_TIMER_CTRL_ENABLE); /* IT_MASK=0 */
	dsb_sy();
	isb_();
}

void arch_mem_timer_start_hz(u32 hz)
{
	if (!g_active_base || !g_active_spi) {
		/* default to frame0 if user didn't select */
		arch_mem_timer_select_frame(0);
	}

	g_active_hz = hz;

	/* route+enable SPI in GICD */
	arch_mem_timer_route_and_enable_spi(g_active_spi);

	/* program timer */
	arch_mem_timer_program_oneshot(g_active_base, hz);
}

void arch_mem_timer_ack_and_rearm(void)
{
	if (!g_active_base || !g_active_spi || !g_active_hz)
		return;

	/*
	 * Ack model used by Linux arch_timer_mmio_handler():
	 * if IT_STAT is set, write IT_MASK=1 to clear/ack,
	 * then schedule next event.
	 */
	u32 ctl = mmio_read32(g_active_base + CNTP_CTL);
	if (ctl & ARCH_TIMER_CTRL_IT_STAT) {
		ctl |= ARCH_TIMER_CTRL_IT_MASK;
		mmio_write32(g_active_base + CNTP_CTL, ctl);
		dsb_sy();
	}

	arch_mem_timer_program_oneshot(g_active_base, g_active_hz);
}

/* For convenience if you want old name */
void arch_mem_timer_ack_and_rearm_hz(u32 hz)
{
	g_active_hz = hz;
	arch_mem_timer_ack_and_rearm();
}

/* -------------------- probing helpers -------------------- */

/* Optional: read/restore DAIF (local IRQ mask) */
static inline u64 read_daif(void)
{
	u64 v;
	__asm__ volatile("mrs %0, daif" : "=r"(v));
	return v;
}

static inline void write_daif(u64 v)
{
	__asm__ volatile("msr daif, %0" :: "r"(v) : "memory");
	isb_();
}

static inline void local_irq_disable_daif(void)
{
	/* mask IRQ+FIQ */
	__asm__ volatile("msr daifset, #3" ::: "memory");
	isb_();
}

int arch_mem_timer_probe_frames(u32 hz, volatile u64 *shm, int include_frame0)
{
	u64 mpidr = read_mpidr_el1();
	u64 daif_saved = read_daif();

	/* keep local IRQs masked so pending won't be consumed */
	local_irq_disable_daif();

	u32 out = 300;

	for (u32 i = 0; i < (u32)(sizeof(frames)/sizeof(frames[0])); i++) {
		u64 base = frames[i].base;
		u32 spi  = frames[i].spi;
		u32 fno  = frames[i].frame_no;

		if (!include_frame0 && fno == 0)
			continue;

		/* -------- Step 1: program GICD (route+enable) -------- */
		gicv3_route_spi_to_self(spi, mpidr);
		gicv3_enable_spi(spi, 0x80, mpidr);

		/* -------- Step 2: read-back GICD state -------- */
		u32 isen = gicd_read_isenabler(spi);
		u32 igrp = gicd_read_igroupr(spi);
		u8  ipri = gicd_read_ipriority(spi);
		u64 irou = gicd_read_irouter(spi);

		u32 en_bit = (isen >> (spi % 32)) & 1U;
		u32 grp_bit = (igrp >> (spi % 32)) & 1U; /* 1 => Group1NS usually */

		/* -------- Step 3: program timer -------- */
		arch_mem_timer_program_oneshot(base, hz);

		/* -------- Step 4: tight poll for pending -------- */
		const u32 max_iter = 200000;
		u32 seen_stat = 0;
		u32 seen_pend = 0;
		u32 first_pend_iter = 0;

		u32 ctl = 0;
		u32 pend = 0;
		u32 ctl_at_first_pend = 0;
		u32 pend_at_first_pend = 0;

		for (u32 it = 1; it <= max_iter; it++) {
			ctl = mmio_read32(base + CNTP_CTL);
			if (ctl & ARCH_TIMER_CTRL_IT_STAT)
				seen_stat = 1;

			pend = mmio_read32(GICD_BASE + GICD_ISPENDR(0));
			if (((pend >> spi) & 1U) && !seen_pend) {
				seen_pend = 1;
				first_pend_iter = it;
				ctl_at_first_pend = ctl;
				pend_at_first_pend = pend;
				break;
			}

			if ((it & 0x3ff) == 0)
				__asm__ volatile("nop");
		}

		u32 ctl_final  = mmio_read32(base + CNTP_CTL);
		u32 pend_final = mmio_read32(GICD_BASE + GICD_ISPENDR(0));

		/*
		 * shm layout per frame (16 u64 each):
		 *  [0]  frame_no
		 *  [1]  base
		 *  [2]  spi
		 *  [3]  summary:
		 *        bit0: seen_stat
		 *        bit1: seen_pend
		 *        bit2: en_bit (GICD ISENABLER readback)
		 *        bit3: grp_bit (GICD IGROUPR readback)
		 *        bits[63:32]: first_pend_iter
		 *  [4]  ctl_final
		 *  [5]  pend_final (ISPENDR0)
		 *  [6]  ctl_at_first_pend
		 *  [7]  pend_at_first_pend
		 *  [8]  isenabler_word
		 *  [9]  igroupr_word
		 *  [10] ipriority (low 8 bits)
		 *  [11] irouter (64-bit)
		 *  [12] mpidr used
		 *  [13..15] reserved
		 */
		u64 summary = 0;
		summary |= (u64)(seen_stat ? 1 : 0) << 0;
		summary |= (u64)(seen_pend ? 1 : 0) << 1;
		summary |= (u64)(en_bit ? 1 : 0) << 2;
		summary |= (u64)(grp_bit ? 1 : 0) << 3;
		summary |= ((u64)first_pend_iter) << 32;

		shm[out + 0] = fno;
		shm[out + 1] = base;
		shm[out + 2] = spi;
		shm[out + 3] = summary;
		shm[out + 4] = ctl_final;
		shm[out + 5] = pend_final;
		shm[out + 6] = ctl_at_first_pend;
		shm[out + 7] = pend_at_first_pend;
		shm[out + 8] = isen;
		shm[out + 9] = igrp;
		shm[out +10] = (u64)ipri;
		shm[out +11] = irou;
		shm[out +12] = mpidr;
		shm[out +13] = 0;
		shm[out +14] = 0;
		shm[out +15] = 0;

		/* advance */
		out += 16;

		if (seen_pend) {
			shm[397] = fno;
			shm[398] = spi;
			shm[399] = base;
			write_daif(daif_saved);
			return 0;
		}
	}

	write_daif(daif_saved);
	return -1;
}
