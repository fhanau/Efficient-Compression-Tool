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


#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "blocksplitter.h"
#include "deflate.h"
#include "katajainen.h"
#include "util.h"
#include "squeeze.h"
#include "match.h"
#include "../LzFind.h"

static void CopyStats(const SymbolStats* source, SymbolStats* dest) {
  memcpy(dest->litlens, source->litlens, 288 * sizeof(dest->litlens[0]));
  memcpy(dest->dists, source->dists, 32 * sizeof(dest->dists[0]));

  memcpy(dest->ll_symbols, source->ll_symbols,
         288 * sizeof(dest->ll_symbols[0]));
  memcpy(dest->d_symbols, source->d_symbols, 32 * sizeof(dest->d_symbols[0]));
}

static void MixCostmodels(const SymbolStats* src, SymbolStats* prod, float share){
  for (unsigned i = 0; i < 288; i++) {
    prod->ll_symbols[i] = prod->ll_symbols[i] * (1.0 - share) + src->ll_symbols[i] * share;
  }
  for (unsigned i = 0; i < 32; i++) {
    prod->d_symbols[i] = prod->d_symbols[i] * (1.0 - share) + src->d_symbols[i] * share;
  }
}

/* Adds the bit lengths. */
static void AddWeighedStatFreqs(const SymbolStats* stats1, float w1,
                                const SymbolStats* stats2, float w2,
                                SymbolStats* result) {
  size_t i;
  for (i = 0; i < 288; i++) {
    result->litlens[i] = (size_t) (stats1->litlens[i] * w1 + stats2->litlens[i] * w2);
  }
  for (i = 0; i < 32; i++) {
    result->dists[i] = (size_t) (stats1->dists[i] * w1 + stats2->dists[i] * w2);
  }
  result->litlens[256] = 1;  /* End symbol. */
}

typedef struct RanState {
  unsigned m_w, m_z;
} RanState;

static void InitRanState(RanState* state) {
  state->m_w = 1;
  state->m_z = 2;
}

/* Get random number: "Multiply-With-Carry" generator of G. Marsaglia */
static unsigned Ran(RanState* state) {
  state->m_z = 36969 * (state->m_z & 65535) + (state->m_z >> 16);
  state->m_w = 18000 * (state->m_w & 65535) + (state->m_w >> 16);
  return (state->m_z << 16) + state->m_w;  /* 32-bit result. */
}

static void RandomizeFreqs(RanState* state, size_t* freqs, int n) {
  for (int i = 0; i < n; i++) {
    if ((Ran(state) >> 4) % 3 == 0) freqs[i] = freqs[Ran(state) % n];
  }
}

static void RandomizeStatFreqs(RanState* state, SymbolStats* stats) {
  RandomizeFreqs(state, stats->litlens, 288);
  RandomizeFreqs(state, stats->dists, 32);
  stats->litlens[256] = 1;  /* End symbol. */
}

/*
Performs the forward pass for "squeeze". Gets the most optimal length to reach
every byte from a previous byte, using cost calculations.
s: the ZopfliBlockState
in: the input data array
instart: where to start
inend: where to stop (not inclusive)
costcontext: abstract context for the costmodel function
length_array: output array of size (inend - instart) which will receive the best
    length to reach this byte from a previous byte.
*/

typedef struct _LZCache{
  unsigned* cache;
  size_t size;
  size_t pointer;
} LZCache;

static void CreateCache(size_t len, LZCache* c){
  c->size = len + (ZOPFLI_MAX_MATCH - ZOPFLI_MIN_MATCH + 1) * 2;
  c->cache = (unsigned*)malloc(c->size * sizeof(unsigned));
  if (!c->cache){
    exit(1);
  }
  c->pointer = 0;
}

static void CleanCache(LZCache* c){
  free(c->cache);
}

