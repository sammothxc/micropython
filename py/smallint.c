/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <limits.h>

#include "py/smallint.h"

#if UINT_MAX == 0xffff
// On a 16-bit target dividing mp_int_t values is a slow library call, while two
// non-negative values that both fit an unsigned int can use a CPU instruction.
#define SMALL_INT_BOTH_FIT_UINT(x, y) ((((mp_uint_t)(x) | (mp_uint_t)(y)) >> 16) == 0)
#endif

bool mp_small_int_mul_overflow(mp_int_t x, mp_int_t y) {
    // Two non-negative values that each use less than half of the bits of a
    // small int cannot overflow, and that needs none of the divisions below.
    if (((mp_uint_t)x | (mp_uint_t)y) < ((mp_uint_t)1 << (sizeof(mp_uint_t) * 4 - 2))) {
        return false;
    }
    // Check for multiply overflow; see CERT INT32-C
    if (x > 0) { // x is positive
        if (y > 0) { // x and y are positive
            if (x > (MP_SMALL_INT_MAX / y)) {
                return true;
            }
        } else { // x positive, y nonpositive
            if (y < (MP_SMALL_INT_MIN / x)) {
                return true;
            }
        } // x positive, y nonpositive
    } else { // x is nonpositive
        if (y > 0) { // x is nonpositive, y is positive
            if (x < (MP_SMALL_INT_MIN / y)) {
                return true;
            }
        } else { // x and y are nonpositive
            if (x != 0 && y < (MP_SMALL_INT_MAX / x)) {
                return true;
            }
        } // End if x and y are nonpositive
    } // End if x is nonpositive
    return false;
}

mp_int_t mp_small_int_modulo(mp_int_t dividend, mp_int_t divisor) {
    #ifdef SMALL_INT_BOTH_FIT_UINT
    if (SMALL_INT_BOTH_FIT_UINT(dividend, divisor)) {
        return (unsigned int)dividend % (unsigned int)divisor;
    }
    #endif
    // Python specs require that mod has same sign as second operand
    dividend %= divisor;
    if ((dividend < 0 && divisor > 0) || (dividend > 0 && divisor < 0)) {
        dividend += divisor;
    }
    return dividend;
}

mp_int_t mp_small_int_floor_divide(mp_int_t num, mp_int_t denom) {
    #ifdef SMALL_INT_BOTH_FIT_UINT
    if (SMALL_INT_BOTH_FIT_UINT(num, denom)) {
        return (unsigned int)num / (unsigned int)denom;
    }
    #endif
    if (num >= 0) {
        if (denom < 0) {
            num += -denom - 1;
        }
    } else {
        if (denom >= 0) {
            num += -denom + 1;
        }
    }
    return num / denom;
}
