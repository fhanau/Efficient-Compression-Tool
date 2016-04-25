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
Bounded package merge algorithm, based on the paper
"A Fast and Space-Economical Algorithm for Length-Limited Coding
Jyrki Katajainen, Alistair Moffat, Andrew Turpin".
*/

/*Modified by Felix Hanau*/

#include "katajainen.h"
#include "util.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <algorithm>

/*
Nodes forming chains. Also used to represent leaves.
*/
typedef struct Node
{
  size_t weight;  /* Total weight (symbol count) of this chain. */
  Node* tail;  /* Previous node(s) of this chain, or 0 if none. */
  int count;  /* Leaf symbol index, or number of leaves before this chain. */
}
#if defined(__GNUC__) && (defined(__i386__) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64))
__attribute__((packed)) Node;
#else
Node;
#endif

/*
Memory pool for nodes.
*/
typedef struct NodePool {
  Node* next;  /* Pointer to a free node in the pool. */
} NodePool;

/*
Initializes a chain node with the given values and marks it as in use.
*/
static void InitNode(size_t weight, int count, Node* tail, Node* node) {
  node->weight = weight;
  node->count = count;
  node->tail = tail;
}

/*
Performs a Boundary Package-Merge step. Puts a new chain in the given list. The
new chain is, depending on the weights, a leaf or a combination of two chains
from the previous list.
lists: The lists of chains.
maxbits: Number of lists.
leaves: The leaves, one per symbol.
numsymbols: Number of leaves.
pool: the node memory pool.
index: The index of the list in which a new chain or leaf is required.
*/
static void BoundaryPM(Node* (*lists)[2], Node* leaves, int numsymbols, NodePool* pool, int index) {

  int lastcount = lists[index][1]->count;  /* Count of last chain of list. */

  if (unlikely(!index && lastcount >= numsymbols)) return;

  Node* newchain = pool->next++;
  Node* oldchain = lists[index][1];

  /* These are set up before the recursive calls below, so that there is a list
  pointing to the new node, to let the garbage collection know it's in use. */
  lists[index][0] = oldchain;
  lists[index][1] = newchain;

  if (unlikely(!index)) {
    /* New leaf node in list 0. */
    InitNode(leaves[lastcount].weight, lastcount + 1, 0, newchain);
  } else {
    size_t sum = lists[index - 1][0]->weight + lists[index - 1][1]->weight;
    if (lastcount < numsymbols && sum > leaves[lastcount].weight) {
      /* New leaf inserted in list, so count is incremented. */
      InitNode(leaves[lastcount].weight, lastcount + 1, oldchain->tail, newchain);
    } else {
      InitNode(sum, lastcount, lists[index - 1][1], newchain);
      /* Two lookahead chains of previous list used up, create new ones. */
      BoundaryPM(lists, leaves, numsymbols, pool, index - 1);
      BoundaryPM(lists, leaves, numsymbols, pool, index - 1);
    }
  }
}

static void BoundaryPMfinal(Node* (*lists)[2],
                       Node* leaves, int numsymbols, NodePool* pool, int index) {
  int lastcount = lists[index][1]->count;  /* Count of last chain of list. */

  size_t sum = lists[index - 1][0]->weight + lists[index - 1][1]->weight;

  if (lastcount < numsymbols && sum > leaves[lastcount].weight) {

    Node* newchain = pool->next;
    Node* oldchain = lists[index][1]->tail;

    lists[index][1] = newchain;
    newchain->count = lastcount + 1;
    newchain->tail = oldchain;

  }
  else{
    lists[index][1]->tail = lists[index - 1][1];
  }
}

/*
Initializes each list with as lookahead chains the two leaves with lowest
weights.
*/
static void InitLists(
    NodePool* pool, const Node* leaves, int maxbits, Node* (*lists)[2]) {
  Node* node0 = pool->next++;
  Node* node1 = pool->next++;
  InitNode(leaves[0].weight, 1, 0, node0);
  InitNode(leaves[1].weight, 2, 0, node1);
  for (int i = 0; i < maxbits; i++) {
    lists[i][0] = node0;
    lists[i][1] = node1;
  }
}

