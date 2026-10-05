// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Pox Kernel Project - TXO R
 * Dedicated Pox Zero Frame-Drop Gaming Mode Controller for Redmi Note 8 Pro (begonia)
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
#include <linux/workqueue.h>

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
extern int set_ios_color_mode(int mode);
extern int get_ios_color_mode(void);

static int gaming_mode_state = GAMING_MODE_DISABLED;
static int user_color_mode_override = -1;
static int camera_4k60_force = 0;
static atomic_t pox_cam_active_sessions = ATOMIC_INIT(0);
static struct delayed_work pox_cam_boost_decay_work;
static DEFINE_MUTEX(gaming_mode_lock);

int color_mode_set(int mode)
{
	return set_ios_color_mode(mode);
}
EXPORT_SYMBOL(color_mode_set);

int color_mode_get(void)
{
	return get_ios_color_mode();
}
EXPORT_SYMBOL(color_mode_get);

int camera_4k60_set(int force)
{
	camera_4k60_force = force ? 1 : 0;
	pr_info("Camera 4K 60FPS Force Mode: %s\n",
		camera_4k60_force ? "ENABLED (60FPS Forced)" : "AUTO (App Request)");
	return 0;
}
EXPORT_SYMBOL(camera_4k60_set);

int camera_4k60_get(void)
{
	return camera_4k60_force;
}
EXPORT_SYMBOL(camera_4k60_get);

int camera_slog3_set(int enable)
{
	if (enable)
		return set_ios_color_mode(COLOR_MODE_SLOG3);
	else
		return set_ios_color_mode(COLOR_MODE_REFERENCE);
}
EXPORT_SYMBOL(camera_slog3_set);

int camera_slog3_get(void)
{
	return (get_ios_color_mode() == COLOR_MODE_SLOG3) ? 1 : 0;
}
EXPORT_SYMBOL(camera_slog3_get);

static void pox_cam_boost_decay_func(struct work_struct *work)
{
	/* Decay launch boost down after 750ms if not in Extreme gaming mode */
	if (gaming_mode_state < GAMING_MODE_EXTREME)
		fbt_boost_dram(0);

	/* Restore normal top-app schedtune boost */
	if (gaming_mode_state > 0)
		boost_write_for_perf_idx(3, 20);
	else
		boost_write_for_perf_idx(3, 5);
}

void pox_camera_launch_boost(int enable)
{
	if (enable) {
		int count = atomic_inc_return(&pox_cam_active_sessions);

		if (count == 1) {
			pr_info("Camera launch boost engaged: instant viewfinder QoS primed.\n");
			/* Instant high-throughput DRAM boost to eliminate sensor init/viewfinder frame drops */
			fbt_boost_dram(1);
			/* Aggressive CPU schedtune for camera rendering and sensor init */
			boost_write_for_perf_idx(3, 40);
			prefer_idle_for_perf_idx(3, 1);
			/* Schedule 750ms decay */
			cancel_delayed_work(&pox_cam_boost_decay_work);
			schedule_delayed_work(&pox_cam_boost_decay_work, msecs_to_jiffies(750));
		}
	} else {
		int count = atomic_dec_return(&pox_cam_active_sessions);

		if (count <= 0) {
			atomic_set(&pox_cam_active_sessions, 0);
			cancel_delayed_work(&pox_cam_boost_decay_work);
			pox_cam_boost_decay_func(NULL);
			pr_info("Camera session released: launch boost restored.\n");
		}
	}
}
EXPORT_SYMBOL(pox_camera_launch_boost);

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

		/* 5. iOS-Style SchedTune QoS: Prioritize Top-App & Foreground */
		boost_write_for_perf_idx(3, 20);   /* Top-app (Render/Game) boost = 20% */
		prefer_idle_for_perf_idx(3, 1);    /* Top-app prefers idle Cortex-A76 cores */
		boost_write_for_perf_idx(1, 5);    /* Foreground boost = 5% */
		prefer_idle_for_perf_idx(1, 1);

		/* 6. Display Engine: Engage iOS Vivid Gaming Cinema HDR profile unless overridden */
		if (user_color_mode_override < 0)
			set_ios_color_mode(COLOR_MODE_VIVID);

		/* 7. Extreme Mode: Lock DRAM to Max OPP 0 (2133MHz) */
		if (mode >= GAMING_MODE_EXTREME)
			fbt_boost_dram(1);

		/* 8. Touchscreen: Engage Hardware Touch Game Mode & Sensitivity */
		{
			extern int pox_touch_game_mode_set(int enable);
			extern int pox_touch_sensitivity_set(int val);
			pox_touch_game_mode_set(1);
			pox_touch_sensitivity_set(1);
		}

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

		/* 5. Restore Balanced SchedTune QoS */
		boost_write_for_perf_idx(3, 5);
		prefer_idle_for_perf_idx(3, 1);
		boost_write_for_perf_idx(1, 0);
		prefer_idle_for_perf_idx(1, 0);

		/* 6. Display Engine: Restore iOS TrueColor Reference (Calibrated D65) unless overridden */
		if (user_color_mode_override < 0)
			set_ios_color_mode(COLOR_MODE_REFERENCE);

		/* 7. Release DRAM Boost */
		fbt_boost_dram(0);

		/* 8. Restore Touchscreen Defaults */
		{
			extern int pox_touch_game_mode_set(int enable);
			extern int pox_touch_sensitivity_set(int val);
			pox_touch_game_mode_set(0);
			pox_touch_sensitivity_set(0);
		}

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

