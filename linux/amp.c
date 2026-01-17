// SPDX-License-Identifier: GPL-2.0
/*
 * ampcpu7: load and start baremetal on CPU7 via PSCI CPU_ON.
 * Adds:
 *  - debugfs reset node: writes RESET command to shared memory for baremetal to PSCI CPU_OFF itself.
 *  - on rmmod: best-effort send reset command.
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
#include <asm/io.h>      // virt_to_phys
#include <linux/kallsyms.h>
#include <linux/firmware.h>

#define DRV_NAME "ampcpu7"
#define DEV_NAME "ampcpu7"
#define FW_DEFAULT_NAME "rubikpi3_amp.bin"

#define PSCI_0_2_FN64_CPU_ON  0xC4000003ULL

/* shared memory reset protocol */
#define AMP_CMD_IDX      32
#define AMP_CMD_RESET    0x52534554ULL /* 'RSET' */

struct ampcpu_ctx {
	struct mutex lock;

	int target_cpu;         /* logical cpu id, default 7 */
	u64 mpidr;              /* PSCI target MPIDR, default 0x700 */
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

static struct ampcpu_ctx g = {
	.target_cpu = 7,
	.mpidr = 0x700,
	.entry_pa = 0xD0800000ULL,
	.shm_pa = 0xD7C00000ULL,
	.max_load_size = 40 * 1024 * 1024,
	.last_psci_ret = 0,

	.boot_via_secondary = 0,
	.secondary_entry_va = 0,

	.fw_name = FW_DEFAULT_NAME,
};

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

/* best-effort: write RESET command into shared memory */
static int stop_amp(void)
{
	void *va;
	phys_addr_t cmd_pa;
	u64 *p;
	int cpu;
	int retry;

	mutex_lock(&g.lock);
	cmd_pa = g.shm_pa + (phys_addr_t)(AMP_CMD_IDX * sizeof(u64));
	cpu = g.target_cpu;
	mutex_unlock(&g.lock);

	va = memremap(cmd_pa, sizeof(u64), MEMREMAP_WB);
	if (!va)
		return -ENOMEM;

	p = (u64 *)va;
	WRITE_ONCE(*p, AMP_CMD_RESET);

	/* flush so CPU7 sees it */
	flush_cache_vmap((unsigned long)va, (unsigned long)va + sizeof(u64));

	memunmap(va);

	pr_info(DRV_NAME ": reset command sent, waiting for CPU%d to go offline...\n", cpu);

	/* give CPU7 more time to observe and CPU_OFF */
	msleep(100);

	/* Wait for CPU to actually go offline, with timeout */
	for (retry = 0; retry < 50; retry++) {
		if (!cpu_online(cpu)) {
			pr_info(DRV_NAME ": CPU%d is now offline after %d retries\n", cpu, retry);
			return 0;
		}
		msleep(10);
	}

	pr_warn(DRV_NAME ": CPU%d still online after 500ms, proceeding anyway\n", cpu);
	return 0;
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
	char buf[320];
	int len;
	int cpu;
	u64 mpidr;
	u64 entry;
	u64 shm;
	size_t maxsz;
	u64 loaded;
	s64 last;
	int boot_via_secondary;
	u64 secondary_entry_va;

	mutex_lock(&g.lock);
	cpu = g.target_cpu;
	mpidr = g.mpidr;
	entry = (u64)g.entry_pa;
	shm = (u64)g.shm_pa;
	maxsz = g.max_load_size;
	loaded = g.bytes_loaded;
	last = g.last_psci_ret;
	mutex_unlock(&g.lock);
	boot_via_secondary = g.boot_via_secondary;
	secondary_entry_va = g.secondary_entry_va;

	len = scnprintf(buf, sizeof(buf),
			"target_cpu=%d\n"
			"mpidr=0x%llx\n"
			"entry_pa=0x%llx\n"
			"shm_pa=0x%llx (cmd idx=%u)\n"
			"max_load_size=0x%zx\n"
			"bytes_loaded=0x%llx\n"
			"cpu_online=%d (must be 0 before start)\n"
			"last_psci_ret=%lld (0x%llx)\n",
			"boot_via_secondary=%d\n"
			"secondary_entry_va=0x%llx\n",
			cpu,
			(unsigned long long)mpidr,
			(unsigned long long)entry,
			(unsigned long long)shm, AMP_CMD_IDX,
			maxsz,
			(unsigned long long)loaded,
			cpu_possible(cpu) ? cpu_online(cpu) : -1,
			(long long)last, (unsigned long long)last,
			boot_via_secondary,
			(unsigned long long)secondary_entry_va);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
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

		/*
		 * Convert kernel VA -> PA. This relies on linear mapping.
		 * For secondary_entry (in kernel image) this should be OK.
		 */
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
	int cpu, ret;
	u64 mpidr;
	phys_addr_t entry;
	u64 flush_sz;

	mutex_lock(&g.lock);
	cpu = g.target_cpu;
	mpidr = g.mpidr;
	entry = g.entry_pa;
	flush_sz = g.bytes_loaded ? g.bytes_loaded : 0x1000;
	mutex_unlock(&g.lock);

	if (!cpu_possible(cpu))
		return -EINVAL;
	if (cpu_online(cpu))
		return -EBUSY;

	amp_flush_payload(entry, (size_t)flush_sz);

	entry = resolve_entry_pa();
	ret = psci_cpu_on(mpidr, entry);

	mutex_lock(&g.lock);
	g.last_psci_ret = ret;
	mutex_unlock(&g.lock);

	pr_err(DRV_NAME ": PSCI CPU_ON mpidr=0x%llx entry=0x%llx ret=%d\n",
	       (unsigned long long)mpidr, (unsigned long long)entry, ret);

	return ret;
}

static ssize_t dbg_start_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	return start_amp();
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
	int cpu;

