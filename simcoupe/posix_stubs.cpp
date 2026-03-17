/*
 * simcoupe/posix_stubs.cpp
 *
 * Newlib retargetable syscall implementations for SimCoupe bare-metal.
 *
 * Newlib's fopen/fread/fwrite/fclose call these _xxx_r functions.
 * We implement them using FatFs via fatfs_posix.cpp.
 *
 * File descriptor table: fd 0/1/2 = stdin/stdout/stderr (stubs).
 * fd >= 3: FatFs file handles.
 */

/* Keep __circle__ defined but don't use our compat macros */

/* Include Circle types FIRST to get its time_t (signed long) before
 * newlib's <time.h> tries to define it as __int_least64_t (GCC 15).
 * Then guard newlib from redefining it. */
#include <circle/types.h>
#define __time_t_defined
#define _TIME_T_DECLARED
#include <circle/logger.h>

/* Now safe to include newlib headers (time_t guard prevents redefinition) */
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>   /* struct timeval */
#include <stdlib.h>
#include <stdio.h>      /* snprintf */
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <reent.h>    /* newlib reentrant struct */

/* FatFs - rename DIR to avoid POSIX conflict */
#define DIR FF_DIR
#include <ff.h>
#undef DIR

extern "C" unsigned long long circle_get_clock_ticks64(void);
extern "C" void circle_delay_ns(unsigned long long);

static const char TAG[] = "posix";

/* ================================================================
 * File Descriptor Table
 * ================================================================ */

#define MAX_FDS  32
#define FD_BASE  3    /* 0=stdin, 1=stdout, 2=stderr */

static FIL s_files[MAX_FDS];
static bool s_open[MAX_FDS] = {};

static int alloc_fd()
{
    for (int i = 0; i < MAX_FDS; i++) {
        if (!s_open[i]) return i + FD_BASE;
    }
    return -1;
}

static FIL *get_fil(int fd)
{
    int idx = fd - FD_BASE;
    if (idx < 0 || idx >= MAX_FDS || !s_open[idx]) return nullptr;
    return &s_files[idx];
}

/* Path translation: "/foo" -> "0:/foo" */
static void xlat(char *dst, size_t dsz, const char *src)
{
    if (!src || !src[0]) { strncpy(dst, "0:/", dsz); return; }
    if (src[0] == '/') snprintf(dst, dsz, "0:%s", src);
    else               snprintf(dst, dsz, "0:/%s", src);
}

static int fr_to_errno(FRESULT fr)
{
    switch (fr) {
    case FR_OK:              return 0;
    case FR_NO_FILE:
    case FR_NO_PATH:         return ENOENT;
    case FR_EXIST:           return EEXIST;
    case FR_DENIED:          return EACCES;
    case FR_WRITE_PROTECTED: return EROFS;
    case FR_NO_FILESYSTEM:   return ENODEV;
    case FR_NOT_ENOUGH_CORE: return ENOMEM;
    case FR_TOO_MANY_OPEN_FILES: return EMFILE;
    default:                 return EIO;
    }
}

/* ================================================================
 * Newlib retargetable syscalls
 * ================================================================ */