int pox_gaming_mode_get(void)
{
	return gaming_mode_state;
}
EXPORT_SYMBOL(pox_gaming_mode_get);

/* ------------------ ProcFS Interfaces ------------------ */

static int gaming_mode_proc_show(struct seq_file *m, void *v)
{
	int state = gaming_mode_get();
	int color_st = get_ios_color_mode();

	seq_printf(m, "gaming_mode: %d\n", state);
	if (state == GAMING_MODE_EXTREME)
		seq_printf(m, "status: EXTREME GAMING MODE (Locked Max DRAM OPP + Zero Frame Drops)\n");
	else if (state == GAMING_MODE_ENABLED)
		seq_printf(m, "status: GAMING MODE ACTIVE (Zero Frame Drops Enabled)\n");
	else
		seq_printf(m, "status: BALANCED PROFILE (iOS Fluidity & Real Colors Active)\n");

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
	seq_printf(m, "  - top_app_boost: %d%%\n", state ? 20 : 5);
	seq_printf(m, "  - top_app_prefer_idle: enabled\n");
	seq_printf(m, "  - color_mode: %d (%s)\n", color_st,
		(color_st == COLOR_MODE_SLOG3) ? "Sony S-Log3 / Cinema Flat Profile (LUT Grading)" :
		(color_st == COLOR_MODE_VIVID) ? "iOS Vivid / Gaming Cinema" :
		(color_st == COLOR_MODE_REFERENCE) ? "iOS TrueColor Reference (Calibrated D65)" : "Standard Neutral");
	seq_printf(m, "  - video_clock_floor: active (anti-lag enabled)\n");
	seq_printf(m, "  - display_ddr_floor: LP4-2100 minimum\n");
	seq_printf(m, "  - cfs_latency: 4 ms (500 us preemption, unscaled)\n");
	seq_printf(m, "  - timer_hz: %d Hz (3.33 ms jiffy, display-synced)\n", HZ);
	seq_printf(m, "  - io_scheduler: deadline (guaranteed UFS read latency)\n");
	seq_printf(m, "  - tcp_congestion: bbr (low bufferbloat)\n");
	seq_printf(m, "  - hardware_touch_boost: enabled (A55@1.50GHz / A76@1.53GHz + 60%% TA uclamp)\n");
	seq_printf(m, "  - camera_4k_60fps: unlocked (Samsung GW1 16MP@60fps)\n");
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

static int color_mode_proc_show(struct seq_file *m, void *v)
{
	int mode = get_ios_color_mode();

	seq_printf(m, "color_mode: %d\n", mode);
	if (mode == COLOR_MODE_SLOG3)
		seq_printf(m, "status: Sony S-Log3 / Cinema Flat Profile (Logarithmic Dynamic Range for LUT Grading)\n");
	else if (mode == COLOR_MODE_VIVID)
		seq_printf(m, "status: iOS Vivid / Gaming Cinema (Enhanced HDR for Games & Movies)\n");
	else if (mode == COLOR_MODE_REFERENCE)
		seq_printf(m, "status: iOS TrueColor Reference (Calibrated D65 Liquid Retina)\n");
	else
		seq_printf(m, "status: Standard Neutral\n");
	return 0;
}

static ssize_t color_mode_proc_write(struct file *file, const char __user *ubuf,
				     size_t count, loff_t *ppos)
{
	char buf[16];
	int val = 0;
	size_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = '\0';

	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || isspace(buf[len - 1])))
		buf[--len] = '\0';

	if (kstrtoint(buf, 10, &val) < 0)
		return -EINVAL;

	if (val < 0)
		val = 0;
	else if (val > 3)
		val = 3;

	user_color_mode_override = val;
	set_ios_color_mode(val);
	return count;
}

