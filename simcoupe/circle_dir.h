/*
 * circle_dir.h
 *
 * Simple directory listing API for SimCoupe bare-metal.
 * Uses our FatFs-backed opendir/readdir without exposing
 * the DIR type (which conflicts with FatFs FF_DIR).
 *
 * Designed to be included from C++ files that also include
 * Circle headers (which define their own DIR types).
 */
#pragma once

#ifdef __circle__

#ifdef __cplusplus
extern "C" {
#endif

#define CIRCLE_DIR_MAXNAME 256

typedef struct {
    char  name[CIRCLE_DIR_MAXNAME];
    int   is_dir;
} circle_dir_entry_t;

/* Opaque handle */
typedef void* circle_dir_t;

circle_dir_t  circle_dir_open(const char *path);
int           circle_dir_read(circle_dir_t handle, circle_dir_entry_t *entry);
void          circle_dir_close(circle_dir_t handle);

#ifdef __cplusplus
}
#endif

#endif /* __circle__ */
