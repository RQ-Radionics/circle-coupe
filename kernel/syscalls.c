/*
 * kernel/syscalls.c
 *
 * Minimal newlib syscall stubs for Circle bare-metal.
 * These are required when linking newlib (libc.a) without an OS.
 *
 * Only the stubs actually called by SDL3/newlib at runtime are implemented.
 * Fatal calls (that should never be reached on bare-metal) loop forever.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

/* Memory: Circle provides its own malloc/free via CMemorySystem.
 * _sbrk is called by newlib's malloc fallback only if Circle's malloc
 * isn't available. Use Circle's _end symbol (end of BSS = start of heap). */
extern unsigned char _end; /* defined in circle.ld as end of BSS */

void *_sbrk(ptrdiff_t incr)
{
    static unsigned char *heap_end = 0;
    unsigned char *prev_heap_end;

    if (heap_end == 0) {
        heap_end = &_end;
    }
    prev_heap_end = heap_end;
    heap_end += incr;
    return (void *)prev_heap_end;
}

/* Process exit - reboot the RPi */
void _exit(int status)
{
    (void)status;
    /* Hang - CKernel handles proper shutdown */
    for (;;) {}
}

/* File operations - not used on bare-metal (SDL3 uses SDL_IOFromMem/SDL_IOFromConstMem) */
int _close(int fd)               { (void)fd; errno = ENOSYS; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; (void)st; errno = ENOSYS; return -1; }
int _isatty(int fd)              { (void)fd; return 1; }  /* pretend everything is a tty */
off_t _lseek(int fd, off_t o, int w) { (void)fd; (void)o; (void)w; errno = ENOSYS; return -1; }
int _open(const char *n, int f, ...) { (void)n; (void)f; errno = ENOSYS; return -1; }
int _read(int fd, void *buf, size_t len) { (void)fd; (void)buf; (void)len; errno = ENOSYS; return -1; }
int _write(int fd, const void *buf, size_t len) { (void)fd; (void)buf; (void)len; errno = ENOSYS; return -1; }
int _stat(const char *p, struct stat *st) { (void)p; (void)st; errno = ENOSYS; return -1; }