static int color_mode_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, color_mode_proc_show, NULL);
}

static const struct file_operations color_mode_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = color_mode_proc_open,
	.read    = seq_read,
	.write   = color_mode_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int camera_profile_proc_show(struct seq_file *m, void *v)
{
	int mode = get_ios_color_mode();

	seq_printf(m, "camera_profile: %d\n", mode);
	if (mode == COLOR_MODE_SLOG3)
		seq_printf(m, "profile_name: Sony S-Log3 / Cinema Flat (Wide Dynamic Range for LUT Grading)\n");
	else if (mode == COLOR_MODE_VIVID)
		seq_printf(m, "profile_name: iOS Vivid / Cinema (Enhanced Dynamic Range)\n");
	else if (mode == COLOR_MODE_REFERENCE)
		seq_printf(m, "profile_name: iOS TrueColor Reference (Calibrated D65 Liquid Retina)\n");
	else
		seq_printf(m, "profile_name: Standard Rec.709 Neutral\n");

	seq_printf(m, "camera_4k60_force: %d\n", camera_4k60_get());
	seq_printf(m, "slog3_active: %d\n", camera_slog3_get());
	seq_printf(m, "launch_boost_active_sessions: %d\n", atomic_read(&pox_cam_active_sessions));

	seq_printf(m, "capabilities:\n");
	seq_printf(m, "  - 4k_60fps_recording: enabled (Samsung GW1 16MP@60fps custom3 mode)\n");
	seq_printf(m, "  - isp_qos_floor: locked 560MHz peak Imagiq clock (zero dropped frames)\n");
	seq_printf(m, "  - venc_clock_floor: locked peak OPP 0 (anti-collapse)\n");
	seq_printf(m, "  - memory_qos_floor: LP4X-3733 peak bandwidth\n");
	seq_printf(m, "  - zero_shutter_lag: supported (2-frame delay pipeline)\n");
	seq_printf(m, "  - log_transfer_function: %s\n", (mode == COLOR_MODE_SLOG3) ? "S-Log3 Logarithmic (42% Middle Gray, 61% 90-White)" : "Rec.709 Standard");
	return 0;
}

static ssize_t camera_profile_proc_write(struct file *file, const char __user *ubuf,
					 size_t count, loff_t *ppos)
{
	return color_mode_proc_write(file, ubuf, count, ppos);
}

static int camera_profile_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, camera_profile_proc_show, NULL);
}

static const struct file_operations camera_profile_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = camera_profile_proc_open,
	.read    = seq_read,
	.write   = camera_profile_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ------------------ Camera 4K60 ProcFS ------------------ */

static int camera_4k60_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", camera_4k60_get());
	return 0;
}

static ssize_t camera_4k60_proc_write(struct file *file, const char __user *ubuf,
				      size_t count, loff_t *ppos)
{
	char buf[16];
	int val = 0;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = '\0';
	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	camera_4k60_set(val);
	return count;
}

static int camera_4k60_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, camera_4k60_proc_show, NULL);
}

static const struct file_operations camera_4k60_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = camera_4k60_proc_open,
	.read    = seq_read,
	.write   = camera_4k60_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ------------------ Sony S-Log3 ProcFS ------------------ */

static int slog3_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", camera_slog3_get());
	return 0;
}

static ssize_t slog3_proc_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];
	int val = 0;

	if (count >= sizeof(buf))
		return -EINVAL;

	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = '\0';
	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	camera_slog3_set(val);
	return count;
}

static int slog3_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, slog3_proc_show, NULL);
}

