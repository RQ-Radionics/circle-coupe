/*
 * src/fatfs_posix.cpp
 *
 * POSIX shim over Circle FatFs for bare-metal SimCoupe (RPi 3B AArch32).
 *
 * Maps POSIX file/dir operations to FatFs, satisfying:
 *   - SimCoupe direct fopen/fread/fclose calls
 *   - ghc::filesystem (opendir, stat, mkdir, rename, remove)
 *
 * SD card is mounted as FatFs drive "0:" which maps to Unix root "/".
 * Path translation: "/foo/bar" -> "0:/foo/bar"
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>

/* FatFs - include BEFORE POSIX headers; rename DIR to FF_DIR to avoid conflict */
#define DIR FF_DIR
#include <ff.h>
#include <diskio.h>
#undef DIR

/* Include Circle types FIRST to get its time_t before newlib redefines it */
#include <circle/types.h>
#define __time_t_defined
#define _TIME_T_DECLARED
#include <circle/devicenameservice.h>
#include <circle/logger.h>

/* POSIX types (time_t already defined by Circle above) */
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdio.h>

#include "fatfs_posix.h"

static const char TAG[] = "fatfs";

/* ---- Mount state ---- */

static FATFS s_fatfs;
static bool  s_mounted = false;

/* Path translation: "/foo/bar" -> "0:/foo/bar", "/" -> "0:/" */
static void translate_path(char *dst, size_t dsz, const char *src)
{
    if (!src || src[0] == '\0') {
        strncpy(dst, "0:/", dsz);
        return;
    }
    if (src[0] == '/') {
        snprintf(dst, dsz, "0:%s", src);
    } else {
        /* relative path - prepend current FatFs dir */
        snprintf(dst, dsz, "0:/%s", src);
    }
}

/* Map FatFs FRESULT to errno */
static int fresult_to_errno(FRESULT fr)
{
    switch (fr) {
    case FR_OK:             return 0;
    case FR_NO_FILE:
    case FR_NO_PATH:        return ENOENT;
    case FR_EXIST:          return EEXIST;
    case FR_DENIED:         return EACCES;
    case FR_WRITE_PROTECTED:return EROFS;
    case FR_INVALID_NAME:   return EINVAL;
    case FR_NOT_ENABLED:
    case FR_NO_FILESYSTEM:  return ENODEV;
    case FR_NOT_ENOUGH_CORE:return ENOMEM;
    case FR_TOO_MANY_OPEN_FILES: return EMFILE;
    case FR_DISK_ERR:
    default:                return EIO;
    }
}

/* ---- Mount / Unmount ---- */

int fatfs_mount(void)
{
    if (s_mounted) return 0;

    FRESULT fr = f_mount(&s_fatfs, "0:", 1 /* force mount */);
    if (fr != FR_OK) {
        CLogger::Get()->Write(TAG, LogError,
            "f_mount failed: %d", (int)fr);
        errno = fresult_to_errno(fr);
        return -1;
    }
    s_mounted = true;
    CLogger::Get()->Write(TAG, LogNotice, "SD card mounted at '0:/'");
    return 0;
}

void fatfs_unmount(void)
{
    if (s_mounted) {
        f_mount(nullptr, "0:", 0);
        s_mounted = false;
    }
}

int fatfs_is_mounted(void)
{
    return s_mounted ? 1 : 0;
}

/* ================================================================
 * FILE I/O
 * ================================================================ */

/* We allocate FIL structs on the heap and cast to FILE*.
 * FatFs FIL is not the same as newlib FILE - we use a wrapper. */
struct FatFsFILE {
    uint32_t magic;   /* 0xFA7F5 to detect our FILEs vs libc FILEs */
    FIL      fil;
    bool     eof;
    bool     err;
};
#define FATFS_FILE_MAGIC 0x00FA7F50u

static inline bool is_fatfs_file(FILE *fp)
{
    if (!fp) return false;
    return ((FatFsFILE *)fp)->magic == FATFS_FILE_MAGIC;
}

FILE *fatfs_fopen(const char *path, const char *mode)
{
    if (!s_mounted || !path) { errno = ENODEV; return nullptr; }

    BYTE flags = 0;
    bool write = false;
    bool append = false;

    if (strchr(mode, 'r')) flags |= FA_READ;
    if (strchr(mode, 'w')) { flags |= FA_WRITE | FA_CREATE_ALWAYS; write = true; }
    if (strchr(mode, 'a')) { flags |= FA_WRITE | FA_OPEN_APPEND; write = true; append = true; }
    if (strchr(mode, '+')) { flags |= FA_READ | FA_WRITE; }
    if (!write && !append && !(flags & FA_WRITE)) flags |= FA_READ;

    char fpath[256];
    translate_path(fpath, sizeof(fpath), path);

    FatFsFILE *f = (FatFsFILE *)malloc(sizeof(FatFsFILE));
    if (!f) { errno = ENOMEM; return nullptr; }

    FRESULT fr = f_open(&f->fil, fpath, flags);
    if (fr != FR_OK) {
        free(f);
        errno = fresult_to_errno(fr);
        return nullptr;
    }
    f->magic = FATFS_FILE_MAGIC;
    f->eof   = false;
    f->err   = false;
    return (FILE *)f;
}

