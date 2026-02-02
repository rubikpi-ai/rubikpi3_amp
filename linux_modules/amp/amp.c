// SPDX-License-Identifier: GPL-2.0
/*
 * ampcpu: load and start baremetal on multiple CPUs via PSCI CPU_ON.
 * Supports module parameter to specify target CPUs.
 *
 * Usage:
 *   insmod amp.ko                      # Use default CPU 7
 *   insmod amp.ko target_cpus=7        # Use CPU 7
 *   insmod amp.ko target_cpus=6,7      # Use CPUs 6 and 7
 *   insmod amp.ko target_cpus=4,5,6,7  # Use CPUs 4,5,6,7
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/arm-smccc.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <asm/cacheflush.h>
#include <asm/io.h>
#include <linux/kallsyms.h>
#include <linux/firmware.h>

#define DRV_NAME "ampcpu"
#define DEV_NAME "ampcpu"
#define FW_DEFAULT_NAME "rubikpi3_amp.bin"

#define PSCI_0_2_FN64_CPU_ON  0xC4000003ULL

/* shared memory reset protocol */
#define AMP_CMD_IDX      32
#define AMP_CMD_RESET    0x52534554ULL /* 'RSET' */

/* Maximum number of CPUs supported */
#define MAX_AMP_CPUS     8

/* Per-CPU context */
struct ampcpu_info {
	int cpu;                /* logical cpu id */
	u64 mpidr;              /* PSCI target MPIDR */
	bool active;            /* is this CPU slot active */
	bool was_online;        /* was CPU online before we took it */
};

struct ampcpu_ctx {
	struct mutex lock;

	/* CPU configuration */
	int num_cpus;                       /* number of CPUs to use */
	struct ampcpu_info cpus[MAX_AMP_CPUS];

	phys_addr_t entry_pa;   /* payload entry PA, default 0xD0800000 */
	phys_addr_t shm_pa;     /* shared memory PA, default 0xD7C00000 */
	size_t max_load_size;   /* payload load limit */

	char fw_name[128];

	u64 bytes_loaded;
	s64 last_psci_ret;

	int boot_via_secondary;     /* 0: use entry_pa; 1: use secondary_entry_va */
	u64 secondary_entry_va;     /* kernel VA from /proc/kallsyms */

	/* chrdev */
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct device *device;

	/* debugfs */
	struct dentry *dbg_dir;
};

/* Module parameters */
static int target_cpus[MAX_AMP_CPUS] = { 7, -1, -1, -1, -1, -1, -1, -1 };
static int target_cpus_count = 1;
module_param_array(target_cpus, int, &target_cpus_count, 0444);
MODULE_PARM_DESC(target_cpus, "List of target CPU IDs (default: 7)");

static struct ampcpu_ctx g = {
	.entry_pa = 0xD0800000ULL,
	.shm_pa = 0xD7C00000ULL,
	.max_load_size = 40 * 1024 * 1024,
	.last_psci_ret = 0,

	.boot_via_secondary = 0,
	.secondary_entry_va = 0,

	.fw_name = FW_DEFAULT_NAME,
};

/* Convert CPU id to MPIDR */
static u64 cpu_to_mpidr(int cpu)
{
	/* For QCS6490: CPU 0-7 map to MPIDR 0x000-0x700 */
	return (u64)cpu << 8;
}

static int psci_cpu_on(u64 mpidr, phys_addr_t entry_pa)
{
	struct arm_smccc_res res;

	arm_smccc_smc(PSCI_0_2_FN64_CPU_ON,
		      mpidr, (u64)entry_pa, 0,
		      0, 0, 0, 0, &res);
	return (int)res.a0;
}

static void amp_flush_payload(phys_addr_t pa, size_t sz)
{
	void *va;

	if (sz == 0)
		return;

	va = memremap(pa, sz, MEMREMAP_WB);
	if (!va) {
		pr_err(DRV_NAME ": memremap(WB) failed pa=0x%llx sz=0x%zx\n",
		       (unsigned long long)pa, sz);
		return;
	}

	flush_cache_vmap((unsigned long)va, (unsigned long)va + sz);
	flush_icache_range((unsigned long)va, (unsigned long)va + sz);

	memunmap(va);
}

