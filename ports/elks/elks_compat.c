// Replacements for ELKS C library functions, used in the Open Watcom build
// (see mpconfigport.h, which redirects the calls here).
//
// In the large model a pointer has a segment and an offset, but an ELKS
// system call takes only the offsets of its pointer arguments, plus one data
// segment for all of them.  Strings on the Python heap are in a different
// segment from buffers on the C stack, so a call such as stat(path, &buf)
// would read the path from the wrong segment.  These wrappers copy the
// arguments to the stack, which is in the program's data segment.  Some
// also work around other limits of the kernel and the C library.

#if defined(__WATCOMC__)

// The C library headers come first, so that their prototypes of stat() and
// rename() are not renamed by the macros in mpconfigport.h.
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "watcom/syselks.h"

#include "py/mpconfig.h"

#undef stat
#undef rename
#undef system

// Copy a path into buf (size MICROPY_ALLOC_PATH_MAX + 1).  Returns 0, or -1
// with errno set if the path is too long.
static int copy_path(char *buf, const char *path) {
    size_t len = strlen(path);
    if (len > MICROPY_ALLOC_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buf, path, len + 1);
    return 0;
}

int elks_stat(const char *path, struct stat *buf) {
    char p[MICROPY_ALLOC_PATH_MAX + 1];
    struct stat st;
    int ret;
    if (copy_path(p, path) != 0) {
        return -1;
    }
    ret = stat(p, &st);
    if (ret == 0) {
        *buf = st;
    }
    return ret;
}

// Copy the regular file src to dst, replacing dst.  Returns 0, or -1 with
// errno set (and no partial dst left behind).
static int copy_file(const char *src, const char *dst, mode_t mode) {
    char buf[512];
    int in, out, n = 0, err = 0;
    in = open(src, O_RDONLY);
    if (in < 0) {
        return -1;
    }
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode & 07777);
    if (out < 0) {
        err = errno;
        close(in);
        errno = err;
        return -1;
    }
    while ((n = read(in, buf, sizeof(buf))) > 0) {
        if (write(out, buf, n) != n) {
            err = errno != 0 ? errno : ENOSPC;
            break;
        }
    }
    if (n < 0) {
        err = errno;
    }
    close(in);
    if (close(out) != 0 && err == 0) {
        err = errno;
    }
    if (err != 0) {
        unlink(dst);
        errno = err;
        return -1;
    }
    return 0;
}

// The kernel's rename() is a link() followed by an unlink() of the old name.
// That fails with EEXIST when the new name exists (POSIX rename() replaces
// it), and with EPERM on FAT file systems, which cannot link at all.  ELKS's
// mv copies the file in that case, and so does this, for regular files.
int elks_rename(const char *old_path, const char *new_path) {
    char o[MICROPY_ALLOC_PATH_MAX + 1];
    char n[MICROPY_ALLOC_PATH_MAX + 1];
    struct stat so, sn;
    int err;
    if (copy_path(o, old_path) != 0 || copy_path(n, new_path) != 0) {
        return -1;
    }
    if (rename(o, n) == 0) {
        return 0;
    }
    err = errno;
    if ((err != EEXIST && err != EPERM) || stat(o, &so) != 0 || !S_ISREG(so.st_mode)) {
        errno = err;
        return -1;
    }
    if (stat(n, &sn) == 0) {
        if (sn.st_dev == so.st_dev && sn.st_ino == so.st_ino) {
            return 0;   // both names are the same file
        }
        if (S_ISDIR(sn.st_mode)) {
            errno = EISDIR;
            return -1;
        }
        if (err == EEXIST) {
            // A file system that can link: replace the old target.
            if (unlink(n) != 0) {
                return -1;
            }
            return rename(o, n);
        }
    }
    if (copy_file(o, n, so.st_mode) != 0) {
        return -1;
    }
    return unlink(o);
}

// The C library's system() hangs in this build.  It uses vfork(), which the
// Open Watcom library implements as an ordinary C function: the child
// returns from it and calls execl() on the stack it shares with the parent,
// overwriting what the parent's return path depends on.  fork() avoids that
// but copies the whole data segment, after which there is often not enough
// memory left to load the shell.
//
// So this version makes the vfork system call inline.  The child only calls
// exec_child(), whose stack frame lies below this function's, and the exec
// arguments are built beforehand on this function's stack.  (The library's
// execve() would take them from the heap with sbrk(), which under vfork
// would move the parent's heap break for good.)

// Size limit of the shell command plus environment.
#define EXEC_BLOCK_SIZE (1024)

// Build the argument block that the ELKS execve system call takes: argc,
// the argv and envp arrays as offsets from the start of the block, each
// ending with 0, then the strings.  Returns its size, or -1 if it does not
// fit in EXEC_BLOCK_SIZE bytes.
static int build_exec_block(char *blk, const char *const *argv, char *const *envp) {
    int argc = 0;
    int envc = 0;
    size_t bytes = 2 * sizeof(unsigned short);
    unsigned short *ptrs = (unsigned short *)blk;
    char *str;
    int i;
    for (; argv[argc] != NULL; argc++) {
        bytes += sizeof(unsigned short) + strlen(argv[argc]) + 1;
    }
    for (; envp != NULL && envp[envc] != NULL; envc++) {
        bytes += sizeof(unsigned short) + strlen(envp[envc]) + 1;
        if (bytes > EXEC_BLOCK_SIZE) {
            return -1;
        }
    }
    bytes += sizeof(unsigned short);
    if (bytes > EXEC_BLOCK_SIZE) {
        return -1;
    }
    str = blk + sizeof(unsigned short) * (1 + argc + 1 + envc + 1);
    *ptrs++ = (unsigned short)argc;
    for (i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        *ptrs++ = (unsigned short)(str - blk);
        memcpy(str, argv[i], len);
        str += len;
    }
    *ptrs++ = 0;
    for (i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1;
        *ptrs++ = (unsigned short)(str - blk);
        memcpy(str, envp[i], len);
        str += len;
    }
    *ptrs = 0;
    return (int)bytes;
}

static void exec_child(char *blk, int bytes) {
    _execve(_PATH_BSHELL, blk, bytes);
    _exit(127);
}

int elks_system(const char *command) {
    char blk[EXEC_BLOCK_SIZE];
    const char *argv[4];
    void (*save_int)(int);
    void (*save_quit)(int);
    int bytes;
    int status;
    int res;
    pid_t r;
    if (command == NULL) {
        return 1;
    }
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = command;
    argv[3] = NULL;
    bytes = build_exec_block(blk, argv, environ);
    if (bytes < 0) {
        errno = E2BIG;
        return -1;
    }
    // The parent is suspended until the child has called exec or exited.
    res = sys_call0(SYS_vfork);
    if (res == 0) {
        exec_child(blk, bytes);
    }
    if (res < 0) {
        errno = -res;
        return -1;
    }
    // Like the C library, ignore Ctrl-C and Ctrl-\ while the command runs;
    // they go to the command, which shares the terminal.
    save_quit = signal(SIGQUIT, SIG_IGN);
    save_int = signal(SIGINT, SIG_IGN);
    do {
        r = waitpid((pid_t)res, &status, 0);
    } while ((int)r == -1 && errno == EINTR);
    signal(SIGQUIT, save_quit);
    signal(SIGINT, save_int);
    return (int)r == -1 ? -1 : status;
}

// ELKS has no fsync(), so write out every dirty block.
int elks_fsync(int fd) {
    (void)fd;
    sync();
    return 0;
}

#endif