int fatfs_fclose(FILE *fp)
{
    if (!is_fatfs_file(fp)) return fclose(fp);
    FatFsFILE *f = (FatFsFILE *)fp;
    f_close(&f->fil);
    f->magic = 0;
    free(f);
    return 0;
}

size_t fatfs_fread(void *buf, size_t size, size_t count, FILE *fp)
{
    if (!is_fatfs_file(fp)) return fread(buf, size, count, fp);
    FatFsFILE *f = (FatFsFILE *)fp;
    if (f->eof) return 0;
    UINT br = 0;
    FRESULT fr = f_read(&f->fil, buf, (UINT)(size * count), &br);
    if (fr != FR_OK) { f->err = true; errno = fresult_to_errno(fr); return 0; }
    if (br < size * count) f->eof = true;
    return (size > 0) ? (br / size) : 0;
}

size_t fatfs_fwrite(const void *buf, size_t size, size_t count, FILE *fp)
{
    if (!is_fatfs_file(fp)) return fwrite(buf, size, count, fp);
    FatFsFILE *f = (FatFsFILE *)fp;
    UINT bw = 0;
    FRESULT fr = f_write(&f->fil, buf, (UINT)(size * count), &bw);
    if (fr != FR_OK) { f->err = true; errno = fresult_to_errno(fr); return 0; }
    return (size > 0) ? (bw / size) : 0;
}

int fatfs_fseek(FILE *fp, long offset, int whence)
{
    if (!is_fatfs_file(fp)) return fseek(fp, offset, whence);
    FatFsFILE *f = (FatFsFILE *)fp;
    FSIZE_t pos;
    switch (whence) {
    case SEEK_SET: pos = (FSIZE_t)offset; break;
    case SEEK_CUR: pos = f_tell(&f->fil) + (FSIZE_t)offset; break;
    case SEEK_END: pos = f_size(&f->fil) + (FSIZE_t)offset; break;
    default: errno = EINVAL; return -1;
    }
    FRESULT fr = f_lseek(&f->fil, pos);
    if (fr != FR_OK) { errno = fresult_to_errno(fr); return -1; }
    f->eof = false;
    return 0;
}

long fatfs_ftell(FILE *fp)
{
    if (!is_fatfs_file(fp)) return ftell(fp);
    return (long)f_tell(&((FatFsFILE *)fp)->fil);
}

int fatfs_feof(FILE *fp)
{
    if (!is_fatfs_file(fp)) return feof(fp);
    return ((FatFsFILE *)fp)->eof ? 1 : 0;
}

int fatfs_ferror(FILE *fp)
{
    if (!is_fatfs_file(fp)) return ferror(fp);
    return ((FatFsFILE *)fp)->err ? 1 : 0;
}

void fatfs_rewind(FILE *fp)
{
    fatfs_fseek(fp, 0, SEEK_SET);
    if (is_fatfs_file(fp)) ((FatFsFILE *)fp)->eof = ((FatFsFILE *)fp)->err = false;
}

int fatfs_fflush(FILE *fp)
{
    if (!is_fatfs_file(fp)) return fflush(fp);
    FRESULT fr = f_sync(&((FatFsFILE *)fp)->fil);
    return (fr == FR_OK) ? 0 : -1;
}

