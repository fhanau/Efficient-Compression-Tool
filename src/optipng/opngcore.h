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

/**
 * Prints a warning message (@c level = @c OPNG_MSG_WARNING) to the logger.
 **/
void opng_warning(const char *fname, const char *message);

/**
 * Prints an error message (@c level = @c OPNG_MSG_ERROR), optionally
 * accompanied by a submessage, to the logger.
 **/
void opng_error(const char *fname, const char *message);

#define OPNG_ASSERT(condition, message) ((void)(0))

#ifdef __cplusplus
}  /* extern "C" */
#endif


#endif  /* OPNGLIB_OPNGCORE_H */
