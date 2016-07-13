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
  Maximum amount of blocks to split into (0 for unlimited, but this can give
  extreme results that hurt compression on some files). Default value: 15.
  */
  unsigned blocksplittingmax;

  unsigned filter_style;

  /* Don't try to use dynamic block under this size. */
  unsigned skipdynamic;

  /* Try static block if dynamic block is smaller than this. Needs to be much higher than skipdynamic. */
  unsigned trystatic;

  /* Don't blocksplit under this size. */
  unsigned noblocksplit;

  /* Don't blocksplit under this size (LZ77'd data). */
  unsigned noblocksplitlz;

  /* Number of parallel block split point searches*/
  unsigned num;

  /* Search longer for best huffman header compression:
     Value outstream important less important
     0:    1         1         1
     1:    10        10        1
     2:    32        32        32
   */
  unsigned searchext;

  unsigned reuse_costmodel;

  /*When using more than one iteration, this will save the found matches on the first run so they don't need to be found again. Uses large amounts of memory.*/
  unsigned useCache;

  /*Use per block multithreading*/
  unsigned multithreading;

  /*Use tuning for PNG files*/
  unsigned isPNG;

  unsigned midsplit;

  /*Replace short lengths with literals if that improves compression. Higher numbers mean more aggressive behaviour.*/
  unsigned replaceCodes;

  /*Block split twice*/
  unsigned twice;

  /*Sorry, can't figure out a better name. Leads to better final LZ costmodel*/
  unsigned ultra;

  /*Use greedy search instead of lazy search above this value.*/
  unsigned greed;

  /*Use shannon entropy instead of real code lengths in blocksplitting.*/
  unsigned entropysplit;

  /*Use advanced huffman and header optimizations.*/
  unsigned advanced;
} ZopfliOptions;

typedef struct ZopfliOptionsMin {
  int numiterations;
  unsigned searchext;
  unsigned short filter_style;
  unsigned noblocksplit;
  unsigned trystatic;
  unsigned skipdynamic;
  unsigned noblocksplitlz;
} ZopfliOptionsMin;
/* Initializes options with default values. */
void ZopfliInitOptions(ZopfliOptions* options, unsigned mode, unsigned multithreading, unsigned isPNG);

/* Output format */
typedef enum {
  ZOPFLI_FORMAT_GZIP,
  ZOPFLI_FORMAT_ZLIB,
  ZOPFLI_FORMAT_DEFLATE,
  ZOPFLI_FORMAT_ZIP
} ZopfliFormat;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  /* ZOPFLI_ZOPFLI_H_ */