/*
Converts result of boundary package-merge to the bitlengths. The result in the
last chain of the last list contains the amount of active leaves in each list.
chain: Chain to extract the bit length from (last chain from last list).
*/
static void ExtractBitLengths(Node* chain, Node* leaves, unsigned* bitlengths) {
  //Messy, but fast
  int counts[16] = {0};
  unsigned end = 16;
  for (Node* node = chain; node; node = node->tail) {
    end--;
    counts[end] = node->count;
  }

  unsigned ptr = 15;
  unsigned value = 1;
  unsigned val = counts[15];
  while (ptr >= end) {

    for (; val > counts[ptr - 1]; val--) {
      bitlengths[leaves[val - 1].count] = value;
    }
    ptr--;
    value++;
  }
}

void ZopfliLengthLimitedCodeLengths(const size_t* frequencies, int n, int maxbits, unsigned* bitlengths) {
  int i;
  int numsymbols = 0;  /* Amount of symbols with frequency > 0. */

  /* Array of lists of chains. Each list requires only two lookahead chains at
  a time, so each list is a array of two Node*'s. */

  /* One leaf per symbol. Only numsymbols leaves will be used. */
  Node leaves[286];

  /* Initialize all bitlengths at 0. */
  memset(bitlengths, 0, n * sizeof(unsigned));

  /* Count used symbols and place them in the leaves. */
  for (i = 0; i < n; i++) {
    if (frequencies[i]) {
      leaves[numsymbols].weight = frequencies[i];
      leaves[numsymbols].count = i;  /* Index of symbol this leaf represents. */
      numsymbols++;
    }
  }

  /* Check special cases and error conditions. */
  assert((1 << maxbits) >= numsymbols); /* Error, too few maxbits to represent symbols. */
  if (numsymbols == 0) {
    return;  /* No symbols at all. OK. */
  }
  if (numsymbols == 1) {
    bitlengths[leaves[0].count] = 1;
    return;  /* Only one symbol, give it bitlength 1, not 0. OK. */
  }
  if (numsymbols == 2){
    bitlengths[leaves[0].count]++;
    bitlengths[leaves[1].count]++;
    return;
  }

  struct {
    bool operator()(const Node a, const Node b) {
      return (a.weight < b.weight);
    }
  } cmpstable;

  /* Sort the leaves from lightest to heaviest. */
  std::stable_sort(leaves, leaves + numsymbols, cmpstable);

  if (numsymbols - 1 < maxbits) {
    maxbits = numsymbols - 1;
  }

  /* Initialize node memory pool. */
  NodePool pool;
  Node stack[8580]; //maxbits(<=15) * 2 * numsymbols(<=286), the theoretical maximum. This needs about 170kb of memory, but is much faster than a node pool using garbage collection.
  //If you need to conserve stack size.
  /*Node* stack = (Node*)malloc(maxbits * 2 * numsymbols * sizeof(Node));
  if (!stack){
    exit(1);
  }*/
  pool.next = stack;


  Node list[15][2];
  Node* (* lists)[2]  = ( Node* (*)[2])list;
  InitLists(&pool, leaves, maxbits, lists);

  /* In the last list, 2 * numsymbols - 2 active chains need to be created. Two
  are already created in the initialization. Each BoundaryPM run creates one. */
  int numBoundaryPMRuns = 2 * numsymbols - 4;
  for (i = 0; i < numBoundaryPMRuns - 1; i++) {
    BoundaryPM(lists, leaves, numsymbols, &pool, maxbits - 1);
  }
  BoundaryPMfinal(lists, leaves, numsymbols, &pool, maxbits - 1);

  ExtractBitLengths(lists[maxbits - 1][1], leaves, bitlengths);
  //free(stack);
}
