// time module for the ELKS port.  This file is included by extmod/modtime.c
// (see MICROPY_PY_TIME_INCLUDEFILE), it is not compiled on its own.
//
// The generic module provides sleep(), sleep_ms(), sleep_us(), the ticks_*()
// functions on top of elks_mphal.c, and gmtime(), localtime() and mktime()
// (in UTC: the ELKS kernel keeps no time zone) on top of the function
// below.  time() is added here.

#include <sys/time.h>

#include "shared/timeutils/timeutils.h"

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

// The current time as the 8-tuple returned by localtime().
static mp_obj_t mp_time_localtime_get(void) {
    struct timeval tv;
    timeutils_struct_time_t tm;
    mp_obj_t tuple[8];
    gettimeofday(&tv, NULL);
    timeutils_seconds_since_epoch_to_struct_time((mp_uint_t)tv.tv_sec, &tm);
    tuple[0] = mp_obj_new_int(tm.tm_year);
    tuple[1] = mp_obj_new_int(tm.tm_mon);
    tuple[2] = mp_obj_new_int(tm.tm_mday);
    tuple[3] = mp_obj_new_int(tm.tm_hour);
    tuple[4] = mp_obj_new_int(tm.tm_min);
    tuple[5] = mp_obj_new_int(tm.tm_sec);
    tuple[6] = mp_obj_new_int(tm.tm_wday);
    tuple[7] = mp_obj_new_int(tm.tm_yday);
    return mp_obj_new_tuple(8, tuple);
}

#define MICROPY_PY_TIME_EXTRA_GLOBALS \
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&mp_time_time_obj) },
