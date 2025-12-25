#include <asm/pgtable.h>
#include <asm/pgtable_prot.h>
#include <asm/pgtable_hwdef.h>
#include <asm/sysregs.h>
#include <asm/barrier.h>
#include <string.h>
#include <asm/base.h>

#define NO_BLOCK_MAPPINGS BIT(0)
#define NO_CONT_MAPPINGS BIT(1)

extern char idmap_pg_dir[];

extern char _text_boot[], _etext_boot[];
extern char _text[], _etext[];

extern char _shared_memory[];

static void alloc_init_pte(pmd_t *pmdp, unsigned long addr,
		unsigned long end, unsigned long phys,
		unsigned long prot,
		unsigned long (*alloc_pgtable)(void),
		unsigned long flags)
{
	pmd_t pmd = *pmdp;
	pte_t *ptep;

	if (pmd_none(pmd)) {
		unsigned long pte_phys;

		pte_phys = alloc_pgtable();
		set_pmd(pmdp, __pmd(pte_phys | PMD_TYPE_TABLE));
		pmd = *pmdp;
	}

	ptep = pte_offset_phys(pmdp, addr);
	do {
		set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, prot));
		phys += PAGE_SIZE;
	} while (ptep++, addr += PAGE_SIZE, addr != end);
}

void pmd_set_section(pmd_t *pmdp, unsigned long phys,
		unsigned long prot)
{
	unsigned long sect_prot = PMD_TYPE_SECT | mk_sect_prot(prot);

	pmd_t new_pmd = pfn_pmd(phys >> PMD_SHIFT, sect_prot);

	set_pmd(pmdp, new_pmd);
}

static void alloc_init_pmd(pud_t *pudp, unsigned long addr,
		unsigned long end, unsigned long phys,
		unsigned long prot,
		unsigned long (*alloc_pgtable)(void),
		unsigned long flags)
{
	unsigned long *SHM_BASE = _shared_memory;
	pud_t pud = *pudp;
	pmd_t *pmdp;
	unsigned long next;
	//*SHM_BASE = 0xAAAAAAA4;

	if (pud_none(pud)) {
		unsigned long pmd_phys;

		pmd_phys = alloc_pgtable();
		set_pud(pudp, __pud(pmd_phys | PUD_TYPE_TABLE));
		pud = *pudp;
	}

	pmdp = pmd_offset_phys(pudp, addr);
	do {
		next = pmd_addr_end(addr, end);

		if (((addr | next | phys) & ~SECTION_MASK) == 0 &&
				(flags & NO_BLOCK_MAPPINGS) == 0)
			pmd_set_section(pmdp, phys, prot);
		else
			alloc_init_pte(pmdp, addr, next, phys,
					prot,  alloc_pgtable, flags);

		phys += next - addr;
	} while (pmdp++, addr = next, addr != end);
}

static void alloc_init_pud(pgd_t *pgdp, unsigned long addr,
		unsigned long end, unsigned long phys,
		unsigned long prot,
		unsigned long (*alloc_pgtable)(void),
		unsigned long flags)
{
	unsigned long *SHM_BASE = _shared_memory;
	pgd_t pgd = *pgdp;
	pud_t *pudp;
	unsigned long next;

	//*SHM_BASE = 0xAAAAAAA0;

	if (pgd_none(pgd)) {
		unsigned long pud_phys;

		//*SHM_BASE = 0xAAAAAAA1;
		pud_phys = alloc_pgtable();
		*SHM_BASE = 0xAAAAAAA2;

		set_pgd(pgdp, __pgd(pud_phys | PUD_TYPE_TABLE));
		pgd = *pgdp;
	}

	//*SHM_BASE = 0xAAAAAAA3;
	pudp = pud_offset_phys(pgdp, addr);
	do {
		next = pud_addr_end(addr, end);
		alloc_init_pmd(pudp, addr, next, phys,
				prot, alloc_pgtable, flags);
		phys += next - addr;

	} while (pudp++, addr = next, addr != end);
}

static void __create_pgd_mapping(pgd_t *pgdir, unsigned long phys,
		unsigned long virt, unsigned long size,
		unsigned long prot,
		unsigned long (*alloc_pgtable)(void),
		unsigned long flags)
{
	unsigned long *SHM_BASE = _shared_memory;

	//*SHM_BASE = 0x88888888;

	pgd_t *pgdp = pgd_offset_raw(pgdir, virt);
	unsigned long addr, end, next;

	phys &= PAGE_MASK;
	addr = virt & PAGE_MASK;
	end = PAGE_ALIGN(virt + size);

	do {
		next = pgd_addr_end(addr, end);
		alloc_init_pud(pgdp, addr, next, phys,
				prot, alloc_pgtable, flags);
		phys += next - addr;
	} while (pgdp++, addr = next, addr != end);
}

