/*
 * src/include/dirent.h
 *
 * POSIX dirent.h for Circle bare-metal.
 * Provides the DIR and struct dirent types backed by FatFs.
 * The actual implementations are in fatfs_posix.cpp.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define DT_UNKNOWN  0
#define DT_REG      8
#define DT_DIR      4
#define DT_LNK      10

struct dirent {
    unsigned char   d_type;
    char            d_name[256];
};

/* Opaque DIR handle - backed by FatFsDIRWrapper in fatfs_posix.cpp */
typedef struct _FatFsDIROpaque {
    unsigned int magic;
    unsigned char _data[512]; /* enough for FatFs FF_DIR + dirent */
} DIR;

extern DIR            *opendir (const char *path);
extern struct dirent  *readdir (DIR *dp);
extern int             closedir(DIR *dp);
extern void            rewinddir(DIR *dp);

#ifdef __cplusplus
}
#endif