	ret = stop_amp();
	if (ret) {
		pr_err(DRV_NAME ": stop_amp failed: %d\n", ret);
		return ret;
	}

	mutex_lock(&g.lock);
	cpu = g.target_cpu;
	mutex_unlock(&g.lock);

	pr_info(DRV_NAME ": calling add_cpu(%d)...\n", cpu);
	ret = add_cpu(cpu);

	if (ret) {
		pr_err(DRV_NAME ": add_cpu(%d) failed: %d\n", cpu, ret);
		return ret;
	}

	pr_info(DRV_NAME ": CPU%d successfully added back to Linux\n", cpu);
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

	/* send reset cmd even if cpu is online/offline; baremetal decides */
	ret = stop_amp();
	if (ret)
		return ret;

//	start_amp();

	pr_info(DRV_NAME ": reset command sent via shm[%" __stringify(AMP_CMD_IDX) "]\n");
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

/* -------------------- module init/exit -------------------- */

static int __init ampcpu_init(void)
{
	int ret;

	mutex_init(&g.lock);

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
		if (cpu_online(g.target_cpu)) {
			remove_cpu(g.target_cpu);
		}
		ret = start_amp();
		printk("start_amp returned %d\n", ret);
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

	debugfs_create_file("target_cpu", 0644, g.dbg_dir, &g.target_cpu, &dbg_int_fops);
	debugfs_create_file("mpidr", 0644, g.dbg_dir, &g.mpidr, &dbg_u64_fops);
	debugfs_create_file("entry_pa", 0644, g.dbg_dir, (u64 *)&g.entry_pa, &dbg_u64_fops);
	debugfs_create_file("shm_pa", 0644, g.dbg_dir, (u64 *)&g.shm_pa, &dbg_u64_fops);
	debugfs_create_file("max_load_size", 0644, g.dbg_dir, &g.max_load_size, &dbg_size_fops);

	debugfs_create_file("boot_via_secondary", 0644, g.dbg_dir, &g.boot_via_secondary, &dbg_int_fops);
	debugfs_create_file("secondary_entry_va", 0644, g.dbg_dir, &g.secondary_entry_va, &dbg_u64_fops);

	pr_info(DRV_NAME ": /dev/%s created; debugfs: /sys/kernel/debug/%s/\n",
		DEV_NAME, DRV_NAME);
	pr_info(DRV_NAME ": target_cpu=%d mpidr=0x%llx entry_pa=0x%llx shm_pa=0x%llx\n",
		g.target_cpu,
		(unsigned long long)g.mpidr,
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
	/* best-effort reset on unload */
	stop_amp();

	debugfs_remove_recursive(g.dbg_dir);
	device_destroy(g.class, g.devt);
	class_destroy(g.class);
	cdev_del(&g.cdev);
	unregister_chrdev_region(g.devt, 1);

	pr_info(DRV_NAME ": unloaded (reset cmd sent)\n");
}

module_init(ampcpu_init);
module_exit(ampcpu_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMP CPU7 loader/starter via PSCI CPU_ON + debugfs reset");
MODULE_AUTHOR("hongyang-rp + Copilot");
