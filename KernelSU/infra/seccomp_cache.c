#include <linux/seccomp.h>

#include "klog.h" // IWYU pragma: keep
#include "infra/seccomp_cache.h"

/*
 * The seccomp action cache these entry points manipulate is a 5.x-era feature
 * keyed on SECCOMP_ARCH_NATIVE_NR / SECCOMP_ARCH_COMPAT bitmasks. This kernel
 * has neither:
 *
 *   struct seccomp_filter {
 *       refcount_t usage;
 *       bool log;
 *       struct seccomp_filter *prev;
 *       struct bpf_prog *prog;
 *   };
 *
 * There is no cache to consult, and mirroring the upstream layout here would
 * read past the end of the kernel's object. Keep both entry points as no-ops
 * so the setuid/reboot hook in hook/setuid_hook.c still links.
 */
void ksu_seccomp_clear_cache(struct seccomp_filter *filter, int nr)
{
}

void ksu_seccomp_allow_cache(struct seccomp_filter *filter, int nr)
{
}