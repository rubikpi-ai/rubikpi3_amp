// SPDX-License-Identifier: GPL-2.0
/*
 * ampcpu7: load and start a baremetal payload on CPU7 via PSCI CPU_ON.
 *
 * Interfaces:
 *  1) /dev/ampcpu7
 *     - write(): write payload bytes to entry_pa + file_offset
 *     - llseek(): set file offset
 *
 *  2) debugfs: /sys/kernel/debug/ampcpu7/
 *     - status (ro)
 *     - start  (wo)        : PSCI CPU_ON(mpidr, entry_pa)
 *     - flush  (wo)        : flush <size> bytes from entry_pa (decimal/hex)
 *     - mpidr (rw)         : default 0x700
 *     - entry_pa (rw)      : default 0xD0800000
 *     - max_load_size (rw) : default 10MB
 *
 * Notes:
 *  - CPU7 must be offline in Linux before start. This driver only checks.
 *  - Cache maintenance uses flush_cache_vmap + flush_icache_range for module portability.
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
#include <asm/smp.h>

#define DRV_NAME "ampcpu7"
#define DEV_NAME "ampcpu7"

#define PSCI_0_2_FN64_CPU_ON  0xC4000003ULL

struct ampcpu_ctx {
	struct mutex lock;

	int target_cpu;
	u64 mpidr;              /* 0x700 */
	phys_addr_t entry_pa;   /* 0xD0800000 */
	size_t max_load_size;   /* 10MB default */

	u64 bytes_loaded;       /* high-water mark */
	s64 last_psci_ret;

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
	.max_load_size = 10 * 1024 * 1024,
	.last_psci_ret = 0,
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

	/* flush this chunk so CPU7 sees it */
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
	/* .open/.release not required; omit to avoid simple_release mismatch */
};

/* -------------------- debugfs helpers -------------------- */

static ssize_t dbg_status_read(struct file *f, char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[256];
	int len;
	int cpu;

	mutex_lock(&g.lock);
	cpu = g.target_cpu;
	len = scnprintf(buf, sizeof(buf),
			"mpidr=0x%llx (cpu%d)\n"
			"entry_pa=0x%llx\n"
			"max_load_size=0x%zx\n"
			"bytes_loaded=0x%llx\n"
			"cpu_online=%d (must be 0 before start)\n"
			"last_psci_ret=%lld (0x%llx)\n",
			(unsigned long long)g.mpidr, cpu,
			(unsigned long long)g.entry_pa,
			g.max_load_size,
			(unsigned long long)g.bytes_loaded,
			cpu_possible(cpu) ? cpu_online(cpu) : -1,
			(long long)g.last_psci_ret, (unsigned long long)g.last_psci_ret);
	mutex_unlock(&g.lock);

	return simple_read_from_buffer(ubuf, cnt, ppos, buf, len);
}

static const struct file_operations dbg_status_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = dbg_status_read,
	.llseek = default_llseek,
};

static ssize_t dbg_start_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	int cpu, ret;
	u64 mpidr;
	phys_addr_t entry;
	u64 flush_sz;

	mutex_lock(&g.lock);
	mpidr = g.mpidr;
	entry = g.entry_pa;
	flush_sz = g.bytes_loaded ? g.bytes_loaded : 0x1000;
	mutex_unlock(&g.lock);

	cpu = g.target_cpu;
	if (!cpu_possible(cpu))
		return -EINVAL;
	if (cpu_online(cpu))
		return -EBUSY; /* you want offline */

	amp_flush_payload(entry, (size_t)flush_sz);

	ret = psci_cpu_on(mpidr, entry);

	mutex_lock(&g.lock);
	g.last_psci_ret = ret;
	mutex_unlock(&g.lock);

	pr_err(DRV_NAME ": PSCI CPU_ON mpidr=0x%llx entry=0x%llx ret=%d\n",
       (unsigned long long)mpidr, (unsigned long long)entry, ret);

	return ret ? ret : cnt;
}

static const struct file_operations dbg_start_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dbg_start_write,
	.llseek = default_llseek,
};

static ssize_t dbg_flush_write(struct file *f, const char __user *ubuf, size_t cnt, loff_t *ppos)
{
	char buf[64];
	u64 sz;
	phys_addr_t entry;

	if (cnt >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, cnt))
		return -EFAULT;
	buf[cnt] = '\0';

	if (kstrtoull(buf, 0, &sz))
		return -EINVAL;

	mutex_lock(&g.lock);
	entry = g.entry_pa;
	mutex_unlock(&g.lock);

	amp_flush_payload(entry, (size_t)min_t(u64, sz, (u64)g.max_load_size));
	return cnt;
}

static const struct file_operations dbg_flush_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = dbg_flush_write,
	.llseek = default_llseek,
};

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

	/* debugfs */
	g.dbg_dir = debugfs_create_dir(DRV_NAME, NULL);
	if (IS_ERR_OR_NULL(g.dbg_dir)) {
		ret = -ENOMEM;
		goto err_device_destroy;
	}

	debugfs_create_file("status", 0444, g.dbg_dir, NULL, &dbg_status_fops);
	debugfs_create_file("start", 0200, g.dbg_dir, NULL, &dbg_start_fops);
	debugfs_create_file("flush", 0200, g.dbg_dir, NULL, &dbg_flush_fops);
	debugfs_create_file("mpidr", 0644, g.dbg_dir, &g.mpidr, &dbg_u64_fops);
	debugfs_create_file("entry_pa", 0644, g.dbg_dir, (u64 *)&g.entry_pa, &dbg_u64_fops);
	debugfs_create_file("max_load_size", 0644, g.dbg_dir, &g.max_load_size, &dbg_size_fops);
	debugfs_create_file("target_cpu", 0644, g.dbg_dir, &g.target_cpu, &dbg_int_fops);

	pr_info(DRV_NAME ": /dev/%s created; debugfs: /sys/kernel/debug/%s/\n",
		DEV_NAME, DRV_NAME);
	pr_info(DRV_NAME ": mpidr=0x%llx entry_pa=0x%llx max_load_size=%zu\n",
		(unsigned long long)g.mpidr, (unsigned long long)g.entry_pa, g.max_load_size);

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
MODULE_DESCRIPTION("AMP CPU7 loader/starter via PSCI CPU_ON + debugfs");
MODULE_AUTHOR("hongyang-rp + Copilot");
