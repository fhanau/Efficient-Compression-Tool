/* zutil.c -- target dependent utility functions for the compression library
 * Copyright (C) 1995-2005, 2010, 2011, 2012 Jean-loup Gailly.
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "zutil.h"

z_const char * const z_errmsg[10] = {
"need dictionary",     /* Z_NEED_DICT       2  */
"stream end",          /* Z_STREAM_END      1  */
"",                    /* Z_OK              0  */
"file error",          /* Z_ERRNO         (-1) */
"stream error",        /* Z_STREAM_ERROR  (-2) */
"data error",          /* Z_DATA_ERROR    (-3) */
"insufficient memory", /* Z_MEM_ERROR     (-4) */
"buffer error",        /* Z_BUF_ERROR     (-5) */
"incompatible version",/* Z_VERSION_ERROR (-6) */
""};

/* exported to allow conversion of error code to string for compress() and
 * uncompress()
 */
const char* zError(int err)
{
    return ERR_MSG(err);
}

void* ZLIB_INTERNAL zcalloc (void* opaque, unsigned items, unsigned size)
{
    if (opaque) items += size - size; /* make compiler happy */
    return (void*)malloc(items * size);
}

void ZLIB_INTERNAL zcfree (void* opaque, void* ptr)
{
    free(ptr);
    if (opaque) return; /* make compiler happy */
}
