/*
 * Base/FatFsFile.h
 *
 * Circle bare-metal FatFs file wrapper for SimCoupe.
 *
 * On Circle we can't use the standard fopen/FILE* because there's no OS.
 * Instead we subclass a thin C++ wrapper around FatFs FIL objects.
 *
 * Usage:
 *   - Include this header in SimCoupe files that need file I/O on Circle.
 *   - FatFsFile replaces FILE* for all SimCoupe file operations.
 *   - The #ifdef __circle__ blocks in Stream.cpp, Util.h etc. use this.
 *
 * Path convention: all paths are absolute from "/" which maps to "0:/" on SD.
 */
#pragma once

#ifdef __circle__

// Rename FatFs DIR to avoid conflict with any POSIX DIR
#define DIR FF_DIR_TYPE
#include <ff.h>
#undef DIR

#include <string.h>
#include <stdlib.h>

// Path translation: "/foo/bar" -> "0:/foo/bar"
inline void fatfs_xlat(char* dst, size_t dsz, const char* src)
{
    if (!src || !src[0]) { strncpy(dst, "0:/", dsz); return; }
    if (src[0] == '/') snprintf(dst, dsz, "0:%s", src);
    else               snprintf(dst, dsz, "0:/%s", src);
}

// ---- FATFS_FILE: thin wrapper around FIL ----

struct FATFS_FILE
{
    FIL   fil;
    bool  eof   = false;
    bool  error = false;
};

// Open modes: r, rb, w, wb, r+b, w+b, a, ab
inline FATFS_FILE* fatfs_file_open(const char* path, const char* mode)
{
    BYTE flags = 0;
    if (strchr(mode, 'r')) flags |= FA_READ;
    if (strchr(mode, 'w')) flags |= FA_WRITE | FA_CREATE_ALWAYS;
    if (strchr(mode, 'a')) flags |= FA_WRITE | FA_OPEN_APPEND;
    if (strchr(mode, '+')) flags |= FA_READ | FA_WRITE;
    if (!flags) flags = FA_READ;

    char fpath[256];
    fatfs_xlat(fpath, sizeof(fpath), path);

    FATFS_FILE* f = new FATFS_FILE;
    FRESULT fr = f_open(&f->fil, fpath, flags);
    if (fr != FR_OK) { delete f; return nullptr; }
    return f;
}

inline void fatfs_file_close(FATFS_FILE* f)
{
    if (f) { f_close(&f->fil); delete f; }
}

inline size_t fatfs_file_read(void* buf, size_t size, size_t count, FATFS_FILE* f)
{
    if (!f || f->eof) return 0;
    UINT br = 0;
    FRESULT fr = f_read(&f->fil, buf, (UINT)(size * count), &br);
    if (fr != FR_OK) { f->error = true; return 0; }
    if (br < size * count) f->eof = true;
    return size ? br / size : 0;
}

inline size_t fatfs_file_write(const void* buf, size_t size, size_t count, FATFS_FILE* f)
{
    if (!f) return 0;
    UINT bw = 0;
    f_write(&f->fil, buf, (UINT)(size * count), &bw);
    return size ? bw / size : 0;
}

inline int fatfs_file_seek(FATFS_FILE* f, long offset, int whence)
{
    if (!f) return -1;
    FSIZE_t pos;
    switch (whence) {
    case 0: pos = (FSIZE_t)offset; break;
    case 1: pos = f_tell(&f->fil) + (FSIZE_t)offset; break;
    case 2: pos = f_size(&f->fil) + (FSIZE_t)offset; break;
    default: return -1;
    }
    f->eof = false;
    return f_lseek(&f->fil, pos) == FR_OK ? 0 : -1;
}

inline long fatfs_file_tell(FATFS_FILE* f)
{
    return f ? (long)f_tell(&f->fil) : -1;
}

inline bool fatfs_file_eof(FATFS_FILE* f)
{
    return !f || f->eof;
}

inline bool fatfs_file_error(FATFS_FILE* f)
{
    return !f || f->error;
}

inline bool fatfs_file_exists(const char* path)
{
    char fpath[256];
    fatfs_xlat(fpath, sizeof(fpath), path);
    FILINFO fi;
    return f_stat(fpath, &fi) == FR_OK;
}

inline long fatfs_file_size(FATFS_FILE* f)
{
    return f ? (long)f_size(&f->fil) : 0;
}

inline int fatfs_file_putc(int c, FATFS_FILE* f)
{
    if (!f) return -1;
    uint8_t byte = (uint8_t)c;
    UINT bw = 0;
    if (f_write(&f->fil, &byte, 1, &bw) != FR_OK || bw != 1)
        return -1;
    return (int)byte;
}

inline int fatfs_file_getc(FATFS_FILE* f)
{
    if (!f) return -1;
    uint8_t byte;
    UINT br = 0;
    if (f_read(&f->fil, &byte, 1, &br) != FR_OK || br != 1) {
        if (br == 0) f->eof = true;
        return -1;
    }
    return (int)byte;
}

