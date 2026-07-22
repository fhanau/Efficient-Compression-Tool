/* zutil.c -- target dependent utility functions for the compression library
 * Copyright (C) 1995-2026 Jean-loup Gailly
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "zutil.h"

z_const char * const z_errmsg[10] = {
(z_const char *)"need dictionary",     /* Z_NEED_DICT       2  */
(z_const char *)"stream end",          /* Z_STREAM_END      1  */
(z_const char *)"",                    /* Z_OK              0  */
(z_const char *)"file error",          /* Z_ERRNO         (-1) */
(z_const char *)"stream error",        /* Z_STREAM_ERROR  (-2) */
(z_const char *)"data error",          /* Z_DATA_ERROR    (-3) */
(z_const char *)"insufficient memory", /* Z_MEM_ERROR     (-4) */
(z_const char *)"buffer error",        /* Z_BUF_ERROR     (-5) */
(z_const char *)"incompatible version",/* Z_VERSION_ERROR (-6) */
(z_const char *)""};

void* ZLIB_INTERNAL zcalloc(void* opaque, unsigned items, unsigned size) {
    (void)opaque;
    return (void*)malloc(items * size);
}

void ZLIB_INTERNAL zcfree (void* opaque, void* ptr) {
    (void)opaque;
    free(ptr);
}