CMatchFinder mf;
int right;
static void GetBestLengths(const ZopfliOptions* options,
                             const unsigned char* in,
                             size_t instart, size_t inend,
                             SymbolStats* costcontext, unsigned* length_array, unsigned char storeincache, LZCache* c, unsigned mfinexport) {
  size_t i;

  /*TODO: Put this in seperate function*/
  float litlentable [259];
  float* disttable = (float*)malloc(ZOPFLI_WINDOW_SIZE * sizeof(float));
  float* literals;
  if (!disttable){
    exit(1);
  }
  if (costcontext){  /* Dynamic Block */

    literals = costcontext->ll_symbols;
    for (i = 3; i < 259; i++){
      litlentable[i] = costcontext->ll_symbols[ZopfliGetLengthSymbol(i)] + ZopfliGetLengthExtraBits(i);
    }
    for (i = 0; i < 513; i++){
      disttable[i] = costcontext->d_symbols[ZopfliGetDistSymbol(i)] + ZopfliGetDistExtraBits(i);
    }
    float counter = costcontext->d_symbols[18] + 8;
    for (; i < 769; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[19] + 8;
    for (; i < 1025; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[20] + 9;
    for (; i < 1537; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[21] + 9;
    for (; i < 2049; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[22] + 10;
    for (; i < 3073; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[23] + 10;
    for (; i < 4097; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[24] + 11;
    for (; i < 6145; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[25] + 11;
    for (; i < 8193; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[26] + 12;
    for (; i < 12289; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[27] + 12;
    for (; i < 16385; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[28] + 13;
    for (; i < 24577; i++){
      disttable[i] = counter;
    }
    counter = costcontext->d_symbols[29] + 13;
    for (; i < 32768; i++){
      disttable[i] = counter;
    }
  }
  else {
    literals = (float*)malloc(256 * sizeof(float));
    if (!literals){
      exit(1);
    }

    for (i = 0; i < 144; i++){
      literals[i] = 8;
    }
    for (; i < 256; i++){
      literals[i] = 9;
    }
    for (i = 3; i < 259; i++){
      litlentable[i] = 12 + (i > 114) + ZopfliGetLengthExtraBits(i);
    }
    for (i = 0; i < 1024; i++){
      disttable[i] = ZopfliGetDistExtraBits(i);
    }
    for (; i < 2049; i++){
      disttable[i] = 9;
    }
    for (; i < 4097; i++){
      disttable[i] = 10;
    }
    for (; i < 8193; i++){
      disttable[i] = 11;
    }
    for (; i < 16385; i++){
      disttable[i] = 12;
    }
    for (; i < 32768; i++){
      disttable[i] = 13;
    }
  }

  size_t blocksize = inend - instart;

  float* costs = (float*)malloc(sizeof(float) * (blocksize + 1));
  if (!costs) exit(1); /* Allocation failed. */
  costs[0] = 0;  /* Because it's the start. */
  memset(costs + 1, 127, sizeof(float) * blocksize);

  size_t windowstart = instart > ZOPFLI_WINDOW_SIZE ? instart - ZOPFLI_WINDOW_SIZE : 0;
  if (blocksize < ZOPFLI_MIN_MATCH){
    windowstart = instart;
  }

  CMatchFinder p;
  if (storeincache != 2){
    if (mfinexport & right){
      p = mf;
      p.bufend = &in[inend];

      Bt3Zip_MatchFinder_Skip(&p, ZOPFLI_MAX_MATCH);

      assert(p.buffer == &in[instart]);
    }
    else{
      p.buffer = &in[windowstart];
      p.bufend = &in[inend];

      MatchFinder_Create(&p);
      Bt3Zip_MatchFinder_Skip(&p, instart - windowstart);
    }
  }
  right = 0;

  unsigned* matches;
  if (!storeincache){
    unsigned matchesarr[(ZOPFLI_MAX_MATCH - ZOPFLI_MIN_MATCH + 1) * 2];
    matches = matchesarr;
  }

  unsigned notenoughsame = instart + ZOPFLI_MAX_MATCH;
  for (i = instart; i < inend; i++) {
    size_t j = i - instart;  /* Index in the costs array and length_array. */

    //You think ECT will choke on files with minimum entropy? Think again!
    if (i < inend - ZOPFLI_MAX_MATCH - 1 && i > notenoughsame && *(long*)&in[i - 200] == *(long*)&in[i - 8]){
      unsigned same = GetMatch(&in[i + 1], &in[i], &in[inend], &in[inend] - 8) - &in[i];
      /* If we're in a long repetition of the same character and have more than
       ZOPFLI_MAX_MATCH characters before and after our position. */
      unsigned same2 = GetMatch(&in[i + 1 - ZOPFLI_MAX_MATCH], &in[i - ZOPFLI_MAX_MATCH], &in[i + 1], &in[i] - 7) - &in[i - ZOPFLI_MAX_MATCH];
      if (same > ZOPFLI_MAX_MATCH
          && same2
          > ZOPFLI_MAX_MATCH) {
        unsigned match = same - ZOPFLI_MAX_MATCH;

        float symbolcost = costcontext ? costcontext->ll_symbols[285] + costcontext->d_symbols[0] : 13;
        /* Set the length to reach each one to ZOPFLI_MAX_MATCH, and the cost to
         the cost corresponding to that length. Doing this, we skip
         ZOPFLI_MAX_MATCH values to avoid calling ZopfliFindLongestMatch. */
        for (unsigned k = 0; k < match; k++) {
          costs[j + ZOPFLI_MAX_MATCH] = costs[j] + symbolcost;
          length_array[j + ZOPFLI_MAX_MATCH] = ZOPFLI_MAX_MATCH + (1 << 9);
          j++;
        }

        if (storeincache != 2){
          Bt3Zip_MatchFinder_Skip(&p, match);
        }
        notenoughsame = i + same + ZOPFLI_MAX_MATCH - 1;

        i += match;
      }
      else if (same <= ZOPFLI_MAX_MATCH){
        notenoughsame = i + same + ZOPFLI_MAX_MATCH - 1;
      }
      else{
        notenoughsame = i + ZOPFLI_MAX_MATCH - same2 < i + same2 - 1 ? i + ZOPFLI_MAX_MATCH - same2 : i + same2 - 1;
      }
    }

    int numPairs;
    if (!storeincache){
      numPairs = Bt3Zip_MatchFinder_GetMatches(&p, matches);
    }
    else{
      if (storeincache == 2){
        matches = c->cache + c->pointer;
        numPairs = *matches;
        matches++;
        c->pointer += numPairs + 1;
      }
      else{
        if (c->size < c->pointer + (ZOPFLI_MAX_MATCH - ZOPFLI_MIN_MATCH + 1) * 2 + 1){
          c->size *= 2;
          c->cache = realloc(c->cache, c->size * sizeof(unsigned));
        }
        matches = c->cache + c->pointer + 1;
        numPairs = Bt3Zip_MatchFinder_GetMatches(&p, matches);
        *(matches - 1) = numPairs;
        c->pointer += numPairs + 1;
      }
    }
    if (numPairs){
      const unsigned * mend = matches + numPairs;

      //It would be really nice to get this faster, but that seems impossible. Using AVX1 is slower.
      if (*(mend - 2) == ZOPFLI_MAX_MATCH && numPairs == 2){
        unsigned dist = matches[1];
        costs[j + ZOPFLI_MAX_MATCH] = costs[j] + disttable[dist] + litlentable[ZOPFLI_MAX_MATCH];
        length_array[j + ZOPFLI_MAX_MATCH] = ZOPFLI_MAX_MATCH + (dist << 9);
      }
#if 0 //More speed, less compression.
      else if (*(mend - 2) == ZOPFLI_MAX_MATCH){
        unsigned dist = matches[numPairs - 1];
        costs[j + ZOPFLI_MAX_MATCH] = costs[j] + disttable[dist] + litlentable[ZOPFLI_MAX_MATCH];
        length_array[j + ZOPFLI_MAX_MATCH] = ZOPFLI_MAX_MATCH + (dist << 9);
      }
      else if(*(mend - 2) > 100){
        unsigned match = *(mend - 2);
        unsigned dist = *(mend - 1);
        float x = costs[j] + disttable[dist] + litlentable[match];
        if (x < costs[j + match]){
          costs[j + match] = x;
          length_array[j + match] = match + (dist << 9);
        }
      }
#endif
      else{
        float price = costs[j];
        unsigned* mp = matches;

        unsigned curr = ZOPFLI_MIN_MATCH;
        while (mp < mend){

          unsigned len = *mp++;
          unsigned dist = *mp++;
          float price2 = price + disttable[dist];
          for (; curr <= len; curr++) {
            float x = price2 + litlentable[curr];
            if (x < costs[j + curr]){
              costs[j + curr] = x;
              length_array[j + curr] = curr + (dist << 9);
            }
          }
        }
      }
    }

    /* Literal. */
    float newCost = costs[j] + literals[in[i]];
    if (newCost < costs[j + 1]) {
      costs[j + 1] = newCost;
      length_array[j + 1] = 1;
    }

    if (i == inend - ZOPFLI_MAX_MATCH - 1 && mfinexport & 2 && storeincache != 2){
      CopyMF(&p, &mf);
      right = 1;
    }
  }

  MatchFinder_Free(&p);
  if (storeincache){
    c->pointer = 0;
  }

  if (!costcontext){
    free(literals);
  }
  free(disttable);
  free(costs);
}

/*
Calculates the optimal path of lz77 lengths to use, from the calculated
length_array. The length_array must contain the optimal length to reach that
byte. The path will be filled with the lengths to use, so its data size will be
the amount of lz77 symbols.
*/
static void TraceBackwards(size_t size, const unsigned* length_array,
                           unsigned** path, size_t* pathsize) {
  size_t osize = size * sizeof(unsigned);
  size_t allocsize = size / 258 + 50;
  *path = (unsigned*)malloc(allocsize * sizeof(unsigned));
  for (;size;) {
    (*path)[*pathsize] = length_array[size];
    (*pathsize)++;
    if(*pathsize == allocsize){
      allocsize *= 2;
      if (allocsize > osize){
        allocsize = osize;
      }
      *path = (unsigned*)realloc(*path, allocsize * sizeof(unsigned));
    }
    assert((length_array[size] & 511) <= size);
    assert((length_array[size] & 511) <= ZOPFLI_MAX_MATCH);
    assert(length_array[size]);
    size -= (length_array[size] & 511);
  }
}

static void FollowPath(const unsigned char* in, size_t instart, size_t inend, unsigned* path, size_t pathsize, ZopfliLZ77Store* store) {
  store->litlens = (unsigned short*)malloc(pathsize * sizeof(unsigned short));
  store->dists = (unsigned short*)malloc(pathsize * sizeof(unsigned short));
  if (!store->litlens || !store->dists){
    exit(1);
  }

  size_t pos = instart;
  /*pathsize contains matches in reverted order.*/
  for (size_t i = pathsize - 1;; i--) {
    unsigned short length = path[i] & 511;

    /* Add to output. */
    if (length >= ZOPFLI_MIN_MATCH) {

      unsigned short dist = path[i] >> 9;

#ifndef NDEBUG
        ZopfliVerifyLenDist(in, inend, pos, dist, length);
#endif
      store->litlens[store->size] = length;
      store->dists[store->size] = dist;

    } else {
      length = 1;
      store->litlens[store->size] = in[pos];
      store->dists[store->size] = 0;
    }

    assert(pos + length <= inend);

    pos += length;
    store->size++;
    if (!i){break;}
  }
}

/*
 Calculates the entropy of each symbol, based on the counts of each symbol. The
 result is similar to the result of ZopfliCalculateBitLengths, but with the
 actual theoritical bit lengths according to the entropy. Since the resulting
 values are fractional, they cannot be used to encode the tree specified by
 DEFLATE.
 */
static void ZopfliCalculateEntropy(const size_t* count, size_t n, float* bitlengths) {
  unsigned sum = 0;
  unsigned i;
  for (i = 0; i < n; ++i) {
    sum += count[i];
  }
  float log2sum = sum == 0 ? log(n) : log2(sum);

  for (i = 0; i < n; ++i) {
    /* When the count of the symbol is 0, but its cost is requested anyway, it
     means the symbol will appear at least once anyway, so give it the cost as if
     its count is 1.*/
    if (count[i] == 0) bitlengths[i] = log2sum;
    else bitlengths[i] = log2sum - log2f(count[i]);
    /* Depending on compiler and architecture, the above subtraction of two
     floating point numbers may give a negative result very close to zero
     instead of zero (e.g. -5.973954e-17 with gcc 4.1.2 on Ubuntu 11.4). Clamp
     it to zero. These floating point imprecisions do not affect the cost model
     significantly so this is ok. */
    if (bitlengths[i] < 0) bitlengths[i] = 0;
  }
}

/* Calculates the entropy of the statistics */
static void CalculateStatistics(SymbolStats* stats) {
  ZopfliCalculateEntropy(stats->litlens, 288, stats->ll_symbols);
  ZopfliCalculateEntropy(stats->dists, 32, stats->d_symbols);
}

/* Appends the symbol statistics from the store. */
void GetStatistics(const ZopfliLZ77Store* store, SymbolStats* stats) {
  ZopfliLZ77Counts(store->litlens, store->dists, 0, store->size, stats->litlens, stats->dists, store->symbols);

  CalculateStatistics(stats);
}

/*
Does a single run for ZopfliLZ77Optimal. For good compression, repeated runs
with updated statistics should be performed.

s: the block state
in: the input data array
instart: where to start
inend: where to stop (not inclusive)
path: pointer to dynamically allocated memory to store the path
pathsize: pointer to the size of the dynamic path array
length_array: array if size (inend - instart) used to store lengths
costcontext: abstract context for the costmodel function
store: place to output the LZ77 data
returns the cost that was, according to the costmodel, needed to get to the end.
    This is not the actual cost.
*/
static void LZ77OptimalRun(const ZopfliOptions* options, const unsigned char* in, size_t instart, size_t inend, unsigned* length_array, SymbolStats* costcontext, ZopfliLZ77Store* store, unsigned char storeincache, LZCache* c, unsigned mfinexport) {
  GetBestLengths(options, in, instart, inend, costcontext, length_array, storeincache, c, mfinexport);
  unsigned* path = 0;
  size_t pathsize = 0;
  TraceBackwards(inend - instart, length_array, &path, &pathsize);
  FollowPath(in, instart, inend, path, pathsize, store);
  free(path);
}

/*TODO: Replace this w/ proper implementation. This performs bad on files w/ changing redundancy */
static SymbolStats st;

static void ZopfliLZ77Optimal(const ZopfliOptions* options,
                       const unsigned char* in, size_t instart, size_t inend,
                       ZopfliLZ77Store* store, unsigned char first, SymbolStats* statsp, unsigned mfinexport) {
  /* Dist to get to here with smallest cost. */
  unsigned* length_array = (unsigned*)malloc(sizeof(unsigned) * (inend - instart + 1));
  ZopfliLZ77Store currentstore;
  SymbolStats stats, beststats, laststats;
  double cost;
  double bestcost = ZOPFLI_LARGE_FLOAT;
  double lastcost = 0;
  /* Try randomizing the costs a bit once the size stabilizes. */
  RanState ran_state;
  int lastrandomstep = -1;

  if (!length_array) exit(1); /* Allocation failed. */

  InitRanState(&ran_state);
  ZopfliInitLZ77Store(&currentstore);

  /* Do regular deflate, then loop multiple shortest path runs, each time using
  the statistics of the previous run. */

  /* Initial run. */
  if (first || !options->reuse_costmodel){
    SymbolStats fromBlocksplitting = *statsp;
    CopyStats(&fromBlocksplitting, &stats);
  }
  else{
    CopyStats(&st, &stats);
  }

  LZCache c;
  int stinit = 0;
  if (options->useCache){
    CreateCache(inend - instart, &c);
  }
  /* Repeat statistics with each time the cost model from the previous stat
  run. */
  for (int i = 1; i < options->numiterations + 1; i++) {
    ZopfliCleanLZ77Store(&currentstore);
    ZopfliInitLZ77Store(&currentstore);

    //TODO: This is very powerful and needs additional tuning.
    if ((i == options->numiterations - 1 && options->numiterations > 5)|| i == 9 || i == 30){
      unsigned bl[288];

      OptimizeHuffmanCountsForRle(32, beststats.dists);
      OptimizeHuffmanCountsForRle(288, beststats.litlens);

      ZopfliLengthLimitedCodeLengths(beststats.litlens, 288, 15, bl);
      for (int j = 0; j < 288; j++){
        stats.ll_symbols[j] = bl[j];
      }
      unsigned bld[32];
      ZopfliLengthLimitedCodeLengths(beststats.dists, 32, 15, bld);
      for (int j = 0; j < 32; j++){
        stats.d_symbols[j] = bld[j];
      }
    }

    LZ77OptimalRun(options, in, instart, inend, length_array, &stats, &currentstore, options->useCache ? i == 1 ? 1 : 2 : 0, &c, mfinexport);

    cost = ZopfliCalculateBlockSize(currentstore.litlens, currentstore.dists, 0, currentstore.size, 2, options->searchext, currentstore.symbols);
    if (cost < bestcost) {
      /* Copy to the output store. */
      ZopfliCopyLZ77Store(&currentstore, store);
      CopyStats(&stats, &beststats);
      bestcost = cost;
    }
    CopyStats(&stats, &laststats);
    GetStatistics(&currentstore, &stats);
    if (i == 4 && options->reuse_costmodel){
      CopyStats(&beststats, &st);
      stinit = 1;
    }
    if (lastrandomstep) {
      /* This makes it converge slower but better. Do it only once the
      randomness kicks in so that if the user does few iterations, it gives a
      better result sooner. */
      AddWeighedStatFreqs(&stats, 1.0, &laststats, .5, &stats);
      CalculateStatistics(&stats);
    }
    if (i > 6 && cost == lastcost) {
      CopyStats(&beststats, &stats);
      RandomizeStatFreqs(&ran_state, &stats);
      CalculateStatistics(&stats);
      lastrandomstep = i;
    }
    lastcost = cost;
  }

  if (options->ultra){

    for (;;){
      SymbolStats sta;
      GetStatistics(store, &sta);

      unsigned bl[288];

      OptimizeHuffmanCountsForRle(32, sta.dists);
      OptimizeHuffmanCountsForRle(288, sta.litlens);

      ZopfliLengthLimitedCodeLengths(sta.litlens, 288, 15, bl);
      for (int j = 0; j < 286; j++){
        sta.ll_symbols[j] = bl[j];
      }
      unsigned bld[32];
      ZopfliLengthLimitedCodeLengths(sta.dists, 32, 15, bld);
      for (int j = 0; j < 30; j++){
        sta.d_symbols[j] = bld[j];
      }


      ZopfliLZ77Store peace;
      ZopfliInitLZ77Store(&peace);
      LZ77OptimalRun(options, in, instart, inend, length_array, &sta, &peace, options->useCache ? 2 : 0, &c, mfinexport);
      double newcost = ZopfliCalculateBlockSize(peace.litlens, peace.dists, 0, peace.size, 2, options->searchext, peace.symbols);
      if (newcost < bestcost){
        bestcost = newcost;
        ZopfliCopyLZ77Store(&peace, store);
        ZopfliCleanLZ77Store(&peace);

      }
      else{
        ZopfliCleanLZ77Store(&peace);
        break;
      }

    }
  }


  if (options->useCache){
    CleanCache(&c);
  }
  free(length_array);
  if (options->reuse_costmodel && !stinit){
    CopyStats(&beststats, &st);
  }
  ZopfliCleanLZ77Store(&currentstore);
}

void ZopfliLZ77Optimal2(const ZopfliOptions* options,
                        const unsigned char* in, size_t instart, size_t inend,
                        ZopfliLZ77Store* store, unsigned char costmodelnotinited, SymbolStats* statsp, unsigned mfinexport) {
  SymbolStats stats;
  if (options->numiterations != 1){
    ZopfliLZ77Optimal(options, in, instart, inend, store, costmodelnotinited, statsp, mfinexport);
    return;
  }

  //TODO: Test this for PNG if last two cost models similar
  if (costmodelnotinited || (!options->reuse_costmodel)){
    SymbolStats fromBlocksplitting = *statsp;
    CopyStats(&fromBlocksplitting, &stats);

    if (options->isPNG){
      /*TODO:Corrections for cost model inaccuracies. There is still much potential here
       Enable this in Mode 4 too, though less aggressive*/
      for (unsigned i = 0; i < 256; i++){
        stats.ll_symbols[i] -= .25;
      }
      if (inend - instart < 1000){
        for (unsigned i = 0; i < 256; i++){
          stats.ll_symbols[i] -= 0.2;
        }
      }
      stats.ll_symbols[0] -= 1.2;
      stats.ll_symbols[1] -= 0.4;
      stats.d_symbols[0] -= 1.5;
      stats.d_symbols[3] -= 1.4;
      stats.ll_symbols[255] -= 0.5;
      stats.ll_symbols[257] -= .8;
      stats.ll_symbols[258] += 0.3;
      stats.ll_symbols[272] += 1.2;
      stats.ll_symbols[282] += 0.2;
      stats.ll_symbols[283] += 0.2;
      stats.ll_symbols[284] += 0.4;

      for (unsigned i = 270; i < 286; i++){
          stats.ll_symbols[i] += .4;
      }
      for (unsigned i = 0; i < 286; i++){
        if (stats.ll_symbols[i] < 1){
          stats.ll_symbols[i] = 1;
        }
      }
      for (unsigned i = 0; i < 30; i++){
        if (stats.d_symbols[i] < 1){
          stats.d_symbols[i] = 1;
        }
      }
      for (unsigned i = 0; i < 286; i++){
        if (stats.ll_symbols[i] > 15){
          stats.ll_symbols[i] = 15;
        }
      }
      for (unsigned i = 0; i < 30; i++){
        if (stats.d_symbols[i] > 15){
          stats.d_symbols[i] = 15;
        }
      }
    }
    if (!costmodelnotinited && !options->multithreading){
      MixCostmodels(&st, &stats, .2);
    }
  }
  else{
    SymbolStats fromBlocksplitting = *statsp;
    MixCostmodels(&fromBlocksplitting, &st, .3);
  }

  ZopfliInitLZ77Store(store);
  /* Dist to get to here with smallest cost. */
  unsigned* length_array = (unsigned*)malloc(sizeof(unsigned) * (inend - instart + 1));
  if (!length_array) exit(1); /* Allocation failed. */
  LZ77OptimalRun(options, in, instart, inend, length_array, options->reuse_costmodel ? &st : &stats, store, 0, 0, mfinexport);
  free(length_array);

  if (!options->multithreading){
    GetStatistics(store, &st);
  }
}

void ZopfliLZ77OptimalFixed(const ZopfliOptions* options,
                            const unsigned char* in,
                            size_t instart, size_t inend,
                            ZopfliLZ77Store* store, unsigned mfinexport)
{
  /* Dist to get to here with smallest cost. */
  unsigned* length_array = (unsigned*)malloc(sizeof(unsigned) * (inend - instart + 1));
  if (!length_array) exit(1); /* Allocation failed. */

  /* Shortest path for fixed tree This one should give the shortest possible
  result for fixed tree, no repeated runs are needed since the tree is known. */
  LZ77OptimalRun(options, in, instart, inend, length_array, 0, store, 0, 0, mfinexport);

  free(length_array);
}
