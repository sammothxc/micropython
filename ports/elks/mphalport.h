#include "py/mpconfig.h"

// Console access for ELKS: plain POSIX file descriptors 0 and 1.

void mp_hal_stdio_mode_raw(void);
void mp_hal_stdio_mode_orig(void);
void mp_hal_set_interrupt_char(int c);

// input() switches the terminal to raw mode while it reads, see elks_mphal.c.
#include "py/misc.h"
int elks_readline(vstr_t *line, const char *prompt);
#define mp_hal_readline elks_readline

// Clock and delays, see elks_mphal.c.  The ELKS kernel clock has a resolution
// of 10 ms, so that is also the resolution of the tick counters and delays.
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
#define mp_hal_ticks_cpu mp_hal_ticks_us
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);

// Retry a system call interrupted by a signal (PEP 475), used by the POSIX
// VFS.  A pending KeyboardInterrupt from Ctrl-C is raised first.
#include <errno.h>
#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) { \
        for (;;) { \
            ret = syscall; \
            if (ret == -1) { \
                int err = errno; \
                if (err == EINTR) { \
                    mp_handle_pending(true); \
                    continue; \
                } \
                raise; \
            } \
            break; \
        } \
}
