/*
Copyright 2011 Google Inc. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Author: lode.vandevenne@gmail.com (Lode Vandevenne)
Author: jyrki.alakuijala@gmail.com (Jyrki Alakuijala)
*/

/*Modified by Felix Hanau*/

#ifndef ZOPFLI_ZOPFLI_H_
#define ZOPFLI_ZOPFLI_H_

#include <stddef.h>
#include <stdlib.h> /* for size_t */

#ifdef __cplusplus
extern "C" {
#endif

/*
Options used throughout the program.
*/
typedef struct ZopfliOptions {

  /*
  Maximum amount of times to rerun forward and backward pass to optimize LZ77
  compression cost. Good values: 10, 15 for small files, 5 for files over
  several MB in size or it will be too slow.
  */
  int numiterations;

  /*
  If true, splits the data in multiple deflate blocks with optimal choice
  for the block boundaries. Block splitting gives better compression. Default:
  true (1).
  */
  int blocksplitting;

  /*
  If true, chooses the optimal block split points only after doing the iterative
  LZ77 compression. If false, chooses the block split points first, then does
  iterative LZ77 on each individual block. Depending on the file, either first
  or last gives the best compression. Default: false (0).
  */
  int blocksplittinglast;

  /*
  Maximum amount of blocks to split into (0 for unlimited, but this can give
  extreme results that hurt compression on some files). Default value: 15.
  */
  int blocksplittingmax;

  /*
  Limit the max hash chain hits for this hash value. This has an effect only
  on files where the hash value is the same very often. On these files, this
  gives worse compression (the value should ideally be 32768, which is the
  ZOPFLI_WINDOW_SIZE, while zlib uses 4096 even for best level), but makes it
  faster on some specific files.
  Good value: e.g. 8192.
  */
  int chain_length;

} ZopfliOptions;

/* Initializes options with default values. */
void ZopfliInitOptions(ZopfliOptions* options, int mode);

/* Output format */
typedef enum {
    ZOPFLI_FORMAT_GZIP,
    ZOPFLI_FORMAT_ZLIB,
    ZOPFLI_FORMAT_DEFLATE
} ZopfliFormat;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  /* ZOPFLI_ZOPFLI_H_ */
