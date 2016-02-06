/* LzFind.c -- Match finder for LZ algorithms
2009-04-22 : Igor Pavlov : Public domain */

/* Modified by Felix Hanau*/
#include <string.h>
#include <stdlib.h>

#include "LzFind.h"
#include "zopfli/util.h"
#include "zopfli/match.h"

void MatchFinder_Free(CMatchFinder *p)
{
  free(p->hash);
}

void MatchFinder_Create(CMatchFinder *p)
{
  //256kb hash, 256kb binary tree
  p->hash = (UInt32*)malloc(131072 * sizeof(UInt32));
  if (!p->hash)
  {
    exit(1);
  }
  p->son = p->hash + 65536;

  memset(p->hash, 0, 65536 * sizeof(unsigned));
  p->cyclicBufferPos = 0;
  p->pos = ZOPFLI_WINDOW_SIZE;
}

static UInt32 * GetMatches(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, UInt32 *son,
                         UInt32 _cyclicBufferPos, UInt32 cutValue,
                         UInt32 *distances, UInt32 maxLen)
{
  UInt32 *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  UInt32 *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    if (cutValue-- == 0 || delta >= ZOPFLI_WINDOW_SIZE)
    {
      *ptr0 = *ptr1 = 0;
      return distances;
    }
    UInt32 *pair = son + ((_cyclicBufferPos - delta + ((delta > _cyclicBufferPos) ? ZOPFLI_WINDOW_SIZE : 0)) << 1);
    const Byte *pb = cur - delta;
    UInt32 len = (len0 < len1 ? len0 : len1);
    if (pb[len] == cur[len])
    {
      ++len;
      if (len != lenLimit && pb[len] == cur[len])
      {
        len = GetMatch(&cur[len], &pb[len], cur + lenLimit, cur + lenLimit - 8) - cur;
      }

      if (maxLen < len)
      {
        *distances++ = maxLen = len;
        *distances++ = delta;
        if (len == lenLimit)
        {
          *ptr1 = pair[0];
          *ptr0 = pair[1];
          return distances;
        }
      }
    }
    if (pb[len] < cur[len])
    {
      *ptr1 = curMatch;
      ptr1 = pair + 1;
      curMatch = *ptr1;
      len1 = len;
    }
    else
    {
      *ptr0 = curMatch;
      ptr0 = pair;
      curMatch = *ptr0;
      len0 = len;
    }
  }
}

static void SkipMatches(UInt32 lenLimit, UInt32 curMatch, UInt32 pos, const Byte *cur, UInt32 *son,
                        UInt32 _cyclicBufferPos, UInt32 cutValue)
{
  UInt32 *ptr0 = son + (_cyclicBufferPos << 1) + 1;
  UInt32 *ptr1 = son + (_cyclicBufferPos << 1);
  UInt32 len0 = 0, len1 = 0;
  for (;;)
  {
    UInt32 delta = pos - curMatch;
    if (cutValue-- == 0 || delta >= ZOPFLI_WINDOW_SIZE)
    {
      *ptr0 = *ptr1 = 0;
      return;
    }

    UInt32 *pair = son + ((_cyclicBufferPos - delta + ((delta > _cyclicBufferPos) ? (ZOPFLI_WINDOW_SIZE) : 0)) << 1);
    const Byte *pb = cur - delta;
    UInt32 len = (len0 < len1 ? len0 : len1);
    if (pb[len] == cur[len])
    {
      if (len != lenLimit && pb[len] == cur[len])
      {
        len = GetMatch(&cur[len], &pb[len], cur + lenLimit, cur + lenLimit - 8) - cur;
      }

      if (len == lenLimit)
      {
        *ptr1 = pair[0];
        *ptr0 = pair[1];
        return;
      }
    }
    if (pb[len] < cur[len])
    {
      *ptr1 = curMatch;
      ptr1 = pair + 1;
      curMatch = *ptr1;
      len1 = len;
    }
    else
    {
      *ptr0 = curMatch;
      ptr0 = pair;
      curMatch = *ptr0;
      len0 = len;
    }
  }
}

#define MOVE_POS \
  ++p->cyclicBufferPos; \
  p->cyclicBufferPos &= ZOPFLI_WINDOW_MASK; \
  p->buffer++; \
  ++p->pos;

#define MF_PARAMS(p) p->pos, p->buffer, p->son, p->cyclicBufferPos, p->cutValue

UInt32 Bt3Zip_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  unsigned lenl = p->bufend - p->buffer; { if (lenl < ZOPFLI_MIN_MATCH) {return 0;}}
  const Byte *cur = p->buffer;
  UInt32 hashValue = ((cur[2] | ((UInt32)cur[0] << 8)) ^ crc[cur[1]]) & 0xFFFF;
  UInt32 curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  UInt32 offset = (UInt32)(GetMatches(lenl > ZOPFLI_MAX_MATCH ? ZOPFLI_MAX_MATCH : lenl, curMatch, MF_PARAMS(p), distances, 2) - distances);
  MOVE_POS;
  return offset;
}

void Bt3Zip_MatchFinder_Skip(CMatchFinder* p, UInt32 num)
{
  do
  {
    const Byte *cur = p->buffer;
    UInt32 hashValue = ((cur[2] | ((UInt32)cur[0] << 8)) ^ crc[cur[1]]) & 0xFFFF;
    UInt32 curMatch = p->hash[hashValue];
    p->hash[hashValue] = p->pos;
    unsigned lenlimit = p->bufend - p->buffer;
    SkipMatches(lenlimit > ZOPFLI_MAX_MATCH ? ZOPFLI_MAX_MATCH : lenlimit, curMatch, MF_PARAMS(p));
    MOVE_POS;
  }
  while (--num);
}

void CopyMF(const CMatchFinder *p, CMatchFinder* copy){
  copy->cutValue = p->cutValue;
  copy->hash = (UInt32*)malloc(131072 * sizeof(UInt32));
  if (!copy->hash)
  {
    exit(1);
  }
  copy->son = copy->hash + 65536;
  memcpy(copy->hash, p->hash, 131072 * sizeof(UInt32));

  copy->cyclicBufferPos = p->cyclicBufferPos;
  copy->pos = p->pos;
  copy->buffer = p->buffer;
}