extern "C" {

int _open_r(struct _reent *r, const char *path, int flags, int mode)
{
    (void)mode;
    char fpath[256];
    xlat(fpath, sizeof(fpath), path);

    BYTE fa = 0;
    if ((flags & 3) == 0)      fa = FA_READ;           /* O_RDONLY */
    else if ((flags & 3) == 1) fa = FA_WRITE | FA_CREATE_ALWAYS; /* O_WRONLY */
    else                       fa = FA_READ | FA_WRITE;  /* O_RDWR  */
    if (flags & 0x200) fa |= FA_CREATE_ALWAYS;   /* O_CREAT  */
    if (flags & 0x400) fa |= FA_OPEN_APPEND;     /* O_APPEND */

    int fd = alloc_fd();
    if (fd < 0) { r->_errno = EMFILE; return -1; }

    int idx = fd - FD_BASE;
    FRESULT fr = f_open(&s_files[idx], fpath, fa);
    if (fr != FR_OK) { r->_errno = fr_to_errno(fr); return -1; }
    s_open[idx] = true;
    return fd;
}

int _close_r(struct _reent *r, int fd)
{
    if (fd < FD_BASE) return 0; /* stdin/stdout/stderr */
    FIL *fil = get_fil(fd);
    if (!fil) { r->_errno = EBADF; return -1; }
    f_close(fil);
    s_open[fd - FD_BASE] = false;
    return 0;
}

_ssize_t _read_r(struct _reent *r, int fd, void *buf, size_t len)
{
    if (fd < FD_BASE) return 0; /* no stdin on bare-metal */
    FIL *fil = get_fil(fd);
    if (!fil) { r->_errno = EBADF; return -1; }
    UINT br = 0;
    FRESULT fr = f_read(fil, buf, (UINT)len, &br);
    if (fr != FR_OK) { r->_errno = fr_to_errno(fr); return -1; }
    return (_ssize_t)br;
}

_ssize_t _write_r(struct _reent *r, int fd, const void *buf, size_t len)
{
    if (fd == 1 || fd == 2) {
        /* stdout/stderr: log to Circle logger */
        char tmp[256];
        size_t n = len < 255 ? len : 255;
        memcpy(tmp, buf, n); tmp[n] = '\0';
        /* Strip trailing newline for logger */
        if (n > 0 && tmp[n-1] == '\n') tmp[n-1] = '\0';
        if (CLogger::Get()) CLogger::Get()->Write(TAG, LogNotice, "%s", tmp);
        return (long)len;
    }
    FIL *fil = get_fil(fd);
    if (!fil) { r->_errno = EBADF; return -1; }
    UINT bw = 0;
    FRESULT fr = f_write(fil, buf, (UINT)len, &bw);
    if (fr != FR_OK) { r->_errno = fr_to_errno(fr); return -1; }
    return (long)bw;
}

off_t _lseek_r(struct _reent *r, int fd, off_t offset, int whence)
{
    if (fd < FD_BASE) return 0;
    FIL *fil = get_fil(fd);
    if (!fil) { r->_errno = EBADF; return -1; }
    FSIZE_t pos;
    switch (whence) {
    case 0: pos = (FSIZE_t)offset; break;                         /* SEEK_SET */
    case 1: pos = f_tell(fil) + (FSIZE_t)offset; break;           /* SEEK_CUR */
    case 2: pos = f_size(fil) + (FSIZE_t)offset; break;           /* SEEK_END */
    default: r->_errno = EINVAL; return -1;
    }
    FRESULT fr = f_lseek(fil, pos);
    if (fr != FR_OK) { r->_errno = fr_to_errno(fr); return -1; }
    return (off_t)f_tell(fil);
}

int _fstat_r(struct _reent *r, int fd, struct stat *st)
{
    (void)fd;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFREG | 0644;
    st->st_size = (fd >= FD_BASE && get_fil(fd)) ? (off_t)f_size(get_fil(fd)) : 0;
    return 0;
}

int _stat_r(struct _reent *r, const char *path, struct stat *st)
{
    char fpath[256];
    xlat(fpath, sizeof(fpath), path);
    if (strcmp(path, "/") == 0) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFDIR | 0755;
        return 0;
    }
    FILINFO fi;
    FRESULT fr = f_stat(fpath, &fi);
    if (fr != FR_OK) { r->_errno = fr_to_errno(fr); return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = (fi.fattrib & AM_DIR) ? (S_IFDIR|0755) : (S_IFREG|0644);
    st->st_size = (off_t)fi.fsize;
    return 0;
}

/* stat() without _r wrapper */
int stat(const char *path, struct stat *st)
{
    struct _reent r = {};
    return _stat_r(&r, path, st);
}

int _isatty_r(struct _reent *r, int fd)
{
    (void)r;
    return (fd < FD_BASE) ? 1 : 0;
}

/* Directory operations - used by ghc::filesystem */
/* We implement these by wrapping FatFs directly */
void *_opendir_impl(const char *path)
{
    char fpath[256];
    xlat(fpath, sizeof(fpath), path);
    FF_DIR *d = (FF_DIR *)malloc(sizeof(FF_DIR));
    if (!d) return nullptr;
    FRESULT fr = f_opendir(d, fpath);
    if (fr != FR_OK) { free(d); return nullptr; }
    return (void *)d;
}

/* ---- time ---- */
time_t time(time_t *t)
{
    time_t s = (time_t)(circle_get_clock_ticks64() / 1000000ULL);
    if (t) *t = s;
    return s;
}

/* ---- gettimeofday via newlib hook ---- */
int _gettimeofday_r(struct _reent * /*r*/, struct timeval *tv, void * /*tz*/)
{
    if (tv) {
        unsigned long long us = circle_get_clock_ticks64();
        tv->tv_sec  = (long)(us / 1000000ULL);
        tv->tv_usec = (long)(us % 1000000ULL);
    }
    return 0;
}

/* nanosleep */
struct timespec;
int nanosleep(const struct timespec *req, struct timespec *rem)
{
    extern void circle_delay_ns(unsigned long long);
    if (req) {
        unsigned long long ns =
            (unsigned long long)((struct timespec*)req)->tv_sec * 1000000000ULL +
            ((struct timespec*)req)->tv_nsec;
        circle_delay_ns(ns);
    }
    (void)rem;
    return 0;
}

/* ---- abort / exit / _Exit ---- */
void abort(void) { for(;;) {} }
void exit(int)   { for(;;) {} }
void _Exit(int)  { for(;;) {} }

/* ---- Pure virtual ---- */
void __cxa_pure_virtual(void) { for(;;) {} }

/* ---- mkdir / rename / remove / getcwd ---- */
int mkdir(const char *path, mode_t /*mode*/)
{
    char fpath[256]; xlat(fpath, sizeof(fpath), path);
    FRESULT fr = f_mkdir(fpath);
    if (fr != FR_OK) { errno = fr_to_errno(fr); return -1; }
    return 0;
}

int rename(const char *old, const char *nw)
{
    char fo[256], fn[256];
    xlat(fo, sizeof(fo), old);
    xlat(fn, sizeof(fn), nw);
    FRESULT fr = f_rename(fo, fn);
    if (fr != FR_OK) { errno = fr_to_errno(fr); return -1; }
    return 0;
}

int remove(const char *path)
{
    char fpath[256]; xlat(fpath, sizeof(fpath), path);
    FRESULT fr = f_unlink(fpath);
    if (fr != FR_OK) { errno = fr_to_errno(fr); return -1; }
    return 0;
}

char *getcwd(char *buf, size_t size)
{
    if (!buf) return nullptr;
    strncpy(buf, "/simcoupe", size - 1);
    buf[size-1] = '\0';
    return buf;
}

/* clock_gettime */
int clock_gettime(int /*clk*/, struct timespec *ts)
{
    if (ts) {
        unsigned long long us = circle_get_clock_ticks64();
        ts->tv_sec  = (long)(us / 1000000ULL);
        ts->tv_nsec = (long)((us % 1000000ULL) * 1000ULL);
    }
    return 0;
}

} /* extern "C" */

