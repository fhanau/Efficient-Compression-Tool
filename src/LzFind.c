/* LzFind.c -- Match finder for LZ algorithms
2009-04-22 : Igor Pavlov : Public domain */

/* Modified by Felix Hanau*/
#include <string.h>
#include <stdlib.h>

#include "LzFind.h"
#include "zopfli/util.h"
#include "zopfli/match.h"

#define WINDOWSIZE 32768U
#define MAX_MATCH 258U

void MatchFinder_Free(CMatchFinder *p)
{
  free(p->hash);
}

static void MatchFinder_SetLimits(CMatchFinder *p)
{
  UInt32 limit = WINDOWSIZE - p->cyclicBufferPos;
  UInt32 limit2 = p->streamPos - p->pos;
  if (limit2 <= MAX_MATCH)
  {
    if (limit2 > 0)
      limit2 = 1;
  }
  else
    limit2 -= MAX_MATCH;
  if (limit2 < limit)
    limit = limit2;
  {
    UInt32 lenLimit = p->streamPos - p->pos;
    if (lenLimit > MAX_MATCH) 
      lenLimit = MAX_MATCH;
    p->lenLimit = lenLimit;
  }
  p->posLimit = p->pos + limit;
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
  p->pos = p->streamPos = WINDOWSIZE;
  p->streamPos += p->directInputRem;
  MatchFinder_SetLimits(p);
}

static void MatchFinder_CheckLimits(CMatchFinder *p)
{
  if (p->cyclicBufferPos == WINDOWSIZE)
    p->cyclicBufferPos = 0;
  MatchFinder_SetLimits(p);
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
    if (cutValue-- == 0 || delta >= WINDOWSIZE)
    {
      *ptr0 = *ptr1 = 0;
      return distances;
    }
      UInt32 *pair = son + ((_cyclicBufferPos - delta + ((delta > _cyclicBufferPos) ? WINDOWSIZE : 0)) << 1);
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
    if (cutValue-- == 0 || delta >= WINDOWSIZE)
    {
      *ptr0 = *ptr1 = 0;
      return;
    }
    {
      UInt32 *pair = son + ((_cyclicBufferPos - delta + ((delta > _cyclicBufferPos) ? (WINDOWSIZE) : 0)) << 1);
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
}

#define MOVE_POS \
  ++p->cyclicBufferPos; \
  p->buffer++; \
  if (++p->pos == p->posLimit) MatchFinder_CheckLimits(p);

#define GET_MATCHES_HEADER2(ret_op) \
  UInt32 lenLimit = p->lenLimit; { if (lenLimit < 3) { MOVE_POS; ret_op; }} \
  const Byte *cur = p->buffer;

#define MF_PARAMS(p) p->pos, p->buffer, p->son, p->cyclicBufferPos, p->cutValue

UInt32 Bt3Zip_MatchFinder_GetMatches(CMatchFinder *p, UInt32 *distances)
{
  GET_MATCHES_HEADER2(return 0)
  UInt32 hashValue = ((cur[2] | ((UInt32)cur[0] << 8)) ^ crc[cur[1]]) & 0xFFFF;
  UInt32 curMatch = p->hash[hashValue];
  p->hash[hashValue] = p->pos;
  UInt32 offset = (UInt32)(GetMatches(lenLimit, curMatch, MF_PARAMS(p), distances, 2) - distances);
  MOVE_POS;
  return offset;
}

void Bt3Zip_MatchFinder_Skip(CMatchFinder *p, UInt32 num)
{
  do
  {
    GET_MATCHES_HEADER2(continue)
    UInt32 hashValue = ((cur[2] | ((UInt32)cur[0] << 8)) ^ crc[cur[1]]) & 0xFFFF;
    UInt32 curMatch = p->hash[hashValue];
    p->hash[hashValue] = p->pos;
    SkipMatches(lenLimit, curMatch, MF_PARAMS(p));
    MOVE_POS;
  }
  while (--num != 0);
}
