// SPDX-License-Identifier: GPL-2.0
/*
 * amp_handoff: Jump from a Linux-online CPU (default cpu7) into AMP payload
 * located in a no-map reserved-memory region, using memremap() to obtain VA.
 *
 * WARNING:
 *  - This steals the CPU from Linux. System may become unstable unless CPU is isolated.
 *  - Executing from memremap() VA may fail if mapping is XN or not suitable for instruction fetch.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

static int target_cpu = 7;
module_param(target_cpu, int, 0644);
MODULE_PARM_DESC(target_cpu, "Logical CPU to handoff from (default 7)");

static unsigned long long entry_pa = 0xD0800000ULL;
module_param(entry_pa, ullong, 0644);
MODULE_PARM_DESC(entry_pa, "AMP entry physical address (default 0xD0800000)");

static unsigned long long stack_top_pa = 0xD87FF000ULL;
module_param(stack_top_pa, ullong, 0644);
MODULE_PARM_DESC(stack_top_pa, "AMP stack top physical address within carveout (default 0xD87FF000)");

static unsigned long stack_map_size = 0x10000; /* map 64KB for stack */
module_param(stack_map_size, ulong, 0644);
MODULE_PARM_DESC(stack_map_size, "Bytes to map for stack (default 64KB)");

static unsigned long entry_map_size = 0x200000; /* map first 2MB */
module_param(entry_map_size, ulong, 0644);
MODULE_PARM_DESC(entry_map_size, "Bytes to map for entry/code (default 2MB)");

static struct dentry *dbg_dir;

struct handoff_ctx {
	void *entry_va;
	void *stack_va_base;
	unsigned long stack_top_va;
};

static void handoff_fn(void *info)
{
	struct handoff_ctx *c = info;
	void (*entry)(void);

	/* Stop Linux activity on this CPU as much as possible */
	local_irq_disable();
	preempt_disable();

	/* Make sure state is observed */
	isb();
	dsb(sy);

	/*
	 * Switch stack.
	 * We assume stack_top_va points inside a mapped RW region.
	 */
	asm volatile(
		"mov sp, %0\n"
		:
		: "r"(c->stack_top_va)
		: "memory");

	isb();
	dsb(sy);

	/*
	 * Jump to AMP entry.
	 * IMPORTANT: this never returns.
	 */
	entry = (void (*)(void))c->entry_va;

	asm volatile(
		"br %0\n"
		:
		: "r"(entry)
		: "memory");

	/* Not reached */
	while (1)
		cpu_relax();
}

static ssize_t dbg_status_read(struct file *f, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[256];
	int len;

	len = scnprintf(buf, sizeof(buf),
			"target_cpu=%d online=%d possible=%d\n"
			"entry_pa=0x%llx entry_map_size=0x%lx\n"
			"stack_top_pa=0x%llx stack_map_size=0x%lx\n",
			target_cpu,
			cpu_online(target_cpu) ? 1 : 0,
			cpu_possible(target_cpu) ? 1 : 0,
			entry_pa, entry_map_size,
			stack_top_pa, stack_map_size);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

static const struct file_operations dbg_status_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = dbg_status_read,
	.llseek = default_llseek,
};

static ssize_t dbg_handoff_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int ret;
	struct handoff_ctx ctx;
	phys_addr_t entry_base_pa, stack_base_pa;
	unsigned long entry_off, stack_off;

	if (!cpu_possible(target_cpu))
		return -EINVAL;
	if (!cpu_online(target_cpu))
		return -EINVAL;

	/*
	 * Map entry region.
	 * memremap returns a kernel VA. Whether it is executable depends on platform/kernel settings.
	 */
	entry_base_pa = (phys_addr_t)(entry_pa & ~((unsigned long long)PAGE_SIZE - 1));
	entry_off = (unsigned long)(entry_pa - (unsigned long long)entry_base_pa);

	ctx.entry_va = memremap(entry_base_pa, entry_map_size, MEMREMAP_WB);
	if (!ctx.entry_va) {
		pr_err("amp_handoff: memremap entry failed pa=0x%llx\n",
		       (unsigned long long)entry_base_pa);
		return -ENOMEM;
	}
	ctx.entry_va = (void *)((char *)ctx.entry_va + entry_off);

	/*
	 * Map stack region: map [stack_top - stack_map_size, stack_top)
	 * and compute stack_top VA.
	 */
	stack_base_pa = (phys_addr_t)((stack_top_pa - stack_map_size) & ~((unsigned long long)PAGE_SIZE - 1));
	stack_off = (unsigned long)(stack_top_pa - (unsigned long long)stack_base_pa);

	ctx.stack_va_base = memremap(stack_base_pa, stack_map_size, MEMREMAP_WB);
	if (!ctx.stack_va_base) {
		pr_err("amp_handoff: memremap stack failed pa=0x%llx\n",
		       (unsigned long long)stack_base_pa);
		memunmap((void *)((unsigned long)ctx.entry_va & PAGE_MASK));
		return -ENOMEM;
	}
	ctx.stack_top_va = (unsigned long)ctx.stack_va_base + stack_off;

	pr_warn("amp_handoff: HANDOFF cpu%d -> entry_pa=0x%llx entry_va=%px stack_top_pa=0x%llx stack_top_va=0x%lx\n",
		target_cpu,
		entry_pa, ctx.entry_va,
		stack_top_pa, ctx.stack_top_va);

	/*
	 * Run on target CPU and jump.
	 * This never returns if successful.
	 */
	ret = smp_call_function_single(target_cpu, handoff_fn, &ctx, 1);

	/*
	 * If we got here, handoff didn't jump (or returned unexpectedly).
	 * Unmap best-effort.
	 */
	pr_err("amp_handoff: smp_call_function_single returned ret=%d (handoff did not take over)\n", ret);

	memunmap(ctx.stack_va_base);
	/* entry_va points into mapping; unmap base by recomputing base mapping pointer */
	memunmap((void *)((unsigned long)ctx.entry_va & PAGE_MASK));

	return ret ? ret : cnt;
}

static const struct file_operations dbg_handoff_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dbg_handoff_write,
	.llseek = default_llseek,
};

static int __init amp_handoff_init(void)
{
	dbg_dir = debugfs_create_dir("amp_handoff", NULL);
	if (IS_ERR_OR_NULL(dbg_dir))
		return -ENOMEM;

	debugfs_create_file("status", 0444, dbg_dir, NULL, &dbg_status_fops);
	debugfs_create_file("handoff", 0200, dbg_dir, NULL, &dbg_handoff_fops);

	pr_info("amp_handoff: loaded. Use /sys/kernel/debug/amp_handoff/handoff\n");
	return 0;
}

static void __exit amp_handoff_exit(void)
{
	debugfs_remove_recursive(dbg_dir);
	pr_info("amp_handoff: unloaded\n");
}

module_init(amp_handoff_init);
module_exit(amp_handoff_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Handoff a Linux-online CPU to AMP payload in reserved memory");
MODULE_AUTHOR("hongyang-rp + Copilot");
