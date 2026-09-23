#!/usr/bin/env python3
"""Backport fixes onto KernelSU-Next for 4.14 arm64 compatibility.

KSUN upstream targets GKI/5.x; begonia is 4.14 arm64. This script
patches the submodule checkout at build time so the pin stays pristine.
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)
KERNELSU = os.path.join(REPO_ROOT, "KernelSU", "kernel")

def apply_patch(path_rel, old_text, new_text):
    full_path = os.path.join(REPO_ROOT, path_rel)
    with open(full_path, "r") as f:
        content = f.read()
    if old_text == "":
        # just prepend
        if new_text not in content:
            with open(full_path, "w") as f:
                f.write(new_text + content)
            print("prepended", path_rel)
        else:
            print("already patched or upstream changed:", path_rel)
        return
    if old_text in content:
        with open(full_path, "w") as f:
            f.write(content.replace(old_text, new_text))
        print("patched", path_rel)
    else:
        print("already patched or upstream changed:", path_rel)

def main():
    # 1. syscall_fn_t: arm64 needs a real function-pointer typedef
    apply_patch(
        os.path.join(KERNELSU, "hook", "syscall_hook.h"),
        "#if defined(__x86_64__)\n"
        "typedef sys_call_ptr_t syscall_fn_t;\n"
        "#endif\n",
        "#if defined(__x86_64__)\n"
        "typedef sys_call_ptr_t syscall_fn_t;\n"
        "#elif defined(__aarch64__)\n"
        "typedef long (*syscall_fn_t)(const struct pt_regs *regs);\n"
        "#else\n"
        "typedef long (*syscall_fn_t)(const struct pt_regs *regs);\n"
        "#endif\n",
    )

    # 2. linux/pgtable.h (split out of linux/mm.h in 5.x, guarded)
    apply_patch(
        os.path.join(KERNELSU, "feature", "sucompat.c"),
        "#include <linux/pgtable.h>\n",
        "#include <linux/version.h>\n"
        "#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\n"
        "#include <linux/pgtable.h>\n"
        "#endif\n",
    )

    # 3. linux/uaccess.h needed by syscall_event_bridge.c on 4.14
    apply_patch(
        os.path.join(KERNELSU, "hook", "syscall_event_bridge.c"),
        "#include <linux/static_key.h>\n",
        "#include <linux/static_key.h>\n"
        "#include <linux/uaccess.h>\n",
    )

    # 4. patch_memory.c: three functions missing on 4.14
    apply_patch(
        os.path.join(KERNELSU, "hook", "arm64", "patch_memory.c"),
        "",
        "#include <linux/version.h>\n"
        "#include <linux/uaccess.h>\n"
        "#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)\n"
        "#include <asm/kprobes.h>\n"
        "#else\n"
        "static inline unsigned long ksu_pte_to_phys(pte_t pte)\n"
        "{\n"
        "    return __pa(pte_val(pte));\n"
        "}\n"
        "#define __pte_to_phys ksu_pte_to_phys\n"
        "#endif\n"
        "#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0)\n"
        "#define ksu_copy_to_kernel_nofault copy_to_kernel_nofault\n"
        "#else\n"
        "static inline long ksu_copy_to_kernel_nofault(void *to, const void *from, unsigned long n)\n"
        "{\n"
        "    return __copy_to_user_nofault(to, from, n) ? -EFAULT : 0;\n"
        "}\n"
        "#endif\n"
        "#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)\n"
        "#elif defined(__aarch64__)\n"
        "#define __flush_icache_range flush_icache_range\n"
        "#endif\n",
    )

if __name__ == "__main__":
    main()
