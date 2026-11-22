// MicroPython for ELKS: program entry, heap setup and script execution.
//
//   micropython              interactive REPL (Ctrl-D exits)
//   micropython script.py    run a file
//   micropython -c "code"    run a string
//
// The Python heap is one block of far memory taken from the kernel, so it
// does not use up the program's 64 KB data segment.  Its size is chosen
// automatically, or set in bytes with the MPYHEAP environment variable.

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/persistentcode.h"
#include "py/emitglue.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/objlist.h"
#include "py/repl.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"

#if defined(__WATCOMC__)
#include <malloc.h>
// A single far segment is addressed with 16-bit offsets, so the heap must
// stay below 64 KB.
#define HEAP_MAX (60000U)
#define HEAP_MIN (8000U)
#define HEAP_STEP (4000U)
static void *heap_alloc(unsigned size) {
    void __far *p = fmemalloc((unsigned long)size);
    if (p == (void __far *)0 || p == (void __far *)-1L) {
        return NULL;
    }
    return (void *)p;
}
#else
#define HEAP_MAX (60000U * (unsigned)(sizeof(void *) / 2))
#define HEAP_MIN (8000U)
#define HEAP_STEP (4000U)
static void *heap_alloc(unsigned size) {
    return malloc(size);
}
#endif

static char *stack_top;

static NORETURN void die(const char *msg) {
    mp_hal_stdio_mode_orig();
    write(2, msg, strlen(msg));
    write(2, "\n", 1);
    exit(1);
}

// Allocate the Python heap: the size from MPYHEAP if set, otherwise the
// largest size that the kernel can provide.
static void heap_init(void) {
    const char *setting = getenv("MPYHEAP");
    unsigned size = HEAP_MAX;
    char *heap = NULL;
    if (setting != NULL && *setting != '\0') {
        unsigned long want = strtoul(setting, NULL, 10);
        if (want < HEAP_MIN || want > HEAP_MAX) {
            die("micropython: MPYHEAP out of range");
        }
        size = (unsigned)want;
        heap = heap_alloc(size);
    } else {
        for (;;) {
            heap = heap_alloc(size);
            if (heap != NULL || size < HEAP_MIN + HEAP_STEP) {
                break;
            }
            size -= HEAP_STEP;
        }
    }
    if (heap == NULL) {
        die("micropython: not enough memory for the Python heap");
    }
    gc_init(heap, heap + size);
}

// Compile and run a file or a string.  Returns the process exit status.
static int execute(const char *src, int is_file) {
    nlr_buf_t nlr;
    int ret = 0;
    mp_hal_set_interrupt_char(CHAR_CTRL_C);
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex;
        qstr source_name;
        mp_parse_tree_t parse_tree;
        mp_obj_t module_fun;
        size_t len = strlen(src);
        #if MICROPY_PERSISTENT_CODE_LOAD
        if (is_file && len > 4 && strcmp(src + len - 4, ".mpy") == 0) {
            // A file precompiled with mpy-cross: nothing to parse or compile.
            mp_compiled_module_t cm;
            mp_module_context_t *context = m_new_obj(mp_module_context_t);
            context->module.globals = mp_globals_get();
            cm.context = context;
            source_name = qstr_from_str(src);
            mp_store_global(MP_QSTR___file__, MP_OBJ_NEW_QSTR(source_name));
            mp_raw_code_load_file(source_name, &cm);
            module_fun = mp_make_function_from_proto_fun(cm.rc, cm.context, NULL);
        } else
        #endif
        {
            if (is_file) {
                lex = mp_lexer_new_from_file(qstr_from_str(src));
            } else {
                lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, len, 0);
            }
            source_name = lex->source_name;
            if (is_file) {
                mp_store_global(MP_QSTR___file__, MP_OBJ_NEW_QSTR(source_name));
            }
            parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
            module_fun = mp_compile(&parse_tree, source_name, false);
        }
        mp_call_function_0(module_fun);
        mp_handle_pending(true);
        nlr_pop();
    } else {
        mp_obj_t exc = MP_OBJ_FROM_PTR(nlr.ret_val);
        if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(mp_obj_get_type(exc)), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
            // sys.exit(): an int is the exit status, None is success and
            // anything else is printed and counts as failure.
            mp_obj_t val = mp_obj_exception_get_value(exc);
            mp_int_t code = 0;
            if (val != mp_const_none && !mp_obj_get_int_maybe(val, &code)) {
                mp_obj_print_helper(&mp_plat_print, val, PRINT_STR);
                mp_print_str(&mp_plat_print, "\n");
                code = 1;
            }
            ret = (int)(code & 0xff);
        } else {
            mp_obj_print_exception(&mp_plat_print, exc);
            ret = 1;
        }
    }
    mp_hal_set_interrupt_char(-1);
    return ret;
}