/* Send reset command and wait for CPUs to go offline */
static int stop_amp(void)
{
	void *va;
	phys_addr_t cmd_pa;
	u64 *p;
	int i, retry;
	int all_offline;

	mutex_lock(&g.lock);
	cmd_pa = g.shm_pa + (phys_addr_t)(AMP_CMD_IDX * sizeof(u64));
	mutex_unlock(&g.lock);

	va = memremap(cmd_pa, sizeof(u64), MEMREMAP_WB);
	if (!va)
		return -ENOMEM;

	p = (u64 *)va;
	WRITE_ONCE(*p, AMP_CMD_RESET);

	/* flush so CPUs see it */
	flush_cache_vmap((unsigned long)va, (unsigned long)va + sizeof(u64));

	memunmap(va);

	pr_info(DRV_NAME ": reset command sent, waiting for CPUs to go offline...\n");

	/* give CPUs time to observe and CPU_OFF */
	msleep(100);

	/* Wait for CPUs to actually go offline, with timeout */
	for (retry = 0; retry < 50; retry++) {
		all_offline = 1;
		mutex_lock(&g.lock);
		for (i = 0; i < g.num_cpus; i++) {
			if (g.cpus[i].active && cpu_online(g.cpus[i].cpu)) {
				all_offline = 0;
				break;
			}
		}
		mutex_unlock(&g.lock);

		if (all_offline) {
			pr_info(DRV_NAME ": all CPUs offline after %d retries\n", retry);
			return 0;
		}
		msleep(10);
	}

	pr_warn(DRV_NAME ": some CPUs still online after 500ms, proceeding anyway\n");
	return 0;
}

/* Return all CPUs to Linux */
static int return_cpus_to_linux(void)
{
	int i, ret;
	int errors = 0;
	int cpu;

	mutex_lock(&g.lock);
	for (i = 0; i < g.num_cpus; i++) {
		if (!g.cpus[i].active)
			continue;

		cpu = g.cpus[i].cpu;
		mutex_unlock(&g.lock);

		if (!cpu_online(cpu)) {
			pr_info(DRV_NAME ": calling add_cpu(%d)...\n", cpu);
			ret = add_cpu(cpu);
			if (ret) {
				pr_err(DRV_NAME ": add_cpu(%d) failed: %d\n", cpu, ret);
				errors++;
			} else {
				pr_info(DRV_NAME ": CPU%d successfully returned to Linux\n", cpu);
			}
		}

		mutex_lock(&g.lock);
	}
	mutex_unlock(&g.lock);

	return errors ? -EIO : 0;
}

/* -------------------- chrdev: write payload -------------------- */

static ssize_t ampcpu_write(struct file *file, const char __user *ubuf, size_t len, loff_t *ppos)
{
	void *va;
	size_t to_copy;
	phys_addr_t dst;
	loff_t off = *ppos;

	if (len == 0)
		return 0;

	mutex_lock(&g.lock);

	if (off < 0 || off >= g.max_load_size) {
		mutex_unlock(&g.lock);
		return -ENOSPC;
	}

	to_copy = min_t(size_t, len, g.max_load_size - (size_t)off);
	dst = g.entry_pa + (phys_addr_t)off;

	va = memremap(dst, to_copy, MEMREMAP_WB);
	if (!va) {
		mutex_unlock(&g.lock);
		return -ENOMEM;
	}

	if (copy_from_user(va, ubuf, to_copy)) {
		memunmap(va);
		mutex_unlock(&g.lock);
		return -EFAULT;
	}

	flush_cache_vmap((unsigned long)va, (unsigned long)va + to_copy);
	flush_icache_range((unsigned long)va, (unsigned long)va + to_copy);

	memunmap(va);

	off += to_copy;
	*ppos = off;

	if ((u64)off > g.bytes_loaded)
		g.bytes_loaded = (u64)off;

	mutex_unlock(&g.lock);
	return to_copy;
}

