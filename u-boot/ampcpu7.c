// SPDX-License-Identifier: GPL-2.0+
#include <command.h>
#include <console.h>
#include <linux/types.h>
#include <asm/cache.h>
#include <linux/arm-smccc.h>
#include <env.h>
#include <image.h>
#include <linux/ctype.h>
#include <asm/byteorder.h>
#include <zfs_common.h>
#include <linux/stat.h>
#include <malloc.h>
#include <cpu_func.h>

#define PSCI_0_2_FN64_CPU_ON  0xC4000003ULL

static int do_ampcpu7(struct cmd_tbl *cmdtp, int flag, int argc, char *const argv[])
{
	u64 mpidr = 0x700;          /* CPU7 MPIDR from DT reg = <0 0x700> */
	ulong entry;
	struct arm_smccc_res res;

	for (int i = 0; i < argc; i++) {
		printf("argv[%d]: %s\n", i, argv[i]);
	}

	if (argc < 2)
		return CMD_RET_USAGE;

	entry = hextoul(argv[1], NULL);
	if (argc >= 3)
		mpidr = hextoul(argv[2], NULL);

	/*
	 * Make sure the image at 'entry' is visible to the other CPU:
	 * - Clean D-cache for the whole image region if you know its size.
	 *   Here we do a conservative full flush to keep the command simple.
	 *
	 * If you want range flush: pass filesize and flush_dcache_range(entry, entry+size).
	 */
	flush_dcache_all();
	invalidate_icache_all();

	arm_smccc_smc(PSCI_0_2_FN64_CPU_ON,
		      mpidr, (u64)entry, 0,
		      0, 0, 0, 0, &res);

	printf("PSCI CPU_ON: mpidr=0x%llx entry=0x%lx ret=%lld (0x%llx)\n",
	       mpidr, entry, (long long)res.a0, (unsigned long long)res.a0);

	return (res.a0 == 0) ? CMD_RET_SUCCESS : CMD_RET_FAILURE;

	return 0;
}

U_BOOT_CMD(
	ampcpu7, 3, 0, do_ampcpu7,
	"Start CPU7 via PSCI CPU_ON",
	"<entry_pa_hex> <size_hex>\n"
	"  Example:\n"
	"    load mmc 0:1 ${loadaddr} cpu7.bin\n"
	"    cp.b ${loadaddr} 0xD0000000 ${filesize}\n"
	"    ampcpu7 0xD0000000 ${filesize}\n"
);
