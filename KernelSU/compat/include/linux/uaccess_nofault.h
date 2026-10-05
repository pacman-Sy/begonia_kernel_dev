/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux 4.14 compat for the fault-tolerant usercopy helpers:
 *   copy_from_user_nofault() / copy_to_user_nofault()   added upstream 4.12
 *   strncpy_from_user_nofault()                         added upstream 5.8
 *
 * Upstream variants fault the user page in and never return a short count.
 * This tree only has the plain helpers, which already validate the address and
 * return the number of bytes not copied, so map a short copy onto -EFAULT --
 * that is the contract every KernelSU caller relies on.
 *
 * Include this after <linux/uaccess.h>.
 */
#ifndef _KSU_COMPAT_UACCESS_NOFAULT_H
#define _KSU_COMPAT_UACCESS_NOFAULT_H

#include <linux/uaccess.h>
#include <asm/uaccess.h>

#if KSU_USERCOPY_NO_NOFAULT
static inline long copy_from_user_nofault(void *dst, const void __user *src,
					  size_t size)
{
	if (!size)
		return 0;
	return copy_from_user(dst, src, size) ? -EFAULT : 0;
}

static inline long copy_to_user_nofault(void __user *dst, const void *src,
					size_t size)
{
	if (!size)
		return 0;
	return copy_to_user(dst, src, size) ? -EFAULT : 0;
}
#endif /* KSU_USERCOPY_NO_NOFAULT */

/*
 * strncpy_from_user() here takes "long count", so use that and not size_t or
 * the definition conflicts with the one in asm/uaccess.h.
 */
#ifndef strncpy_from_user_nofault
static inline long strncpy_from_user_nofault(char *dst,
					      const char __user *src,
					      long count)
{
	return strncpy_from_user(dst, src, count);
}
#endif

#endif /* _KSU_COMPAT_UACCESS_NOFAULT_H */