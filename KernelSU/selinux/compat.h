#ifndef __KSU_H_SELINUX_COMPAT
#define __KSU_H_SELINUX_COMPAT

#include <linux/cred.h>
#include <linux/fs.h>

#ifdef CONFIG_SECURITY_SELINUX

#include "objsec.h"

/*
 * upstream include/linux/security.h exposes selinux_cred() and
 * selinux_inode() to reach the LSM blobs. This tree omits both, though the
 * blob fields themselves are still there, so map them straight through.
 *
 * On this kernel the cred-side blob is struct task_security_struct; upstream
 * renamed it to cred_security_struct in 6.18, which is the branch that does
 * not apply here.
 */
static inline struct task_security_struct *selinux_cred(const struct cred *cred)
{
	return cred ? (struct task_security_struct *)cred->security : NULL;
}

static inline struct inode_security_struct *selinux_inode(struct inode *inode)
{
	return inode ? (struct inode_security_struct *)inode->i_security : NULL;
}

#endif /* CONFIG_SECURITY_SELINUX */

#endif /* __KSU_H_SELINUX_COMPAT */