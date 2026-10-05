/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux 4.14 compat: <linux/ioctl.h> is a 5.x wrapper; on 4.14 the ioctl
 * request encoding macros live in the uapi generic header.
 */
#ifndef _KSU_COMPAT_LINUX_IOCTL_H
#define _KSU_COMPAT_LINUX_IOCTL_H
#include <asm-generic/ioctl.h>
#endif
