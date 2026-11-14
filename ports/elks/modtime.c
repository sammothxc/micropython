// time module for the ELKS port.  This file is included by extmod/modtime.c
// (see MICROPY_PY_TIME_INCLUDEFILE), it is not compiled on its own.
//
// The generic module provides sleep(), sleep_ms(), sleep_us() and the
// ticks_*() functions on top of the functions in elks_mphal.c.  time() is
// added here.  localtime(), gmtime() and mktime() are not provided.

#include <sys/time.h>

// time(): seconds since 1970-01-01 as a float, or as an int when floating
// point is disabled.  The ELKS kernel clock advances in steps of 10 ms.
static mp_obj_t mp_time_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    #if MICROPY_PY_BUILTINS_FLOAT
    return mp_obj_new_float((mp_float_t)tv.tv_sec + (mp_float_t)tv.tv_usec / 1000000);
    #else
    return mp_obj_new_int_from_uint((mp_uint_t)tv.tv_sec);
    #endif
}
static MP_DEFINE_CONST_FUN_OBJ_0(mp_time_time_obj, mp_time_time);

#define MICROPY_PY_TIME_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&mp_time_time_obj) },
