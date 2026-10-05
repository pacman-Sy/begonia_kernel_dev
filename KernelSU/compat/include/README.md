# KernelSU-Next compat shims for Linux 4.14

KernelSU-Next `dev` targets 5.x+ kernels. This tree is Android 10 / Linux
4.14.357, which does not have several headers that were split out upstream.
Each shim here forwards to the 4.14 equivalent so the module compiles.

Added to the include path via `KernelSU/Kbuild`.
