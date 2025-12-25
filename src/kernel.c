#include <asm/pgtable.h>
#include <asm/pgtable_prot.h>
#include <asm/pgtable_hwdef.h>
#include <asm/sysregs.h>
#include <asm/barrier.h>
#include <string.h>
#include <mm.h>

extern char _shared_memory[];
extern char _stack_bottom[];

void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{

}

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
	unsigned long *SHM_BASE = _shared_memory;
	unsigned long phys;
	volatile unsigned long *shm = (volatile unsigned long*)_shared_memory;

	//memset(_shared_memory, 0, _stack_bottom - _shared_memory);
	*SHM_BASE = 0xeeeeeeeee;

	//mem_init(0, 0);00000000d87fffc0
	mem_init(0, 0);

	//shm[10] = read_sp();
	paging_init();

	*SHM_BASE = 0x10101010;

	return;
}
