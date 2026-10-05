/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KernelSU compatibility shim.
 *
 * This tree keeps the MS_* mount flags and the MNT_* constants in
 * uapi/linux/fs.h; uapi/linux/mount.h was split out later and does not exist
 * here. Provide the expected path so sources that include it still resolve.
 */
#ifndef _KSU_COMPAT_UAPI_LINUX_MOUNT_H
#define _KSU_COMPAT_UAPI_LINUX_MOUNT_H

#include <uapi/linux/fs.h>

#endif /* _KSU_COMPAT_UAPI_LINUX_MOUNT_H */