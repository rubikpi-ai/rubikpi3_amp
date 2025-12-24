#include <asm/pgtable.h>
#include <asm/pgtable_prot.h>
#include <asm/pgtable_hwdef.h>
#include <asm/sysregs.h>
#include <asm/barrier.h>
#include <string.h>

void bad_mode(struct pt_regs *regs, int reason, unsigned int esr)
{
}


void kernel_main(void)
{
	memset(idmap_pg_dir, 0, PAGE_SIZE);

	int *SHM_BASE = 0xD1000000;
	*SHM_BASE = 0x123456789;

	return;
}
