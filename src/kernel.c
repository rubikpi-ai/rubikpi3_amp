#include <asm/pgtable.h>
#include <asm/pgtable_prot.h>
#include <asm/pgtable_hwdef.h>
#include <asm/sysregs.h>
#include <asm/barrier.h>
#include <string.h>
#include <mm.h>
#include <mmu.h>
#include <page_alloc.h>
#include <asm/gpio.h>

extern char _shared_memory[];
extern char _stack_bottom[];

static inline unsigned long read_sctlr_el1(void)
{
    unsigned long v;
    asm volatile("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}

static inline unsigned long read_currentel(void)
{
    unsigned long v;
    asm volatile("mrs %0, CurrentEL" : "=r"(v));
    return v;
}

static inline unsigned long read_sp(void) {
    unsigned long v;
    asm volatile("mov %0, sp" : "=r"(v));
    return v;
}

void kernel_main(void)
{
	unsigned long *SHM_BASE = (unsigned long *)_shared_memory;

	mem_init(0, 0);

	paging_init();

	*SHM_BASE = 0x10101010;

	SHM_BASE[10] = read_sctlr_el1();
	SHM_BASE[11] = read_currentel();

	gpio_pinmux_set(14, mux_gpio);
	gpio_direction_output(14, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

	*SHM_BASE = 0x20202020;

	return;
}
