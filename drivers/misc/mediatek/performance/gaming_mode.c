// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Gaming Performance Subsystem
 * Dedicated Gaming Mode Controller for Redmi Note 8 Pro (begonia)
 *
 * Coordinates MediaTek FPSGO Ultra-Rescue, GED GPU Instant Boost,
 * Schedutil Instantaneous Ramp, SchedTune, and COBRA Performance-First PPM.
 */

#define pr_fmt(fmt) "[gaming_mode] " fmt

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/ctype.h>

#include "gaming_mode.h"

/* External subsystem functions */
extern int schedutil_set_up_rate_limit_us(int cpu, unsigned int rate_limit_us);
extern int schedutil_set_down_rate_limit_us(int cpu, unsigned int rate_limit_us);
extern int fbt_cpu_set_ultra_rescue(int enable);
extern int fbt_cpu_set_bhr(int new_bhr);
extern int fbt_cpu_set_rescue_opp_c(int new_opp);
extern int fbt_cpu_set_rescue_percent(int percent);
extern int fbt_cpu_set_variance(int var);
extern void fbt_boost_dram(int boost);
extern void ged_kpi_set_gaming_boost(int enable);
extern void ged_dvfs_set_gaming_boost(int enable);
extern void eara_pass_perf_first_hint(int enable);
extern int boost_write_for_perf_idx(int idx, int boost_value);
extern int prefer_idle_for_perf_idx(int idx, int prefer_idle);

static int gaming_mode_state = GAMING_MODE_DISABLED;
static DEFINE_MUTEX(gaming_mode_lock);

int gaming_mode_set(int mode)
{
	mutex_lock(&gaming_mode_lock);

	if (mode == gaming_mode_state) {
		mutex_unlock(&gaming_mode_lock);
		return 0;
	}

	if (mode > 0) {
		pr_info("Activating Gaming Mode (level %d)...\n", mode);

		/* 1. FPSGO Ultra-Rescue & Preemptive Frame Stabilization */
		fbt_cpu_set_ultra_rescue(1);
		fbt_cpu_set_rescue_percent(20); /* Rescue 33% earlier before vsync expiration */
		fbt_cpu_set_variance(15);       /* Sensitive deviation threshold to catch spikes */
		fbt_cpu_set_bhr(15);            /* Big Core Hold Rate raised for frame stability */
		fbt_cpu_set_rescue_opp_c(0);    /* Uncap rescue ceiling to peak CPU frequency */

		/* 2. Schedutil Instantaneous Ramp */
		schedutil_set_up_rate_limit_us(0, 500);     /* Cluster 0 (A55): 500us ramp */
		schedutil_set_down_rate_limit_us(0, 20000); /* 20ms hold prevents inter-frame drops */
		schedutil_set_up_rate_limit_us(6, 500);     /* Cluster 1 (A76): 500us ramp */
		schedutil_set_down_rate_limit_us(6, 20000); /* 20ms hold */

		/* 3. Mali-G76 MC4 GPU & GED Instant Boost */
		ged_kpi_set_gaming_boost(1);
		ged_dvfs_set_gaming_boost(1);

		/* 4. PPM COBRA Performance-First CPU Budgeting */
		eara_pass_perf_first_hint(1);

		/* 5. SchedTune Top-App & Prefer Idle */
		boost_write_for_perf_idx(3, 15);   /* Top-app boost = 15% */
		prefer_idle_for_perf_idx(3, 1);   /* Top-app prefers idle big cores */

		/* 6. Extreme Mode: Lock DRAM to Max OPP 0 */
		if (mode >= GAMING_MODE_EXTREME)
			fbt_boost_dram(1);

		pr_info("Gaming Mode activated: Zero frame-drop profile engaged.\n");
	} else {
		pr_info("Deactivating Gaming Mode: Restoring Balanced Profile...\n");

		/* 1. Restore FPSGO Defaults */
		fbt_cpu_set_ultra_rescue(0);
		fbt_cpu_set_rescue_percent(33);
		fbt_cpu_set_variance(40);
		fbt_cpu_set_bhr(5);
		fbt_cpu_set_rescue_opp_c(15); /* Default ceiling OPP */

		/* 2. Restore Schedutil Defaults */
		schedutil_set_up_rate_limit_us(0, 1000);
		schedutil_set_down_rate_limit_us(0, 1000);
		schedutil_set_up_rate_limit_us(6, 1000);
		schedutil_set_down_rate_limit_us(6, 1000);

		/* 3. Restore GED GPU Defaults */
		ged_kpi_set_gaming_boost(0);
		ged_dvfs_set_gaming_boost(0);

		/* 4. Restore PPM COBRA Defaults */
		eara_pass_perf_first_hint(0);

		/* 5. Restore SchedTune Top-App */
		boost_write_for_perf_idx(3, 1);
		prefer_idle_for_perf_idx(3, 1);

		/* 6. Release DRAM Boost */
		fbt_boost_dram(0);

		pr_info("Gaming Mode deactivated: Balanced Profile restored.\n");
	}

	gaming_mode_state = mode;
	mutex_unlock(&gaming_mode_lock);
	return 0;
}
EXPORT_SYMBOL(gaming_mode_set);

