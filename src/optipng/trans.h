/*
 * trans.h
 * Image transformations.
 *
 * Copyright (C) 2011-2012 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file.
 */

/*Modified by Felix Hanau.*/

#ifndef OPNGTRANS_TRANS_H
#define OPNGTRANS_TRANS_H

#include "../libpng/png.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 The transformer type.

 A transformer object can be used to apply transformations to
 the image properties or to the image contents.

 Currently-supported transformations are erasing the alpha channel
 and stripping metadata.
*/
typedef struct opng_transformer opng_transformer_t;

/*
 * The transformer structure.
 */
struct opng_transformer
{
    int strip_chunks;
    int strip_apng;
};

#ifdef __cplusplus
}  /* extern "C" */
#endif


#endif  /* OPNGTRANS_TRANS_H */