#if 1
static unsigned long early_pgtable_alloc(void)
{
	unsigned long *phys;
	unsigned long *SHM_BASE = _shared_memory;

	*SHM_BASE = 0xBBBBBBB0;

	phys = get_free_page();
	*SHM_BASE = phys;
	*phys = 0x87654321;
	*SHM_BASE = 0xBBBBBBB9;
	memset((void *)phys, 0, PAGE_SIZE);
	*SHM_BASE = 0xBBBBBBB2;

	return phys;
}
#else
static inline unsigned long read_sctlr_el1(void)
{
    unsigned long v;
    asm volatile("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}

static unsigned long early_pgtable_alloc(void)
{
    unsigned long phys;
    volatile long *shm = (volatile long *)_shared_memory;

    shm[0] = 0xBBBBBBB0ULL;
    shm[1] = read_sctlr_el1();

    phys = get_free_page();
    shm[2] = phys;

    if (!phys) { shm[3] = 0xDEAD0000ULL; for(;;); }

    *(volatile long *)phys = 0;
    shm[4] = 0xBBBBBBB9ULL;

    memset((void *)phys, 0, PAGE_SIZE);
    shm[5] = 0xBBBBBBB2ULL;

    return phys;
}
#endif

static void create_identical_mapping(void)
{
	unsigned long start;
	unsigned long end;

	unsigned long *SHM_BASE = _shared_memory;

	/*map text*/
	start = (unsigned long)_text_boot;
	end = (unsigned long)_etext;
	__create_pgd_mapping((pgd_t *)idmap_pg_dir, start, start,
			end - start, PAGE_KERNEL_ROX,
			early_pgtable_alloc,
			0);

	//*SHM_BASE = 0x99999999;

	/*map memory*/
	start = PAGE_ALIGN((unsigned long)_etext);
	end = TOTAL_MEMORY;
	__create_pgd_mapping((pgd_t *)idmap_pg_dir, start, start,
			end - start, PAGE_KERNEL,
			early_pgtable_alloc,
			0);
}

static void create_mmio_mapping(void)
{
	__create_pgd_mapping((pgd_t *)idmap_pg_dir, TLMM_PBASE, TLMM_PBASE,
			TLMM_SIZE, PROT_DEVICE_nGnRnE,
			early_pgtable_alloc,
			0);
}

static void cpu_init(void)
{
	unsigned long mair = 0;
	unsigned long tcr = 0;
	unsigned long tmp;
	unsigned long parang;

	asm("tlbi vmalle1");
	dsb(nsh);

	write_sysreg(3UL << 20, cpacr_el1);
	write_sysreg(1 << 12, mdscr_el1);

	mair = MAIR(0x00UL, MT_DEVICE_nGnRnE) |
	       MAIR(0x04UL, MT_DEVICE_nGnRE) |
	       MAIR(0x0cUL, MT_DEVICE_GRE) |
	       MAIR(0x44UL, MT_NORMAL_NC) |
	       MAIR(0xffUL, MT_NORMAL) |
	       MAIR(0xbbUL, MT_NORMAL_WT);
	write_sysreg(mair, mair_el1);

	tcr = TCR_TxSZ(VA_BITS) | TCR_TG_FLAGS;

	tmp = read_sysreg(ID_AA64MMFR0_EL1);
	parang = tmp & 0xf;
	if (parang > ID_AA64MMFR0_PARANGE_48)
		parang = ID_AA64MMFR0_PARANGE_48;

	tcr |= parang << TCR_IPS_SHIFT;

	write_sysreg(tcr, tcr_el1);
}

static int enable_mmu(void)
{
	unsigned long tmp;
	int tgran4;

	tmp = read_sysreg(ID_AA64MMFR0_EL1);
	tgran4 = (tmp >> ID_AA64MMFR0_TGRAN4_SHIFT) & 0xf;
	if (tgran4 != ID_AA64MMFR0_TGRAN4_SUPPORTED)
		return -1;

	write_sysreg(idmap_pg_dir, ttbr0_el1);
	isb();

	write_sysreg(SCTLR_ELx_M, sctlr_el1);
	isb();
	asm("ic iallu");
	dsb(nsh);
	isb();

	return 0;
}

void paging_init(void)
{
	unsigned long *SHM_BASE = _shared_memory;

	memset(idmap_pg_dir, 0, PAGE_SIZE);
	*SHM_BASE = 0x33333333;
	create_identical_mapping();
	*SHM_BASE = 0x44444444;
	create_mmio_mapping();
	*SHM_BASE = 0x55555555;
	cpu_init();
	*SHM_BASE = 0x66666666;
	enable_mmu();
	*SHM_BASE = 0x77777777;
}
