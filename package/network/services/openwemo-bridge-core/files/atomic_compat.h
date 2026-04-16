#ifndef OPENWEMO_ATOMIC_COMPAT_H
#define OPENWEMO_ATOMIC_COMPAT_H

/*
 * GCC lowers 64-bit __sync_* atomics to helper symbols that are not available
 * on this MIPS target. The __atomic_* builtins use libatomic instead.
 */
#define __sync_fetch_and_add(ptr, value) __atomic_fetch_add((ptr), (value), __ATOMIC_SEQ_CST)

#endif