static loff_t ampcpu_llseek(struct file *file, loff_t off, int whence)
{
	loff_t newpos;

	mutex_lock(&g.lock);

	switch (whence) {
	case SEEK_SET: newpos = off; break;
	case SEEK_CUR: newpos = file->f_pos + off; break;
	case SEEK_END: newpos = (loff_t)g.max_load_size + off; break;
	default:
		mutex_unlock(&g.lock);
		return -EINVAL;
	}

	if (newpos < 0 || newpos > (loff_t)g.max_load_size) {
		mutex_unlock(&g.lock);
		return -EINVAL;
	}

	file->f_pos = newpos;
	mutex_unlock(&g.lock);
	return newpos;
}

static const struct file_operations ampcpu_fops = {
	.owner  = THIS_MODULE,
	.write  = ampcpu_write,
	.llseek = ampcpu_llseek,
};

/* -------------------- debugfs helpers -------------------- */

static ssize_t dbg_status_read(struct file *f, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char *buf;
	int len = 0;
	int i;
	int buf_size = 1024;

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&g.lock);

	len += scnprintf(buf + len, buf_size - len,
			"num_cpus=%d\n", g.num_cpus);

	for (i = 0; i < g.num_cpus; i++) {
		if (!g.cpus[i].active)
			continue;
		len += scnprintf(buf + len, buf_size - len,
				"cpu[%d]: id=%d mpidr=0x%llx online=%d\n",
				i, g.cpus[i].cpu,
				(unsigned long long)g.cpus[i].mpidr,
				cpu_online(g.cpus[i].cpu));
	}

	len += scnprintf(buf + len, buf_size - len,
			"entry_pa=0x%llx\n"
			"shm_pa=0x%llx (cmd idx=%u)\n"
			"max_load_size=0x%zx\n"
			"bytes_loaded=0x%llx\n"
			"last_psci_ret=%lld (0x%llx)\n"
			"boot_via_secondary=%d\n"
			"secondary_entry_va=0x%llx\n",
			(unsigned long long)g.entry_pa,
			(unsigned long long)g.shm_pa, AMP_CMD_IDX,
			g.max_load_size,
			(unsigned long long)g.bytes_loaded,
			(long long)g.last_psci_ret, (unsigned long long)g.last_psci_ret,
			g.boot_via_secondary,
			(unsigned long long)g.secondary_entry_va);

	mutex_unlock(&g.lock);

	len = simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
	kfree(buf);
	return len;
}

static phys_addr_t resolve_entry_pa(void)
{
	phys_addr_t entry;

	mutex_lock(&g.lock);
	entry = g.entry_pa;

	if (g.boot_via_secondary) {
		unsigned long va = (unsigned long)g.secondary_entry_va;
		if (!va) {
			mutex_unlock(&g.lock);
			return 0;
		}

		entry = (phys_addr_t)virt_to_phys((void *)va);
	}
	mutex_unlock(&g.lock);

	return entry;
}


static const struct file_operations dbg_status_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = dbg_status_read,
	.llseek = default_llseek,
};

static int start_amp(void)
{
	int i, ret;
	phys_addr_t entry;
	u64 flush_sz;
	int started = 0;
	int cpu;
	u64 mpidr;

	mutex_lock(&g.lock);
	entry = g.entry_pa;
	flush_sz = g.bytes_loaded ? g.bytes_loaded : 0x1000;
	mutex_unlock(&g.lock);

	amp_flush_payload(entry, (size_t)flush_sz);

	entry = resolve_entry_pa();

	mutex_lock(&g.lock);
	for (i = 0; i < g.num_cpus; i++) {
		if (!g.cpus[i].active)
			continue;

		cpu = g.cpus[i].cpu;
		mpidr = g.cpus[i].mpidr;

		if (!cpu_possible(cpu)) {
			pr_err(DRV_NAME ": CPU%d not possible\n", cpu);
			continue;
		}

		if (cpu_online(cpu)) {
			pr_err(DRV_NAME ": CPU%d still online, skipping\n", cpu);
			continue;
		}

		mutex_unlock(&g.lock);

		ret = psci_cpu_on(mpidr, entry);

		mutex_lock(&g.lock);
		g.last_psci_ret = ret;

		pr_info(DRV_NAME ": PSCI CPU_ON cpu=%d mpidr=0x%llx entry=0x%llx ret=%d\n",
		       cpu, (unsigned long long)mpidr, (unsigned long long)entry, ret);

		if (ret == 0)
			started++;
	}
	mutex_unlock(&g.lock);

	return started > 0 ? 0 : -EIO;
}