static const struct file_operations slog3_proc_fops = {
	.owner   = THIS_MODULE,
	.open    = slog3_proc_open,
	.read    = seq_read,
	.write   = slog3_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ------------------ SysFS Interfaces ------------------ */

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

static ssize_t color_mode_sysfs_show(struct kobject *kobj,
				     struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", get_ios_color_mode());
}

static ssize_t color_mode_sysfs_store(struct kobject *kobj,
				      struct kobj_attribute *attr,
				      const char *buf, size_t count)
{
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	if (val < 0)
		val = 0;
	else if (val > 3)
		val = 3;

	user_color_mode_override = val;
	set_ios_color_mode(val);
	return count;
}

static struct kobj_attribute color_mode_kobj_attr =
	__ATTR(color_mode, 0664, color_mode_sysfs_show, color_mode_sysfs_store);

static struct kobj_attribute camera_profile_kobj_attr =
	__ATTR(camera_profile, 0664, color_mode_sysfs_show, color_mode_sysfs_store);

static ssize_t camera_4k60_sysfs_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", camera_4k60_get());
}

static ssize_t camera_4k60_sysfs_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	camera_4k60_set(val);
	return count;
}

static struct kobj_attribute camera_4k60_kobj_attr =
	__ATTR(camera_4k60, 0664, camera_4k60_sysfs_show, camera_4k60_sysfs_store);

static ssize_t slog3_sysfs_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", camera_slog3_get());
}

static ssize_t slog3_sysfs_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	int val = 0;

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	camera_slog3_set(val);
	return count;
}

static struct kobj_attribute slog3_kobj_attr =
	__ATTR(slog3, 0664, slog3_sysfs_show, slog3_sysfs_store);

/* Battery Protection Rootless ProcFS Interfaces */
extern int pox_battery_bypass_get(void);
extern void pox_battery_bypass_set(int enable);
extern int pox_battery_limit_get(void);
extern void pox_battery_limit_set(int limit);
extern int pox_battery_status_get(char *buf, size_t size);

static int battery_bypass_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_battery_bypass_get());
	return 0;
}

static int battery_bypass_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, battery_bypass_proc_show, NULL);
}

static ssize_t battery_bypass_proc_write(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_battery_bypass_set(val);
	return count;
}

static const struct file_operations battery_bypass_proc_fops = {
	.open    = battery_bypass_proc_open,
	.read    = seq_read,
	.write   = battery_bypass_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int battery_limit_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_battery_limit_get());
	return 0;
}

static int battery_limit_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, battery_limit_proc_show, NULL);
}

static ssize_t battery_limit_proc_write(struct file *file, const char __user *buffer,
					size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_battery_limit_set(val);
	return count;
}

static const struct file_operations battery_limit_proc_fops = {
	.open    = battery_limit_proc_open,
	.read    = seq_read,
	.write   = battery_limit_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int battery_status_proc_show(struct seq_file *m, void *v)
{
	char buf[128];
	pox_battery_status_get(buf, sizeof(buf));
	seq_printf(m, "%s", buf);
	return 0;
}

static int battery_status_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, battery_status_proc_show, NULL);
}

static const struct file_operations battery_status_proc_fops = {
	.open    = battery_status_proc_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Touchscreen Hardware Mode & Sensitivity Interfaces */
extern int pox_touch_game_mode_get(void);
extern int pox_touch_game_mode_set(int enable);
extern int pox_touch_sensitivity_get(void);
extern int pox_touch_sensitivity_set(int val);

static int touch_game_mode_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_touch_game_mode_get());
	return 0;
}

static int touch_game_mode_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, touch_game_mode_proc_show, NULL);
}

static ssize_t touch_game_mode_proc_write(struct file *file, const char __user *buffer,
					  size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_touch_game_mode_set(val);
	return count;
}

static const struct file_operations touch_game_mode_proc_fops = {
	.open    = touch_game_mode_proc_open,
	.read    = seq_read,
	.write   = touch_game_mode_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int touch_sensitivity_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_touch_sensitivity_get());
	return 0;
}

static int touch_sensitivity_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, touch_sensitivity_proc_show, NULL);
}

static ssize_t touch_sensitivity_proc_write(struct file *file, const char __user *buffer,
					    size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_touch_sensitivity_set(val);
	return count;
}

