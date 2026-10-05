#ifndef __KSU_H_ARCH
#define __KSU_H_ARCH

#include <linux/version.h>

/*
 * CONFIG_ARCH_HAS_SYSCALL_WRAPPER landed in Linux 5.9. It is what provides
 * the __arm64_sys_* trampolines that take a struct pt_regs. Kernels older
 * than that (this tree is 4.14) call sys_* directly with real arguments, so
 * the pt_regs-based syscall table/interception machinery cannot be used.
 */
#if defined(CONFIG_ARCH_HAS_SYSCALL_WRAPPER)
#define KSU_HAVE_SYSCALL_WRAPPER 1
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
#define KSU_HAVE_SYSCALL_WRAPPER 1
#else
#define KSU_HAVE_SYSCALL_WRAPPER 0
#endif

/*
 * ksun's LSM list patching walks the security_hook_list hlist directly and
 * assumes the modern layout, where hlist_head embeds a struct list_head and
 * list_head grew first/pprev again. 4.14 still has
 * hlist_head { struct hlist_node *first; } and a two-pointer list_head, so
 * the head->first / list.pprev juggling cannot even compile there. Enable the
 * patch only where the modern layout is known to be present.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 18, 0)
#define KSU_HAVE_LSM_HOOK 1
#else
#define KSU_HAVE_LSM_HOOK 0
#endif

#if defined(__aarch64__)

#define __PT_PARM1_REG regs[0]
#define __PT_PARM2_REG regs[1]
#define __PT_PARM3_REG regs[2]
#define __PT_SYSCALL_PARM4_REG regs[3]
#define __PT_CCALL_PARM4_REG regs[3]
#define __PT_PARM5_REG regs[4]
#define __PT_PARM6_REG regs[5]
#define __PT_RET_REG regs[30]
#define __PT_FP_REG regs[29] /* Works only with CONFIG_FRAME_POINTER */
#define __PT_RC_REG regs[0]
#define __PT_SP_REG sp
#define __PT_IP_REG pc
#define __PT_ORIG_SYSCALL_REG regs[8]

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#define REBOOT_SYMBOL "__arm64_sys_reboot"
#define SYS_READ_SYMBOL "__arm64_sys_read"
#define SYS_EXECVE_SYMBOL "__arm64_sys_execve"
#define SYS_FSTAT_SYMBOL "__arm64_sys_newfstatat"
#else
// Pre-4.17 arm64 has no CONFIG_ARCH_HAS_SYSCALL_WRAPPER. SYSCALL_DEFINE emits
// the C function "sys_reboot", but the exported ELF/kallsyms symbol is the
// arm64 syscall-table name "SyS_reboot". Verified with llvm-nm on the built
// objects: SyS_reboot (kernel/reboot.o), SyS_execve (fs/exec.o), SyS_read
// (fs/read_write.o), SyS_newfstatat (fs/stat.o).
#define REBOOT_SYMBOL "SyS_reboot"
#define SYS_READ_SYMBOL "SyS_read"
#define SYS_EXECVE_SYMBOL "SyS_execve"
#define SYS_FSTAT_SYMBOL "SyS_newfstatat"
#endif

#elif defined(__x86_64__)

#define __PT_PARM1_REG di
#define __PT_PARM2_REG si
#define __PT_PARM3_REG dx
/* syscall uses r10 for PARM4 */
#define __PT_SYSCALL_PARM4_REG r10
#define __PT_CCALL_PARM4_REG cx
#define __PT_PARM5_REG r8
#define __PT_PARM6_REG r9
#define __PT_RET_REG sp
#define __PT_FP_REG bp
#define __PT_RC_REG ax
#define __PT_SP_REG sp
#define __PT_IP_REG ip
#define __PT_ORIG_SYSCALL_REG orig_ax
#define REBOOT_SYMBOL "__x64_sys_reboot"
#define SYS_READ_SYMBOL "__x64_sys_read"
#define SYS_EXECVE_SYMBOL "__x64_sys_execve"
#define SYS_FSTAT_SYMBOL "__x64_sys_newfstat"

#elif defined(__riscv)

#define __PT_PARM1_REG a0
#define __PT_SYSCALL_PARM1_REG orig_a0
#define __PT_PARM2_REG a1
#define __PT_PARM3_REG a2
#define __PT_SYSCALL_PARM4_REG a3
#define __PT_CCALL_PARM4_REG a3
#define __PT_PARM5_REG a4
#define __PT_PARM6_REG a5
#define __PT_RET_REG ra
#define __PT_FP_REG s0
#define __PT_RC_REG a0
#define __PT_SP_REG sp
#define __PT_IP_REG epc
#define __PT_ORIG_SYSCALL_REG a7

#define REBOOT_SYMBOL "__riscv_sys_reboot"
#define SYS_READ_SYMBOL "__riscv_sys_read"
#define SYS_EXECVE_SYMBOL "__riscv_sys_execve"
#define SYS_FSTAT_SYMBOL "__riscv_sys_newfstat"

#else
#error "Unsupported arch"
#endif

/* allow some architecutres to override `struct pt_regs` */
#ifndef __PT_REGS_CAST
#define __PT_REGS_CAST(x) (x)
#endif

#define PT_REGS_PARM1(x) (__PT_REGS_CAST(x)->__PT_PARM1_REG)
/* RISC-V saves syscall argument zero before using a0 as the return slot.
 * Kprobe C-call arguments (including PT_REAL_REGS) still use the live a0.
 */
#ifndef __PT_SYSCALL_PARM1_REG
#define __PT_SYSCALL_PARM1_REG __PT_PARM1_REG
#endif
#define PT_REGS_SYSCALL_PARM1(x) (__PT_REGS_CAST(x)->__PT_SYSCALL_PARM1_REG)
#define PT_REGS_PARM2(x) (__PT_REGS_CAST(x)->__PT_PARM2_REG)
#define PT_REGS_PARM3(x) (__PT_REGS_CAST(x)->__PT_PARM3_REG)
#define PT_REGS_SYSCALL_PARM4(x) (__PT_REGS_CAST(x)->__PT_SYSCALL_PARM4_REG)
#define PT_REGS_CCALL_PARM4(x) (__PT_REGS_CAST(x)->__PT_CCALL_PARM4_REG)
#define PT_REGS_PARM5(x) (__PT_REGS_CAST(x)->__PT_PARM5_REG)
#define PT_REGS_PARM6(x) (__PT_REGS_CAST(x)->__PT_PARM6_REG)
#define PT_REGS_RET(x) (__PT_REGS_CAST(x)->__PT_RET_REG)
#define PT_REGS_FP(x) (__PT_REGS_CAST(x)->__PT_FP_REG)
#define PT_REGS_RC(x) (__PT_REGS_CAST(x)->__PT_RC_REG)
#define PT_REGS_SP(x) (__PT_REGS_CAST(x)->__PT_SP_REG)
#define PT_REGS_IP(x) (__PT_REGS_CAST(x)->__PT_IP_REG)
#define PT_REGS_ORIG_SYSCALL(x) (__PT_REGS_CAST(x)->__PT_ORIG_SYSCALL_REG)

#define PT_REAL_REGS(regs) ((struct pt_regs *)PT_REGS_PARM1(regs))

#endif
