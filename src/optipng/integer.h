/*
 * optk/integer.h
 * Integer types and associated macros.
 *
 * Copyright (C) 2011-2012 Cosmin Truta.
 *
 * This software is distributed under the zlib license.
 * Please see the accompanying LICENSE file.
 */

/*Modified by Felix Hanau.*/

#ifndef OPTK_INTEGER_H_
#define OPTK_INTEGER_H_

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS 1
#endif
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif
#include <stdint.h>
#include <inttypes.h>

/*
 * Use the Standard C minimum-width integer types.
 *
 * The exact-width types intN_t and uintN_t are not guaranteed to exist
 * for all N=8,16,32,64. For example, certain dedicated CPUs may handle
 * 32-bit and 64-bit integers only, with sizeof(int)==sizeof(char)==1.
 * On the other hand, any minimum-width integer type is guaranteed to be
 * the same as its exact-width counterpart, when the latter does exist.
 *
 * Since exact-width integer semantics is not strictly required, we use
 * int_leastN_t and uint_leastN_t, which are required to exist for all
 * N=8,16,32,64.
 */

typedef int_least64_t  optk_int64_t;
typedef uint_least64_t optk_uint64_t;

#define OPTK_INT64_MAX  INT_LEAST64_MAX

#endif  /* OPTK_INTEGER_H_ */