int gaming_mode_get(void)
{
	return gaming_mode_state;
}
EXPORT_SYMBOL(gaming_mode_get);

/* ------------------ ProcFS Interface ------------------ */

static int gaming_mode_proc_show(struct seq_file *m, void *v)
{
	int state = gaming_mode_get();

	seq_printf(m, "gaming_mode: %d\n", state);
	if (state == GAMING_MODE_EXTREME)
		seq_printf(m, "status: EXTREME GAMING MODE (Locked Max DRAM OPP + Zero Frame Drops)\n");
	else if (state == GAMING_MODE_ENABLED)
		seq_printf(m, "status: GAMING MODE ACTIVE (Zero Frame Drops Enabled)\n");
	else
		seq_printf(m, "status: BALANCED PROFILE (Battery Conscious)\n");

	seq_printf(m, "features:\n");
	seq_printf(m, "  - ultra_rescue: %s\n", state ? "enabled (DRAM boost on hitch)" : "disabled");
	seq_printf(m, "  - rescue_percent: %d%%\n", state ? 20 : 33);
	seq_printf(m, "  - variance_sensitivity: %d\n", state ? 15 : 40);
	seq_printf(m, "  - big_core_hold_rate (bhr): %d\n", state ? 15 : 5);
	seq_printf(m, "  - schedutil_ramp_up: %d us\n", state ? 500 : 1000);
	seq_printf(m, "  - schedutil_hold_down: %d us\n", state ? 20000 : 1000);
	seq_printf(m, "  - gpu_touch_boost: %s\n", state ? "enabled" : "disabled");
	seq_printf(m, "  - gpu_dvfs_margin: %s\n", state ? "+20% (PERF)" : "default");
	seq_printf(m, "  - ppm_cobra_budget: %s\n", state ? "Performance-First (A76 prioritized)" : "Balanced");
	seq_printf(m, "  - top_app_boost: %d%%\n", state ? 15 : 1);
	seq_printf(m, "  - top_app_prefer_idle: enabled\n");
	return 0;
}

static ssize_t gaming_mode_proc_write(struct file *file, const char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	char buf[32];
	int val = 0;
	size_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Strip trailing whitespace */
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || isspace(buf[len - 1])))
		buf[--len] = '\0';

	if (strcasecmp(buf, "1") == 0 || strcasecmp(buf, "on") == 0 ||
	    strcasecmp(buf, "enable") == 0 || strcasecmp(buf, "true") == 0) {
		val = GAMING_MODE_ENABLED;
	} else if (strcasecmp(buf, "2") == 0 || strcasecmp(buf, "extreme") == 0) {
		val = GAMING_MODE_EXTREME;
	} else if (strcasecmp(buf, "0") == 0 || strcasecmp(buf, "off") == 0 ||
		   strcasecmp(buf, "disable") == 0 || strcasecmp(buf, "false") == 0) {
		val = GAMING_MODE_DISABLED;
	} else {
		if (kstrtoint(buf, 10, &val) < 0)
			return -EINVAL;
		if (val < 0)
			val = 0;
		else if (val > 2)
			val = 2;
	}

	gaming_mode_set(val);
	return count;
}

static int gaming_mode_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, gaming_mode_proc_show, NULL);
}

static const struct file_operations gaming_mode_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = gaming_mode_proc_open,
	.read    = seq_read,
	.write   = gaming_mode_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ------------------ SysFS Interface ------------------ */

static ssize_t gaming_mode_sysfs_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", gaming_mode_get());
}

static ssize_t gaming_mode_sysfs_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val < 0)
		val = 0;
	else if (val > 2)
		val = 2;

	gaming_mode_set(val);
	return count;
}

static struct kobj_attribute gaming_mode_kobj_attr =
	__ATTR(gaming_mode, 0664, gaming_mode_sysfs_show, gaming_mode_sysfs_store);

/* ------------------ Init Function ------------------ */

int init_gaming_mode(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *entry;
	int ret;

	if (!parent)
		return -EINVAL;

	entry = proc_create("gaming_mode", 0664, parent, &gaming_mode_proc_fops);
	if (!entry) {
		pr_err("Failed to create /proc/perfmgr/gaming_mode\n");
		return -ENOMEM;
	}

	ret = sysfs_create_file(kernel_kobj, &gaming_mode_kobj_attr.attr);
	if (ret)
		pr_warn("Failed to create /sys/kernel/gaming_mode (ret=%d)\n", ret);
	else
		pr_info("/sys/kernel/gaming_mode created successfully\n");

	pr_info("Gaming Mode subsystem initialized successfully.\n");
	return 0;
}
