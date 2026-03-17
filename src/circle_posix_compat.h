/*
 * src/circle_posix_compat.h
 *
 * Minimal compatibility stubs for SimCoupe on Circle bare-metal.
 * Force-included via -include in the SimCoupe Makefile.
 *
 * Strategy: NO fopen/stat/opendir macros here - those conflict with C++ headers.
 * File I/O is handled by FatFsFile.h / FatFs directly inside SimCoupe.
 * Only stubs for truly missing POSIX functions.
 */
#pragma once

#ifdef __circle__

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

/* ---- Declare our Circle helper functions ---- */
#ifdef __cplusplus
extern "C" {
#endif
void               circle_delay_ns(unsigned long long ns);
unsigned long long circle_get_clock_ticks64(void);
#ifdef __cplusplus
}
#endif

/* ---- usleep ---- */
static inline int usleep(unsigned long us) {
    circle_delay_ns((unsigned long long)us * 1000ULL);
    return 0;
}

/* ---- nl_langinfo: always UTF-8 ---- */
#ifndef _LANGINFO_H
#define nl_langinfo(x) ((char*)"UTF-8")
#endif

/* ---- lstat: no symlinks on FatFs ---- */
#ifndef lstat
#include <sys/stat.h>
#define lstat(p,s) stat((p),(s))
#endif

/* ---- symlink / readlink: always fail ---- */
static inline int symlink(const char *t, const char *l)
    { (void)t; (void)l; errno = EPERM; return -1; }
static inline int readlink(const char *p, char *b, size_t s)
    { (void)p; (void)b; (void)s; errno = EINVAL; return -1; }

/* ---- chdir: no-op (only if not already declared by unistd.h) ---- */
#ifndef _UNISTD_H_
static inline int chdir(const char *p) { (void)p; return 0; }
#endif

/* ---- signal: no-op ---- */
#ifndef _SIGNAL_H_
typedef void (*sighandler_t)(int);
static inline sighandler_t signal(int s, sighandler_t h) { (void)s; return h; }
static inline int raise(int s) { (void)s; return 0; }
#endif

/* ---- ioctl: always fail ---- */
static inline int ioctl(int fd, unsigned long r, ...)
    { (void)fd; (void)r; errno = ENOSYS; return -1; }

/* ---- wordexp: not available ---- */
#ifndef _WORDEXP_H
#define WRDE_NOCMD 0
typedef struct { size_t we_wordc; char **we_wordv; } wordexp_t;
static inline int   wordexp(const char *s, wordexp_t *p, int f)
    { (void)s;(void)p;(void)f; return 1; }
static inline void  wordfree(wordexp_t *p) { (void)p; }
#endif

#endif /* __circle__ */
