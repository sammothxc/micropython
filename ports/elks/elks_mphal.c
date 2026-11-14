// Console input/output, Ctrl-C handling, clock and delays for the ELKS port.
//
// When stdin is a terminal it is switched to raw mode for the whole session,
// so the shared readline code can do its own line editing.  Output
// post-processing stays on, so a plain '\n' still moves to the start of the
// next line.  Signal generation (ISIG) is turned on only while Python code
// runs, which makes Ctrl-C raise KeyboardInterrupt; at the prompt Ctrl-C
// arrives as an ordinary character and cancels the current line.

#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "py/mphal.h"
#include "py/mpstate.h"
#include "py/runtime.h"
#include "shared/readline/readline.h"

static struct termios orig_termios;
static int raw_active;

void mp_hal_stdio_mode_raw(void) {
    struct termios t;
    if (raw_active || !isatty(0) || tcgetattr(0, &orig_termios) != 0) {
        return;
    }
    t = orig_termios;
    t.c_iflag &= ~(ICRNL | INLCR | IXON);
    t.c_lflag &= ~(ICANON | ECHO | ISIG);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(0, TCSANOW, &t) == 0) {
        raw_active = 1;
    }
}

void mp_hal_stdio_mode_orig(void) {
    if (raw_active) {
        tcsetattr(0, TCSANOW, &orig_termios);
        raw_active = 0;
    }
}

static void sigint_handler(int sig) {
    (void)sig;
    signal(SIGINT, sigint_handler);
    mp_sched_keyboard_interrupt();
}

void mp_hal_set_interrupt_char(int c) {
    if (c == CHAR_CTRL_C) {
        signal(SIGINT, sigint_handler);
    } else {
        signal(SIGINT, SIG_IGN);
    }
    if (raw_active) {
        struct termios t;
        if (tcgetattr(0, &t) == 0) {
            if (c == CHAR_CTRL_C) {
                t.c_lflag |= ISIG;
            } else {
                t.c_lflag &= ~ISIG;
            }
            tcsetattr(0, TCSANOW, &t);
        }
    }
}

int mp_hal_stdin_rx_chr(void) {
    unsigned char c;
    for (;;) {
        int n = (int)read(0, &c, 1);
        if (n == 1) {
            // Input that is not a raw terminal (a pipe or file) ends lines
            // with '\n'; readline expects '\r'.
            return c == '\n' ? '\r' : c;
        }
        if (n == 0) {
            return CHAR_CTRL_D;
        }
        if (errno != EINTR) {
            return CHAR_CTRL_D;
        }
        // Interrupted by Ctrl-C while Python code waits for input: raise
        // the pending KeyboardInterrupt.
        mp_handle_pending(true);
    }
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    size_t done = 0;
    while (done < len) {
        int n = (int)write(1, str + done, len - done);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
        done += (size_t)n;
    }
    return done;
}

void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    mp_hal_stdout_tx_strn(str, len);
}

void mp_hal_stdout_tx_str(const char *str) {
    size_t len = 0;
    while (str[len] != '\0') {
        ++len;
    }
    mp_hal_stdout_tx_strn(str, len);
}

// Tick counters.  They wrap around; the time module only uses differences.
mp_uint_t mp_hal_ticks_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (mp_uint_t)tv.tv_sec * 1000 + (mp_uint_t)tv.tv_usec / 1000;
}

mp_uint_t mp_hal_ticks_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (mp_uint_t)tv.tv_sec * 1000000 + (mp_uint_t)tv.tv_usec;
}

// Sleep in pieces of at most half a second, so that a pending
// KeyboardInterrupt (Ctrl-C also interrupts usleep) is raised promptly.
void mp_hal_delay_us(mp_uint_t us) {
    mp_uint_t start = mp_hal_ticks_us();
    for (;;) {
        mp_uint_t elapsed = mp_hal_ticks_us() - start;
        mp_uint_t left;
        if (elapsed >= us) {
            break;
        }
        left = us - elapsed;
        if (left > 500000) {
            left = 500000;
        }
        usleep((unsigned long)left);
        mp_handle_pending(true);
    }
}

void mp_hal_delay_ms(mp_uint_t ms) {
    // Whole seconds first: the microsecond count of a long delay does not
    // fit in 32 bits.
    while (ms >= 1000) {
        mp_hal_delay_us(1000000);
        ms -= 1000;
    }
    mp_hal_delay_us(ms * 1000);
}
