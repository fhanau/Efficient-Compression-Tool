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

#include "cache.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef ZOPFLI_LONGEST_MATCH_CACHE

#define CacheBytes ZOPFLI_CACHE_LENGTH * 3

void ZopfliInitCache(size_t blocksize, ZopfliLongestMatchCache* lmc) {
  lmc->length = (unsigned short*)malloc(sizeof(unsigned short) * blocksize);
  lmc->dist = (unsigned short*)calloc(blocksize, sizeof(unsigned short));
  /* Rather large amount of memory. */
  lmc->sublen = (unsigned char*)calloc(CacheBytes * blocksize, 1);
  /* length > 0 and dist 0 is invalid combination, which indicates on purpose
    that this cache value is not filled in yet. */
  for (size_t i = 0; i < blocksize; i++) lmc->length[i] = 1;
}

void ZopfliCleanCache(ZopfliLongestMatchCache* lmc) {
  free(lmc->length);
  free(lmc->dist);
  free(lmc->sublen);
  free(lmc);
}


void ZopfliSublenToCache(const unsigned short* sublen,
                         size_t pos, unsigned short length,
                         ZopfliLongestMatchCache* lmc) {

#if ZOPFLI_CACHE_LENGTH == 0
  return;
#endif

  unsigned char* cache = &lmc->sublen[CacheBytes * pos];
  unsigned char j = 0;

  for (unsigned short i = 3; i <= length; i++) {
    if (i == length || sublen[i] != sublen[i + 1]) {
      cache[j] = i - 3;
      cache[++j] = sublen[i] % 256;
      cache[++j] = (sublen[i] >> 8) % 256;
      if (++j >= CacheBytes) break;
    }
  }
  if (j < CacheBytes)
    cache[CacheBytes - 9] = length - 3;
}

void ZopfliCacheToSublen(const ZopfliLongestMatchCache* lmc, size_t pos, unsigned short* sublen) {
#if ZOPFLI_CACHE_LENGTH == 0
  return;
#endif
    size_t i;
    unsigned maxlength = ZopfliMaxCachedSublen(lmc, pos);
    unsigned prevlength = 0;

    unsigned char* cache = &lmc->sublen[CacheBytes * pos];
    for (unsigned char j = 0; j < ZOPFLI_CACHE_LENGTH; j++) {
        unsigned lengthtwo = cache[j * 3] + 3;
        unsigned dist = cache[j * 3 + 1] + 256 * cache[j * 3 + 2];
        for (i = prevlength; i <= lengthtwo; i++) {
            sublen[i] = dist;
        }
        if (lengthtwo == maxlength) break;
        prevlength = lengthtwo + 1;
    }
}

/*
Returns the length up to which could be stored in the cache.
*/
unsigned ZopfliMaxCachedSublen(const ZopfliLongestMatchCache* lmc, size_t pos) {
#if ZOPFLI_CACHE_LENGTH == 0
  return 0;
#endif
  unsigned char* cache = &lmc->sublen[CacheBytes * pos];
  if (cache[1] == 0 && cache[2] == 0) return 0;  /* No sublen cached. */
  return cache[CacheBytes - 3] + 3;
}

#endif  /* ZOPFLI_LONGEST_MATCH_CACHE */
