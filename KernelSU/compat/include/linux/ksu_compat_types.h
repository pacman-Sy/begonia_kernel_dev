/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KernelSU compatibility types.
 *
 * This tree's headers predate a few typedefs/macros that upstream 4.14 defines
 * and that KernelSU relies on. The definitions below match upstream so types
 * and calling conventions line up with the rest of the kernel.
 *
 * Force-included via -include in KernelSU/Kbuild. Keep this header free of
 * anything that needs linux/uaccess.h: including it here would pull uaccess
 * declarations ahead of the kernel's own and break the build. Use
 * <linux/uaccess_nofault.h> for the usercopy helpers instead.
 */
#ifndef _KSU_COMPAT_TYPES_H
#define _KSU_COMPAT_TYPES_H

/*
 * upstream: include/uapi/asm-generic/poll.h
 *   typedef unsigned int __poll_t;
 *
 * Also the return type of file_operations->poll in this tree, which is
 * declared as plain "unsigned int".
 */
typedef unsigned int __poll_t;

/*
 * upstream 4.11+ include/linux/compiler_types.h
 *   #define fallthrough __attribute__((__fallthrough__))
 * Absent from this tree's compiler headers.
 */
#ifndef fallthrough
#define fallthrough __attribute__((__fallthrough__))
#endif

/*
 * upstream 4.11+ include/linux/task_work.h notify modes. This tree's
 * task_work_add() takes a plain bool "notify" instead, so map the one mode
 * KernelSU uses onto it.
 */
#ifndef TWA_RESUME
#define TWA_RESUME true
#endif

/*
 * upstream 5.10+ ktime_get_boottime_ts64(struct timespec64 *ts). This tree's
 * timekeeping.h only offers ktime_get_boottime(); expand at the call site so
 * the header can stay free of kernel includes. Requires <linux/timekeeping.h>.
 */
#ifndef ktime_get_boottime_ts64
#define ktime_get_boottime_ts64(ts)                                              \
	(*(ts) = ktime_to_timespec64(ktime_get_boottime()))
#endif

#endif /* _KSU_COMPAT_TYPES_H */