/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Pox Kernel Project - TXO R
 * Dedicated Pox Zero Frame-Drop Gaming Mode Controller for Redmi Note 8 Pro (begonia)
 */

#ifndef _GAMING_MODE_H_
#define _GAMING_MODE_H_

#include <linux/proc_fs.h>

#define GAMING_MODE_DISABLED 0
#define GAMING_MODE_ENABLED  1
#define GAMING_MODE_EXTREME  2

#define COLOR_MODE_STANDARD  0
#define COLOR_MODE_REFERENCE 1
#define COLOR_MODE_VIVID     2
#define COLOR_MODE_SLOG3     3

int gaming_mode_set(int mode);
int gaming_mode_get(void);
int color_mode_set(int mode);
int color_mode_get(void);
int camera_4k60_set(int force);
int camera_4k60_get(void);
int camera_slog3_set(int enable);
int camera_slog3_get(void);
void pox_camera_launch_boost(int enable);
int pox_gaming_mode_get(void);
int init_gaming_mode(struct proc_dir_entry *parent);

#endif /* _GAMING_MODE_H_ */
