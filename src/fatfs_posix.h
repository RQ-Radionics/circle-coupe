/*
 * src/fatfs_posix.h
 *
 * POSIX-compatible shim over Circle FatFs for bare-metal SimCoupe.
 *
 * Provides:
 *   - fopen/fread/fwrite/fclose/fseek/ftell/feof/ferror/rewind/fflush
 *   - opendir/readdir/closedir
 *   - stat/mkdir/rename/remove/getcwd/access
 *   - fatfs_mount() - must be called from CKernel::Initialize() after SD init
 *
 * SD card layout expected:
 *   /              -> FatFs drive "0:/"  (SD card FAT32 partition)
 *   /simcoupe/     -> SimCoupe data (samcoupe.rom, disk images)
 *
 * Thread safety: none - single-threaded Circle model.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the SD card FAT32 partition. Call once from CKernel::Initialize()
 * after CEMMCDevice or CSDHostDevice is ready.
 * Returns 0 on success, -1 on failure. */
int fatfs_mount(void);
void fatfs_unmount(void);

/* Returns 1 if the filesystem is mounted and usable */
int fatfs_is_mounted(void);

/* ---- POSIX FILE I/O ---- */
/* These shadow the newlib stubs, providing real SD card access */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>   /* for FILE, SEEK_*, EOF - we reuse the types */

/* Override fopen etc. via macro if needed, or use directly */
FILE *fatfs_fopen  (const char *path, const char *mode);
int   fatfs_fclose (FILE *fp);
size_t fatfs_fread (void *buf, size_t size, size_t count, FILE *fp);
size_t fatfs_fwrite(const void *buf, size_t size, size_t count, FILE *fp);
int   fatfs_fseek  (FILE *fp, long offset, int whence);
long  fatfs_ftell  (FILE *fp);
int   fatfs_feof   (FILE *fp);
int   fatfs_ferror (FILE *fp);
void  fatfs_rewind (FILE *fp);
int   fatfs_fflush (FILE *fp);
char *fatfs_fgets  (char *buf, int n, FILE *fp);
int   fatfs_fputs  (const char *s, FILE *fp);

/* ---- POSIX directory I/O ---- */
DIR           *fatfs_opendir (const char *path);
struct dirent *fatfs_readdir (DIR *dp);
int            fatfs_closedir(DIR *dp);
void           fatfs_rewinddir(DIR *dp);

/* ---- POSIX file system ops ---- */
int  fatfs_stat   (const char *path, struct stat *st);
int  fatfs_mkdir  (const char *path, mode_t mode);
int  fatfs_rename (const char *oldp, const char *newp);
int  fatfs_remove (const char *path);  /* unlink + rmdir */
int  fatfs_access (const char *path, int mode);
char *fatfs_getcwd(char *buf, size_t size);

/* ---- Override macros ----
 * SimCoupe code includes <cstdio>/<dirent.h> before SimCoupe.h.
 * We inject these via a prefix header (-include fatfs_posix_overrides.h)
 * rather than macros here to avoid circular includes. */

#ifdef __cplusplus
}
#endif