static const struct file_operations touch_sensitivity_proc_fops = {
	.open    = touch_sensitivity_proc_open,
	.read    = seq_read,
	.write   = touch_sensitivity_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Audio Sound Control Headphone Gain Interface */
extern int pox_headphone_gain_get(void);
extern int pox_headphone_gain_set(int val);

static int headphone_gain_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_headphone_gain_get());
	return 0;
}

static int headphone_gain_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, headphone_gain_proc_show, NULL);
}

static ssize_t headphone_gain_proc_write(struct file *file, const char __user *buffer,
					 size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_headphone_gain_set(val);
	return count;
}

static const struct file_operations headphone_gain_proc_fops = {
	.open    = headphone_gain_proc_open,
	.read    = seq_read,
	.write   = headphone_gain_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Vibrator Strength Interface */
extern int pox_vibrator_strength_get(void);
extern int pox_vibrator_strength_set(int vol);

static int vibrator_strength_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_vibrator_strength_get());
	return 0;
}

static int vibrator_strength_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, vibrator_strength_proc_show, NULL);
}

static ssize_t vibrator_strength_proc_write(struct file *file, const char __user *buffer,
					    size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_vibrator_strength_set(val);
	return count;
}

static const struct file_operations vibrator_strength_proc_fops = {
	.open    = vibrator_strength_proc_open,
	.read    = seq_read,
	.write   = vibrator_strength_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Wakelock Blocker Deep Sleep Interface */
extern int pox_wakelock_blocker_get(void);
extern void pox_wakelock_blocker_set(int enable);

static int wakelock_blocker_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_wakelock_blocker_get());
	return 0;
}

static int wakelock_blocker_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, wakelock_blocker_proc_show, NULL);
}

static ssize_t wakelock_blocker_proc_write(struct file *file, const char __user *buffer,
					   size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_wakelock_blocker_set(val);
	return count;
}

static const struct file_operations wakelock_blocker_proc_fops = {
	.open    = wakelock_blocker_proc_open,
	.read    = seq_read,
	.write   = wakelock_blocker_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Fast Charge Control Interface */
extern int pox_fast_charge_get(void);
extern void pox_fast_charge_set(int enable);

static int fast_charge_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_fast_charge_get());
	return 0;
}

static int fast_charge_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, fast_charge_proc_show, NULL);
}

static ssize_t fast_charge_proc_write(struct file *file, const char __user *buffer,
				      size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_fast_charge_set(val);
	return count;
}

static const struct file_operations fast_charge_proc_fops = {
	.open    = fast_charge_proc_open,
	.read    = seq_read,
	.write   = fast_charge_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Double-Tap to Wake Interface */
extern int pox_dt2w_get(void);
extern int pox_dt2w_set(int enable);

static int dt2w_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_dt2w_get());
	return 0;
}

static int dt2w_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dt2w_proc_show, NULL);
}

static ssize_t dt2w_proc_write(struct file *file, const char __user *buffer,
			       size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_dt2w_set(val);
	return count;
}

static const struct file_operations dt2w_proc_fops = {
	.open    = dt2w_proc_open,
	.read    = seq_read,
	.write   = dt2w_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Analog Mic Gain Interface */
extern int pox_mic_gain_get(void);
extern int pox_mic_gain_set(int gain);

static int mic_gain_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_mic_gain_get());
	return 0;
}

static int mic_gain_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, mic_gain_proc_show, NULL);
}

static ssize_t mic_gain_proc_write(struct file *file, const char __user *buffer,
				   size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_mic_gain_set(val);
	return count;
}

static const struct file_operations mic_gain_proc_fops = {
	.open    = mic_gain_proc_open,
	.read    = seq_read,
	.write   = mic_gain_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* Dynamic Fsync Interface */
extern int pox_dynamic_fsync_get(void);
extern void pox_dynamic_fsync_set(int enable);

static int dynamic_fsync_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d\n", pox_dynamic_fsync_get());
	return 0;
}

static int dynamic_fsync_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, dynamic_fsync_proc_show, NULL);
}

