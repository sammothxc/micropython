// Console input/output, Ctrl-C handling, clock and delays for the ELKS port.
//
// The terminal stays in its normal (cooked) mode while Python code runs, so
// sys.stdin reads whole lines with the kernel's echo and editing, Ctrl-C
// raises KeyboardInterrupt, and commands started by os.system() find the
// terminal as usual.  Only while the shared readline code reads a line (the
// REPL prompt and input()) is it switched to raw mode, so readline can do
// its own line editing and history.  Output post-processing stays on in both
// modes, so a plain '\n' still moves to the start of the next line.

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
static int have_orig;       // orig_termios holds the settings at startup
static int tty_raw;         // the terminal is in raw mode now
static int repl_active;     // raw mode between statements (the REPL is running)

static void tty_set_raw(int raw) {
    struct termios t;
    if (!have_orig || raw == tty_raw) {
        return;
    }
    t = orig_termios;
    if (raw) {
        t.c_iflag &= ~(ICRNL | INLCR | IXON);
        t.c_lflag &= ~(ICANON | ECHO | ISIG);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
    }
    if (tcsetattr(0, TCSANOW, &t) == 0) {
        tty_raw = raw;
    }
}

static void tty_save(void) {
    if (!have_orig && isatty(0) && tcgetattr(0, &orig_termios) == 0) {
        have_orig = 1;
    }
}

// Called by main() around the interactive REPL.
void mp_hal_stdio_mode_raw(void) {
    tty_save();
    repl_active = 1;
    tty_set_raw(1);
}

// Called by main() at exit, and before exiting on a fatal error.
void mp_hal_stdio_mode_orig(void) {
    repl_active = 0;
    tty_set_raw(0);
}

static void sigint_handler(int sig) {
    (void)sig;
    signal(SIGINT, sigint_handler);
    mp_sched_keyboard_interrupt();
}

// Called with CHAR_CTRL_C before Python code runs and with -1 afterwards.
void mp_hal_set_interrupt_char(int c) {
    if (c == CHAR_CTRL_C) {
        signal(SIGINT, sigint_handler);
        tty_set_raw(0);
    } else {
        signal(SIGINT, SIG_IGN);
        if (repl_active) {
            tty_set_raw(1);
        }
    }
}

// input(): read a line with the shared readline code in raw mode.  Ctrl-C
// then arrives as a character, and input() raises KeyboardInterrupt for it.
int elks_readline(vstr_t *line, const char *prompt) {
    int was_raw = tty_raw;
    int ret;
    tty_save();
    tty_set_raw(1);
    ret = readline(line, prompt);
    tty_set_raw(was_raw);
    return ret;
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

// A seed for the random module.  The clock advances in steps of 10 ms, so a
// counter makes seeds taken within one step differ, and the process id makes
// programs started within one step differ.
mp_uint_t elks_random_seed(void) {
    static mp_uint_t count;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ++count;
    return ((mp_uint_t)tv.tv_sec * 1000003UL) ^ (mp_uint_t)tv.tv_usec
           ^ (count * 0x9e3779b9UL) ^ ((mp_uint_t)getpid() << 16);
}
