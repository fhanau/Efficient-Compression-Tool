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

#include "squeeze.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "blocksplitter.h"
#include "deflate.h"
#include "tree.h"
#include "util.h"

typedef struct SymbolStats {
  /* The literal and length symbols. */
  size_t litlens[288];
  /* The 32 unique dist symbols, not the 32768 possible dists. */
  size_t dists[32];

  float ll_symbols[288];  /* Length of each lit/len symbol in bits. */
  float d_symbols[32];  /* Length of each dist symbol in bits. */
} SymbolStats;

static void CopyStats(SymbolStats* source, SymbolStats* dest) {
  memcpy(dest->litlens, source->litlens, 288 * sizeof(dest->litlens[0]));
  memcpy(dest->dists, source->dists, 32 * sizeof(dest->dists[0]));

  memcpy(dest->ll_symbols, source->ll_symbols,
         288 * sizeof(dest->ll_symbols[0]));
  memcpy(dest->d_symbols, source->d_symbols, 32 * sizeof(dest->d_symbols[0]));
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

static void GetBestLengths(ZopfliBlockState *s,
                             const unsigned char* in,
                             size_t instart, size_t inend,
                             SymbolStats* costcontext, unsigned short* length_array, unsigned char storeincache) {
  size_t i;

  float litlentable [259];
  float* disttable = (float*)malloc(32768 * sizeof(float));
  if (!disttable){
    exit(1);
  }
  if (costcontext){  /* Dynamic Block */

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
    for (i = 3; i < 259; i++){
      litlentable[i] = 12 + (i > 114) + ZopfliGetLengthExtraBits(i);
    }
    for (i = 0; i < 513; i++){
      disttable[i] = ZopfliGetDistExtraBits(i);
    }
    for (; i < 1025; i++){
      disttable[i] = 8;
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

  /* Best cost to get here so far. */
  size_t blocksize = inend - instart;
  unsigned short leng;
  unsigned short sublen[259];
  size_t windowstart = instart > ZOPFLI_WINDOW_SIZE ? instart - ZOPFLI_WINDOW_SIZE : 0;
  ZopfliHash hash;
  ZopfliHash* h = &hash;

  if (instart == inend) return;

  float* costs = (float*)malloc(sizeof(float) * (blocksize + 1));
  if (!costs) exit(1); /* Allocation failed. */
  costs[0] = 0;  /* Because it's the start. */
  memset(costs + 1, 127, sizeof(float) * blocksize);

  ZopfliInitHash(h);
  ZopfliWarmupHash(in, windowstart, h);
  LoopedUpdateHash(in, windowstart, inend, h, instart - windowstart);

  for (i = instart; i < inend; i++) {
    size_t j = i - instart;  /* Index in the costs array and length_array. */
    ZopfliUpdateHash(in, i, inend, h);

#ifdef ZOPFLI_SHORTCUT_LONG_REPETITIONS
    /* If we're in a long repetition of the same character and have more than
    ZOPFLI_MAX_MATCH characters before and after our position. */
    if (h->same[i & ZOPFLI_WINDOW_MASK] > ZOPFLI_MAX_MATCH * 2
        && i > instart + ZOPFLI_MAX_MATCH + 1
        && i + ZOPFLI_MAX_MATCH * 2 + 1 < inend
        && h->same[(i - ZOPFLI_MAX_MATCH) & ZOPFLI_WINDOW_MASK]
            > ZOPFLI_MAX_MATCH) {
      //Simplified costmodel(ZOPFLI_MAX_MATCH, 1, costcontext) call
      float symbolcost = costcontext == NULL ? 13 : costcontext->ll_symbols[285] + costcontext->d_symbols[16];
      /* Set the length to reach each one to ZOPFLI_MAX_MATCH, and the cost to
      the cost corresponding to that length. Doing this, we skip
      ZOPFLI_MAX_MATCH values to avoid calling ZopfliFindLongestMatch. */
      i++;
      LoopedUpdateHash(in, i, inend, h, ZOPFLI_MAX_MATCH);
      for (unsigned short k = 0; k < ZOPFLI_MAX_MATCH; k++) {
        costs[j + ZOPFLI_MAX_MATCH] = costs[j] + symbolcost;
        length_array[j + ZOPFLI_MAX_MATCH] = ZOPFLI_MAX_MATCH;
        j++;
      }
      i += (ZOPFLI_MAX_MATCH - 1);
    }
#endif
    ZopfliFindLongestMatch2(s, h, in, i, inend, sublen, &leng, storeincache);

    /* Literal. */
    float newCost = costs[j] + (costcontext == 0 ? 8 + (in[i] > 143) : costcontext->ll_symbols[in[i]]);
    if (newCost < costs[j + 1]) {//Can we unlikely
      costs[j + 1] = newCost;
      length_array[j + 1] = 1;
    }

    for (unsigned short k = 3; k <= leng; k++) {
      newCost = costs[j] + litlentable[k] + disttable[sublen[k]];
      if (newCost < costs[j + k]) {
        costs[j + k] = newCost;
        length_array[j + k] = k;
      }
    }
  }

  ZopfliCleanHash(h);
  free(disttable);
  free(costs);
}

/*
Calculates the optimal path of lz77 lengths to use, from the calculated
length_array. The length_array must contain the optimal length to reach that
byte. The path will be filled with the lengths to use, so its data size will be
the amount of lz77 symbols.
*/
static void TraceBackwards(size_t size, const unsigned short* length_array,
                           unsigned short** path, size_t* pathsize) {
  size_t index = size;
  if (size == 0) return;
  for (;;) {
    ZOPFLI_APPEND_DATA(length_array[index], path, pathsize);
    assert(length_array[index] <= index);
    assert(length_array[index] <= ZOPFLI_MAX_MATCH);
    assert(length_array[index] != 0);
    index -= length_array[index];
    if (index == 0) break;
  }

  /* Mirror result. */
  for (index = 0; index < *pathsize / 2; index++) {
    unsigned short temp = (*path)[index];
    (*path)[index] = (*path)[*pathsize - index - 1];
    (*path)[*pathsize - index - 1] = temp;
  }
}

static void FollowPath(ZopfliBlockState* s,
                       const unsigned char* in, size_t instart, size_t inend,
                       unsigned short* path, size_t pathsize,
                       ZopfliLZ77Store* store) {
  size_t i;
  size_t windowstart = instart > ZOPFLI_WINDOW_SIZE
      ? instart - ZOPFLI_WINDOW_SIZE : 0;

  ZopfliHash hash;
  ZopfliHash* h = &hash;

  if (instart == inend) return;

  ZopfliInitHash(h);
  ZopfliWarmupHash(in, windowstart, h);
  LoopedUpdateHash(in, windowstart, inend, h, instart - windowstart);
  size_t pos = instart;
  for (i = 0; i < pathsize; i++) {
    unsigned short length = path[i];
    assert(pos < inend);

    ZopfliUpdateHash(in, pos, inend, h);

    /* Add to output. */
    if (length >= ZOPFLI_MIN_MATCH) {
      /* Get the distance by recalculating longest match. The found length
      should match the length from the path. */
      unsigned short dummy_length;
      unsigned short dist;
      ZopfliFindLongestMatch(s, h, in, pos, inend, length, 0,
                             &dist, &dummy_length, 1);
      assert(!(dummy_length != length && length > 2 && dummy_length > 2));
#ifndef NDEBUG
        ZopfliVerifyLenDist(in, inend, pos, dist, length);
#endif
      ZopfliStoreLitLenDist(length, dist, store);

      for (size_t j = 1; j < length; j++) {
        ZopfliUpdateHash(in, pos + j, inend, h);
      }

    } else {
      length = 1;
      ZopfliStoreLitLenDist(in[pos], 0, store);
    }

    assert(pos + length <= inend);

    pos += length;
  }

  ZopfliCleanHash(h);
}

/* Calculates the entropy of the statistics */
static void CalculateStatistics(SymbolStats* stats) {
  ZopfliCalculateEntropy(stats->litlens, 288, stats->ll_symbols);
  ZopfliCalculateEntropy(stats->dists, 32, stats->d_symbols);
}

/* Appends the symbol statistics from the store. */
static void GetStatistics(const ZopfliLZ77Store* store, SymbolStats* stats) {
  memset(stats->litlens, 0, 288 * sizeof(stats->litlens[0]));
  memset(stats->dists, 0, 32 * sizeof(stats->dists[0]));

  size_t i;
  for (i = 0; i < store->size; i++) {
    if (store->dists[i] == 0) {
      stats->litlens[store->litlens[i]]++;
    } else {
      stats->litlens[ZopfliGetLengthSymbol(store->litlens[i])]++;
      stats->dists[ZopfliGetDistSymbol(store->dists[i])]++;
    }
  }
  stats->litlens[256] = 1;  /* End symbol. */

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
static void LZ77OptimalRun(ZopfliBlockState* s, const unsigned char* in, size_t instart, size_t inend, unsigned short* length_array, SymbolStats* costcontext, ZopfliLZ77Store* store, unsigned char storeincache) {
  GetBestLengths(s, in, instart, inend, costcontext, length_array, storeincache);
  unsigned short* path = 0;
  size_t pathsize = 0;
  TraceBackwards(inend - instart, length_array, &path, &pathsize);
  FollowPath(s, in, instart, inend, path, pathsize, store);
  free(path);
}

void ZopfliLZ77Optimal(ZopfliBlockState *s,
                       const unsigned char* in, size_t instart, size_t inend,
                       ZopfliLZ77Store* store) {
  /* Dist to get to here with smallest cost. */
  unsigned short* length_array = (unsigned short*)malloc(sizeof(unsigned short) * (inend - instart + 1));
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
  ZopfliLZ77Greedy(s, in, instart, inend, &currentstore, 0);
  GetStatistics(&currentstore, &stats);

  /* Repeat statistics with each time the cost model from the previous stat
  run. */
  for (int i = 1; i < s->options->numiterations+1; i++) {
    ZopfliCleanLZ77Store(&currentstore);
    ZopfliInitLZ77Store(&currentstore);
    LZ77OptimalRun(s, in, instart, inend, length_array, &stats, &currentstore, i == 1 && i != s->options->numiterations);

    cost = ZopfliCalculateBlockSize(currentstore.litlens, currentstore.dists,
                                    0, currentstore.size, 2, 1);
    if (cost < bestcost) {
      /* Copy to the output store. */
      ZopfliCopyLZ77Store(&currentstore, store);
      CopyStats(&stats, &beststats);
      bestcost = cost;
    }
    CopyStats(&stats, &laststats);
    GetStatistics(&currentstore, &stats);
    if (lastrandomstep != -1) {
      /* This makes it converge slower but better. Do it only once the
      randomness kicks in so that if the user does few iterations, it gives a
      better result sooner. */
      AddWeighedStatFreqs(&stats, 1.0, &laststats, 0.5, &stats);
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

  free(length_array);
  ZopfliCleanLZ77Store(&currentstore);
}

void ZopfliLZ77Optimal2(ZopfliBlockState *s,
                        const unsigned char* in, size_t instart, size_t inend,
                        ZopfliLZ77Store* store) {
  if (s->options->numiterations != 1){
    ZopfliLZ77Optimal(s, in, instart, inend, store);
    return;
  }

  SymbolStats stats;
  ZopfliLZ77Greedy(s, in, instart, inend, store, 0);
  GetStatistics(store, &stats);
  ZopfliCleanLZ77Store(store);

  if (s->options->isPNG){
    /*TODO:Corrections for cost model inaccuracies. There is still much potential here
    Enable this in Mode 3, too, though less aggressive*/
    for (unsigned i = 0; i < 256; i++){
      stats.ll_symbols[i] -= 0.4;
    }
    if (inend - instart < 1000){
      for (unsigned i = 0; i < 256; i++){
        stats.ll_symbols[i] -= 0.2;
      }
    }
    stats.ll_symbols[0] -= 1;
    stats.d_symbols[0] -= 1.5;
    stats.d_symbols[3] -= 1.4;
    stats.ll_symbols[255] -= 0.5;
    stats.ll_symbols[257] -= 1.2;
    stats.ll_symbols[258] += 0.3;
    stats.ll_symbols[272] += 1.2;
    stats.ll_symbols[282] += 0.2;
    stats.ll_symbols[283] += 0.2;
    stats.ll_symbols[284] += 0.4;
    if (inend - instart < 32768 && inend - instart > 100){
      for (unsigned i = ZopfliGetDistSymbol(inend - instart) - 1; i < 32; i++){
        stats.d_symbols[i] += 0.5;
      }
    }
    for (unsigned i = 0; i < 288; i++){
      if (!stats.ll_symbols[i]){
        stats.ll_symbols[i] = 0;
      }
    }
    for (unsigned i = 0; i < 32; i++){
      if (!stats.d_symbols[i]){
        stats.d_symbols[i] = 0;
      }
    }
  }


  ZopfliInitLZ77Store(store);
  /* Dist to get to here with smallest cost. */
  unsigned short* length_array = (unsigned short*)malloc(sizeof(unsigned short) * (inend - instart + 1));
  if (!length_array) exit(1); /* Allocation failed. */
  LZ77OptimalRun(s, in, instart, inend, length_array, &stats, store, 0);

  free(length_array);
}

void ZopfliLZ77OptimalFixed(ZopfliBlockState *s,
                            const unsigned char* in,
                            size_t instart, size_t inend,
                            ZopfliLZ77Store* store)
{
  /* Dist to get to here with smallest cost. */
  unsigned short* length_array = (unsigned short*)malloc(sizeof(unsigned short) * (inend - instart + 1));
  if (!length_array) exit(1); /* Allocation failed. */

  /* Shortest path for fixed tree This one should give the shortest possible
  result for fixed tree, no repeated runs are needed since the tree is known. */
  LZ77OptimalRun(s, in, instart, inend, length_array, 0, store, 0);

  free(length_array);
}
