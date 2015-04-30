/**
 * @file opnglib/opngcore.h
 *
 * @brief
 * OPNGCORE is a PNG Compression Optimization and Recovery Engine.
 *
 * Copyright (C) 2001-2012 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file, or visit
 * http://www.opensource.org/licenses/zlib-license.php
 **/

/*Modified by Felix Hanau.*/

#ifndef OPNGLIB_OPNGCORE_H
#define OPNGLIB_OPNGCORE_H

#include <stdlib.h>


#ifdef __cplusplus
extern "C" {
#endif

/*** Types ***/

/**
 * The optimizer type.
 *
 * An optimizer object can optimize one or more images, sequentially.
 *
 * @bug
 * Multiple optimizer objects are designed to run in parallel
 * (but they don't, yet).
 **/
typedef struct opng_optimizer opng_optimizer_t;

/**
 * The image type.
 *
 * Image objects are currently used internally only.
 * There is no public API to access them yet.
 *
 * @todo
 * 3rd-party applications may wish to use this type in order to create
 * optimized PNG files from in-memory images (e.g. in-memory compressed
 * files, raw pixel data, BMP handles, etc.).
 * Your contribution will be appreciated ;-)
 **/
typedef struct opng_image opng_image_t;

/*** Logging ***/

/**
 * Message severity levels.
 * Each level allows up to 10 custom sub-levels. The level numbers
 * are loosely based on the Python logging module.
 **/
enum
{
    OPNG_MSG_INFO     = 10,
    OPNG_MSG_WARNING  = 20,
    OPNG_MSG_ERROR    = 30,
    OPNG_MSG_DEFAULT  = OPNG_MSG_WARNING
};

/**
 * Prints a printf-formatted informational message (@c level = @c OPNG_MSG_INFO)
 * to the logger.
 **/
void opng_printf(const char *format, ...);

/**
 * Prints a warning message (@c level = @c OPNG_MSG_WARNING) to the logger.
 **/
void opng_warning(const char *fname, const char *message);

/**
 * Prints an error message (@c level = @c OPNG_MSG_ERROR), optionally
 * accompanied by a submessage, to the logger.
 **/
void opng_error(const char *fname, const char *message);


/*** PNG encoding ***/

/**
 * Encoder constants and limits.
 **/
enum
{
    OPNG_FILTER_MIN            = 0,
    OPNG_FILTER_MAX            = 5,
};

#define OPNG_ASSERT(condition, message) ((void)(0))
#define OPNG_WEAK_ASSERT(condition, message) ((void)(0))

#ifdef __cplusplus
}  /* extern "C" */
#endif


#endif  /* OPNGLIB_OPNGCORE_H */
