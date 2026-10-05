/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux 4.14 compat: strncpy_from_user_nofault() was added in 5.8. The
 * definition now lives in linux/uaccess_nofault.h, which is what this header
 * forwards to.
 */
#ifndef _KSU_COMPAT_STRNCPY_FROM_USER_NOFAULT_H
#define _KSU_COMPAT_STRNCPY_FROM_USER_NOFAULT_H

#include <linux/uaccess_nofault.h>

#endif /* _KSU_COMPAT_STRNCPY_FROM_USER_NOFAULT_H */