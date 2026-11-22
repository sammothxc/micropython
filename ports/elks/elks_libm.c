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

// Wrappers for the C math functions (see MICROPY_FLOAT_C_FUN in
// mpconfigport.h).  Open Watcom's math library does not follow the IEEE and
// C99 rules for special values: sqrt(-1) and asin(2) return 0 instead of
// NaN, so Python gets no "math domain error"; infinity and NaN arguments
// are treated as huge numbers (log(inf) is 709.78); fmod() of infinity or by
// zero aborts the program; and asin(0) and atan(0) return 1e-307.  These
// wrappers handle the special cases themselves, as C99 Annex F specifies,
// and pass the rest on.  Values are classified by their bits, because the
// soft-float library's comparisons are wrong for NaN.

typedef union {
    double d;
    unsigned short w[4];    // little endian: w[3] holds sign and exponent
} dbl_bits;

#define CLASS_FINITE (0)
#define CLASS_INF (1)
#define CLASS_NAN (2)

static int dbl_class(double x) {
    dbl_bits u;
    u.d = x;
    if ((u.w[3] & 0x7ff0) != 0x7ff0) {
        return CLASS_FINITE;
    }
    if ((u.w[3] & 0x000f) == 0 && u.w[2] == 0 && u.w[1] == 0 && u.w[0] == 0) {
        return CLASS_INF;
    }
    return CLASS_NAN;
}

static int dbl_neg(double x) {
    dbl_bits u;
    u.d = x;
    return u.w[3] >> 15;
}

static int dbl_is_zero(double x) {
    dbl_bits u;
    u.d = x;
    return (u.w[3] & 0x7fff) == 0 && u.w[2] == 0 && u.w[1] == 0 && u.w[0] == 0;
}

static double dbl_make(unsigned short hi) {
    dbl_bits u;
    u.w[0] = u.w[1] = u.w[2] = 0;
    u.w[3] = hi;
    return u.d;
}

#define NAN_HI (0x7ff8)
#define INF_HI (0x7ff0)
#define SIGN_HI (0x8000)
#define PI (3.14159265358979323846)
#define PI_2 (1.57079632679489661923)
#define PI_4 (0.78539816339744830962)

static double make_nan(void) {
    return dbl_make(NAN_HI);
}

static double make_inf(int neg) {
    return dbl_make(neg ? (INF_HI | SIGN_HI) : INF_HI);
}

static double with_sign(double x, int neg) {
    dbl_bits u;
    u.d = x;
    u.w[3] = (u.w[3] & 0x7fff) | (neg ? SIGN_HI : 0);
    return u.d;
}

static int is_integer(double y) {
    return floor(y) == y;
}

static int is_odd_integer(double y) {
    return is_integer(y) && fmod(y, 2.0) != 0.0;
}

double elks_copysign(double x, double y) {
    return with_sign(x, dbl_neg(y));
}

double elks_fabs(double x) {
    return with_sign(x, 0);
}

double elks_nan(const char *tag) {
    (void)tag;
    return make_nan();
}

double elks_sqrt(double x) {
    int c = dbl_class(x);
    if (c == CLASS_NAN || dbl_is_zero(x)) {
        return x;
    }
    if (dbl_neg(x)) {
        return make_nan();
    }
    return c == CLASS_INF ? x : sqrt(x);
}

double elks_exp(double x) {
    switch (dbl_class(x)) {
        case CLASS_NAN:
            return x;
        case CLASS_INF:
            return dbl_neg(x) ? 0.0 : x;
    }
    return exp(x);
}

static double log_special(double x, double (*f)(double)) {
    int c = dbl_class(x);
    if (c == CLASS_NAN) {
        return x;
    }
    if (dbl_is_zero(x)) {
        return make_inf(1);
    }
    if (dbl_neg(x)) {
        return make_nan();
    }
    return c == CLASS_INF ? x : f(x);
}

double elks_log(double x) {
    return log_special(x, log);
}

double elks_log10(double x) {
    return log_special(x, log10);
}

static double trig_special(double x, double (*f)(double)) {
    switch (dbl_class(x)) {
        case CLASS_NAN:
            return x;
        case CLASS_INF:
            return make_nan();
    }
    return f(x);
}

double elks_sin(double x) {
    return dbl_is_zero(x) ? x : trig_special(x, sin);
}

double elks_cos(double x) {
    return trig_special(x, cos);
}

double elks_tan(double x) {
    return dbl_is_zero(x) ? x : trig_special(x, tan);
}

double elks_asin(double x) {
    int c = dbl_class(x);
    if (c == CLASS_NAN || dbl_is_zero(x)) {
        return x;
    }
    if (c == CLASS_INF || fabs(x) > 1.0) {
        return make_nan();
    }
    return asin(x);
}

