// The ELKS C library has no <poll.h>.  extmod/vfs_posix_file.c includes it
// but only uses poll() for the select module, which this port does not have.