inline int fatfs_file_flush(FATFS_FILE* f)
{
    if (!f) return -1;
    return f_sync(&f->fil) == FR_OK ? 0 : -1;
}

inline char* fatfs_file_gets(char* buf, int n, FATFS_FILE* f)
{
    if (!f || !buf || n <= 0) return nullptr;
    int i = 0;
    while (i < n - 1) {
        int c = fatfs_file_getc(f);
        if (c < 0) break;
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0) return nullptr;
    buf[i] = '\0';
    return buf;
}

// fileno is meaningless on FatFs (no file descriptors). Return a dummy fd.
// fstat on this dummy fd: we intercept via macro to get size from FATFS_FILE.
inline int fatfs_file_fileno(FATFS_FILE* f)
{
    // Return address-based pseudo fd (unique, non-negative)
    return f ? (int)(uintptr_t)f : -1;
}

// fstat-like: fills st_size from the FatFs file object.
// We store a thread-local (single-threaded on Circle) pointer to the last
// FATFS_FILE* passed to fileno, so fstat can retrieve the size.
// This is a hack but only HardDisk.cpp uses fstat(fileno(f), &st).
#include <sys/stat.h>
static inline FATFS_FILE* _fatfs_last_file;

inline int fatfs_file_fileno_save(FATFS_FILE* f)
{
    _fatfs_last_file = f;
    return f ? 0 : -1;
}

inline int fatfs_file_fstat(int /*fd*/, struct stat* st)
{
    if (!_fatfs_last_file || !st) return -1;
    memset(st, 0, sizeof(*st));
    st->st_size = (off_t)f_size(&_fatfs_last_file->fil);
    st->st_mode = S_IFREG | 0644;
    return 0;
}

// ---- FatFs directory iterator for ghc::filesystem ----
// Used in OSD.cpp / GUI.cpp when listing disk files

struct FATFS_DIR_ITER
{
    FF_DIR_TYPE dir;
    FILINFO     info;
    bool        valid = false;
};

inline FATFS_DIR_ITER* fatfs_dir_open(const char* path)
{
    char fpath[256];
    fatfs_xlat(fpath, sizeof(fpath), path);
    FATFS_DIR_ITER* d = new FATFS_DIR_ITER;
    if (f_opendir(&d->dir, fpath) != FR_OK) { delete d; return nullptr; }
    // Pre-read first entry
    d->valid = (f_readdir(&d->dir, &d->info) == FR_OK && d->info.fname[0] != '\0');
    return d;
}

inline bool fatfs_dir_next(FATFS_DIR_ITER* d)
{
    if (!d) return false;
    d->valid = (f_readdir(&d->dir, &d->info) == FR_OK && d->info.fname[0] != '\0');
    return d->valid;
}

inline bool fatfs_dir_valid(const FATFS_DIR_ITER* d)
{
    return d && d->valid;
}

inline const char* fatfs_dir_name(const FATFS_DIR_ITER* d)
{
    return (d && d->valid) ? d->info.fname : "";
}

inline bool fatfs_dir_is_dir(const FATFS_DIR_ITER* d)
{
    return d && d->valid && (d->info.fattrib & AM_DIR);
}

inline void fatfs_dir_close(FATFS_DIR_ITER* d)
{
    if (d) { f_closedir(&d->dir); delete d; }
}

// ---- Convenience: check if path exists (file or dir) ----
inline bool fatfs_path_exists(const char* path)
{
    if (!path || !path[0] || strcmp(path, "/") == 0) return true;
    char fpath[256];
    fatfs_xlat(fpath, sizeof(fpath), path);
    FILINFO fi;
    return f_stat(fpath, &fi) == FR_OK;
}

inline bool fatfs_is_directory(const char* path)
{
    if (!path || !path[0] || strcmp(path, "/") == 0) return true;
    char fpath[256];
    fatfs_xlat(fpath, sizeof(fpath), path);
    FILINFO fi;
    return (f_stat(fpath, &fi) == FR_OK) && (fi.fattrib & AM_DIR);
}

inline bool fatfs_mkdir(const char* path)
{
    char fpath[256];
    fatfs_xlat(fpath, sizeof(fpath), path);
    return f_mkdir(fpath) == FR_OK;
}

// Provide fgets overload for FATFS_FILE* so std::fgets(buf, n, file) works
// when FILE is #defined to FATFS_FILE. This must be a real function (not macro)
// because macros can't intercept std::fgets.
inline char* fgets(char* buf, int n, FATFS_FILE* f)
{
    return fatfs_file_gets(buf, n, f);
}

namespace std {
    inline char* fgets(char* buf, int n, FATFS_FILE* f)
    {
        return fatfs_file_gets(buf, n, f);
    }
}

#endif // __circle__