static void usage(void) {
    static const char text[] =
        "usage: micropython [-c command | file.py | file.mpy] [arg ...]\n";
    write(2, text, sizeof(text) - 1);
}

int main(int argc, char **argv) {
    int stack_dummy;
    int ret = 0;
    int first = 1;      // index of the script name or -c in argv
    int i;

    stack_top = (char *)&stack_dummy;

    if (argc > 1 && argv[1][0] == '-' && strcmp(argv[1], "-c") != 0) {
        usage();
        return strcmp(argv[1], "-h") == 0 ? 0 : 2;
    }
    if (argc > 1 && strcmp(argv[1], "-c") == 0 && argc < 3) {
        usage();
        return 2;
    }

    mp_stack_ctrl_init();
    mp_stack_set_limit(MICROPY_ELKS_STACK_LIMIT);
    heap_init();
    mp_init();

    {
        // Mount the ELKS file system at the root of MicroPython's VFS, so
        // open(), import and the os module see the usual paths.
        mp_obj_t args[2];
        args[0] = MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 0, 0, NULL);
        args[1] = MP_OBJ_NEW_QSTR(MP_QSTR__slash_);
        mp_vfs_mount(2, args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }

    // sys.path = ["", "/lib/micropython"]; sys.argv = script and arguments.
    // The first entry is replaced below when a script file is run.
    mp_obj_list_append(mp_sys_path, mp_obj_new_str(MICROPY_ELKS_LIB_DIR, sizeof(MICROPY_ELKS_LIB_DIR) - 1));
    for (i = first; i < argc; i++) {
        if (i == first + 1 && strcmp(argv[first], "-c") == 0) {
            continue;   // the command string itself is not an argument
        }
        mp_obj_list_append(mp_sys_argv, mp_obj_new_str(argv[i], strlen(argv[i])));
    }

    if (argc > 1 && strcmp(argv[1], "-c") != 0) {
        // Like CPython, the first entry of sys.path is the directory of the
        // script being run, so it can import modules stored next to it.
        const char *slash = strrchr(argv[1], '/');
        if (slash != NULL) {
            size_t len = slash == argv[1] ? 1 : (size_t)(slash - argv[1]);
            mp_obj_list_store(mp_sys_path, MP_OBJ_NEW_SMALL_INT(0), mp_obj_new_str(argv[1], len));
        }
    }

    if (argc == 1) {
        mp_hal_stdio_mode_raw();
        pyexec_friendly_repl();
    } else if (strcmp(argv[1], "-c") == 0) {
        ret = execute(argv[2], 0);
    } else {
        ret = execute(argv[1], 1);
    }
    mp_hal_stdio_mode_orig();

    mp_deinit();
    return ret;
}

// Collect garbage.  Roots outside the heap are the C stack and the CPU
// registers; setjmp saves the registers into a buffer on the stack so one
// scan covers both.  The 8086 stack is only 2-byte aligned while a far
// pointer is 4 bytes, so every 2-byte position is checked.
void gc_collect(void) {
    jmp_buf regs;
    char *p;
    setjmp(regs);
    gc_collect_start();
    for (p = (char *)&regs; p + sizeof(void *) <= stack_top; p += sizeof(short)) {
        void *root;
        memcpy(&root, p, sizeof(root));
        gc_collect_root(&root, 1);
    }
    gc_collect_end();
}

void nlr_jump_fail(void *val) {
    (void)val;
    die("micropython: fatal: uncaught NLR");
}

#ifndef NDEBUG
void __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)file;
    (void)line;
    (void)func;
    die(expr);
}
#endif