char *fatfs_fgets(char *buf, int n, FILE *fp)
{
    if (!is_fatfs_file(fp)) return fgets(buf, n, fp);
    FatFsFILE *f = (FatFsFILE *)fp;
    if (f->eof || n <= 0) return nullptr;
    int i = 0;
    while (i < n - 1) {
        char c;
        UINT br;
        FRESULT fr = f_read(&f->fil, &c, 1, &br);
        if (fr != FR_OK || br == 0) { f->eof = true; break; }
        buf[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return nullptr;
    buf[i] = '\0';
    return buf;
}

int fatfs_fputs(const char *s, FILE *fp)
{
    if (!is_fatfs_file(fp)) return fputs(s, fp);
    size_t len = strlen(s);
    size_t w = fatfs_fwrite(s, 1, len, fp);
    return (w == len) ? (int)w : EOF;
}

/* ================================================================
 * DIRECTORY I/O
 * ================================================================ */

/* FatFs DIR was renamed to FF_DIR via #define DIR FF_DIR above.
 * POSIX DIR is a different type from dirent.h. */
#define FATFS_DIR_MAGIC 0x00FA7D00u

struct FatFsDIRWrapper {
    uint32_t    magic;
    FF_DIR      ffdir;   /* FatFs directory object (was named DIR in ff.h) */
    struct dirent de;
};

DIR *fatfs_opendir(const char *path)
{
    if (!s_mounted) { errno = ENODEV; return nullptr; }
    char fpath[256];
    translate_path(fpath, sizeof(fpath), path);

    FatFsDIRWrapper *d = (FatFsDIRWrapper *)malloc(sizeof(FatFsDIRWrapper));
    if (!d) { errno = ENOMEM; return nullptr; }

    FRESULT fr = f_opendir(&d->ffdir, fpath);
    if (fr != FR_OK) {
        free(d);
        errno = fresult_to_errno(fr);
        return nullptr;
    }
    d->magic = FATFS_DIR_MAGIC;
    return (DIR *)d;
}

struct dirent *fatfs_readdir(DIR *dp)
{
    if (!dp) return nullptr;
    FatFsDIRWrapper *d = (FatFsDIRWrapper *)dp;
    if (d->magic != FATFS_DIR_MAGIC) return readdir(dp);

    FILINFO fi;
    FRESULT fr = f_readdir(&d->ffdir, &fi);
    if (fr != FR_OK || fi.fname[0] == '\0') {
        errno = (fr != FR_OK) ? fresult_to_errno(fr) : 0;
        return nullptr;
    }
    strncpy(d->de.d_name, fi.fname, sizeof(d->de.d_name) - 1);
    d->de.d_name[sizeof(d->de.d_name) - 1] = '\0';
    d->de.d_type = (fi.fattrib & AM_DIR) ? DT_DIR : DT_REG;
    return &d->de;
}

int fatfs_closedir(DIR *dp)
{
    if (!dp) return 0;
    FatFsDIRWrapper *d = (FatFsDIRWrapper *)dp;
    if (d->magic != FATFS_DIR_MAGIC) return closedir(dp);
    f_closedir(&d->ffdir);
    d->magic = 0;
    free(d);
    return 0;
}

void fatfs_rewinddir(DIR *dp)
{
    if (!dp) return;
    FatFsDIRWrapper *d = (FatFsDIRWrapper *)dp;
    if (d->magic != FATFS_DIR_MAGIC) { rewinddir(dp); return; }
    /* FatFs: rewind by closing and re-opening is not practical without storing path.
     * Use f_readdir with null to rewind (FatFs extension). */
    f_rewinddir(&d->ffdir);
}

/* ================================================================
 * FILESYSTEM OPS
 * ================================================================ */

int fatfs_stat(const char *path, struct stat *st)
{
    if (!s_mounted) { errno = ENODEV; return -1; }
    if (!path || !st) { errno = EINVAL; return -1; }

    /* Special case: root "/" always exists */
    if (strcmp(path, "/") == 0 || strcmp(path, "") == 0) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFDIR | 0755;
        return 0;
    }

    char fpath[256];
    translate_path(fpath, sizeof(fpath), path);

    FILINFO fi;
    FRESULT fr = f_stat(fpath, &fi);
    if (fr != FR_OK) { errno = fresult_to_errno(fr); return -1; }

    memset(st, 0, sizeof(*st));
    st->st_size  = (off_t)fi.fsize;
    st->st_mode  = (fi.fattrib & AM_DIR) ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_nlink = 1;
    /* FatFs timestamp: fdate/ftime in FAT format */
    /* st_mtime conversion omitted for simplicity */
    return 0;
}

int fatfs_mkdir(const char *path, mode_t /*mode*/)
{
    if (!s_mounted) { errno = ENODEV; return -1; }
    char fpath[256];
    translate_path(fpath, sizeof(fpath), path);
    FRESULT fr = f_mkdir(fpath);
    if (fr != FR_OK) { errno = fresult_to_errno(fr); return -1; }
    return 0;
}

int fatfs_rename(const char *oldp, const char *newp)
{
    if (!s_mounted) { errno = ENODEV; return -1; }
    char fp_old[256], fp_new[256];
    translate_path(fp_old, sizeof(fp_old), oldp);
    translate_path(fp_new, sizeof(fp_new), newp);
    FRESULT fr = f_rename(fp_old, fp_new);
    if (fr != FR_OK) { errno = fresult_to_errno(fr); return -1; }
    return 0;
}

int fatfs_remove(const char *path)
{
    if (!s_mounted) { errno = ENODEV; return -1; }
    char fpath[256];
    translate_path(fpath, sizeof(fpath), path);
    FRESULT fr = f_unlink(fpath);
    if (fr != FR_OK) { errno = fresult_to_errno(fr); return -1; }
    return 0;
}

int fatfs_access(const char *path, int /*mode*/)
{
    struct stat st;
    return fatfs_stat(path, &st);
}

char *fatfs_getcwd(char *buf, size_t size)
{
    if (!s_mounted) { errno = ENODEV; return nullptr; }
    /* FatFs getcwd returns "0:/path" - strip the drive prefix */
    char tmp[256];
    FRESULT fr = f_getcwd(tmp, sizeof(tmp));
    if (fr != FR_OK) { errno = fresult_to_errno(fr); return nullptr; }
    /* "0:/foo" -> "/foo" */
    const char *p = strchr(tmp, ':');
    const char *src = p ? p + 1 : tmp;
    strncpy(buf, src, size - 1);
    buf[size - 1] = '\0';
    return buf;
}
