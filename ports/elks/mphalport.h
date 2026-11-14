#include "py/mpconfig.h"

// Console access for ELKS: plain POSIX file descriptors 0 and 1.

void mp_hal_stdio_mode_raw(void);
void mp_hal_stdio_mode_orig(void);
void mp_hal_set_interrupt_char(int c);

// Clock and delays, see elks_mphal.c.  The ELKS kernel clock has a resolution
// of 10 ms, so that is also the resolution of the tick counters and delays.
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
#define mp_hal_ticks_cpu mp_hal_ticks_us
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);