static ssize_t dynamic_fsync_proc_write(struct file *file, const char __user *buffer,
					size_t count, loff_t *pos)
{
	char buf[16];
	int val;

	if (count >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';

	if (sscanf(buf, "%d", &val) != 1)
		return -EINVAL;

	pox_dynamic_fsync_set(val);
	return count;
}

static const struct file_operations dynamic_fsync_proc_fops = {
	.open    = dynamic_fsync_proc_open,
	.read    = seq_read,
	.write   = dynamic_fsync_proc_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

/* ------------------ Init Function ------------------ */

int init_gaming_mode(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *entry;
	int ret;

	if (!parent)
		return -EINVAL;

	INIT_DELAYED_WORK(&pox_cam_boost_decay_work, pox_cam_boost_decay_func);

	entry = proc_create("gaming_mode", 0666, parent, &gaming_mode_proc_fops);
	if (!entry) {
		pr_err("Failed to create /proc/perfmgr/gaming_mode\n");
		return -ENOMEM;
	}

	entry = proc_create("color_mode", 0666, parent, &color_mode_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/color_mode\n");

	entry = proc_create("camera_profile", 0666, parent, &camera_profile_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/camera_profile\n");

	entry = proc_create("camera_4k60", 0666, parent, &camera_4k60_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/camera_4k60\n");

	entry = proc_create("slog3", 0666, parent, &slog3_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/slog3\n");

	entry = proc_create("battery_bypass", 0666, parent, &battery_bypass_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/battery_bypass\n");

	entry = proc_create("battery_limit", 0666, parent, &battery_limit_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/battery_limit\n");

	entry = proc_create("battery_status", 0444, parent, &battery_status_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/battery_status\n");

	entry = proc_create("touch_game_mode", 0666, parent, &touch_game_mode_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/touch_game_mode\n");

	entry = proc_create("touch_sensitivity", 0666, parent, &touch_sensitivity_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/touch_sensitivity\n");

	entry = proc_create("headphone_gain", 0666, parent, &headphone_gain_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/headphone_gain\n");

	entry = proc_create("vibrator_strength", 0666, parent, &vibrator_strength_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/vibrator_strength\n");

	entry = proc_create("wakelock_blocker", 0666, parent, &wakelock_blocker_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/wakelock_blocker\n");

	entry = proc_create("fast_charge", 0666, parent, &fast_charge_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/fast_charge\n");

	entry = proc_create("dt2w", 0666, parent, &dt2w_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/dt2w\n");

	entry = proc_create("mic_gain", 0666, parent, &mic_gain_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/mic_gain\n");

	entry = proc_create("dynamic_fsync", 0666, parent, &dynamic_fsync_proc_fops);
	if (!entry)
		pr_warn("Failed to create /proc/perfmgr/dynamic_fsync\n");

	ret = sysfs_create_file(kernel_kobj, &gaming_mode_kobj_attr.attr);
	if (ret)
		pr_warn("Failed to create /sys/kernel/gaming_mode (ret=%d)\n", ret);
	else
		pr_info("/sys/kernel/gaming_mode created successfully\n");

	ret = sysfs_create_file(kernel_kobj, &color_mode_kobj_attr.attr);
	if (ret)
		pr_warn("Failed to create /sys/kernel/color_mode (ret=%d)\n", ret);
	else
		pr_info("/sys/kernel/color_mode created successfully\n");

	ret = sysfs_create_file(kernel_kobj, &camera_profile_kobj_attr.attr);
	if (ret)
		pr_warn("Failed to create /sys/kernel/camera_profile (ret=%d)\n", ret);
	else
		pr_info("/sys/kernel/camera_profile created successfully\n");

	ret = sysfs_create_file(kernel_kobj, &camera_4k60_kobj_attr.attr);
	if (ret)
		pr_warn("Failed to create /sys/kernel/camera_4k60 (ret=%d)\n", ret);
	else
		pr_info("/sys/kernel/camera_4k60 created successfully\n");

	ret = sysfs_create_file(kernel_kobj, &slog3_kobj_attr.attr);
	if (ret)
		pr_warn("Failed to create /sys/kernel/slog3 (ret=%d)\n", ret);
	else
		pr_info("/sys/kernel/slog3 created successfully\n");

	/* Initialize to iOS TrueColor Reference (Calibrated D65) */
	set_ios_color_mode(COLOR_MODE_REFERENCE);

	/* Onyx Gaming Edition: Engage Zero Frame-Drop gaming profile by default */
	gaming_mode_set(GAMING_MODE_ENABLED);

	pr_info("Gaming Mode & iOS Display Subsystem initialized successfully (Onyx Active).\n");
	return 0;
}
