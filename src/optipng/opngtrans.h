/**
 * @file opnglib/opngtrans.h
 *
 * @brief
 * OPNGTRANS is a PNG Transformer.
 *
 * Copyright (C) 2001-2011 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file, or visit
 * http://www.opensource.org/licenses/zlib-license.php
 **/

/*Modified by Felix Hanau.*/

#ifndef OPNGLIB_OPNGTRANS_H
#define OPNGLIB_OPNGTRANS_H

#include "opngcore.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * The transformer type.
 *
 * A transformer object can be used to apply transformations to
 * the image properties or to the image contents.
 *
 * Currently-supported transformations are erasing the alpha channel
 * and stripping metadata.
 **/
typedef struct opng_transformer opng_transformer_t;

/**
 * Creates a transformer object.
 * @return the transformer created, or @c NULL on failure.
 **/
opng_transformer_t * opng_create_transformer();

/* Protect or strip a chunk*/
void opng_transform_chunk (opng_transformer_t *transformer, const char *chunk, int strip);

/**
 * Seals a transformer object.
 * @param transformer
 *        the transformer object to be sealed.
 * @return the sealed transformer.
 **/
const opng_transformer_t * opng_seal_transformer(opng_transformer_t *transformer);

/**
 * Destroys a transformer object.
 * @param transformer
 *        the transformer object to be destroyed.
 **/
void opng_destroy_transformer(opng_transformer_t *transformer);


#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* OPNGLIB_OPNGTRANS_H */
