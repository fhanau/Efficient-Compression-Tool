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

/*
Several utilities, including: #defines to try different compression results,
basic deflate specification values and generic program options.
*/

/*Modified by Felix Hanau*/

#ifndef ZOPFLI_UTIL_H_
#define ZOPFLI_UTIL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

/* Minimum and maximum length that can be encoded in deflate. */
#define ZOPFLI_MAX_MATCH 258
#define ZOPFLI_MIN_MATCH 3

/*
The window size for deflate. Must be a power of two. This should be 32768, the
maximum possible by the deflate spec. Anything less hurts compression more than
speed.
*/
#define ZOPFLI_WINDOW_SIZE 32768

/*
The window mask used to wrap indices into the window. This is why the
window size must be a power of two.
*/
#define ZOPFLI_WINDOW_MASK (ZOPFLI_WINDOW_SIZE - 1)

/*
A block structure of huge, non-smart, blocks to divide the input into, to allow
operating on huge files without exceeding memory, such as the 1GB wiki9 corpus.
The whole compression algorithm, including the smarter block splitting, will
be executed independently on each huge block.
Dividing into huge blocks hurts compression, but not much relative to the size.
Set this to, for example, 20MB (20000000). Set it to 0 to disable master blocks.
*/
#define ZOPFLI_MASTER_BLOCK_SIZE 5000000

/*
Used to initialize costs for example
*/
#define ZOPFLI_LARGE_FLOAT 1e30

/*
Gets the symbol for the given length, cfr. the DEFLATE spec.
Returns the symbol in the range [257-285] (inclusive)
*/
unsigned ZopfliGetLengthSymbol(unsigned l);

/* Gets the amount of extra bits for the given length, cfr. the DEFLATE spec. */
unsigned ZopfliGetLengthExtraBits(unsigned l);

/* Gets value of the extra bits for the given length, cfr. the DEFLATE spec. */
unsigned ZopfliGetLengthExtraBitsValue(unsigned l);

/* Gets the symbol for the given dist, cfr. the DEFLATE spec. */
int ZopfliGetDistSymbol(int dist);

/* Gets the amount of extra bits for the given dist, cfr. the DEFLATE spec. */
unsigned ZopfliGetDistExtraBits(unsigned dist);

/* Gets value of the extra bits for the given dist, cfr. the DEFLATE spec. */
unsigned ZopfliGetDistExtraBitsValue(unsigned dist);

#ifdef __GNUC__
#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#else
#define likely(x)      x
#define unlikely(x)    x
#endif

/*
Appends value to dynamically allocated memory, doubling its allocation size
whenever needed.

value: the value to append, type T
data: pointer to the dynamic array to append to, type T**
size: pointer to the size of the array to append to, type size_t*. This is the
size that you consider the array to be, not the internal allocation size.
Precondition: allocated size of data is at least a power of two greater than or
equal than *size.
*/
#ifdef __cplusplus /* C++ cannot assign void* from malloc to *data */
#define ZOPFLI_APPEND_DATA(/* T */ value, /* T** */ data, /* size_t* */ size) {\
  if (!((*size) & ((*size) - 1))) {\
    /*double alloc size if it's a power of two*/\
    void** data_void = reinterpret_cast<void**>(data);\
    *data_void = (*size) == 0 ? malloc(sizeof(**data))\
                              : realloc((*data), (*size) * 2 * sizeof(**data));\
  }\
  (*data)[(*size)] = (value);\
  (*size)++;\
}
#else /* C gives problems with strict-aliasing rules for (void**) cast */
#define ZOPFLI_APPEND_DATA(/* T */ value, /* T** */ data, /* size_t* */ size) {\
  if (!((*size) & ((*size) - 1))) {\
    /*double alloc size if it's a power of two*/\
    (*data) = (*size) == 0 ? malloc(sizeof(**data))\
                           : realloc((*data), (*size) * 2 * sizeof(**data));\
  }\
  (*data)[(*size)] = (value);\
  (*size)++;\
}
#endif

#ifdef __cplusplus
}
#endif

#endif  /* ZOPFLI_UTIL_H_ */
