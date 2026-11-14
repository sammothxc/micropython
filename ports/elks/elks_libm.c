// Floating-point support for the ELKS port: replacements for C library
// pieces that are missing or unsuitable.

#include "py/mpconfig.h"

#if defined(__WATCOMC__) && MICROPY_ELKS_FLOAT

#include <errno.h>
#include <math.h>

// The ELKS C library's handlers for floating-point exceptions and math
// function errors abort the program with a failed assertion.  Python code
// must keep running after an overflow or a domain error, so the handlers are
// replaced here.  Every symbol of the two library modules (fpexcept.c and
// math2err.c) is defined, so the linker takes none of them from the library.

// Flags of the Open Watcom soft-float library.
char _8087;
char _real87;
char _chipbug;

// Called by the soft-float routines on overflow, division by zero and
// invalid operations.  Returning lets them produce inf or nan.
void __FPE_exception(int fpe_type) {
    (void)fpe_type;
}

void __set_ERANGE(void) {
    errno = ERANGE;
}

// Called by the math functions on an error; err_info says which value the
// function should return (see mathcode.h in the ELKS C library).
#define V_NEG_HUGEVAL (0x1000)
#define V_ZERO (0x2000)
#define V_ONE (0x4000)
#define V_HUGEVAL (0x8000)

double __math2err(unsigned int err_info, double *arg1, double *arg2) {
    (void)arg1;
    if (err_info & V_NEG_HUGEVAL) {
        return -HUGE_VAL;
    } else if (err_info & V_ZERO) {
        return 0.0;
    } else if (err_info & V_ONE) {
        return 1.0;
    } else if (err_info & V_HUGEVAL) {
        return HUGE_VAL;
    }
    return *arg2;   // partial loss of precision in sin/cos/tan
}

double __math1err(unsigned int err_info, double *arg1) {
    return __math2err(err_info, arg1, arg1);
}

// Round to the nearest integer, ties to even (used by the round() builtin).
double nearbyint(double x) {
    double f = floor(x);
    double d = x - f;
    if (d > 0.5) {
        return f + 1.0;
    }
    if (d < 0.5) {
        return f;
    }
    return fmod(f, 2.0) == 0.0 ? f : f + 1.0;
}

#endif
