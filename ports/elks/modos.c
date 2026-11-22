// os module additions for the ELKS port.  This file is included by
// extmod/modos.c (see MICROPY_PY_OS_INCLUDEFILE), it is not compiled on its
// own.  The file and directory functions (listdir, stat, mkdir, ...) come
// from the VFS layer; this adds the environment functions, system() and
// errno().

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static mp_obj_t mp_os_getenv(size_t n_args, const mp_obj_t *args) {
    const char *s = getenv(mp_obj_str_get_str(args[0]));
    if (s == NULL) {
        return n_args == 2 ? args[1] : mp_const_none;
    }
    return mp_obj_new_str_from_cstr(s);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_os_getenv_obj, 1, 2, mp_os_getenv);

// The ELKS C library has no setenv() or unsetenv().  Its putenv() copies the
// string it is given, takes "NAME=value" to set a variable and "NAME" alone
// to remove one.
static void os_putenv_helper(mp_obj_t key_in, const char *value) {
    size_t key_len;
    const char *key = mp_obj_str_get_data(key_in, &key_len);
    vstr_t vstr;
    int ret;
    if (key_len == 0 || memchr(key, '=', key_len) != NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("illegal environment variable name"));
    }
    vstr_init(&vstr, key_len + 16);
    vstr_add_strn(&vstr, key, key_len);
    if (value != NULL) {
        vstr_add_byte(&vstr, '=');
        vstr_add_str(&vstr, value);
    }
    ret = putenv(vstr_null_terminated_str(&vstr));
    vstr_clear(&vstr);
    if (ret == -1 && value != NULL) {
        mp_raise_OSError(errno);
    }
}

static mp_obj_t mp_os_putenv(mp_obj_t key_in, mp_obj_t value_in) {
    os_putenv_helper(key_in, mp_obj_str_get_str(value_in));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(mp_os_putenv_obj, mp_os_putenv);

static mp_obj_t mp_os_unsetenv(mp_obj_t key_in) {
    os_putenv_helper(key_in, NULL);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_os_unsetenv_obj, mp_os_unsetenv);

// system(): run a command with /bin/sh and return its wait status, as
// CPython does on Unix (the exit status is in the high byte).
static mp_obj_t mp_os_system(mp_obj_t cmd_in) {
    const char *cmd = mp_obj_str_get_str(cmd_in);
    int r = system(cmd);
    if (r == -1) {
        mp_raise_OSError(errno);
    }
    return MP_OBJ_NEW_SMALL_INT(r);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mp_os_system_obj, mp_os_system);

static mp_obj_t mp_os_errno(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return MP_OBJ_NEW_SMALL_INT(errno);
    }
    errno = (int)mp_obj_get_int(args[0]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_os_errno_obj, 0, 1, mp_os_errno);