static ssize_t dbg_start_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int ret = start_amp();
	return ret ? ret : cnt;
}

static const struct file_operations dbg_start_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dbg_start_write,
	.llseek = default_llseek,
};

static ssize_t dbg_stop_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int ret;

	ret = stop_amp();
	if (ret) {
		pr_err(DRV_NAME ": stop_amp failed: %d\n", ret);
		return ret;
	}

	ret = return_cpus_to_linux();
	if (ret) {
		pr_err(DRV_NAME ": return_cpus_to_linux failed: %d\n", ret);
		return ret;
	}

	return cnt;
}

static const struct file_operations dbg_stop_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dbg_stop_write,
	.llseek = default_llseek,
};

static ssize_t dbg_flush_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	u64 sz;
	phys_addr_t entry;
	size_t maxsz;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';

	if (kstrtoull(buf, 0, &sz))
		return -EINVAL;

	mutex_lock(&g.lock);
	entry = g.entry_pa;
	maxsz = g.max_load_size;
	mutex_unlock(&g.lock);

	amp_flush_payload(entry, (size_t)min_t(u64, sz, (u64)maxsz));
	return cnt;
}

static const struct file_operations dbg_flush_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dbg_flush_write,
	.llseek = default_llseek,
};

static ssize_t dbg_reset_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int ret;

	ret = stop_amp();
	if (ret)
		return ret;

	pr_info(DRV_NAME ": reset command sent via shm[%d]\n", AMP_CMD_IDX);
	return cnt;
}

static const struct file_operations dbg_reset_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dbg_reset_write,
	.llseek = default_llseek,
};

/* rw u64 nodes */
static ssize_t dbg_u64_read(struct file *f, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	u64 *val = f->private_data;
	char buf[64];
	int len;

	mutex_lock(&g.lock);
	len = scnprintf(buf, sizeof(buf), "0x%llx\n", (unsigned long long)*val);
	mutex_unlock(&g.lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

static ssize_t dbg_u64_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	u64 *val = f->private_data;
	char buf[64];
	u64 tmp;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';

	if (kstrtoull(buf, 0, &tmp))
		return -EINVAL;

	mutex_lock(&g.lock);
	*val = tmp;
	mutex_unlock(&g.lock);

	return cnt;
}

static const struct file_operations dbg_u64_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = dbg_u64_read,
	.write = dbg_u64_write,
	.llseek = default_llseek,
};

/* rw int node */
static ssize_t dbg_int_read(struct file *f, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int *val = f->private_data;
	char buf[64];
	int len;

	mutex_lock(&g.lock);
	len = scnprintf(buf, sizeof(buf), "%d\n", *val);
	mutex_unlock(&g.lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

static ssize_t dbg_int_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int *val = f->private_data;
	char buf[64];
	long tmp;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';

	if (kstrtol(buf, 0, &tmp))
		return -EINVAL;

	mutex_lock(&g.lock);
	*val = (int)tmp;
	mutex_unlock(&g.lock);

	return cnt;
}

static const struct file_operations dbg_int_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = dbg_int_read,
	.write = dbg_int_write,
	.llseek = default_llseek,
};