double elks_acos(double x) {
    int c = dbl_class(x);
    if (c == CLASS_NAN) {
        return x;
    }
    if (c == CLASS_INF || fabs(x) > 1.0) {
        return make_nan();
    }
    return acos(x);
}

double elks_atan(double x) {
    switch (dbl_class(x)) {
        case CLASS_NAN:
            return x;
        case CLASS_INF:
            return with_sign(PI_2, dbl_neg(x));
    }
    return dbl_is_zero(x) ? x : atan(x);
}

double elks_atan2(double y, double x) {
    int cy = dbl_class(y);
    int cx = dbl_class(x);
    int ny = dbl_neg(y);
    if (cy == CLASS_NAN || cx == CLASS_NAN) {
        return make_nan();
    }
    if (cy == CLASS_INF) {
        if (cx == CLASS_INF) {
            return with_sign(dbl_neg(x) ? 3 * PI_4 : PI_4, ny);
        }
        return with_sign(PI_2, ny);
    }
    if (cx == CLASS_INF) {
        return with_sign(dbl_neg(x) ? PI : 0.0, ny);
    }
    if (dbl_is_zero(y)) {
        return with_sign(dbl_neg(x) ? PI : 0.0, ny);
    }
    if (dbl_is_zero(x)) {
        return with_sign(PI_2, ny);
    }
    return atan2(y, x);
}

double elks_pow(double x, double y) {
    int cx = dbl_class(x);
    int cy = dbl_class(y);
    if (dbl_is_zero(y)) {
        return 1.0;
    }
    if (cx == CLASS_FINITE && !dbl_neg(x) && x == 1.0) {
        return 1.0;
    }
    if (cx == CLASS_NAN || cy == CLASS_NAN) {
        return make_nan();
    }
    if (cy == CLASS_INF) {
        double ax;
        if (cx == CLASS_INF) {
            return dbl_neg(y) ? 0.0 : make_inf(0);
        }
        ax = fabs(x);
        if (ax == 1.0) {
            return 1.0;
        }
        return (ax < 1.0) == (dbl_neg(y) != 0) ? make_inf(0) : 0.0;
    }
    if (cx == CLASS_INF) {
        int odd = dbl_neg(x) && is_odd_integer(y);
        return dbl_neg(y) ? with_sign(0.0, odd) : make_inf(odd);
    }
    if (dbl_is_zero(x)) {
        int odd = dbl_neg(x) && is_odd_integer(y);
        return dbl_neg(y) ? make_inf(odd) : with_sign(0.0, odd);
    }
    if (dbl_neg(x) && !is_integer(y)) {
        return make_nan();
    }
    return pow(x, y);
}

double elks_fmod(double x, double y) {
    int cx = dbl_class(x);
    int cy = dbl_class(y);
    if (cx == CLASS_NAN || cy == CLASS_NAN || cx == CLASS_INF || dbl_is_zero(y)) {
        return make_nan();
    }
    if (cy == CLASS_INF || dbl_is_zero(x)) {
        return x;
    }
    return fmod(x, y);
}

double elks_floor(double x) {
    return dbl_class(x) != CLASS_FINITE ? x : floor(x);
}

double elks_ceil(double x) {
    return dbl_class(x) != CLASS_FINITE ? x : ceil(x);
}

double elks_trunc(double x) {
    return dbl_class(x) != CLASS_FINITE ? x : trunc(x);
}

double elks_ldexp(double x, int e) {
    if (dbl_class(x) != CLASS_FINITE || dbl_is_zero(x)) {
        return x;
    }
    return ldexp(x, e);
}

double elks_frexp(double x, int *e) {
    if (dbl_class(x) != CLASS_FINITE || dbl_is_zero(x)) {
        *e = 0;
        return x;
    }
    return frexp(x, e);
}

double elks_modf(double x, double *ip) {
    switch (dbl_class(x)) {
        case CLASS_NAN:
            *ip = x;
            return x;
        case CLASS_INF:
            *ip = x;
            return with_sign(0.0, dbl_neg(x));
    }
    return modf(x, ip);
}

// Round to the nearest integer, ties to even (used by the round() builtin).
double elks_nearbyint(double x) {
    double f, d;
    if (dbl_class(x) != CLASS_FINITE) {
        return x;
    }
    f = floor(x);
    d = x - f;
    if (d > 0.5) {
        return f + 1.0;
    }
    if (d < 0.5) {
        return f;
    }
    return fmod(f, 2.0) == 0.0 ? f : f + 1.0;
}

#endif
