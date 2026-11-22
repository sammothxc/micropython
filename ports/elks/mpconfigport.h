// MicroPython configuration for ELKS (8086/8088), built with Open Watcom C
// in the large memory model.  The same port also builds natively with the
// host gcc (make HOST=1) to test the port layer without an 8086.

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

// Core feature set.  Kept small: the ELKS kernel loads at most MAX_SEGS (5)
// code/data segments per program, so code plus constant data has to stay
// under about 256 KB.
#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_READER_POSIX (1)
// A system call is expensive on an 8088, so read source files a block at a time.
#define MICROPY_READER_POSIX_BUF_SIZE (512)
// Run and import .mpy files precompiled on another machine with
//   mpy-cross -msmall-int-bits=30 file.py
// Loading one skips the parser and compiler, which are slow on an 8088.
#ifndef MICROPY_PERSISTENT_CODE_LOAD
#define MICROPY_PERSISTENT_CODE_LOAD (1)
#endif
#define MICROPY_ENABLE_SOURCE_LINE (1)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_ROM_TEXT_COMPRESSION (0)
#define MICROPY_KBD_EXCEPTION (1)
#define MICROPY_STACK_CHECK (1)
#define MICROPY_USE_INTERNAL_ERRNO (0)
#define MICROPY_CPYTHON_COMPAT (1)
#define MICROPY_ALLOC_PATH_MAX (128)
#define MICROPY_ALLOC_PARSE_CHUNK_INIT (16)

// Speed options that matter on an 8088: a cache of where names were last found
// in dicts (128 bytes), and small-int arithmetic done inline in the VM.
#define MICROPY_OPT_MAP_LOOKUP_CACHE (1)
#define MICROPY_OPT_VM_SMALL_INT_FAST_PATH (1)
#define MICROPY_OPT_VM_SELECTIVE_EXC_IP (1)
#define MICROPY_OPT_VM_SWITCH_MULTI_OPCODES (1)

#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#ifndef MICROPY_ELKS_FLOAT
#define MICROPY_ELKS_FLOAT (1)
#endif
#if MICROPY_ELKS_FLOAT
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#else
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_NONE)
#endif
#define MICROPY_PY_BUILTINS_COMPLEX (0)
#define MICROPY_PY_MATH (0)
#define MICROPY_PY_CMATH (0)

#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_PY_BUILTINS_INPUT (1)
#define MICROPY_PY_BUILTINS_HELP (1)
#define MICROPY_PY___FILE__ (1)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_IO (0)
#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_SYS_ARGV (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_PY_SYS_PLATFORM "elks"
#define MICROPY_PY_SYS_STDFILES (0)
#define MICROPY_PY_ASYNCIO (0)

// time module: time(), sleep*() and ticks_*(), see modtime.c.
#define MICROPY_PY_TIME (1)
#define MICROPY_PY_TIME_INCLUDEFILE "ports/elks/modtime.c"

#define MICROPY_HW_BOARD_NAME "ELKS"
#define MICROPY_MPHALPORT_H "mphalport.h"
#define MP_STATE_PORT MP_STATE_VM

// Default search path for imports: the current directory, then a system
// library directory.
#define MICROPY_ELKS_LIB_DIR "/lib/micropython"

#if defined(__WATCOMC__)

// 16-bit large model: int and size_t are 16 bits, data pointers are 32-bit
// far pointers (segment:offset), long is 32 bits.  An object word is a long
// so that it can hold a far pointer.
#define MICROPY_HW_MCU_NAME "8086"
#define MICROPY_OBJ_REPR (MICROPY_OBJ_REPR_B)
#define MICROPY_NLR_SETJMP (1)
#define MICROPY_NO_ALLOCA (1)
#define MICROPY_GC_STACK_ENTRY_TYPE unsigned short
#define MPZ_DIG_SIZE (8)
#define MICROPY_QSTR_BYTES_IN_HASH (1)
#define MICROPY_QSTR_BYTES_IN_LEN (1)
#define MICROPY_PY_MATH_CONSTANTS (0)
#define MP_ENDIANNESS_LITTLE (1)
#define MP_SSIZE_MAX (32767)
#define UINT_FMT "%lu"
#define INT_FMT "%ld"
typedef long mp_int_t;
typedef unsigned long mp_uint_t;
typedef long mp_off_t;

#define NORETURN __declspec(aborts)
#define MP_WEAK
#define MP_NOINLINE
#define MP_ALWAYSINLINE
#define MP_LIKELY(x) (x)
#define MP_UNLIKELY(x) (x)
#define MP_UNREACHABLE for (;;) {}

// C stack size given to the linker, and the Python recursion limit derived
// from it (leaves room for the GC and for C library calls).
#define MICROPY_ELKS_STACK_LIMIT (12000)

#else // host build

#include <alloca.h>
#define MICROPY_HW_MCU_NAME "host"
typedef intptr_t mp_int_t;
typedef uintptr_t mp_uint_t;
typedef long mp_off_t;
#define MICROPY_ELKS_STACK_LIMIT (40000 * (sizeof(void *) / 4))

#endif