/* rw size_t node */
static ssize_t dbg_size_read(struct file *f, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	size_t *val = f->private_data;
	char buf[64];
	int len;

	mutex_lock(&g.lock);
	len = scnprintf(buf, sizeof(buf), "0x%zx\n", *val);
	mutex_unlock(&g.lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

static ssize_t dbg_size_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	size_t *val = f->private_data;
	char buf[64];
	u64 tmp;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';

	if (kstrtoull(buf, 0, &tmp))
		return -EINVAL;

	mutex_lock(&g.lock);
	*val = (size_t)tmp;
	mutex_unlock(&g.lock);

	return cnt;
}

static const struct file_operations dbg_size_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = dbg_size_read,
	.write = dbg_size_write,
	.llseek = default_llseek,
};

static int amp_load_firmware(void)
{
	const struct firmware *fw;
	size_t sz;
	int ret;
	void *va;

	ret = request_firmware(&fw, g.fw_name, NULL);
	if (ret) {
		pr_err(DRV_NAME ": request_firmware('%s') failed: %d\n", g.fw_name, ret);
		return ret;
	}

	sz = fw->size;
	if (sz == 0 || sz > g.max_load_size) {
		pr_err(DRV_NAME ": bad firmware size: %zu (max=%zu)\n", sz, g.max_load_size);
		release_firmware(fw);
		return -EINVAL;
	}

	va = memremap(g.entry_pa, sz, MEMREMAP_WB);
	if (!va) {
		pr_err(DRV_NAME ": memremap entry_pa=0x%llx sz=%zu failed\n",
		       (unsigned long long)g.entry_pa, sz);
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(va, fw->data, sz);

	flush_cache_vmap((unsigned long)va, (unsigned long)va + sz);
	flush_icache_range((unsigned long)va, (unsigned long)va + sz);

	memunmap(va);
	release_firmware(fw);

	g.bytes_loaded = sz;

	return 0;
}

/* Initialize CPU configuration from module parameters */
static void init_cpu_config(void)
{
	int i;
	int cpu;

	g.num_cpus = 0;

	for (i = 0; i < MAX_AMP_CPUS && i < target_cpus_count; i++) {
		cpu = target_cpus[i];

		if (cpu < 0 || cpu >= nr_cpu_ids)
			continue;

		if (!cpu_possible(cpu)) {
			pr_warn(DRV_NAME ": CPU%d not possible, skipping\n", cpu);
			continue;
		}

		g.cpus[g.num_cpus].cpu = cpu;
		g.cpus[g.num_cpus].mpidr = cpu_to_mpidr(cpu);
		g.cpus[g.num_cpus].active = true;
		g.cpus[g.num_cpus].was_online = cpu_online(cpu);
		g.num_cpus++;

		pr_info(DRV_NAME ": configured CPU%d (mpidr=0x%llx)\n",
			cpu, (unsigned long long)cpu_to_mpidr(cpu));
	}

	if (g.num_cpus == 0) {
		pr_warn(DRV_NAME ": no valid CPUs configured, using default CPU7\n");
		g.cpus[0].cpu = 7;
		g.cpus[0].mpidr = cpu_to_mpidr(7);
		g.cpus[0].active = true;
		g.cpus[0].was_online = cpu_online(7);
		g.num_cpus = 1;
	}
}

/* Remove CPUs from Linux scheduler */
static int remove_cpus_from_linux(void)
{
	int i, ret;
	int errors = 0;
	int cpu;

	for (i = 0; i < g.num_cpus; i++) {
		if (!g.cpus[i].active)
			continue;

		cpu = g.cpus[i].cpu;

		if (cpu_online(cpu)) {
			pr_info(DRV_NAME ": removing CPU%d from Linux...\n", cpu);
			ret = remove_cpu(cpu);
			if (ret) {
				pr_err(DRV_NAME ": remove_cpu(%d) failed: %d\n", cpu, ret);
				errors++;
				g.cpus[i].active = false;
			}
		}
	}

	return errors ? -EIO : 0;
}

/* -------------------- module init/exit -------------------- */

static int __init ampcpu_init(void)
{
	int ret;

	mutex_init(&g.lock);

	/* Initialize CPU configuration from module parameters */
	init_cpu_config();

	/* chrdev */
	ret = alloc_chrdev_region(&g.devt, 0, 1, DEV_NAME);
	if (ret)
		return ret;

	cdev_init(&g.cdev, &ampcpu_fops);
	g.cdev.owner = THIS_MODULE;

	ret = cdev_add(&g.cdev, g.devt, 1);
	if (ret)
		goto err_unregister;

	g.class = class_create(DEV_NAME);
	if (IS_ERR(g.class)) {
		ret = PTR_ERR(g.class);
		goto err_cdev_del;
	}

	g.device = device_create(g.class, NULL, g.devt, NULL, DEV_NAME);
	if (IS_ERR(g.device)) {
		ret = PTR_ERR(g.device);
		goto err_class_destroy;
	}

	ret = amp_load_firmware();
	if (!ret) {
		/* Remove CPUs from Linux before starting baremetal */
		ret = remove_cpus_from_linux();
		if (!ret) {
			ret = start_amp();
			pr_info(DRV_NAME ": start_amp returned %d\n", ret);
		}
	} else {
		pr_warn(DRV_NAME ": initial firmware load failed: %d\n", ret);
	}

	/* debugfs */
	g.dbg_dir = debugfs_create_dir(DRV_NAME, NULL);
	if (IS_ERR_OR_NULL(g.dbg_dir)) {
		ret = -ENOMEM;
		goto err_device_destroy;
	}

	debugfs_create_file("status", 0444, g.dbg_dir, NULL, &dbg_status_fops);
	debugfs_create_file("start", 0200, g.dbg_dir, NULL, &dbg_start_fops);
	debugfs_create_file("stop", 0200, g.dbg_dir, NULL, &dbg_stop_fops);
	debugfs_create_file("reset", 0200, g.dbg_dir, NULL, &dbg_reset_fops);
	debugfs_create_file("flush", 0200, g.dbg_dir, NULL, &dbg_flush_fops);

	debugfs_create_file("entry_pa", 0644, g.dbg_dir, (u64 *)&g.entry_pa, &dbg_u64_fops);
	debugfs_create_file("shm_pa", 0644, g.dbg_dir, (u64 *)&g.shm_pa, &dbg_u64_fops);
	debugfs_create_file("max_load_size", 0644, g.dbg_dir, &g.max_load_size, &dbg_size_fops);

	debugfs_create_file("boot_via_secondary", 0644, g.dbg_dir, &g.boot_via_secondary, &dbg_int_fops);
	debugfs_create_file("secondary_entry_va", 0644, g.dbg_dir, &g.secondary_entry_va, &dbg_u64_fops);

	pr_info(DRV_NAME ": /dev/%s created; debugfs: /sys/kernel/debug/%s/\n",
		DEV_NAME, DRV_NAME);
	pr_info(DRV_NAME ": using %d CPU(s), entry_pa=0x%llx shm_pa=0x%llx\n",
		g.num_cpus,
		(unsigned long long)g.entry_pa,
		(unsigned long long)g.shm_pa);

	return 0;

err_device_destroy:
	device_destroy(g.class, g.devt);
err_class_destroy:
	class_destroy(g.class);
err_cdev_del:
	cdev_del(&g.cdev);
err_unregister:
	unregister_chrdev_region(g.devt, 1);
	return ret;
}

static void __exit ampcpu_exit(void)
{
	int ret;

	/* best-effort reset on unload */
	ret = stop_amp();

	if (!ret) {
		/* Return all CPUs to Linux */
		ret = return_cpus_to_linux();
		if (ret)
			pr_warn(DRV_NAME ": some CPUs failed to return to Linux\n");
	} else {
		pr_warn(DRV_NAME ": stop_amp failed: %d, CPUs not returned\n", ret);
	}

	debugfs_remove_recursive(g.dbg_dir);
	device_destroy(g.class, g.devt);
	class_destroy(g.class);
	cdev_del(&g.cdev);
	unregister_chrdev_region(g.devt, 1);

	pr_info(DRV_NAME ": unloaded\n");
}

module_init(ampcpu_init);
module_exit(ampcpu_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMP multi-CPU loader/starter via PSCI CPU_ON + debugfs");
MODULE_AUTHOR("Hongyang Zhao <hongyang.zhao@thundersoft.com>");