/* ================================================================
 * opendir / readdir / closedir using FatFs
 * Newlib doesn't provide these - we provide them directly.
 * ================================================================ */

#include "../src/include/dirent.h"

/* Use the opaque DIR struct from our dirent.h.
 * Store FF_DIR in the _data field. */

DIR *opendir(const char *path)
{
    char fpath[256];
    xlat(fpath, sizeof(fpath), path);
    DIR *d = (DIR *)malloc(sizeof(DIR));
    if (!d) return nullptr;
    d->magic = 0xFA7D1700u;
    FRESULT fr = f_opendir((FF_DIR *)d->_data, fpath);
    if (fr != FR_OK) { free(d); errno = fr_to_errno(fr); return nullptr; }
    return d;
}

struct dirent *readdir(DIR *dp)
{
    if (!dp || dp->magic != 0xFA7D1700u) return nullptr;
    /* The dirent is stored right after the FF_DIR in our opaque struct */
    struct dirent *de = (struct dirent *)(dp->_data + sizeof(FF_DIR));
    FILINFO fi;
    FRESULT fr = f_readdir((FF_DIR *)dp->_data, &fi);
    if (fr != FR_OK || fi.fname[0] == '\0') return nullptr;
    strncpy(de->d_name, fi.fname, sizeof(de->d_name) - 1);
    de->d_name[sizeof(de->d_name) - 1] = '\0';
    de->d_type = (fi.fattrib & AM_DIR) ? DT_DIR : DT_REG;
    return de;
}

int closedir(DIR *dp)
{
    if (!dp || dp->magic != 0xFA7D1700u) return -1;
    f_closedir((FF_DIR *)dp->_data);
    dp->magic = 0;
    free(dp);
    return 0;
}

void rewinddir(DIR *dp)
{
    if (!dp || dp->magic != 0xFA7D1700u) return;
    f_rewinddir((FF_DIR *)dp->_data);
}

/* ---- Missing POSIX stubs needed by libstdc++ / newlib ---- */
/* Must be extern "C" to match the expected C linkage */

extern "C" unsigned int sleep(unsigned int seconds)
{
    circle_delay_ns((unsigned long long)seconds * 1000000000ULL);
    return 0;
}

extern "C" int usleep(unsigned long us)
{
    circle_delay_ns((unsigned long long)us * 1000ULL);
    return 0;
}

extern "C" int truncate(const char *path, off_t length)
{
    (void)path; (void)length;
    errno = ENOSYS;
    return -1;
}

extern "C" int _link(const char *old, const char *newp)
{
    (void)old; (void)newp;
    errno = ENOSYS;
    return -1;
}
