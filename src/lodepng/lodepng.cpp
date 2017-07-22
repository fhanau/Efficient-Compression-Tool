/*
LodePNG version 20141130

Copyright (c) 2005-2014 Lode Vandevenne

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
    claim that you wrote the original software. If you use this software
    in a product, an acknowledgment in the product documentation would be
    appreciated but is not required.

    2. Altered source versions must be plainly marked as such, and must not be
    misrepresented as being the original software.

    3. This notice may not be removed or altered from any source
    distribution.
*/


/*
The manual and changelog are in the header file "lodepng.h"
Rename this file to lodepng.cpp to use it for C++, or to lodepng.c to use it for C.
*/

/*Modified by Felix Hanau to remove unused functions*/

#include "lodepng.h"
#include "../zlib/zlib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef LODEPNG_COMPILE_CPP
#include <fstream>
#include <algorithm>
#endif /*LODEPNG_COMPILE_CPP*/

/*
This source file is built up in the following large parts. The code sections
with the "LODEPNG_COMPILE_" #defines divide this up further in an intermixed way.
-Tools for C and common code for PNG and Zlib
-C Code for Zlib (huffman, deflate, ...)
-C Code for PNG (file format chunks, adam7, PNG filters, color conversions, ...)
-The C++ wrapper around all of the above
*/

static void* lodepng_malloc(size_t size)
{
  return malloc(size);
}

static void* lodepng_realloc(void* ptr, size_t new_size)
{
  return realloc(ptr, new_size);
}

/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */
/* // Tools for C, and common code for PNG and Zlib.                       // */
/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */

/*
Often in case of an error a value is assigned to a variable and then it breaks
out of a loop (to go to the cleanup phase of a function). This macro does that.
It makes the error handling code shorter and more readable.
*/
#define CERROR_BREAK(errorvar, code)\
{\
  errorvar = code;\
  break;\
}

/*version of CERROR_BREAK that assumes the common case where the error variable is named "error"*/
#define ERROR_BREAK(code) CERROR_BREAK(error, code)

/*Set error var to the error code, and return it.*/
#define CERROR_RETURN_ERROR(errorvar, code)\
{\
  errorvar = code;\
  return code;\
}

/*Try the code, if it returns error, also return the error.*/
#define CERROR_TRY_RETURN(call)\
{\
  unsigned error = call;\
  if(error) return error;\
}

/*Set error var to the error code, and return from the void function.*/
#define CERROR_RETURN(errorvar, code)\
{\
  errorvar = code;\
  return;\
}

/*
About ucvector and string:
-All of them wrap dynamic arrays or text strings in a similar way.
-LodePNG was originally written in C++. The vectors replace the std::vectors that were used in the C++ version.
-The string tools are made to avoid problems with compilers that declare things like strncat as deprecated.
-They're not used in the interface, only internally in this file as static functions.
-As with many other structs in this file, the init and cleanup functions serve as ctor and dtor.
*/

/*dynamic vector of unsigned chars*/
typedef struct ucvector
{
  unsigned char* data;
  size_t size; /*used size*/
  size_t allocsize; /*allocated size*/
} ucvector;

/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned ucvector_reserve(ucvector* p, size_t allocsize)
{
  if(allocsize > p->allocsize)
  {
    size_t newsize = (allocsize > p->allocsize * 2) ? allocsize : (allocsize * 3 / 2);
    void* data = lodepng_realloc(p->data, newsize);
    if(data)
    {
      p->allocsize = newsize;
      p->data = (unsigned char*)data;
    }
    else return 0; /*error: not enough memory*/
  }
  return 1;
}

/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned ucvector_resize(ucvector* p, size_t size)
{
  if(!ucvector_reserve(p, size * sizeof(unsigned char))) return 0;
  p->size = size;
  return 1; /*success*/
}

#ifdef LODEPNG_COMPILE_PNG

static void ucvector_cleanup(void* p)
{
  ((ucvector*)p)->size = ((ucvector*)p)->allocsize = 0;
  free(((ucvector*)p)->data);
  ((ucvector*)p)->data = 0;
}

static void ucvector_init(ucvector* p)
{
  p->data = 0;
  p->size = p->allocsize = 0;
}
#endif /*LODEPNG_COMPILE_PNG*/

#ifdef LODEPNG_COMPILE_ZLIB
/*you can both convert from vector to buffer&size and vica versa. If you use
init_buffer to take over a buffer and size, it is not needed to use cleanup*/
static void ucvector_init_buffer(ucvector* p, unsigned char* buffer, size_t size)
{
  p->data = buffer;
  p->allocsize = p->size = size;
}
#endif /*LODEPNG_COMPILE_ZLIB*/

#if (defined(LODEPNG_COMPILE_PNG) && defined(LODEPNG_COMPILE_ANCILLARY_CHUNKS)) || defined(LODEPNG_COMPILE_ENCODER)
/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned ucvector_push_back(ucvector* p, unsigned char c)
{
  if(!ucvector_resize(p, p->size + 1)) return 0;
  p->data[p->size - 1] = c;
  return 1;
}
#endif /*defined(LODEPNG_COMPILE_PNG) || defined(LODEPNG_COMPILE_ENCODER)*/

#ifdef LODEPNG_COMPILE_PNG
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
/*returns 1 if success, 0 if failure ==> nothing done*/
static unsigned string_resize(char** out, size_t size)
{
  char* data = (char*)lodepng_realloc(*out, size + 1);
  if(data)
  {
    data[size] = 0; /*null termination char*/
    *out = data;
  }
  return data != 0;
}

/*init a {char*, size_t} pair for use as string*/
static void string_init(char** out)
{
  *out = 0;
  string_resize(out, 0);
}

/*free the above pair again*/
static void string_cleanup(char** out)
{
  free(*out);
  *out = 0;
}

static void string_set(char** out, const char* in)
{
  size_t insize = strlen(in), i;
  //+ 8: Work around potential crash
  if(string_resize(out, insize + 8))
  {
    for(i = 0; i != insize; ++i)
    {
      (*out)[i] = in[i];
    }
  }
}
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
#endif /*LODEPNG_COMPILE_PNG*/

static unsigned lodepng_read32bitInt(const unsigned char* buffer)
{
  return (unsigned)((buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3]);
}

#if defined(LODEPNG_COMPILE_PNG) || defined(LODEPNG_COMPILE_ENCODER)
/*buffer must have at least 4 allocated bytes available*/
static void lodepng_set32bitInt(unsigned char* buffer, unsigned value)
{
  buffer[0] = (unsigned char)((value >> 24) & 0xff);
  buffer[1] = (unsigned char)((value >> 16) & 0xff);
  buffer[2] = (unsigned char)((value >>  8) & 0xff);
  buffer[3] = (unsigned char)((value      ) & 0xff);
}
#endif /*defined(LODEPNG_COMPILE_PNG) || defined(LODEPNG_COMPILE_ENCODER)*/

#ifdef LODEPNG_COMPILE_ENCODER
static void lodepng_add32bitInt(ucvector* buffer, unsigned value)
{
  ucvector_resize(buffer, buffer->size + 4); /*todo: give error if resize failed*/
  lodepng_set32bitInt(&buffer->data[buffer->size - 4], value);
}
#endif /*LODEPNG_COMPILE_ENCODER*/

/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */
/* // End of common code and tools. Begin of Zlib related code.            // */
/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */

#ifdef LODEPNG_COMPILE_ZLIB
#ifdef LODEPNG_COMPILE_DECODER

unsigned lodepng_inflate(unsigned char** out, size_t* outsize,
                                const unsigned char* in, size_t insize)
{

	z_stream inf;
  inf.zalloc = 0;
  inf.zfree = 0;
  inf.opaque = 0;
  inf.next_in = (z_const Byte *)in;
  inf.avail_in = (uInt)insize;

//The reallocation speed on windows(or at least mingw) is pretty bad which makes this a lot faster. A bigger buffer would be even better on large images.
#if defined(_WIN32) || defined(WIN32)
#define BUFSIZE 1024 * 128
 unsigned char* buf = (unsigned char*)malloc(BUFSIZE);
 if(!buf) {exit(1);}
#else
	#define BUFSIZE 1024 * 32
  unsigned char buf[BUFSIZE];
#endif
 
  inf.next_out = buf;
  inf.avail_out = BUFSIZE;

  if(inflateInit2(&inf, -15) != Z_OK){return 83;}

  while(1){
    int err = inflate(&inf, Z_SYNC_FLUSH);
    if (err == Z_OK){
      (*out) = (unsigned char*)realloc((*out), (*outsize) + BUFSIZE);
      memcpy(&((*out)[(*outsize)]), buf, BUFSIZE);
      (*outsize) += BUFSIZE;
      inf.next_out = buf;
      inf.avail_out = BUFSIZE;
    }
    else if(err == Z_STREAM_END){
      unsigned consumed = BUFSIZE - inf.avail_out;
      (*out) = (unsigned char*)realloc((*out), (*outsize) + consumed);
      memcpy(&((*out)[(*outsize)]), buf, consumed);
      (*outsize) += consumed;
      break;
    }
    else{
#if defined(_WIN32) || defined(WIN32)
      free(buf);
#endif
      unsigned ret = 95;
      if(err == Z_MEM_ERROR){ret = 83;}
      inflateEnd(&inf);
      return ret;
    }
  }
  if(inflateEnd(&inf) != Z_OK){
#if defined(_WIN32) || defined(WIN32)
    free(buf);
#endif
    return 83;
  }
  
#if defined(_WIN32) || defined(WIN32)
	free(buf);
#endif
  return 0;
}

#endif /*LODEPNG_COMPILE_DECODER*/

#ifdef LODEPNG_COMPILE_ENCODER

/* ////////////////////////////////////////////////////////////////////////// */
/* / Deflator (Compressor)                                                  / */
/* ////////////////////////////////////////////////////////////////////////// */

static unsigned deflate(unsigned char** out, size_t* outsize,
                        const unsigned char* in, size_t insize,
                        const LodePNGCompressSettings* settings)
{
    return settings->custom_deflate(out, outsize, in, insize, settings);
}

#endif /*LODEPNG_COMPILE_DECODER*/

/*Return the adler32 of the bytes data[0..len-1]*/
static unsigned adler32(const unsigned char* data, unsigned len)
{
  return adler32(1, data, len);
}

/* ////////////////////////////////////////////////////////////////////////// */
/* / Zlib                                                                   / */
/* ////////////////////////////////////////////////////////////////////////// */

#ifdef LODEPNG_COMPILE_DECODER

unsigned lodepng_zlib_decompress(unsigned char** out, size_t* outsize, const unsigned char* in,
                                 size_t insize)
{
  unsigned error = 0;
  unsigned CM, CINFO, FDICT;

  if(insize < 2) return 53; /*error, size of zlib data too small*/
  /*read information from zlib header*/
  if((in[0] * 256 + in[1]) % 31 != 0)
  {
    /*error: 256 * in[0] + in[1] must be a multiple of 31, the FCHECK value is supposed to be made that way*/
    return 24;
  }

  CM = in[0] & 15;
  CINFO = (in[0] >> 4) & 15;
  /*FCHECK = in[1] & 31;*/ /*FCHECK is already tested above*/
  FDICT = (in[1] >> 5) & 1;
  /*FLEVEL = (in[1] >> 6) & 3;*/ /*FLEVEL is not used here*/

  if(CM != 8 || CINFO > 7)
  {
    /*error: only compression method 8: inflate with sliding window of 32k is supported by the PNG spec*/
    return 25;
  }
  if(FDICT != 0)
  {
    /*error: the specification of PNG says about the zlib stream:
      "The additional flags shall not specify a preset dictionary."*/
    return 26;
  }

  error = lodepng_inflate(out, outsize, in + 2, insize - 2);
  if(error) return error;

  unsigned ADLER32 = lodepng_read32bitInt(&in[insize - 4]);
  unsigned checksum = adler32(*out, (unsigned)(*outsize));
  if(checksum != ADLER32) return 58; /*error, adler checksum not correct, data must be corrupted*/

  return 0; /*no error*/
}

#endif /*LODEPNG_COMPILE_DECODER*/

#ifdef LODEPNG_COMPILE_ENCODER

static unsigned lodepng_zlib_compress(unsigned char** out, size_t* outsize, const unsigned char* in,
                               size_t insize, const LodePNGCompressSettings* settings)
{
  /*initially, *out must be NULL and outsize 0, if you just give some random *out
  that's pointing to a non allocated buffer, this'll crash*/
  ucvector outv;
  size_t i;
  unsigned error;
  unsigned char* deflatedata = 0;
  size_t deflatesize = 0;

  /*zlib data: 1 byte CMF (CM+CINFO), 1 byte FLG, deflate data, 4 byte ADLER32 checksum of the Decompressed data*/
  unsigned CMF = 120; /*0b01111000: CM 8, CINFO 7. With CINFO 7, any window size up to 32768 can be used.*/
  unsigned FLEVEL = 3;
  unsigned FDICT = 0;
  unsigned CMFFLG = 256 * CMF + FDICT * 32 + FLEVEL * 64;
  unsigned FCHECK = 31 - CMFFLG % 31;
  CMFFLG += FCHECK;

  /*ucvector-controlled version of the output buffer, for dynamic array*/
  ucvector_init_buffer(&outv, *out, *outsize);

  ucvector_push_back(&outv, (unsigned char)(CMFFLG / 256));
  ucvector_push_back(&outv, (unsigned char)(CMFFLG % 256));

  error = deflate(&deflatedata, &deflatesize, in, insize, settings);

  if(!error)
  {
    unsigned ADLER32 = adler32(in, (unsigned)insize);
    for(i = 0; i != deflatesize; ++i) ucvector_push_back(&outv, deflatedata[i]);
    free(deflatedata);
    lodepng_add32bitInt(&outv, ADLER32);
  }

  *out = outv.data;
  *outsize = outv.size;

  return error;
}

#endif /*LODEPNG_COMPILE_ENCODER*/
#endif /*LODEPNG_COMPILE_ZLIB*/

#ifdef LODEPNG_COMPILE_ENCODER

void lodepng_compress_settings_init(LodePNGCompressSettings* settings)
{
  /*compress with dynamic huffman tree (not in the mathematical sense, just not the predefined one)*/
  settings->btype = 2;
  settings->minmatch = 3;
  settings->nicematch = 128;
  settings->lazymatching = 1;

  settings->custom_deflate = 0;
  settings->custom_context = 0;
}

#endif /*LODEPNG_COMPILE_ENCODER*/

/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */
/* // End of Zlib related code. Begin of PNG related code.                 // */
/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */

#ifdef LODEPNG_COMPILE_PNG

/*Return the CRC of the bytes buf[0..len-1].*/
static unsigned lodepng_crc32(const unsigned char* buf, size_t len)
{
  return crc32(0, buf, len);
}

/* ////////////////////////////////////////////////////////////////////////// */
/* / Reading and writing single bits and bytes from/to stream for LodePNG   / */
/* ////////////////////////////////////////////////////////////////////////// */

static unsigned char readBitFromReversedStream(size_t* bitpointer, const unsigned char* bitstream)
{
  unsigned char result = (unsigned char)((bitstream[(*bitpointer) >> 3] >> (7 - ((*bitpointer) & 0x7))) & 1);
  ++(*bitpointer);
  return result;
}

static unsigned readBitsFromReversedStream(size_t* bitpointer, const unsigned char* bitstream, size_t nbits)
{
  unsigned result = 0;
  size_t i;
  for(i = nbits - 1; i < nbits; --i)
  {
    result += (unsigned)readBitFromReversedStream(bitpointer, bitstream) << i;
  }
  return result;
}

#ifdef LODEPNG_COMPILE_DECODER
static void setBitOfReversedStream0(size_t* bitpointer, unsigned char* bitstream, unsigned char bit)
{
  /*the current bit in bitstream must be 0 for this to work*/
  if(bit)
  {
    /*earlier bit of huffman code is in a lesser significant bit of an earlier byte*/
    bitstream[(*bitpointer) >> 3] |= (bit << (7 - ((*bitpointer) & 0x7)));
  }
  ++(*bitpointer);
}
#endif /*LODEPNG_COMPILE_DECODER*/

static void setBitOfReversedStream(size_t* bitpointer, unsigned char* bitstream, unsigned char bit)
{
  /*the current bit in bitstream may be 0 or 1 for this to work*/
  if(bit == 0) bitstream[(*bitpointer) >> 3] &=  (unsigned char)(~(1 << (7 - ((*bitpointer) & 0x7))));
  else         bitstream[(*bitpointer) >> 3] |=  (1 << (7 - ((*bitpointer) & 0x7)));
  ++(*bitpointer);
}

/* ////////////////////////////////////////////////////////////////////////// */
/* / PNG chunks                                                             / */
/* ////////////////////////////////////////////////////////////////////////// */

unsigned lodepng_chunk_length(const unsigned char* chunk)
{
  return lodepng_read32bitInt(&chunk[0]);
}

void lodepng_chunk_type(char type[5], const unsigned char* chunk)
{
  unsigned i;
  for(i = 0; i != 4; ++i) type[i] = (char)chunk[4 + i];
  type[4] = 0; /*null termination char*/
}

unsigned char lodepng_chunk_type_equals(const unsigned char* chunk, const char* type)
{
  if(strlen(type) != 4) return 0;
  return (chunk[4] == type[0] && chunk[5] == type[1] && chunk[6] == type[2] && chunk[7] == type[3]);
}

unsigned char lodepng_chunk_ancillary(const unsigned char* chunk)
{
  return((chunk[4] & 32) != 0);
}

const unsigned char* lodepng_chunk_data_const(const unsigned char* chunk)
{
  return &chunk[8];
}

static unsigned lodepng_chunk_check_crc(const unsigned char* chunk)
{
  unsigned length = lodepng_chunk_length(chunk);
  unsigned CRC = lodepng_read32bitInt(&chunk[length + 8]);
  /*the CRC is taken of the data and the 4 chunk type letters, not the length*/
  unsigned checksum = lodepng_crc32(&chunk[4], length + 4);
  if(CRC != checksum) return 1;
  else return 0;
}

static void lodepng_chunk_generate_crc(unsigned char* chunk)
{
  unsigned length = lodepng_chunk_length(chunk);
  unsigned CRC = lodepng_crc32(&chunk[4], length + 4);
  lodepng_set32bitInt(chunk + 8 + length, CRC);
}

unsigned char* lodepng_chunk_next(unsigned char* chunk)
{
  unsigned total_chunk_length = lodepng_chunk_length(chunk) + 12;
  return &chunk[total_chunk_length];
}

const unsigned char* lodepng_chunk_next_const(const unsigned char* chunk)
{
  unsigned total_chunk_length = lodepng_chunk_length(chunk) + 12;
  return &chunk[total_chunk_length];
}

static unsigned lodepng_chunk_append(unsigned char** out, size_t* outlength, const unsigned char* chunk)
{
  unsigned i;
  unsigned total_chunk_length = lodepng_chunk_length(chunk) + 12;
  unsigned char *chunk_start, *new_buffer;
  size_t new_length = (*outlength) + total_chunk_length;
  if(new_length < total_chunk_length || new_length < (*outlength)) return 77; /*integer overflow happened*/

  new_buffer = (unsigned char*)lodepng_realloc(*out, new_length);
  if(!new_buffer) return 83; /*alloc fail*/
  (*out) = new_buffer;
  (*outlength) = new_length;
  chunk_start = &(*out)[new_length - total_chunk_length];

  for(i = 0; i != total_chunk_length; ++i) chunk_start[i] = chunk[i];

  return 0;
}

unsigned lodepng_chunk_create(unsigned char** out, size_t* outlength, unsigned length,
                              const char* type, const unsigned char* data)
{
  unsigned i;
  unsigned char *chunk, *new_buffer;
  size_t new_length = (*outlength) + length + 12;
  if(new_length < length + 12 || new_length < (*outlength)) return 77; /*integer overflow happened*/
  new_buffer = (unsigned char*)lodepng_realloc(*out, new_length);
  if(!new_buffer) return 83; /*alloc fail*/
  (*out) = new_buffer;
  (*outlength) = new_length;
  chunk = &(*out)[(*outlength) - length - 12];

  /*1: length*/
  lodepng_set32bitInt(chunk, (unsigned)length);

  /*2: chunk name (4 letters)*/
  chunk[4] = (unsigned char)type[0];
  chunk[5] = (unsigned char)type[1];
  chunk[6] = (unsigned char)type[2];
  chunk[7] = (unsigned char)type[3];

  /*3: the data*/
  for(i = 0; i != length; ++i) chunk[8 + i] = data[i];

  /*4: CRC (of the chunkname characters and the data)*/
  lodepng_chunk_generate_crc(chunk);

  return 0;
}

/* ////////////////////////////////////////////////////////////////////////// */
/* / Color types and such                                                   / */
/* ////////////////////////////////////////////////////////////////////////// */

/*return type is a LodePNG error code*/
static unsigned checkColorValidity(LodePNGColorType colortype, unsigned bd) /*bd = bitdepth*/
{
  switch(colortype)
  {
    case 0: if(!(bd == 1 || bd == 2 || bd == 4 || bd == 8 || bd == 16)) return 37; break; /*grey*/
    case 2: if(!(                                 bd == 8 || bd == 16)) return 37; break; /*RGB*/
    case 3: if(!(bd == 1 || bd == 2 || bd == 4 || bd == 8            )) return 37; break; /*palette*/
    case 4: if(!(                                 bd == 8 || bd == 16)) return 37; break; /*grey + alpha*/
    case 6: if(!(                                 bd == 8 || bd == 16)) return 37; break; /*RGBA*/
    default: return 31;
  }
  return 0; /*allowed color type / bits combination*/
}

static unsigned getNumColorChannels(LodePNGColorType colortype)
{
  switch(colortype)
  {
    case 0: return 1; /*grey*/
    case 2: return 3; /*RGB*/
    case 3: return 1; /*palette*/
    case 4: return 2; /*grey + alpha*/
    case 6: return 4; /*RGBA*/
  }
  return 0; /*unexisting color type*/
}

static unsigned lodepng_get_bpp_lct(LodePNGColorType colortype, unsigned bitdepth)
{
  /*bits per pixel is amount of channels * bits per channel*/
  return getNumColorChannels(colortype) * bitdepth;
}

void lodepng_color_mode_init(LodePNGColorMode* info)
{
  info->key_defined = 0;
  info->key_r = info->key_g = info->key_b = 0;
  info->colortype = LCT_RGBA;
  info->bitdepth = 8;
  info->palette = 0;
  info->palettesize = 0;
}

void lodepng_color_mode_cleanup(LodePNGColorMode* info)
{
  lodepng_palette_clear(info);
}

unsigned lodepng_color_mode_copy(LodePNGColorMode* dest, const LodePNGColorMode* source)
{
  size_t i;
  lodepng_color_mode_cleanup(dest);
  *dest = *source;
  if(source->palette)
  {
    dest->palette = (unsigned char*)lodepng_malloc(1024);
    if(!dest->palette && source->palettesize) return 83; /*alloc fail*/
    for(i = 0; i != source->palettesize * 4; ++i) dest->palette[i] = source->palette[i];
  }
  return 0;
}

static int lodepng_color_mode_equal(const LodePNGColorMode* a, const LodePNGColorMode* b)
{
  size_t i;
  if(a->colortype != b->colortype) return 0;
  if(a->bitdepth != b->bitdepth) return 0;
  if(a->key_defined != b->key_defined) return 0;
  if(a->key_defined)
  {
    if(a->key_r != b->key_r) return 0;
    if(a->key_g != b->key_g) return 0;
    if(a->key_b != b->key_b) return 0;
  }
  if(a->palettesize != b->palettesize) return 0;
  for(i = 0; i != a->palettesize * 4; ++i)
  {
    if(a->palette[i] != b->palette[i]) return 0;
  }
  return 1;
}

void lodepng_palette_clear(LodePNGColorMode* info)
{
  if(info->palette) free(info->palette);
  info->palette = 0;
  info->palettesize = 0;
}

static unsigned lodepng_palette_add(LodePNGColorMode* info,
                             unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  unsigned char* data;
  /*the same resize technique as C++ std::vectors is used, and here it's made so that for a palette with
  the max of 256 colors, it'll have the exact alloc size*/
  if(!info->palette) /*allocate palette if empty*/
  {
    /*room for 256 colors with 4 bytes each*/
    data = (unsigned char*)lodepng_realloc(info->palette, 1024);
    if(!data) return 83; /*alloc fail*/
    else info->palette = data;
  }
  info->palette[4 * info->palettesize] = r;
  info->palette[4 * info->palettesize + 1] = g;
  info->palette[4 * info->palettesize + 2] = b;
  info->palette[4 * info->palettesize + 3] = a;
  ++info->palettesize;
  return 0;
}

static unsigned lodepng_get_bpp(const LodePNGColorMode* info)
{
  /*calculate bits per pixel out of colortype and bitdepth*/
  return lodepng_get_bpp_lct(info->colortype, info->bitdepth);
}

unsigned lodepng_is_greyscale_type(const LodePNGColorMode* info)
{
  return info->colortype == LCT_GREY || info->colortype == LCT_GREY_ALPHA;
}

unsigned lodepng_is_alpha_type(const LodePNGColorMode* info)
{
  return (info->colortype & 4) != 0; /*4 or 6*/
}

unsigned lodepng_has_palette_alpha(const LodePNGColorMode* info)
{
  size_t i;
  for(i = 0; i != info->palettesize; ++i)
  {
    if(info->palette[i * 4 + 3] < 255) return 1;
  }
  return 0;
}

unsigned lodepng_can_have_alpha(const LodePNGColorMode* info)
{
  return info->key_defined
      || lodepng_is_alpha_type(info)
      || lodepng_has_palette_alpha(info);
}

size_t lodepng_get_raw_size(unsigned w, unsigned h, const LodePNGColorMode* color)
{
  return ((unsigned long long) w * h * lodepng_get_bpp(color) + 7) / 8;
}

#ifdef LODEPNG_COMPILE_PNG
#ifdef LODEPNG_COMPILE_DECODER
/*in an idat chunk, each scanline is a multiple of 8 bits, unlike the lodepng output buffer*/
static size_t lodepng_get_raw_size_idat(unsigned w, unsigned h, const LodePNGColorMode* color)
{
  /*will not overflow for any color type if roughly w * h < 268435455*/
  int bpp = lodepng_get_bpp(color);
  size_t line = ((w / 8) * bpp) + ((w & 7) * bpp + 7) / 8;
  return h * line;
}
#endif /*LODEPNG_COMPILE_DECODER*/
#endif /*LODEPNG_COMPILE_PNG*/

#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS

static void LodePNGUnknownChunks_init(LodePNGInfo* info)
{
  unsigned i;
  for(i = 0; i != 3; ++i) info->unknown_chunks_data[i] = 0;
  for(i = 0; i != 3; ++i) info->unknown_chunks_size[i] = 0;
}

static void LodePNGUnknownChunks_cleanup(LodePNGInfo* info)
{
  unsigned i;
  for(i = 0; i != 3; ++i) free(info->unknown_chunks_data[i]);
}

static unsigned LodePNGUnknownChunks_copy(LodePNGInfo* dest, const LodePNGInfo* src)
{
  unsigned i;

  LodePNGUnknownChunks_cleanup(dest);

  for(i = 0; i != 3; ++i)
  {
    size_t j;
    dest->unknown_chunks_size[i] = src->unknown_chunks_size[i];
    dest->unknown_chunks_data[i] = (unsigned char*)lodepng_malloc(src->unknown_chunks_size[i]);
    if(!dest->unknown_chunks_data[i] && dest->unknown_chunks_size[i]) return 83; /*alloc fail*/
    for(j = 0; j < src->unknown_chunks_size[i]; ++j)
    {
      dest->unknown_chunks_data[i][j] = src->unknown_chunks_data[i][j];
    }
  }

  return 0;
}

static void LodePNGText_init(LodePNGInfo* info)
{
  info->text_num = 0;
  info->text_keys = 0;
  info->text_strings = 0;
}

static void LodePNGText_cleanup(LodePNGInfo* info)
{
  size_t i;
  for(i = 0; i != info->text_num; ++i)
  {
    string_cleanup(&info->text_keys[i]);
    string_cleanup(&info->text_strings[i]);
  }
  free(info->text_keys);
  free(info->text_strings);
}

static unsigned LodePNGText_copy(LodePNGInfo* dest, const LodePNGInfo* source)
{
  size_t i = 0;
  dest->text_keys = 0;
  dest->text_strings = 0;
  dest->text_num = 0;
  for(i = 0; i != source->text_num; ++i)
  {
    CERROR_TRY_RETURN(lodepng_add_text(dest, source->text_keys[i], source->text_strings[i]));
  }
  return 0;
}

unsigned lodepng_add_text(LodePNGInfo* info, const char* key, const char* str)
{
  char** new_keys = (char**)(lodepng_realloc(info->text_keys, sizeof(char*) * (info->text_num + 1)));
  char** new_strings = (char**)(lodepng_realloc(info->text_strings, sizeof(char*) * (info->text_num + 1)));
  if(!new_keys || !new_strings)
  {
    free(new_keys);
    free(new_strings);
    return 83; /*alloc fail*/
  }

  ++info->text_num;
  info->text_keys = new_keys;
  info->text_strings = new_strings;

  string_init(&info->text_keys[info->text_num - 1]);
  string_set(&info->text_keys[info->text_num - 1], key);

  string_init(&info->text_strings[info->text_num - 1]);
  string_set(&info->text_strings[info->text_num - 1], str);

  return 0;
}

static void LodePNGIText_init(LodePNGInfo* info)
{
  info->itext_num = 0;
  info->itext_keys = 0;
  info->itext_langtags = 0;
  info->itext_transkeys = 0;
  info->itext_strings = 0;
}

static void LodePNGIText_cleanup(LodePNGInfo* info)
{
  size_t i;
  for(i = 0; i != info->itext_num; ++i)
  {
    string_cleanup(&info->itext_keys[i]);
    string_cleanup(&info->itext_langtags[i]);
    string_cleanup(&info->itext_transkeys[i]);
    string_cleanup(&info->itext_strings[i]);
  }
  free(info->itext_keys);
  free(info->itext_langtags);
  free(info->itext_transkeys);
  free(info->itext_strings);
}

static unsigned LodePNGIText_copy(LodePNGInfo* dest, const LodePNGInfo* source)
{
  size_t i = 0;
  dest->itext_keys = 0;
  dest->itext_langtags = 0;
  dest->itext_transkeys = 0;
  dest->itext_strings = 0;
  dest->itext_num = 0;
  for(i = 0; i != source->itext_num; ++i)
  {
    CERROR_TRY_RETURN(lodepng_add_itext(dest, source->itext_keys[i], source->itext_langtags[i],
                                        source->itext_transkeys[i], source->itext_strings[i]));
  }
  return 0;
}

unsigned lodepng_add_itext(LodePNGInfo* info, const char* key, const char* langtag,
                           const char* transkey, const char* str)
{
  char** new_keys = (char**)(lodepng_realloc(info->itext_keys, sizeof(char*) * (info->itext_num + 1)));
  char** new_langtags = (char**)(lodepng_realloc(info->itext_langtags, sizeof(char*) * (info->itext_num + 1)));
  char** new_transkeys = (char**)(lodepng_realloc(info->itext_transkeys, sizeof(char*) * (info->itext_num + 1)));
  char** new_strings = (char**)(lodepng_realloc(info->itext_strings, sizeof(char*) * (info->itext_num + 1)));
  if(!new_keys || !new_langtags || !new_transkeys || !new_strings)
  {
    free(new_keys);
    free(new_langtags);
    free(new_transkeys);
    free(new_strings);
    return 83; /*alloc fail*/
  }

  ++info->itext_num;
  info->itext_keys = new_keys;
  info->itext_langtags = new_langtags;
  info->itext_transkeys = new_transkeys;
  info->itext_strings = new_strings;

  string_init(&info->itext_keys[info->itext_num - 1]);
  string_set(&info->itext_keys[info->itext_num - 1], key);

  string_init(&info->itext_langtags[info->itext_num - 1]);
  string_set(&info->itext_langtags[info->itext_num - 1], langtag);

  string_init(&info->itext_transkeys[info->itext_num - 1]);
  string_set(&info->itext_transkeys[info->itext_num - 1], transkey);

  string_init(&info->itext_strings[info->itext_num - 1]);
  string_set(&info->itext_strings[info->itext_num - 1], str);

  return 0;
}
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/

static void lodepng_info_init(LodePNGInfo* info)
{
  lodepng_color_mode_init(&info->color);
  info->interlace_method = 0;
  info->compression_method = 0;
  info->filter_method = 0;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  info->background_defined = 0;
  info->background_r = info->background_g = info->background_b = 0;

  LodePNGText_init(info);
  LodePNGIText_init(info);

  LodePNGUnknownChunks_init(info);
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
}

static void lodepng_info_cleanup(LodePNGInfo* info)
{
  lodepng_color_mode_cleanup(&info->color);
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  LodePNGText_cleanup(info);
  LodePNGIText_cleanup(info);

  LodePNGUnknownChunks_cleanup(info);
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
}

static unsigned lodepng_info_copy(LodePNGInfo* dest, const LodePNGInfo* source)
{
  lodepng_info_cleanup(dest);
  *dest = *source;
  lodepng_color_mode_init(&dest->color);
  CERROR_TRY_RETURN(lodepng_color_mode_copy(&dest->color, &source->color));

#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  CERROR_TRY_RETURN(LodePNGText_copy(dest, source));
  CERROR_TRY_RETURN(LodePNGIText_copy(dest, source));

  LodePNGUnknownChunks_init(dest);
  CERROR_TRY_RETURN(LodePNGUnknownChunks_copy(dest, source));
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
  return 0;
}

/*index: bitgroup index, bits: bitgroup size(1, 2 or 4), in: bitgroup value, out: octet array to add bits to*/
static void addColorBits(unsigned char* out, size_t index, unsigned bits, unsigned in)
{
  unsigned m = bits == 1 ? 7 : bits == 2 ? 3 : 1; /*8 / bits - 1*/
  /*p = the partial index in the byte, e.g. with 4 palettebits it is 0 for first half or 1 for second half*/
  unsigned p = index & m;
  in &= (1u << bits) - 1u; /*filter out any other bits of the input value*/
  in = in << (bits * (m - p));
  if(p == 0) out[index * bits / 8] = in;
  else out[index * bits / 8] |= in;
}

typedef struct ColorTree ColorTree;

/*
One node of a color tree
This is the data structure used to count the number of unique colors and to get a palette
index for a color. It's like an octree, but because the alpha channel is used too, each
node has 16 instead of 8 children.
*/
struct ColorTree
{
  ColorTree* children[16]; /*up to 16 pointers to ColorTree of next level*/
  int index; /*the payload. Only has a meaningful value if this is in the last level*/
};

static void color_tree_init(ColorTree* tree)
{
  int i;
  for(i = 0; i != 16; ++i) tree->children[i] = 0;
  tree->index = -1;
}

static void color_tree_cleanup(ColorTree* tree)
{
  int i;
  for(i = 0; i != 16; ++i)
  {
    if(tree->children[i])
    {
      color_tree_cleanup(tree->children[i]);
      free(tree->children[i]);
    }
  }
}

/*returns -1 if color not present, its index otherwise*/
static int color_tree_get(ColorTree* tree, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  static const unsigned colortable[256] =
  {
    0U,
    1U,16U,17U,256U,257U,272U,273U,4096U,
    4097U,4112U,4113U,4352U,4353U,4368U,4369U,65536U,
    65537U,65552U,65553U,65792U,65793U,65808U,65809U,69632U,
    69633U,69648U,69649U,69888U,69889U,69904U,69905U,1048576U,
    1048577U,1048592U,1048593U,1048832U,1048833U,1048848U,1048849U,1052672U,
    1052673U,1052688U,1052689U,1052928U,1052929U,1052944U,1052945U,1114112U,
    1114113U,1114128U,1114129U,1114368U,1114369U,1114384U,1114385U,1118208U,
    1118209U,1118224U,1118225U,1118464U,1118465U,1118480U,1118481U,16777216U,
    16777217U,16777232U,16777233U,16777472U,16777473U,16777488U,16777489U,16781312U,
    16781313U,16781328U,16781329U,16781568U,16781569U,16781584U,16781585U,16842752U,
    16842753U,16842768U,16842769U,16843008U,16843009U,16843024U,16843025U,16846848U,
    16846849U,16846864U,16846865U,16847104U,16847105U,16847120U,16847121U,17825792U,
    17825793U,17825808U,17825809U,17826048U,17826049U,17826064U,17826065U,17829888U,
    17829889U,17829904U,17829905U,17830144U,17830145U,17830160U,17830161U,17891328U,
    17891329U,17891344U,17891345U,17891584U,17891585U,17891600U,17891601U,17895424U,
    17895425U,17895440U,17895441U,17895680U,17895681U,17895696U,17895697U,268435456U,
    268435457U,268435472U,268435473U,268435712U,268435713U,268435728U,268435729U,268439552U,
    268439553U,268439568U,268439569U,268439808U,268439809U,268439824U,268439825U,268500992U,
    268500993U,268501008U,268501009U,268501248U,268501249U,268501264U,268501265U,268505088U,
    268505089U,268505104U,268505105U,268505344U,268505345U,268505360U,268505361U,269484032U,
    269484033U,269484048U,269484049U,269484288U,269484289U,269484304U,269484305U,269488128U,
    269488129U,269488144U,269488145U,269488384U,269488385U,269488400U,269488401U,269549568U,
    269549569U,269549584U,269549585U,269549824U,269549825U,269549840U,269549841U,269553664U,
    269553665U,269553680U,269553681U,269553920U,269553921U,269553936U,269553937U,285212672U,
    285212673U,285212688U,285212689U,285212928U,285212929U,285212944U,285212945U,285216768U,
    285216769U,285216784U,285216785U,285217024U,285217025U,285217040U,285217041U,285278208U,
    285278209U,285278224U,285278225U,285278464U,285278465U,285278480U,285278481U,285282304U,
    285282305U,285282320U,285282321U,285282560U,285282561U,285282576U,285282577U,286261248U,
    286261249U,286261264U,286261265U,286261504U,286261505U,286261520U,286261521U,286265344U,
    286265345U,286265360U,286265361U,286265600U,286265601U,286265616U,286265617U,286326784U,
    286326785U,286326800U,286326801U,286327040U,286327041U,286327056U,286327057U,286330880U,
    286330881U,286330896U,286330897U,286331136U,286331137U,286331152U,286331153U
  };

  unsigned x = 0;
  x |= colortable[r] << 3;
  x |= colortable[g] << 2;
  x |= colortable[b] << 1;
  x |= colortable[a];

  for(int bit = 0; bit < 8; ++bit)
  {
    unsigned i = x & 15;
    if(!tree->children[i]) return -1;
    else tree = tree->children[i];
    x>>=4;
  }
  return tree ? tree->index : -1;
}

static int color_tree_inc(ColorTree* tree,
                          unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  int bit;
  for(bit = 0; bit < 8; ++bit)
  {
    int i = ((r >> bit) & 1) << 3 | ((g >> bit) & 1) << 2 | ((b >> bit) & 1) << 1 | ((a >> bit) & 1);
    if(!tree->children[i])
    {
      tree->children[i] = (ColorTree*)lodepng_malloc(sizeof(ColorTree));
      color_tree_init(tree->children[i]);
    }
    tree = tree->children[i];
  }
  return ++(tree->index);
}

#ifdef LODEPNG_COMPILE_ENCODER
static int color_tree_has(ColorTree* tree, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  return color_tree_get(tree, r, g, b, a) >= 0;
}
#endif /*LODEPNG_COMPILE_ENCODER*/

/*color is not allowed to already exist.
Index should be >= 0 (it's signed to be compatible with using -1 for "doesn't exist")*/
static void color_tree_add(ColorTree* tree,
                           unsigned char r, unsigned char g, unsigned char b, unsigned char a, unsigned index)
{
  int bit;
  for(bit = 0; bit < 8; ++bit)
  {
    int i = 8 * ((r >> bit) & 1) + 4 * ((g >> bit) & 1) + 2 * ((b >> bit) & 1) + 1 * ((a >> bit) & 1);
    if(!tree->children[i])
    {
      tree->children[i] = (ColorTree*)lodepng_malloc(sizeof(ColorTree));
      color_tree_init(tree->children[i]);
    }
    tree = tree->children[i];
  }
  tree->index = (int)index;
}

/*put a pixel, given its RGBA color, into image of any color type*/
static unsigned rgba8ToPixel(unsigned char* out, size_t i,
                             const LodePNGColorMode* mode, ColorTree* tree /*for palette*/,
                             unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
  if(mode->colortype == LCT_GREY)
  {
    unsigned char grey = r; /*((unsigned short)r + g + b) / 3*/;
    if(mode->bitdepth == 8) out[i] = grey;
    else if(mode->bitdepth == 16) out[i * 2] = out[i * 2 + 1] = grey;
    else
    {
      /*take the most significant bits of grey*/
      grey = (grey >> (8 - mode->bitdepth)) & ((1 << mode->bitdepth) - 1);
      addColorBits(out, i, mode->bitdepth, grey);
    }
  }
  else if(mode->colortype == LCT_RGB)
  {
    if(mode->bitdepth == 8)
    {
      out[i * 3] = r;
      out[i * 3 + 1] = g;
      out[i * 3 + 2] = b;
    }
    else
    {
      out[i * 6] = out[i * 6 + 1] = r;
      out[i * 6 + 2] = out[i * 6 + 3] = g;
      out[i * 6 + 4] = out[i * 6 + 5] = b;
    }
  }
  else if(mode->colortype == LCT_PALETTE)
  {
    int index = color_tree_get(tree, r, g, b, a);
    if(index < 0) return 82; /*color not in palette*/
    if(mode->bitdepth == 8) out[i] = index;
    else addColorBits(out, i, mode->bitdepth, (unsigned)index);
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    unsigned char grey = r; /*((unsigned short)r + g + b) / 3*/;
    if(mode->bitdepth == 8)
    {
      out[i * 2] = grey;
      out[i * 2 + 1] = a;
    }
    else if(mode->bitdepth == 16)
    {
      out[i * 4] = out[i * 4 + 1] = grey;
      out[i * 4 + 2] = out[i * 4 + 3] = a;
    }
  }
  else if(mode->colortype == LCT_RGBA)
  {
    if(mode->bitdepth == 8)
    {
      out[i * 4] = r;
      out[i * 4 + 1] = g;
      out[i * 4 + 2] = b;
      out[i * 4 + 3] = a;
    }
    else
    {
      out[i * 8] = out[i * 8 + 1] = r;
      out[i * 8 + 2] = out[i * 8 + 3] = g;
      out[i * 8 + 4] = out[i * 8 + 5] = b;
      out[i * 8 + 6] = out[i * 8 + 7] = a;
    }
  }

  return 0; /*no error*/
}

/*put a pixel, given its RGBA16 color, into image of any color 16-bitdepth type*/
static void rgba16ToPixel(unsigned char* out, size_t i,
                         const LodePNGColorMode* mode,
                         unsigned short r, unsigned short g, unsigned short b, unsigned short a)
{
  if(mode->colortype == LCT_GREY)
  {
    unsigned short grey = r; /*((unsigned)r + g + b) / 3*/;
    out[i * 2] = (grey >> 8) & 255;
    out[i * 2 + 1] = grey & 255;
  }
  else if(mode->colortype == LCT_RGB)
  {
    out[i * 6] = (r >> 8) & 255;
    out[i * 6 + 1] = r & 255;
    out[i * 6 + 2] = (g >> 8) & 255;
    out[i * 6 + 3] = g & 255;
    out[i * 6 + 4] = (b >> 8) & 255;
    out[i * 6 + 5] = b & 255;
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    unsigned short grey = r; /*((unsigned)r + g + b) / 3*/;
    out[i * 4] = (grey >> 8) & 255;
    out[i * 4 + 1] = grey & 255;
    out[i * 4 + 2] = (a >> 8) & 255;
    out[i * 4 + 3] = a & 255;
  }
  else if(mode->colortype == LCT_RGBA)
  {
    out[i * 8] = (r >> 8) & 255;
    out[i * 8 + 1] = r & 255;
    out[i * 8 + 2] = (g >> 8) & 255;
    out[i * 8 + 3] = g & 255;
    out[i * 8 + 4] = (b >> 8) & 255;
    out[i * 8 + 5] = b & 255;
    out[i * 8 + 6] = (a >> 8) & 255;
    out[i * 8 + 7] = a & 255;
  }
}

/*Get RGBA8 color of pixel with index i (y * width + x) from the raw image with given color type.*/
static void getPixelColorRGBA8(unsigned char* r, unsigned char* g,
                               unsigned char* b, unsigned char* a,
                               const unsigned char* in, size_t i,
                               const LodePNGColorMode* mode)
{
  if(mode->colortype == LCT_GREY)
  {
    if(mode->bitdepth == 8)
    {
      *r = *g = *b = in[i];
      if(mode->key_defined && *r == mode->key_r) *a = 0;
      else *a = 255;
    }
    else if(mode->bitdepth == 16)
    {
      *r = *g = *b = in[i * 2];
      if(mode->key_defined && 256U * in[i * 2] + in[i * 2 + 1] == mode->key_r) *a = 0;
      else *a = 255;
    }
    else
    {
      unsigned highest = ((1U << mode->bitdepth) - 1U); /*highest possible value for this bit depth*/
      size_t j = i * mode->bitdepth;
      unsigned value = readBitsFromReversedStream(&j, in, mode->bitdepth);
      *r = *g = *b = (value * 255) / highest;
      if(mode->key_defined && value == mode->key_r) *a = 0;
      else *a = 255;
    }
  }
  else if(mode->colortype == LCT_RGB)
  {
    if(mode->bitdepth == 8)
    {
      *r = in[i * 3]; *g = in[i * 3 + 1]; *b = in[i * 3 + 2];
      if(mode->key_defined && *r == mode->key_r && *g == mode->key_g && *b == mode->key_b) *a = 0;
      else *a = 255;
    }
    else
    {
      *r = in[i * 6];
      *g = in[i * 6 + 2];
      *b = in[i * 6 + 4];
      if(mode->key_defined && 256U * in[i * 6] + in[i * 6 + 1] == mode->key_r
         && 256U * in[i * 6 + 2] + in[i * 6 + 3] == mode->key_g
         && 256U * in[i * 6 + 4] + in[i * 6 + 5] == mode->key_b) *a = 0;
      else *a = 255;
    }
  }
  else if(mode->colortype == LCT_PALETTE)
  {
    unsigned index;
    if(mode->bitdepth == 8) index = in[i];
    else
    {
      size_t j = i * mode->bitdepth;
      index = readBitsFromReversedStream(&j, in, mode->bitdepth);
    }

    if(index >= mode->palettesize)
    {
      /*This is an error according to the PNG spec, but common PNG decoders make it black instead.
      Done here too, slightly faster due to no error handling needed.*/
      *r = *g = *b = 0;
      *a = 255;
    }
    else
    {
      *r = mode->palette[index * 4];
      *g = mode->palette[index * 4 + 1];
      *b = mode->palette[index * 4 + 2];
      *a = mode->palette[index * 4 + 3];
    }
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    if(mode->bitdepth == 8)
    {
      *r = *g = *b = in[i * 2];
      *a = in[i * 2 + 1];
    }
    else
    {
      *r = *g = *b = in[i * 4];
      *a = in[i * 4 + 2];
    }
  }
  else if(mode->colortype == LCT_RGBA)
  {
    if(mode->bitdepth == 8)
    {
      *r = in[i * 4];
      *g = in[i * 4 + 1];
      *b = in[i * 4 + 2];
      *a = in[i * 4 + 3];
    }
    else
    {
      *r = in[i * 8];
      *g = in[i * 8 + 2];
      *b = in[i * 8 + 4];
      *a = in[i * 8 + 6];
    }
  }
}

/*Similar to getPixelColorRGBA8, but with all the for loops inside of the color
mode test cases, optimized to convert the colors much faster, when converting
to RGBA or RGB with 8 bit per cannel. buffer must be RGBA or RGB output with
enough memory, if has_alpha is true the output is RGBA. mode has the color mode
of the input buffer.*/
static void getPixelColorsRGBA8(unsigned char* buffer, size_t numpixels,
                                unsigned has_alpha, const unsigned char* in,
                                const LodePNGColorMode* mode)
{
  unsigned num_channels = has_alpha ? 4 : 3;
  size_t i;
  if(mode->colortype == LCT_GREY)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i];
        if(has_alpha) buffer[3] = mode->key_defined && in[i] == mode->key_r ? 0 : 255;
      }
    }
    else if(mode->bitdepth == 16)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i * 2];
        if(has_alpha) buffer[3] = mode->key_defined && 256U * in[i * 2] + in[i * 2 + 1] == mode->key_r ? 0 : 255;
      }
    }
    else
    {
      unsigned highest = ((1U << mode->bitdepth) - 1U); /*highest possible value for this bit depth*/
      size_t j = 0;
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        unsigned value = readBitsFromReversedStream(&j, in, mode->bitdepth);
        buffer[0] = buffer[1] = buffer[2] = (value * 255) / highest;
        if(has_alpha) buffer[3] = mode->key_defined && value == mode->key_r ? 0 : 255;
      }
    }
  }
  else if(mode->colortype == LCT_RGB)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 3];
        buffer[1] = in[i * 3 + 1];
        buffer[2] = in[i * 3 + 2];
        if(has_alpha) buffer[3] = mode->key_defined && buffer[0] == mode->key_r
           && buffer[1]== mode->key_g && buffer[2] == mode->key_b ? 0 : 255;
      }
    }
    else
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 6];
        buffer[1] = in[i * 6 + 2];
        buffer[2] = in[i * 6 + 4];
        if(has_alpha) buffer[3] = mode->key_defined
           && 256U * in[i * 6] + in[i * 6 + 1] == mode->key_r
           && 256U * in[i * 6 + 2] + in[i * 6 + 3] == mode->key_g
           && 256U * in[i * 6 + 4] + in[i * 6 + 5] == mode->key_b ? 0 : 255;
      }
    }
  }
  else if(mode->colortype == LCT_PALETTE)
  {
    if(mode->bitdepth == 8 && has_alpha){
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        if(in[i] >= mode->palettesize){*(unsigned*)buffer = 0;}
        else{
          *(unsigned*)buffer = *(unsigned*)&mode->palette[in[i] * 4];
        }
      }
    }
    else{
      unsigned index;
      size_t j = 0;
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        if(mode->bitdepth == 8) index = in[i];
        else index = readBitsFromReversedStream(&j, in, mode->bitdepth);

        if(index >= mode->palettesize)
        {
          /*This is an error according to the PNG spec, but most PNG decoders make it black instead.
           Done here too, slightly faster due to no error handling needed.*/
          buffer[0] = buffer[1] = buffer[2] = 0;
          if(has_alpha) buffer[3] = 255;
        }
        else
        {
          buffer[0] = mode->palette[index * 4];
          buffer[1] = mode->palette[index * 4 + 1];
          buffer[2] = mode->palette[index * 4 + 2];
          if(has_alpha) buffer[3] = mode->palette[index * 4 + 3];
        }
      }
    }
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i * 2];
        if(has_alpha) buffer[3] = in[i * 2 + 1];
      }
    }
    else
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = buffer[1] = buffer[2] = in[i * 4];
        if(has_alpha) buffer[3] = in[i * 4 + 2];
      }
    }
  }
  else if(mode->colortype == LCT_RGBA)
  {
    if(mode->bitdepth == 8)
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 4];
        buffer[1] = in[i * 4 + 1];
        buffer[2] = in[i * 4 + 2];
        if(has_alpha) buffer[3] = in[i * 4 + 3];
      }
    }
    else
    {
      for(i = 0; i != numpixels; ++i, buffer += num_channels)
      {
        buffer[0] = in[i * 8];
        buffer[1] = in[i * 8 + 2];
        buffer[2] = in[i * 8 + 4];
        if(has_alpha) buffer[3] = in[i * 8 + 6];
      }
    }
  }
}

/*Get RGBA16 color of pixel with index i (y * width + x) from the raw image with
given color type, but the given color type must be 16-bit itself.*/
static void getPixelColorRGBA16(unsigned short* r, unsigned short* g, unsigned short* b, unsigned short* a,
                                const unsigned char* in, size_t i, const LodePNGColorMode* mode)
{
  if(mode->colortype == LCT_GREY)
  {
    *r = *g = *b = 256 * in[i * 2] + in[i * 2 + 1];
    if(mode->key_defined && 256U * in[i * 2] + in[i * 2 + 1] == mode->key_r) *a = 0;
    else *a = 65535;
  }
  else if(mode->colortype == LCT_RGB)
  {
    *r = 256 * in[i * 6] + in[i * 6 + 1];
    *g = 256 * in[i * 6 + 2] + in[i * 6 + 3];
    *b = 256 * in[i * 6 + 4] + in[i * 6 + 5];
    if(mode->key_defined && 256U * in[i * 6] + in[i * 6 + 1] == mode->key_r
       && 256U * in[i * 6 + 2] + in[i * 6 + 3] == mode->key_g
       && 256U * in[i * 6 + 4] + in[i * 6 + 5] == mode->key_b) *a = 0;
    else *a = 65535;
  }
  else if(mode->colortype == LCT_GREY_ALPHA)
  {
    *r = *g = *b = 256 * in[i * 4] + in[i * 4 + 1];
    *a = 256 * in[i * 4 + 2] + in[i * 4 + 3];
  }
  else if(mode->colortype == LCT_RGBA)
  {
    *r = 256 * in[i * 8] + in[i * 8 + 1];
    *g = 256 * in[i * 8 + 2] + in[i * 8 + 3];
    *b = 256 * in[i * 8 + 4] + in[i * 8 + 5];
    *a = 256 * in[i * 8 + 6] + in[i * 8 + 7];
  }
}

unsigned lodepng_convert(unsigned char* out, const unsigned char* in,
                         LodePNGColorMode* mode_out, const LodePNGColorMode* mode_in,
                         unsigned w, unsigned h)
{
  size_t i;
  ColorTree tree;
  size_t numpixels = (unsigned long long)w * h;

  if(lodepng_color_mode_equal(mode_out, mode_in))
  {
    size_t numbytes = lodepng_get_raw_size(w, h, mode_in);
    for(i = 0; i != numbytes; ++i) out[i] = in[i];
    return 0;
  }

  if(mode_out->colortype == LCT_PALETTE)
  {
    size_t palsize = 1u << mode_out->bitdepth;
    if(mode_out->palettesize < palsize) palsize = mode_out->palettesize;
    color_tree_init(&tree);
    for(i = 0; i != palsize; ++i)
    {
      unsigned char* p = &mode_out->palette[i * 4];
      color_tree_add(&tree, p[0], p[1], p[2], p[3], i);
    }
  }

  if(mode_in->bitdepth == 16 && mode_out->bitdepth == 16)
  {
    for(i = 0; i != numpixels; ++i)
    {
      unsigned short r = 0, g = 0, b = 0, a = 0;
      getPixelColorRGBA16(&r, &g, &b, &a, in, i, mode_in);
      rgba16ToPixel(out, i, mode_out, r, g, b, a);
    }
  }
  else if(mode_out->bitdepth == 8 && mode_out->colortype == LCT_RGBA)
  {
    getPixelColorsRGBA8(out, numpixels, 1, in, mode_in);
  }
  else if(mode_out->bitdepth == 8 && mode_out->colortype == LCT_RGB)
  {
    getPixelColorsRGBA8(out, numpixels, 0, in, mode_in);
  }
  else if (mode_in->colortype == LCT_RGBA && mode_out->colortype == LCT_PALETTE && mode_in->bitdepth == 8 && mode_out->bitdepth == 8){
    unsigned match = (*(unsigned*)in) + 1;
    unsigned char prevbyte = 0;

    for(i = 0; i != numpixels; ++i)
    {
      unsigned m = *(unsigned*)(in + 4 * i);
      if (m == match){
        out[i] = prevbyte;
      }
      else{
        int index = color_tree_get(&tree, in[i * 4], in[i * 4 + 1], in[i * 4 + 2], in[i * 4 + 3]);
        out[i] = index;
        match = m;
        prevbyte = index;
      }
    }
  }
  else{
    unsigned char r = 0, g = 0, b = 0, a = 0;
    for(i = 0; i != numpixels; ++i)
    {
      getPixelColorRGBA8(&r, &g, &b, &a, in, i, mode_in);
      rgba8ToPixel(out, i, mode_out, &tree, r, g, b, a);
    }
  }

  if(mode_out->colortype == LCT_PALETTE)
  {
    color_tree_cleanup(&tree);
  }

  return 0; /*no error (this function currently never has one, but maybe OOM detection added later.)*/
}

#ifdef LODEPNG_COMPILE_ENCODER

void lodepng_color_profile_init(LodePNGColorProfile* profile)
{
  profile->colored = 0;
  profile->key = 0;
  profile->alpha = 0;
  profile->key_r = profile->key_g = profile->key_b = 0;
  profile->numcolors = 0;
  profile->bits = 1;
  profile->white = 1;
}

/*Returns how many bits needed to represent given value (max 8 bit)*/
static unsigned getValueRequiredBits(unsigned char value)
{
  if(value == 0 || value == 255) return 1;
  /*The scaling of 2-bit and 4-bit values uses multiples of 85 and 17*/
  if(value % 17 == 0) return value % 85 == 0 ? 2 : 4;
  return 8;
}

/*profile must already have been inited with mode.
It's ok to set some parameters of profile to done already.*/
unsigned lodepng_get_color_profile(LodePNGColorProfile* profile,
                                   const unsigned char* in, unsigned w, unsigned h,
                                   const LodePNGColorMode* mode)
{
  size_t i;
  size_t numpixels = (unsigned long long)w * h;

  unsigned colored_done = lodepng_is_greyscale_type(mode) ? 1 : 0;
  unsigned alpha_done = lodepng_can_have_alpha(mode) ? 0 : 1;
  unsigned numcolors_done = 0;
  unsigned bpp = lodepng_get_bpp(mode);
  unsigned bits_done = bpp == 1 ? 1 : 0;
  unsigned maxnumcolors = 257;
  unsigned sixteen = 0;
  if(bpp <= 8) maxnumcolors = bpp == 1 ? 2 : (bpp == 2 ? 4 : (bpp == 4 ? 16 : 256));

  /*Check if the 16-bit input is truly 16-bit*/
  if(mode->bitdepth == 16)
  {
    unsigned short r, g, b, a;
    for(i = 0; i != numpixels; ++i)
    {
      getPixelColorRGBA16(&r, &g, &b, &a, in, i, mode);
      if((r & 255) != ((r >> 8) & 255) || (g & 255) != ((g >> 8) & 255) ||
         (b & 255) != ((b >> 8) & 255) || (a & 255) != ((a >> 8) & 255)) /*first and second byte differ*/
      {
        sixteen = 1;
        break;
      }
    }
  }

  if(sixteen)
  {
    unsigned short r = 0, g = 0, b = 0, a = 0;
    profile->bits = 16;
    bits_done = numcolors_done = 1; /*counting colors no longer useful, palette doesn't support 16-bit*/

    for(i = 0; i != numpixels; ++i)
    {
      getPixelColorRGBA16(&r, &g, &b, &a, in, i, mode);

      if(!colored_done && (r != g || r != b))
      {
        profile->colored = 1;
        colored_done = 1;
      }

      if(!alpha_done)
      {
        unsigned matchkey = (r == profile->key_r && g == profile->key_g && b == profile->key_b);
        if(a != 65535 && (a != 0 || (profile->key && !matchkey)))
        {
          profile->alpha = 1;
          alpha_done = 1;
          if(profile->bits < 8) profile->bits = 8; /*PNG has no alphachannel modes with less than 8-bit per channel*/
        }
        else if(a == 0 && !profile->alpha && !profile->key)
        {
          profile->key = 1;
          profile->key_r = r;
          profile->key_g = g;
          profile->key_b = b;
        }
        else if(a == 65535 && profile->key && matchkey)
        {
          /* Color key cannot be used if an opaque pixel also has that RGB color. */
          profile->alpha = 1;
          alpha_done = 1;
        }
      }

      if(alpha_done && numcolors_done && colored_done && bits_done) break;
    }
  }
  else /* < 16-bit */
  {
    ColorTree tree;
    color_tree_init(&tree);

    if (mode->colortype == LCT_RGBA && mode->bitdepth == 8){

      unsigned match = (*(unsigned*)in) + 1;

      for(i = 0; i != numpixels; ++i)
      {
        unsigned char r = 0, g = 0, b = 0, a = 0;
        r = in[i * 4];
        g = in[i * 4 + 1];
        b = in[i * 4 + 2];
        a = in[i * 4 + 3];

        if(!bits_done && profile->bits < 8)
        {
          /*only r is checked, < 8 bits is only relevant for greyscale*/
          unsigned bits = getValueRequiredBits(r);
          if(bits > profile->bits) profile->bits = bits;
        }
        bits_done = (profile->bits >= bpp);

        if(!colored_done && (r != g || r != b))
        {
          profile->colored = 1;
          colored_done = 1;
          if(profile->bits < 8) profile->bits = 8; /*PNG has no colored modes with less than 8-bit per channel*/
        }

        if(!alpha_done)
        {
          unsigned matchkey = (r == profile->key_r && g == profile->key_g && b == profile->key_b);
          if(a != 255 && (a != 0 || (profile->key && !matchkey)))
          {
            profile->alpha = 1;
            alpha_done = 1;
            if(profile->bits < 8) profile->bits = 8; /*PNG has no alphachannel modes with less than 8-bit per channel*/
          }
          else if(a == 0 && !profile->alpha && !profile->key)
          {
            profile->key = 1;
            profile->key_r = r;
            profile->key_g = g;
            profile->key_b = b;
          }
          else if(a == 255 && profile->key && matchkey)
          {
            /* Color key cannot be used if an opaque pixel also has that RGB color. */
            profile->alpha = 1;
            alpha_done = 1;
            if(profile->bits < 8) profile->bits = 8; /*PNG has no alphachannel modes with less than 8-bit per channel*/
          }
        }

        if(!numcolors_done)
        {
          unsigned m = *(unsigned*)(in + 4 * i);
          if (m != match){
            match = m;
            if(!color_tree_has(&tree, r, g, b, a))
            {
              color_tree_add(&tree, r, g, b, a, profile->numcolors);
              if(profile->numcolors < 256)
              {
                unsigned char* p = profile->palette;
                unsigned n = profile->numcolors;
                p[n * 4] = r;
                p[n * 4 + 1] = g;
                p[n * 4 + 2] = b;
                p[n * 4 + 3] = a;
              }
              ++profile->numcolors;
              numcolors_done = profile->numcolors >= maxnumcolors;
            }
          }
        }

        if(alpha_done && numcolors_done && colored_done && bits_done) break;
      }

    }
    else{

    for(i = 0; i != numpixels; ++i)
    {
      unsigned char r = 0, g = 0, b = 0, a = 0;
      getPixelColorRGBA8(&r, &g, &b, &a, in, i, mode);

      if(!bits_done && profile->bits < 8)
      {
        /*only r is checked, < 8 bits is only relevant for greyscale*/
        unsigned bits = getValueRequiredBits(r);
        if(bits > profile->bits) profile->bits = bits;
      }
      bits_done = (profile->bits >= bpp);

      if(!colored_done && (r != g || r != b))
      {
        profile->colored = 1;
        colored_done = 1;
        if(profile->bits < 8) profile->bits = 8; /*PNG has no colored modes with less than 8-bit per channel*/
      }

      if(!alpha_done)
      {
        unsigned matchkey = (r == profile->key_r && g == profile->key_g && b == profile->key_b);
        if(a != 255 && (a != 0 || (profile->key && !matchkey)))
        {
          profile->alpha = 1;
          alpha_done = 1;
          if(profile->bits < 8) profile->bits = 8; /*PNG has no alphachannel modes with less than 8-bit per channel*/
        }
        else if(a == 0 && !profile->alpha && !profile->key)
        {
          profile->key = 1;
          profile->key_r = r;
          profile->key_g = g;
          profile->key_b = b;
        }
        else if(a == 255 && profile->key && matchkey)
        {
          /* Color key cannot be used if an opaque pixel also has that RGB color. */
          profile->alpha = 1;
          alpha_done = 1;
          if(profile->bits < 8) profile->bits = 8; /*PNG has no alphachannel modes with less than 8-bit per channel*/
        }
      }

      if(!numcolors_done)
      {
        if(!color_tree_has(&tree, r, g, b, a))
        {
          color_tree_add(&tree, r, g, b, a, profile->numcolors);
          if(profile->numcolors < 256)
          {
            unsigned char* p = profile->palette;
            unsigned n = profile->numcolors;
            p[n * 4] = r;
            p[n * 4 + 1] = g;
            p[n * 4 + 2] = b;
            p[n * 4 + 3] = a;
          }
          ++profile->numcolors;
          numcolors_done = profile->numcolors >= maxnumcolors;
        }
      }

      if(alpha_done && numcolors_done && colored_done && bits_done) break;
    }
    }

    /*make the profile's key always 16-bit for consistency - repeat each byte twice*/
    profile->key_r += (profile->key_r << 8);
    profile->key_g += (profile->key_g << 8);
    profile->key_b += (profile->key_b << 8);
    color_tree_cleanup(&tree);
  }

  unsigned char r = 0, g = 0, b = 0, a = 0;
  getPixelColorRGBA8(&r, &g, &b, &a, in, 0, mode);
  profile->white = profile->numcolors == 1 && profile->colored == 0 && r == 255 && w > 20 && h > 20 && ((w>225 && h > 225) || w*h > 75000 || (w> 250 && w*h > 40000));

  return 0;
}

static void optimize_palette(LodePNGColorMode* mode_out, const uint32_t* image,
                      unsigned w, unsigned h,
                      LodePNGPalettePriorityStrategy priority,
                      LodePNGPaletteDirectionStrategy direction,
                      LodePNGPaletteTransparencyStrategy transparency,
                      LodePNGPaletteOrderStrategy order) {
  if (order == LPOS_NONE) return;
  size_t count = 0;
  ColorTree tree;
  color_tree_init(&tree);
  for (size_t i = 0; i < w * h; ++i) {
    const unsigned char* c = (unsigned char*)&image[i];
    if (color_tree_inc(&tree, c[0], c[1], c[2], c[3]) == 0) ++count;
  }
  // sortfield format:
  // bit 0-7: original palette index
  // bit 8-39: color encoding or popularity index
  // bit 40-47: order score
  // bit 48-62: unused
  // bit 63: transparency flag
  uint64_t* sortfield = (uint64_t*)lodepng_malloc(count << 4);
  for (size_t i = 0; i < count; ++i) sortfield[i] = i;
  uint32_t* palette_in = (uint32_t*)(mode_out->palette);
  switch (priority) {
    case LPPS_POPULARITY:
      for (size_t i = 0; i < count; ++i) {
        const unsigned char* p = (unsigned char*)&palette_in[i];
        sortfield[i] |= (color_tree_get(&tree, p[0], p[1], p[2], p[3]) + 1) << 8;
      }
      break;
    case LPPS_RGB:
      for (size_t i = 0; i < count; ++i) {
        const unsigned char* c = (unsigned char*)&palette_in[i];
        sortfield[i] |= uint64_t(c[0]) << 32 | uint64_t(c[1]) << 24 | uint64_t(c[2]) << 16;
      }
      break;
    case LPPS_YUV:
      for (size_t i = 0; i < count; ++i) {
        const unsigned char* c = (unsigned char*)&palette_in[i];
        const double r = c[0];
        const double g = c[1];
        const double b = c[2];
        sortfield[i] |= uint64_t(0.299 * r + 0.587 * g + 0.114 * b) << 32
        | uint64_t((-0.14713 * r - 0.28886 * g + 0.436 * b + 111.18) / 0.872) << 24
        | uint64_t((0.615 * r - 0.51499 * g - 0.10001 * b + 156.825) / 1.23) << 16;
      }
      break;
    case LPPS_LAB:
    {
      const double ep = 216. / 24389.;
      const double ka = 24389. / 27.;
      const double ex = 1. / 3.;
      const double de = 4. / 29.;
      for (size_t i = 0; i < count; ++i) {
        const unsigned char* c = (unsigned char*)&palette_in[i];
        const double r = c[0];
        const double g = c[1];
        const double b = c[2];
        double vx = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 255 / 95.047;
        double vy = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b) / 255 / 100;
        double vz = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 255 / 108.883;
        vx = vx > ep ? pow(vx, ex) : ka * vx + de;
        vy = vy > ep ? pow(vy, ex) : ka * vy + de;
        vz = vz > ep ? pow(vz, ex) : ka * vz + de;
        sortfield[i] |= uint64_t((vy * 116 - 16) / 100 * 255) << 32
        | uint64_t((vx - vy) * 500 + 256) << 24
        | uint64_t((vy - vz) * 200 + 256) << 16;
      }
    }
      break;
    case LPPS_MSB:
      for (size_t i = 0; i < count; ++i) {
        const unsigned char* c = (unsigned char*)&palette_in[i];
        const uint64_t r = c[0];
        const uint64_t g = c[1];
        const uint64_t b = c[2];
        sortfield[i] |= (r & 128) << 39 | (g & 128) << 38 | (b & 128) << 37
        | (r & 64) << 35 | (g & 64) << 34 | (b & 64) << 33
        | (r & 32) << 31 | (g & 32) << 30 | (b & 32) << 29
        | (r & 16) << 27 | (g & 16) << 26 | (b & 16) << 25
        | (r & 8) << 23 | (g & 8) << 22 | (b & 8) << 21
        | (r & 4) << 19 | (g & 4) << 18 | (b & 4) << 17
        | (r & 2) << 15 | (g & 2) << 14 | (b & 2) << 13
        | (r & 1) << 11 | (g & 1) << 10 | (b & 1) << 9;
      }
      break;
  }
  switch (transparency) {
    case LPTS_IGNORE:
      break;
    case LPTS_FIRST:
      for (size_t i = 0; i < count; ++i) {
        if (((unsigned char*)&palette_in[i])[3] == 0xFF) {
          sortfield[i] |= 0x8000000000000000ULL;
        }
      }
      // fall through
    case LPTS_SORT:
      if (priority == LPPS_MSB) {
        for (size_t i = 0; i < count; ++i) {
          const uint64_t a = ((unsigned char*)&palette_in[i])[3];
          sortfield[i] |= (a & 0x80ULL) << 36 | (a & 0x40ULL) << 32
          | (a & 0x20ULL) << 28 | (a & 0x10ULL) << 24 | (a & 8ULL) << 20
          | (a & 4ULL) << 16 | (a & 2ULL) << 12 | (a & 1ULL) << 8;
        }
      } else if(priority != LPPS_POPULARITY) {
        for (size_t i = 0; i < count; ++i) {
          sortfield[i] |= uint64_t(((unsigned char*)&palette_in[i])[3]) << 8;
        }
      }
      break;
  }
  size_t best = 0;
  if (order == LPOS_GLOBAL) {
    if (direction == LPDS_DESCENDING) {
      for (size_t i = 0; i < count; ++i) {
        // flip bits, but preserve original index and transparency mode 2
        sortfield[i] = (~sortfield[i] & 0x7FFFFFFFFFFFFF00ULL)
        | (sortfield[i] & 0x80000000000000FFULL);
      }
    }
  } else {
    if (direction == LPDS_DESCENDING) {
      uint64_t value = 0;
      for (size_t i = 1; i < count; ++i) {
        if ((sortfield[i] & 0x7FFFFFFFFFFFFFFFULL) > value) {
          value = (sortfield[i] & 0x7FFFFFFFFFFFFFFFULL);
          best = i;
        }
      }
    } else {
      uint64_t value = UINT64_MAX;
      for (size_t i = 1; i < count; ++i) {
        if ((sortfield[i] & 0x7FFFFFFFFFFFFFFFULL) < value) {
          value = (sortfield[i] & 0x7FFFFFFFFFFFFFFFULL);
          best = i;
        }
      }
    }
  }
  switch(order) {
    case LPOS_NONE:
    case LPOS_GLOBAL:
      break;
    case LPOS_NEAREST:
      for (size_t i = 0; i < count - 1; ++i) {
        if (i != best) {
          sortfield[i] ^= sortfield[best];
          sortfield[best] ^= sortfield[i];
          sortfield[i] ^= sortfield[best];
        }
        sortfield[i] |= uint64_t(i) << 40;
        const unsigned char* c = (unsigned char*)&palette_in[sortfield[i] & 0xFF];
        const int r = c[0];
        const int g = c[1];
        const int b = c[2];
        int bestdist = INT_MAX;
        for (size_t j = i + 1; j < count; ++j) {
          const unsigned char* c2 = (unsigned char*)&palette_in[sortfield[j] & 0xFF];
          const int r2 = c2[0];
          const int g2 = c2[1];
          const int b2 = c2[2];
          int dist = (r - r2) * (r - r2) + (g - g2) + (g - g2) + (b - b2) * (b - b2);
          if (transparency == LPTS_SORT) {
            const int a = c[3];
            const int a2 = c2[3];
            dist += (a - a2) * (a - a2);
          }
          if (dist < bestdist) {
            bestdist = dist;
            best = j;
          }
        }
      }
      sortfield[count - 1] |= uint64_t(count - 1) << 40;
      break;
    case LPOS_NEAREST_WEIGHT:
    {
      for (size_t i = 0; i < count - 1; ++i) {
        if (i != best) {
          sortfield[i] ^= sortfield[best];
          sortfield[best] ^= sortfield[i];
          sortfield[i] ^= sortfield[best];
        }
        sortfield[i] |= uint64_t(i) << 40;
        const unsigned char* c = (unsigned char*)&palette_in[sortfield[i] & 0xFF];
        const int r = c[0];
        const int g = c[1];
        const int b = c[2];
        double bestdist = INT_MAX;
        for (size_t j = i + 1; j < count; ++j) {
          const unsigned char* c2 = (unsigned char*)&palette_in[sortfield[j] & 0xFF];
          const int r2 = c2[0];
          const int g2 = c2[1];
          const int b2 = c2[2];
          double dist = (r - r2) * (r - r2) + (g - g2) + (g - g2) + (b - b2) * (b - b2);
          if (transparency == LPTS_SORT) {
            const int a = c[3];
            const int a2 = c2[3];
            dist += (a - a2) * (a - a2);
          }
          dist /= (color_tree_get(&tree, c2[0], c2[1], c2[2], c2[3]) + 1);
          if (dist < bestdist) {
            bestdist = dist;
            best = j;
          }
        }
      }
      sortfield[count - 1] |= uint64_t(count - 1) << 40;
    }
      break;
    case LPOS_NEAREST_NEIGHBOR:
    {
      ColorTree paltree;
      color_tree_init(&paltree);
      for (size_t i = 0; i < count; ++i) {
        const unsigned char* p = (unsigned char*)&palette_in[i];
        color_tree_add(&paltree, p[0], p[1], p[2], p[3], i);
      }
      ColorTree neighbors;
      color_tree_init(&neighbors);
      for (size_t k = 0; k < h; ++k) {
        for (size_t l = 0; l < w; ++l) {
          const unsigned char* c = (unsigned char*)&image[k * w + l];
          int index = color_tree_get(&paltree, c[0], c[1], c[2], c[3]);
          if (k > 0) { // above
            const unsigned char* c2 = (unsigned char*)&image[(k - 1) * w + l];
            color_tree_inc(&neighbors, index, color_tree_get(&paltree, c2[0], c2[1], c2[2], c2[3]), 0, 0);
          }
          if (k < h - 1) { // below
            const unsigned char* c2 = (unsigned char*)&image[(k + 1) * w + l];
            color_tree_inc(&neighbors, index, color_tree_get(&paltree, c2[0], c2[1], c2[2], c2[3]), 0, 0);
          }
          if (l > 0) { // left
            const unsigned char* c2 = (unsigned char*)&image[k * w + l - 1];
            color_tree_inc(&neighbors, index, color_tree_get(&paltree, c2[0], c2[1], c2[2], c2[3]), 0, 0);
          }
          if (l < w - 1) { // right
            const unsigned char* c2 = (unsigned char*)&image[k * w + l + 1];
            color_tree_inc(&neighbors, index, color_tree_get(&paltree, c2[0], c2[1], c2[2], c2[3]), 0, 0);
          }
        }
      }
      for (size_t i = 0; i < count - 1; ++i) {
        if (i != best) {
          sortfield[i] ^= sortfield[best];
          sortfield[best] ^= sortfield[i];
          sortfield[i] ^= sortfield[best];
        }
        sortfield[i] |= uint64_t(i) << 40;
        const unsigned char* c = (unsigned char*)&palette_in[sortfield[i] & 0xFF];
        const int r = c[0];
        const int g = c[1];
        const int b = c[2];
        double bestdist = INT_MAX;
        best = i + 1;
        for (size_t j = i + 1; j < count; ++j) {
          const unsigned char* c2 = (unsigned char*)&palette_in[sortfield[j] & 0xFF];
          const int r2 = c2[0];
          const int g2 = c2[1];
          const int b2 = c2[2];
          double dist = (r - r2) * (r - r2) + (g - g2) + (g - g2) + (b - b2) * (b - b2);
          if (transparency == LPTS_SORT) {
            const int a = c[3];
            const int a2 = c2[3];
            dist += (a - a2) * (a - a2);
          }
          dist /= (color_tree_get(&neighbors, color_tree_get(&paltree, c[0], c[1], c[2], c[3]),
                                  color_tree_get(&paltree, c2[0], c2[1], c2[2], c2[3]), 0, 0) + 1);
          if (dist != 0 && dist < bestdist) {
            bestdist = dist;
            best = j;
          }
        }
      }
      sortfield[count - 1] |= uint64_t(count - 1) << 40;
      color_tree_cleanup(&paltree);
      color_tree_cleanup(&neighbors);
    }
      break;
  }
  std::sort(sortfield, sortfield + count);
  uint32_t* palette_out = (uint32_t*)lodepng_malloc(mode_out->palettesize << 2);
  for (size_t i = 0; i < mode_out->palettesize; ++i) {
    palette_out[i] = palette_in[sortfield[i] & 0xFF];
  }
  std::copy(palette_out, palette_out + mode_out->palettesize, palette_in);
  free(palette_out);
  free(sortfield);
  color_tree_cleanup(&tree);
}

/*Automatically chooses color type that gives smallest amount of bits in the
output image, e.g. grey if there are only greyscale pixels, palette if there
are less than 256 colors, ...
Updates values of mode with a potentially smaller color model. mode_out should
contain the user chosen color model, but will be overwritten with the new chosen one.*/
static unsigned lodepng_auto_choose_color(LodePNGColorMode* mode_out,
                                   const unsigned char* image, unsigned w, unsigned h,
                                   const LodePNGColorMode* mode_in, unsigned div)
{
  LodePNGColorProfile prof;
  unsigned error = 0;
  unsigned i, n, palettebits, grey_ok, palette_ok;

  lodepng_color_profile_init(&prof);
  error = lodepng_get_color_profile(&prof, image, w, h, mode_in);
  if(error) return error;
  mode_out->key_defined = 0;

  if(prof.key && (unsigned long long)w * h <= 49) {
    prof.alpha = 1; /*too few pixels to justify tRNS chunk overhead*/
    if(prof.bits < 8) prof.bits = 8; /*PNG has no alphachannel modes with less than 8-bit per channel*/
  }
  grey_ok = !prof.colored && !prof.alpha; /*grey without alpha, with potentially low bits*/
  n = prof.numcolors;
  palettebits = n <= 2 ? 1 : (n <= 4 ? 2 : (n <= 16 ? 4 : 8));
  palette_ok = n <= 256 && prof.bits <= 8;
  if(8 + n * 4 > (unsigned long long)w * h / div) {palette_ok = 0;} /*don't add palette overhead if image has only a few pixels*/
  if(grey_ok && prof.bits <= palettebits && !prof.white) {palette_ok = 0;}  /*grey is less overhead*/

  if(palette_ok)
  {
    unsigned char* p = prof.palette;
    lodepng_palette_clear(mode_out); /*remove potential earlier palette*/
    for(i = 0; i != prof.numcolors; ++i)
    {
      error = lodepng_palette_add(mode_out, p[i * 4], p[i * 4 + 1], p[i * 4 + 2], p[i * 4 + 3]);
      if(error) break;
    }

    mode_out->colortype = LCT_PALETTE;
    mode_out->bitdepth = palettebits;
  }
  else /*8-bit or 16-bit per channel*/
  {
    mode_out->bitdepth = prof.bits;
    mode_out->colortype = prof.alpha ? (prof.colored ? LCT_RGBA : LCT_GREY_ALPHA)
                                     : (prof.colored ? LCT_RGB : LCT_GREY);

    if(prof.key && !prof.alpha)
    {
      unsigned mask = (1u << mode_out->bitdepth) - 1u; /*profile always uses 16-bit, mask converts it*/
      mode_out->key_r = prof.key_r & mask;
      mode_out->key_g = prof.key_g & mask;
      mode_out->key_b = prof.key_b & mask;
      mode_out->key_defined = 1;
    }
  }

  return error;
}

#endif /* #ifdef LODEPNG_COMPILE_ENCODER */

/*
Paeth predicter, used by PNG filter type 4
The parameters are of type short, but should come from unsigned chars, the shorts
are only needed to make the paeth calculation correct.
*/
static unsigned char paethPredictor(short a, short b, short c)
{
  short pa = abs(b - c);
  short pb = abs(a - c);
  short pc = abs(a + b - c - c);

  if(pc < pa && pc < pb) return (unsigned char)c;
  else if(pb < pa) return (unsigned char)b;
  else return (unsigned char)a;
}

/*shared values used by multiple Adam7 related functions*/

static const unsigned ADAM7_IX[7] = { 0, 4, 0, 2, 0, 1, 0 }; /*x start values*/
static const unsigned ADAM7_IY[7] = { 0, 0, 4, 0, 2, 0, 1 }; /*y start values*/
static const unsigned ADAM7_DX[7] = { 8, 8, 4, 4, 2, 2, 1 }; /*x delta values*/
static const unsigned ADAM7_DY[7] = { 8, 8, 8, 4, 4, 2, 2 }; /*y delta values*/

/*
Outputs various dimensions and positions in the image related to the Adam7 reduced images.
passw: output containing the width of the 7 passes
passh: output containing the height of the 7 passes
filter_passstart: output containing the index of the start and end of each
 reduced image with filter bytes
padded_passstart output containing the index of the start and end of each
 reduced image when without filter bytes but with padded scanlines
passstart: output containing the index of the start and end of each reduced
 image without padding between scanlines, but still padding between the images
w, h: width and height of non-interlaced image
bpp: bits per pixel
"padded" is only relevant if bpp is less than 8 and a scanline or image does not
 end at a full byte
*/
static void Adam7_getpassvalues(unsigned passw[7], unsigned passh[7], size_t filter_passstart[8],
                                size_t padded_passstart[8], size_t passstart[8], unsigned w, unsigned h, unsigned bpp)
{
  /*the passstart values have 8 values: the 8th one indicates the byte after the end of the 7th (= last) pass*/
  unsigned i;

  /*calculate width and height in pixels of each pass*/
  for(i = 0; i != 7; ++i)
  {
    passw[i] = (w + ADAM7_DX[i] - ADAM7_IX[i] - 1) / ADAM7_DX[i];
    passh[i] = (h + ADAM7_DY[i] - ADAM7_IY[i] - 1) / ADAM7_DY[i];
    if(passw[i] == 0) passh[i] = 0;
    if(passh[i] == 0) passw[i] = 0;
  }

  filter_passstart[0] = padded_passstart[0] = passstart[0] = 0;
  for(i = 0; i != 7; ++i)
  {
    /*if passw[i] is 0, it's 0 bytes, not 1 (no filtertype-byte)*/
    filter_passstart[i + 1] = filter_passstart[i]
                            + ((passw[i] && passh[i]) ? passh[i] * (1 + (passw[i] * bpp + 7) / 8) : 0);
    /*bits padded if needed to fill full byte at end of each scanline*/
    padded_passstart[i + 1] = padded_passstart[i] + passh[i] * ((passw[i] * bpp + 7) / 8);
    /*only padded at end of reduced image*/
    passstart[i + 1] = passstart[i] + (passh[i] * passw[i] * bpp + 7) / 8;
  }
}

#ifdef LODEPNG_COMPILE_DECODER

/* ////////////////////////////////////////////////////////////////////////// */
/* / PNG Decoder                                                            / */
/* ////////////////////////////////////////////////////////////////////////// */

/*read the information from the header and store it in the LodePNGInfo. return value is error*/
unsigned lodepng_inspect(unsigned* w, unsigned* h, LodePNGState* state,
                         const unsigned char* in, size_t insize)
{
  LodePNGInfo* info = &state->info_png;
  if(insize == 0 || in == 0)
  {
    CERROR_RETURN_ERROR(state->error, 48); /*error: the given data is empty*/
  }
  if(insize < 33)
  {
    CERROR_RETURN_ERROR(state->error, 27); /*error: the data length is smaller than the length of a PNG header*/
  }

  /*when decoding a new PNG image, make sure all parameters created after previous decoding are reset*/
  lodepng_info_cleanup(info);
  lodepng_info_init(info);

  if(in[0] != 137 || in[1] != 80 || in[2] != 78 || in[3] != 71
     || in[4] != 13 || in[5] != 10 || in[6] != 26 || in[7] != 10)
  {
    CERROR_RETURN_ERROR(state->error, 28); /*error: the first 8 bytes are not the correct PNG signature*/
  }
  if(lodepng_chunk_length(in + 8) != 13)
  {
    CERROR_RETURN_ERROR(state->error, 94); /*error: header size must be 13 bytes*/
  }
  if(!lodepng_chunk_type_equals(in + 8, "IHDR"))
  {
    CERROR_RETURN_ERROR(state->error, 29); /*error: it doesn't start with a IHDR chunk!*/
  }

  /*read the values given in the header*/
  *w = lodepng_read32bitInt(&in[16]);
  *h = lodepng_read32bitInt(&in[20]);
  info->color.bitdepth = in[24];
  info->color.colortype = (LodePNGColorType)in[25];
  info->compression_method = in[26];
  info->filter_method = in[27];
  info->interlace_method = in[28];

  if(*w == 0 || *h == 0)
  {
    CERROR_RETURN_ERROR(state->error, 93);
  }

  unsigned CRC = lodepng_read32bitInt(&in[29]);
  unsigned checksum = lodepng_crc32(&in[12], 17);
  if(CRC != checksum)
  {
    CERROR_RETURN_ERROR(state->error, 57); /*invalid CRC*/
  }

  /*error: only compression method 0 is allowed in the specification*/
  if(info->compression_method != 0) CERROR_RETURN_ERROR(state->error, 32);
  /*error: only filter method 0 is allowed in the specification*/
  if(info->filter_method != 0) CERROR_RETURN_ERROR(state->error, 33);
  /*error: only interlace methods 0 and 1 exist in the specification*/
  if(info->interlace_method > 1) CERROR_RETURN_ERROR(state->error, 34);

  state->error = checkColorValidity(info->color.colortype, info->color.bitdepth);
  return state->error;
}

static unsigned unfilterScanline(unsigned char* recon, const unsigned char* scanline, const unsigned char* precon,
                                 size_t bytewidth, unsigned char filterType, size_t length)
{
  /*
  For PNG filter method 0
  unfilter a PNG image scanline by scanline. when the pixels are smaller than 1 byte,
  the filter works byte per byte (bytewidth = 1)
  precon is the previous unfiltered scanline, recon the result, scanline the current one
  the incoming scanlines do NOT include the filtertype byte, that one is given in the parameter filterType instead
  recon and scanline MAY be the same memory address! precon must be disjoint.
  */

  size_t i;
  switch(filterType)
  {
    case 0:
      for(i = 0; i != length; ++i) recon[i] = scanline[i];
      break;
    case 1:
      for(i = 0; i != bytewidth; ++i) recon[i] = scanline[i];
      for(i = bytewidth; i < length; ++i) recon[i] = scanline[i] + recon[i - bytewidth];
      break;
    case 2:
      if(precon)
      {
        for(i = 0; i != length; ++i) recon[i] = scanline[i] + precon[i];
      }
      else
      {
        for(i = 0; i != length; ++i) recon[i] = scanline[i];
      }
      break;
    case 3:
      if(precon)
      {
        for(i = 0; i != bytewidth; ++i) recon[i] = scanline[i] + precon[i] / 2;
        for(i = bytewidth; i < length; ++i) recon[i] = scanline[i] + ((recon[i - bytewidth] + precon[i]) / 2);
      }
      else
      {
        for(i = 0; i != bytewidth; ++i) recon[i] = scanline[i];
        for(i = bytewidth; i < length; ++i) recon[i] = scanline[i] + recon[i - bytewidth] / 2;
      }
      break;
    case 4:
      if(precon)
      {
        for(i = 0; i != bytewidth; ++i)
        {
          recon[i] = (scanline[i] + precon[i]); /*paethPredictor(0, precon[i], 0) is always precon[i]*/
        }
        for(i = bytewidth; i < length; ++i)
        {
          recon[i] = (scanline[i] + paethPredictor(recon[i - bytewidth], precon[i], precon[i - bytewidth]));
        }
      }
      else
      {
        for(i = 0; i != bytewidth; ++i)
        {
          recon[i] = scanline[i];
        }
        for(i = bytewidth; i < length; ++i)
        {
          /*paethPredictor(recon[i - bytewidth], 0, 0) is always recon[i - bytewidth]*/
          recon[i] = (scanline[i] + recon[i - bytewidth]);
        }
      }
      break;
    default: return 36; /*error: unexisting filter type given*/
  }
  return 0;
}

static unsigned unfilter(unsigned char* out, const unsigned char* in, unsigned w, unsigned h, unsigned bpp)
{
  /*
  For PNG filter method 0
  this function unfilters a single image (e.g. without interlacing this is called once, with Adam7 seven times)
  out must have enough bytes allocated already, in must have the scanlines + 1 filtertype byte per scanline
  w and h are image dimensions or dimensions of reduced image, bpp is bits per pixel
  in and out are allowed to be the same memory address (but aren't the same size since in has the extra filter bytes)
  */

  unsigned y;
  unsigned char* prevline = 0;

  /*bytewidth is used for filtering, is 1 when bpp < 8, number of bytes per pixel otherwise*/
  size_t bytewidth = (bpp + 7) / 8;
  size_t linebytes = (w * bpp + 7) / 8;

  for(y = 0; y < h; ++y)
  {
    size_t outindex = linebytes * y;
    size_t inindex = (1 + linebytes) * y; /*the extra filterbyte added to each row*/
    unsigned char filterType = in[inindex];

    CERROR_TRY_RETURN(unfilterScanline(&out[outindex], &in[inindex + 1], prevline, bytewidth, filterType, linebytes));

    prevline = &out[outindex];
  }

  return 0;
}

/*
in: Adam7 interlaced image, with no padding bits between scanlines, but between
 reduced images so that each reduced image starts at a byte.
out: the same pixels, but re-ordered so that they're now a non-interlaced image with size w*h
bpp: bits per pixel
out has the following size in bits: w * h * bpp.
in is possibly bigger due to padding bits between reduced images.
out must be big enough AND must be 0 everywhere if bpp < 8 in the current implementation
(because that's likely a little bit faster)
NOTE: comments about padding bits are only relevant if bpp < 8
*/
static void Adam7_deinterlace(unsigned char* out, const unsigned char* in, unsigned w, unsigned h, unsigned bpp)
{
  unsigned passw[7], passh[7];
  size_t filter_passstart[8], padded_passstart[8], passstart[8];
  unsigned i;

  Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

  if(bpp >= 8)
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      size_t bytewidth = bpp / 8;
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        size_t pixelinstart = passstart[i] + (y * passw[i] + x) * bytewidth;
        size_t pixeloutstart = ((ADAM7_IY[i] + y * ADAM7_DY[i]) * w + ADAM7_IX[i] + x * ADAM7_DX[i]) * bytewidth;
        for(b = 0; b < bytewidth; ++b)
        {
          out[pixeloutstart + b] = in[pixelinstart + b];
        }
      }
    }
  }
  else /*bpp < 8: Adam7 with pixels < 8 bit is a bit trickier: with bit pointers*/
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      unsigned ilinebits = bpp * passw[i];
      unsigned olinebits = bpp * w;
      size_t obp, ibp; /*bit pointers (for out and in buffer)*/
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        ibp = (8 * passstart[i]) + (y * ilinebits + x * bpp);
        obp = (ADAM7_IY[i] + y * ADAM7_DY[i]) * olinebits + (ADAM7_IX[i] + x * ADAM7_DX[i]) * bpp;
        for(b = 0; b < bpp; ++b)
        {
          unsigned char bit = readBitFromReversedStream(&ibp, in);
          /*note that this function assumes the out buffer is completely 0, use setBitOfReversedStream otherwise*/
          setBitOfReversedStream0(&obp, out, bit);
        }
      }
    }
  }
}

static void removePaddingBits(unsigned char* out, const unsigned char* in,
                              size_t olinebits, size_t ilinebits, unsigned h)
{
  /*
  After filtering there are still padding bits if scanlines have non multiple of 8 bit amounts. They need
  to be removed (except at last scanline of (Adam7-reduced) image) before working with pure image buffers
  for the Adam7 code, the color convert code and the output to the user.
  in and out are allowed to be the same buffer, in may also be higher but still overlapping; in must
  have >= ilinebits*h bits, out must have >= olinebits*h bits, olinebits must be <= ilinebits
  also used to move bits after earlier such operations happened, e.g. in a sequence of reduced images from Adam7
  only useful if (ilinebits - olinebits) is a value in the range 1..7
  */
  unsigned y;
  size_t diff = ilinebits - olinebits;
  size_t ibp = 0, obp = 0; /*input and output bit pointers*/
  for(y = 0; y < h; ++y)
  {
    size_t x;
    for(x = 0; x < olinebits; ++x)
    {
      unsigned char bit = readBitFromReversedStream(&ibp, in);
      setBitOfReversedStream(&obp, out, bit);
    }
    ibp += diff;
  }
}

/*out must be buffer big enough to contain full image, and in must contain the full decompressed data from
the IDAT chunks (with filter index bytes and possible padding bits)
return value is error*/
static unsigned postProcessScanlines(unsigned char* out, unsigned char* in,
                                     unsigned w, unsigned h, const LodePNGInfo* info_png)
{
  /*
  This function converts the filtered-padded-interlaced data into pure 2D image buffer with the PNG's colortype.
  Steps:
  *) if no Adam7: 1) unfilter 2) remove padding bits (= posible extra bits per scanline if bpp < 8)
  *) if adam7: 1) 7x unfilter 2) 7x remove padding bits 3) Adam7_deinterlace
  NOTE: the in buffer will be overwritten with intermediate data!
  */
  unsigned bpp = lodepng_get_bpp(&info_png->color);
  if(bpp == 0) return 31; /*error: invalid colortype*/

  if(info_png->interlace_method == 0)
  {
    if(bpp < 8 && w * bpp != ((w * bpp + 7) / 8) * 8)
    {
      CERROR_TRY_RETURN(unfilter(in, in, w, h, bpp));
      removePaddingBits(out, in, w * bpp, ((w * bpp + 7) / 8) * 8, h);
    }
    /*we can immediatly filter into the out buffer, no other steps needed*/
    else CERROR_TRY_RETURN(unfilter(out, in, w, h, bpp));
  }
  else /*interlace_method is 1 (Adam7)*/
  {
    unsigned passw[7], passh[7]; size_t filter_passstart[8], padded_passstart[8], passstart[8];
    unsigned i;

    Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

    for(i = 0; i != 7; ++i)
    {
      CERROR_TRY_RETURN(unfilter(&in[padded_passstart[i]], &in[filter_passstart[i]], passw[i], passh[i], bpp));
      /*TODO: possible efficiency improvement: if in this reduced image the bits fit nicely in 1 scanline,
      move bytes instead of bits or move not at all*/
      if(bpp < 8)
      {
        /*remove padding bits in scanlines; after this there still may be padding
        bits between the different reduced images: each reduced image still starts nicely at a byte*/
        removePaddingBits(&in[passstart[i]], &in[padded_passstart[i]], passw[i] * bpp,
                          ((passw[i] * bpp + 7) / 8) * 8, passh[i]);
      }
    }

    Adam7_deinterlace(out, in, w, h, bpp);
  }

  return 0;
}

static unsigned readChunk_PLTE(LodePNGColorMode* color, const unsigned char* data, size_t chunkLength)
{
  unsigned pos = 0, i;
  if(color->palette) free(color->palette);
  color->palettesize = chunkLength / 3;
  color->palette = (unsigned char*)lodepng_malloc(4 * color->palettesize);
  if(!color->palette && color->palettesize)
  {
    color->palettesize = 0;
    return 83; /*alloc fail*/
  }
  if(color->palettesize > 256) return 38; /*error: palette too big*/

  for(i = 0; i != color->palettesize; ++i)
  {
    color->palette[4 * i] = data[pos++]; /*R*/
    color->palette[4 * i + 1] = data[pos++]; /*G*/
    color->palette[4 * i + 2] = data[pos++]; /*B*/
    color->palette[4 * i + 3] = 255; /*alpha*/
  }

  return 0; /* OK */
}

static unsigned readChunk_tRNS(LodePNGColorMode* color, const unsigned char* data, size_t chunkLength)
{
  unsigned i;
  if(color->colortype == LCT_PALETTE)
  {
    /*error: more alpha values given than there are palette entries*/
    if(chunkLength > color->palettesize) return 38;

    for(i = 0; i != chunkLength; ++i) color->palette[4 * i + 3] = data[i];
  }
  else if(color->colortype == LCT_GREY)
  {
    /*error: this chunk must be 2 bytes for greyscale image*/
    if(chunkLength != 2) return 30;

    color->key_defined = 1;
    color->key_r = color->key_g = color->key_b = 256u * data[0] + data[1];
  }
  else if(color->colortype == LCT_RGB)
  {
    /*error: this chunk must be 6 bytes for RGB image*/
    if(chunkLength != 6) return 41;

    color->key_defined = 1;
    color->key_r = 256u * data[0] + data[1];
    color->key_g = 256u * data[2] + data[3];
    color->key_b = 256u * data[4] + data[5];
  }
  else return 42; /*error: tRNS chunk not allowed for other color models*/

  return 0; /* OK */
}


#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
/*background color chunk (bKGD)*/
static unsigned readChunk_bKGD(LodePNGInfo* info, const unsigned char* data, size_t chunkLength)
{
  if(info->color.colortype == LCT_PALETTE)
  {
    /*error: this chunk must be 1 byte for indexed color image*/
    if(chunkLength != 1) return 43;

    info->background_defined = 1;
    info->background_r = info->background_g = info->background_b = data[0];
  }
  else if(info->color.colortype == LCT_GREY || info->color.colortype == LCT_GREY_ALPHA)
  {
    /*error: this chunk must be 2 bytes for greyscale image*/
    if(chunkLength != 2) return 44;

    info->background_defined = 1;
    info->background_r = info->background_g = info->background_b = 256u * data[0] + data[1];
  }
  else if(info->color.colortype == LCT_RGB || info->color.colortype == LCT_RGBA)
  {
    /*error: this chunk must be 6 bytes for greyscale image*/
    if(chunkLength != 6) return 45;

    info->background_defined = 1;
    info->background_r = 256u * data[0] + data[1];
    info->background_g = 256u * data[2] + data[3];
    info->background_b = 256u * data[4] + data[5];
  }

  return 0; /* OK */
}

/*text chunk (tEXt)*/
static unsigned readChunk_tEXt(LodePNGInfo* info, const unsigned char* data, size_t chunkLength)
{
  unsigned error = 0;
  char *key = 0, *str = 0;
  unsigned i;

  while(!error) /*not really a while loop, only used to break on error*/
  {
    unsigned length, string2_begin;

    length = 0;
    while(length < chunkLength && data[length] != 0) ++length;
    /*even though it's not allowed by the standard, no error is thrown if
    there's no null termination char, if the text is empty*/
    if(length < 1 || length > 79) CERROR_BREAK(error, 89); /*keyword too short or long*/

    key = (char*)lodepng_malloc(length + 1);
    if(!key) CERROR_BREAK(error, 83); /*alloc fail*/

    key[length] = 0;
    for(i = 0; i != length; ++i) key[i] = (char)data[i];

    string2_begin = length + 1; /*skip keyword null terminator*/

    length = chunkLength < string2_begin ? 0 : chunkLength - string2_begin;
    str = (char*)lodepng_malloc(length + 1);
    if(!str) CERROR_BREAK(error, 83); /*alloc fail*/

    str[length] = 0;
    for(i = 0; i != length; ++i) str[i] = (char)data[string2_begin + i];

    error = lodepng_add_text(info, key, str);

    break;
  }

  free(key);
  free(str);

  return error;
}

/*compressed text chunk (zTXt)*/
static unsigned readChunk_zTXt(LodePNGInfo* info, const unsigned char* data, size_t chunkLength)
{
  unsigned error = 0;
  unsigned i;

  unsigned length, string2_begin;
  char *key = 0;
  ucvector decoded;

  ucvector_init(&decoded);

  while(!error) /*not really a while loop, only used to break on error*/
  {
    for(length = 0; length < chunkLength && data[length] != 0; ++length) ;
    if(length + 2 >= chunkLength) CERROR_BREAK(error, 75); /*no null termination, corrupt?*/
    if(length < 1 || length > 79) CERROR_BREAK(error, 89); /*keyword too short or long*/

    key = (char*)lodepng_malloc(length + 1);
    if(!key) CERROR_BREAK(error, 83); /*alloc fail*/

    key[length] = 0;
    for(i = 0; i != length; ++i) key[i] = (char)data[i];

    if(data[length + 1] != 0) CERROR_BREAK(error, 72); /*the 0 byte indicating compression must be 0*/

    string2_begin = length + 2;
    if(string2_begin > chunkLength) CERROR_BREAK(error, 75); /*no null termination, corrupt?*/

    length = chunkLength - string2_begin;
    /*will fail if zlib error, e.g. if length is too small*/
    error = lodepng_zlib_decompress(&decoded.data, &decoded.size,
                            (unsigned char*)(&data[string2_begin]),
                            length);
    if(error) break;
    ucvector_push_back(&decoded, 0);

    error = lodepng_add_text(info, key, (char*)decoded.data);

    break;
  }

  free(key);
  ucvector_cleanup(&decoded);

  return error;
}

/*international text chunk (iTXt)*/
static unsigned readChunk_iTXt(LodePNGInfo* info, const unsigned char* data, size_t chunkLength)
{
  unsigned error = 0;
  unsigned i;

  unsigned length, begin, compressed;
  char *key = 0, *langtag = 0, *transkey = 0;
  ucvector decoded;
  ucvector_init(&decoded);

  while(!error) /*not really a while loop, only used to break on error*/
  {
    /*Quick check if the chunk length isn't too small. Even without check
    it'd still fail with other error checks below if it's too short. This just gives a different error code.*/
    if(chunkLength < 5) CERROR_BREAK(error, 30); /*iTXt chunk too short*/

    /*read the key*/
    for(length = 0; length < chunkLength && data[length] != 0; ++length) ;
    if(length + 3 >= chunkLength) CERROR_BREAK(error, 75); /*no null termination char, corrupt?*/
    if(length < 1 || length > 79) CERROR_BREAK(error, 89); /*keyword too short or long*/

    key = (char*)lodepng_malloc(length + 1);
    if(!key) CERROR_BREAK(error, 83); /*alloc fail*/

    key[length] = 0;
    for(i = 0; i != length; ++i) key[i] = (char)data[i];

    /*read the compression method*/
    compressed = data[length + 1];
    if(data[length + 2] != 0) CERROR_BREAK(error, 72); /*the 0 byte indicating compression must be 0*/

    /*even though it's not allowed by the standard, no error is thrown if
    there's no null termination char, if the text is empty for the next 3 texts*/

    /*read the langtag*/
    begin = length + 3;
    length = 0;
    for(i = begin; i < chunkLength && data[i] != 0; ++i) ++length;

    langtag = (char*)lodepng_malloc(length + 1);
    if(!langtag) CERROR_BREAK(error, 83); /*alloc fail*/

    langtag[length] = 0;
    for(i = 0; i != length; ++i) langtag[i] = (char)data[begin + i];

    /*read the transkey*/
    begin += length + 1;
    length = 0;
    for(i = begin; i < chunkLength && data[i] != 0; ++i) ++length;

    transkey = (char*)lodepng_malloc(length + 1);
    if(!transkey) CERROR_BREAK(error, 83); /*alloc fail*/

    transkey[length] = 0;
    for(i = 0; i != length; ++i) transkey[i] = (char)data[begin + i];

    /*read the actual text*/
    begin += length + 1;

    length = chunkLength < begin ? 0 : chunkLength - begin;

    if(compressed)
    {
      /*will fail if zlib error, e.g. if length is too small*/
      error = lodepng_zlib_decompress(&decoded.data, &decoded.size,
                              (unsigned char*)(&data[begin]),
                              length);
      if(error) break;
      if(decoded.allocsize < decoded.size) decoded.allocsize = decoded.size;
      ucvector_push_back(&decoded, 0);
    }
    else
    {
      if(!ucvector_resize(&decoded, length + 1)) CERROR_BREAK(error, 83 /*alloc fail*/);

      decoded.data[length] = 0;
      for(i = 0; i != length; ++i) decoded.data[i] = data[begin + i];
    }

    error = lodepng_add_itext(info, key, langtag, transkey, (char*)decoded.data);

    break;
  }

  free(key);
  free(langtag);
  free(transkey);
  ucvector_cleanup(&decoded);

  return error;
}
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/

/*read a PNG, the result will be in the same color type as the PNG (hence "generic")*/
static void decodeGeneric(unsigned char** out, unsigned* w, unsigned* h,
                          LodePNGState* state,
                          const unsigned char* in, size_t insize)
{
  unsigned char IEND = 0;
  const unsigned char* chunk;
  size_t i;
  ucvector idat; /*the data from idat chunks*/
  ucvector scanlines;
  size_t predict;
  size_t numpixels;

  /*for unknown chunk order*/
  unsigned unknown = 0;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  unsigned critical_pos = 1; /*1 = after IHDR, 2 = after PLTE, 3 = after IDAT*/
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/

  /*provide some proper output values if error will happen*/
  *out = 0;

  state->error = lodepng_inspect(w, h, state, in, insize); /*reads header and resets other parameters in state->info_png*/
  if(state->error) return;

  numpixels = *w * *h;

  /*multiplication overflow*/
  if(*h != 0 && numpixels / *h != *w) CERROR_RETURN(state->error, 92);
  /*multiplication overflow possible further below. Allows up to 2^31-1 pixel
  bytes with 16-bit RGBA, the rest is room for filter bytes.*/
  if(numpixels > 268435455) CERROR_RETURN(state->error, 92);

  ucvector_init(&idat);
  chunk = &in[33]; /*first byte of the first chunk after the header*/

  /*loop through the chunks, ignoring unknown chunks and stopping at IEND chunk.
  IDAT data is put at the start of the in buffer*/
  while(!IEND && !state->error)
  {
    unsigned chunkLength;
    const unsigned char* data; /*the data in the chunk*/

    /*error: size of the in buffer too small to contain next chunk*/
    if((size_t)((chunk - in) + 12) > insize || chunk < in) CERROR_BREAK(state->error, 30);

    /*length of the data of the chunk, excluding the length bytes, chunk type and CRC bytes*/
    chunkLength = lodepng_chunk_length(chunk);
    /*error: chunk length larger than the max PNG chunk size*/
    if(chunkLength > 2147483647) CERROR_BREAK(state->error, 63);

    if((size_t)((chunk - in) + chunkLength + 12) > insize || (chunk + chunkLength + 12) < in)
    {
      CERROR_BREAK(state->error, 64); /*error: size of the in buffer too small to contain next chunk*/
    }

    data = lodepng_chunk_data_const(chunk);

    /*IDAT chunk, containing compressed image data*/
    if(lodepng_chunk_type_equals(chunk, "IDAT"))
    {
      size_t oldsize = idat.size;
      if(!ucvector_resize(&idat, oldsize + chunkLength)) CERROR_BREAK(state->error, 83 /*alloc fail*/);
      for(i = 0; i != chunkLength; ++i) idat.data[oldsize + i] = data[i];
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
      critical_pos = 3;
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    }
    /*IEND chunk*/
    else if(lodepng_chunk_type_equals(chunk, "IEND"))
    {
      IEND = 1;
    }
    /*palette chunk (PLTE)*/
    else if(lodepng_chunk_type_equals(chunk, "PLTE"))
    {
      state->error = readChunk_PLTE(&state->info_png.color, data, chunkLength);
      if(state->error) break;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
      critical_pos = 2;
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    }
    /*palette transparency chunk (tRNS)*/
    else if(lodepng_chunk_type_equals(chunk, "tRNS"))
    {
      state->error = readChunk_tRNS(&state->info_png.color, data, chunkLength);
      if(state->error) break;
    }
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    /*background color chunk (bKGD)*/
    else if(lodepng_chunk_type_equals(chunk, "bKGD"))
    {
      state->error = readChunk_bKGD(&state->info_png, data, chunkLength);
      if(state->error) break;
    }
    /*text chunk (tEXt)*/
    else if(lodepng_chunk_type_equals(chunk, "tEXt"))
    {
      if(state->decoder.read_text_chunks)
      {
        state->error = readChunk_tEXt(&state->info_png, data, chunkLength);
        if(state->error) break;
      }
    }
    /*compressed text chunk (zTXt)*/
    else if(lodepng_chunk_type_equals(chunk, "zTXt"))
    {
      if(state->decoder.read_text_chunks)
      {
        state->error = readChunk_zTXt(&state->info_png, data, chunkLength);
        if(state->error) break;
      }
    }
    /*international text chunk (iTXt)*/
    else if(lodepng_chunk_type_equals(chunk, "iTXt"))
    {
      if(state->decoder.read_text_chunks)
      {
        state->error = readChunk_iTXt(&state->info_png, data, chunkLength);
        if(state->error) break;
      }
    }
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    else /*it's not an implemented chunk type, so ignore it: skip over the data*/
    {
      /*error: unknown critical chunk (5th bit of first byte of chunk type is 0)*/
      if(!lodepng_chunk_ancillary(chunk)) CERROR_BREAK(state->error, 69);

      unknown = 1;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
      if(state->decoder.remember_unknown_chunks)
      {
        state->error = lodepng_chunk_append(&state->info_png.unknown_chunks_data[critical_pos - 1],
                                            &state->info_png.unknown_chunks_size[critical_pos - 1], chunk);
        if(state->error) break;
      }
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    }

    if(!unknown) /*check CRC if wanted, only on known chunk types*/
    {
      if(lodepng_chunk_check_crc(chunk)) CERROR_BREAK(state->error, 57); /*invalid CRC*/
    }

    if(!IEND) chunk = lodepng_chunk_next_const(chunk);
  }

  ucvector_init(&scanlines);
  /*predict output size, to allocate exact size for output buffer to avoid more dynamic allocation.
  If the decompressed size does not match the prediction, the image must be corrupt.*/
  if(state->info_png.interlace_method == 0)
  {
    /*The extra *h is added because this are the filter bytes every scanline starts with*/
    predict = lodepng_get_raw_size_idat(*w, *h, &state->info_png.color) + *h;
  }
  else
  {
    /*Adam-7 interlaced: predicted size is the sum of the 7 sub-images sizes*/
    const LodePNGColorMode* color = &state->info_png.color;
    predict = 0;
    predict += lodepng_get_raw_size_idat((*w + 7) / 8, (*h + 7) / 8, color) + (*h + 7) / 8;
    if(*w > 4) predict += lodepng_get_raw_size_idat((*w + 3) / 8, (*h + 7) / 8, color) + (*h + 7) / 8;
    predict += lodepng_get_raw_size_idat((*w + 3) / 4, (*h + 3) / 8, color) + (*h + 3) / 8;
    if(*w > 2) predict += lodepng_get_raw_size_idat((*w + 1) / 4, (*h + 3) / 4, color) + (*h + 3) / 4;
    predict += lodepng_get_raw_size_idat((*w + 1) / 2, (*h + 1) / 4, color) + (*h + 1) / 4;
    if(*w > 1) predict += lodepng_get_raw_size_idat((*w) / 2, (*h + 1) / 2, color) + (*h + 1) / 2;
    predict += lodepng_get_raw_size_idat((*w) / 1, (*h) / 2, color) + (*h) / 2;
  }
  if(!state->error && !ucvector_reserve(&scanlines, predict)) state->error = 83; /*alloc fail*/
  if(!state->error)
  {
    state->error = lodepng_zlib_decompress(&scanlines.data, &scanlines.size, idat.data,
                                   idat.size);
    if(!state->error && scanlines.size != predict) state->error = 91; /*decompressed size doesn't match prediction*/
  }
  ucvector_cleanup(&idat);

  if(!state->error)
  {
    size_t outsize = lodepng_get_raw_size(*w, *h, &state->info_png.color);
    ucvector outv;
    ucvector_init(&outv);
    outv.data = (unsigned char*)calloc(outsize, sizeof(unsigned char));
    if (!outv.data){
      state->error = 83;
    }
    outv.allocsize = outsize * sizeof(unsigned char);
    outv.size = outsize;
    if(!state->error) state->error = postProcessScanlines(outv.data, scanlines.data, *w, *h, &state->info_png);
    *out = outv.data;
  }
  ucvector_cleanup(&scanlines);
}

unsigned lodepng_decode(unsigned char** out, unsigned* w, unsigned* h,
                        LodePNGState* state,
                        const unsigned char* in, size_t insize)
{
  *out = 0;
  decodeGeneric(out, w, h, state, in, insize);
  if(state->error) return state->error;
  if(!state->decoder.color_convert || lodepng_color_mode_equal(&state->info_raw, &state->info_png.color))
  {
    /*same color type, no copying or converting of data needed*/
    /*store the info_png color settings on the info_raw so that the info_raw still reflects what colortype
    the raw image has to the end user*/
    if(!state->decoder.color_convert)
    {
      state->error = lodepng_color_mode_copy(&state->info_raw, &state->info_png.color);
      if(state->error) return state->error;
    }
  }
  else
  {
    /*color conversion needed; sort of copy of the data*/
    unsigned char* data = *out;
    size_t outsize;

    /*TODO: check if this works according to the statement in the documentation: "The converter can convert
    from greyscale input color type, to 8-bit greyscale or greyscale with alpha"*/
    if(!(state->info_raw.colortype == LCT_RGB || state->info_raw.colortype == LCT_RGBA)
       && !(state->info_raw.bitdepth == 8))
    {
      return 56; /*unsupported color mode conversion*/
    }

    outsize = lodepng_get_raw_size(*w, *h, &state->info_raw);
    *out = (unsigned char*)lodepng_malloc(outsize);
    if(!(*out))
    {
      state->error = 83; /*alloc fail*/
    }
    else state->error = lodepng_convert(*out, data, &state->info_raw,
                                        &state->info_png.color, *w, *h);
    free(data);
  }
  return state->error;
}

unsigned lodepng_decode_memory(unsigned char** out, unsigned* w, unsigned* h, const unsigned char* in,
                               size_t insize, LodePNGColorType colortype, unsigned bitdepth)
{
  unsigned error;
  LodePNGState state;
  lodepng_state_init(&state);
  state.info_raw.colortype = colortype;
  state.info_raw.bitdepth = bitdepth;
  error = lodepng_decode(out, w, h, &state, in, insize);
  lodepng_state_cleanup(&state);
  return error;
}

static void lodepng_decoder_settings_init(LodePNGDecoderSettings* settings)
{
  settings->color_convert = 1;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  settings->read_text_chunks = 1;
  settings->remember_unknown_chunks = 0;
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
}

#endif /*LODEPNG_COMPILE_DECODER*/

#if defined(LODEPNG_COMPILE_DECODER) || defined(LODEPNG_COMPILE_ENCODER)

void lodepng_state_init(LodePNGState* state)
{
#ifdef LODEPNG_COMPILE_DECODER
  lodepng_decoder_settings_init(&state->decoder);
#endif /*LODEPNG_COMPILE_DECODER*/
#ifdef LODEPNG_COMPILE_ENCODER
  lodepng_encoder_settings_init(&state->encoder);
#endif /*LODEPNG_COMPILE_ENCODER*/
  lodepng_color_mode_init(&state->info_raw);
  lodepng_info_init(&state->info_png);
  state->error = 1;
}

void lodepng_state_cleanup(LodePNGState* state)
{
  lodepng_color_mode_cleanup(&state->info_raw);
  lodepng_info_cleanup(&state->info_png);
}

#endif /* defined(LODEPNG_COMPILE_DECODER) || defined(LODEPNG_COMPILE_ENCODER) */

#ifdef LODEPNG_COMPILE_ENCODER

/* ////////////////////////////////////////////////////////////////////////// */
/* / PNG Encoder                                                            / */
/* ////////////////////////////////////////////////////////////////////////// */

/*chunkName must be string of 4 characters*/
static unsigned addChunk(ucvector* out, const char* chunkName, const unsigned char* data, size_t length)
{
  CERROR_TRY_RETURN(lodepng_chunk_create(&out->data, &out->size, (unsigned)length, chunkName, data));
  out->allocsize = out->size; /*fix the allocsize again*/
  return 0;
}

static void writeSignature(ucvector* out)
{
  /*8 bytes PNG signature, aka the magic bytes*/
  ucvector_push_back(out, 137);
  ucvector_push_back(out, 80);
  ucvector_push_back(out, 78);
  ucvector_push_back(out, 71);
  ucvector_push_back(out, 13);
  ucvector_push_back(out, 10);
  ucvector_push_back(out, 26);
  ucvector_push_back(out, 10);
}

static unsigned addChunk_IHDR(ucvector* out, unsigned w, unsigned h,
                              LodePNGColorType colortype, unsigned bitdepth, unsigned interlace_method)
{
  unsigned error = 0;
  ucvector header;
  ucvector_init(&header);

  lodepng_add32bitInt(&header, w); /*width*/
  lodepng_add32bitInt(&header, h); /*height*/
  ucvector_push_back(&header, (unsigned char)bitdepth); /*bit depth*/
  ucvector_push_back(&header, (unsigned char)colortype); /*color type*/
  ucvector_push_back(&header, 0); /*compression method*/
  ucvector_push_back(&header, 0); /*filter method*/
  ucvector_push_back(&header, interlace_method); /*interlace method*/

  error = addChunk(out, "IHDR", header.data, header.size);
  ucvector_cleanup(&header);

  return error;
}

static unsigned addChunk_PLTE(ucvector* out, const LodePNGColorMode* info)
{
  unsigned error = 0;
  size_t i;
  ucvector PLTE;
  ucvector_init(&PLTE);
  for(i = 0; i != info->palettesize * 4; ++i)
  {
    /*add all channels except alpha channel*/
    if(i % 4 != 3) ucvector_push_back(&PLTE, info->palette[i]);
  }
  error = addChunk(out, "PLTE", PLTE.data, PLTE.size);
  ucvector_cleanup(&PLTE);

  return error;
}

static unsigned addChunk_tRNS(ucvector* out, const LodePNGColorMode* info)
{
  unsigned error = 0;
  size_t i;
  ucvector tRNS;
  ucvector_init(&tRNS);
  if(info->colortype == LCT_PALETTE)
  {
    size_t amount = info->palettesize;
    /*the tail of palette values that all have 255 as alpha, does not have to be encoded*/
    for(i = info->palettesize; i != 0; --i)
    {
      if(info->palette[4 * (i - 1) + 3] == 255) --amount;
      else break;
    }
    /*add only alpha channel*/
    for(i = 0; i != amount; ++i) ucvector_push_back(&tRNS, info->palette[4 * i + 3]);
  }
  else if(info->colortype == LCT_GREY)
  {
    if(info->key_defined)
    {
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r / 256));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r % 256));
    }
  }
  else if(info->colortype == LCT_RGB)
  {
    if(info->key_defined)
    {
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r / 256));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_r % 256));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_g / 256));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_g % 256));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_b / 256));
      ucvector_push_back(&tRNS, (unsigned char)(info->key_b % 256));
    }
  }

  error = addChunk(out, "tRNS", tRNS.data, tRNS.size);
  ucvector_cleanup(&tRNS);

  return error;
}

static unsigned addChunk_IDAT(ucvector* out, const unsigned char* data, size_t datasize,
                              LodePNGCompressSettings* zlibsettings)
{
  ucvector zlibdata;
  unsigned error = 0;

  /*compress with the Zlib compressor*/
  ucvector_init(&zlibdata);
  error = lodepng_zlib_compress(&zlibdata.data, &zlibdata.size, data, datasize, zlibsettings);
  if(!error) error = addChunk(out, "IDAT", zlibdata.data, zlibdata.size);
  ucvector_cleanup(&zlibdata);

  return error;
}

static unsigned addChunk_IEND(ucvector* out)
{
  unsigned error = 0;
  error = addChunk(out, "IEND", 0, 0);
  return error;
}

#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS

static unsigned addChunk_tEXt(ucvector* out, const char* keyword, const char* textstring)
{
  unsigned error = 0;
  size_t i;
  ucvector text;
  ucvector_init(&text);
  for(i = 0; keyword[i] != 0; ++i) ucvector_push_back(&text, (unsigned char)keyword[i]);
  if(i < 1 || i > 79) return 89; /*error: invalid keyword size*/
  ucvector_push_back(&text, 0); /*0 termination char*/
  for(i = 0; textstring[i] != 0; ++i) ucvector_push_back(&text, (unsigned char)textstring[i]);
  error = addChunk(out, "tEXt", text.data, text.size);
  ucvector_cleanup(&text);

  return error;
}

static unsigned addChunk_zTXt(ucvector* out, const char* keyword, const char* textstring,
                              LodePNGCompressSettings* zlibsettings)
{
  unsigned error = 0;
  ucvector data, compressed;
  size_t i, textsize = strlen(textstring);

  ucvector_init(&data);
  ucvector_init(&compressed);
  for(i = 0; keyword[i] != 0; ++i) ucvector_push_back(&data, (unsigned char)keyword[i]);
  if(i < 1 || i > 79) return 89; /*error: invalid keyword size*/
  ucvector_push_back(&data, 0); /*0 termination char*/
  ucvector_push_back(&data, 0); /*compression method: 0*/

  error = lodepng_zlib_compress(&compressed.data, &compressed.size,
                        (unsigned char*)textstring, textsize, zlibsettings);
  if(!error)
  {
    for(i = 0; i != compressed.size; ++i) ucvector_push_back(&data, compressed.data[i]);
    error = addChunk(out, "zTXt", data.data, data.size);
  }

  ucvector_cleanup(&compressed);
  ucvector_cleanup(&data);
  return error;
}

static unsigned addChunk_iTXt(ucvector* out, unsigned compressed, const char* keyword, const char* langtag,
                              const char* transkey, const char* textstring, LodePNGCompressSettings* zlibsettings)
{
  unsigned error = 0;
  ucvector data;
  size_t i, textsize = strlen(textstring);

  ucvector_init(&data);

  for(i = 0; keyword[i] != 0; ++i) ucvector_push_back(&data, (unsigned char)keyword[i]);
  if(i < 1 || i > 79) return 89; /*error: invalid keyword size*/
  ucvector_push_back(&data, 0); /*null termination char*/
  ucvector_push_back(&data, compressed ? 1 : 0); /*compression flag*/
  ucvector_push_back(&data, 0); /*compression method*/
  for(i = 0; langtag[i] != 0; ++i) ucvector_push_back(&data, (unsigned char)langtag[i]);
  ucvector_push_back(&data, 0); /*null termination char*/
  for(i = 0; transkey[i] != 0; ++i) ucvector_push_back(&data, (unsigned char)transkey[i]);
  ucvector_push_back(&data, 0); /*null termination char*/

  if(compressed)
  {
    ucvector compressed_data;
    ucvector_init(&compressed_data);
    error = lodepng_zlib_compress(&compressed_data.data, &compressed_data.size,
                          (unsigned char*)textstring, textsize, zlibsettings);
    if(!error)
    {
      for(i = 0; i != compressed_data.size; ++i) ucvector_push_back(&data, compressed_data.data[i]);
    }
    ucvector_cleanup(&compressed_data);
  }
  else /*not compressed*/
  {
    for(i = 0; textstring[i] != 0; ++i) ucvector_push_back(&data, (unsigned char)textstring[i]);
  }

  if(!error) error = addChunk(out, "iTXt", data.data, data.size);
  ucvector_cleanup(&data);
  return error;
}

static unsigned addChunk_bKGD(ucvector* out, const LodePNGInfo* info)
{
  unsigned error = 0;
  ucvector bKGD;
  ucvector_init(&bKGD);
  if(info->color.colortype == LCT_GREY || info->color.colortype == LCT_GREY_ALPHA)
  {
    ucvector_push_back(&bKGD, (unsigned char)(info->background_r / 256));
    ucvector_push_back(&bKGD, (unsigned char)(info->background_r % 256));
  }
  else if(info->color.colortype == LCT_RGB || info->color.colortype == LCT_RGBA)
  {
    ucvector_push_back(&bKGD, (unsigned char)(info->background_r / 256));
    ucvector_push_back(&bKGD, (unsigned char)(info->background_r % 256));
    ucvector_push_back(&bKGD, (unsigned char)(info->background_g / 256));
    ucvector_push_back(&bKGD, (unsigned char)(info->background_g % 256));
    ucvector_push_back(&bKGD, (unsigned char)(info->background_b / 256));
    ucvector_push_back(&bKGD, (unsigned char)(info->background_b % 256));
  }
  else if(info->color.colortype == LCT_PALETTE)
  {
    ucvector_push_back(&bKGD, (unsigned char)(info->background_r % 256)); /*palette index*/
  }

  error = addChunk(out, "bKGD", bKGD.data, bKGD.size);
  ucvector_cleanup(&bKGD);

  return error;
}

#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/

static void filterScanline(unsigned char* out, const unsigned char* scanline, const unsigned char* prevline,
                           size_t length, size_t bytewidth, unsigned char filterType)
{
  size_t i;
  switch(filterType)
  {
    case 0: /*None*/
      for(i = 0; i != length; ++i) out[i] = scanline[i];
      break;
    case 1: /*Sub*/
      for(i = 0; i != bytewidth; ++i) out[i] = scanline[i];
      for(i = bytewidth; i < length; ++i) out[i] = scanline[i] - scanline[i - bytewidth];
      break;
    case 2: /*Up*/
      if(prevline)
      {
        for(i = 0; i != length; ++i) out[i] = scanline[i] - prevline[i];
      }
      else
      {
        for(i = 0; i != length; ++i) out[i] = scanline[i];
      }
      break;
    case 3: /*Average*/
      if(prevline)
      {
        for(i = 0; i != bytewidth; ++i) out[i] = scanline[i] - prevline[i] / 2;
        for(i = bytewidth; i < length; ++i) out[i] = scanline[i] - ((scanline[i - bytewidth] + prevline[i]) / 2);
      }
      else
      {
        for(i = 0; i != bytewidth; ++i) out[i] = scanline[i];
        for(i = bytewidth; i < length; ++i) out[i] = scanline[i] - scanline[i - bytewidth] / 2;
      }
      break;
    case 4: /*Paeth*/
      if(prevline)
      {
        /*paethPredictor(0, prevline[i], 0) is always prevline[i]*/
        for(i = 0; i != bytewidth; ++i) out[i] = (scanline[i] - prevline[i]);
        for(i = bytewidth; i < length; ++i)
        {
          out[i] = (scanline[i] - paethPredictor(scanline[i - bytewidth], prevline[i], prevline[i - bytewidth]));
        }
      }
      else
      {
        for(i = 0; i != bytewidth; ++i) out[i] = scanline[i];
        /*paethPredictor(scanline[i - bytewidth], 0, 0) is always scanline[i - bytewidth]*/
        for(i = bytewidth; i < length; ++i) out[i] = (scanline[i] - scanline[i - bytewidth]);
      }
      break;
    default: return; /*unexisting filter type given*/
  }
}

static void filterScanline2(unsigned char* scanline, const unsigned char* prevline,
                           size_t length, unsigned char filterType, unsigned char forReal)
{
  if (!filterType) {
    for(int i = 0; i < length; i+=4){
      if (!scanline[i + 3]) {
        *(unsigned*)&scanline[i] = 0;
      }
    }
  }
  /*else if(filterType == 1){
    if (!scanline[3]) {
      *(unsigned*)scanline = 0;
    }
    for(int i = 4; i < length; i+=4){
      if (!scanline[i + 3]) {
        scanline[i] = scanline[i - 4];
        scanline[i + 1] = scanline[i - 3];
        scanline[i + 2] = scanline[i - 2];
      }
    }
  }*/
  else if(filterType == 2){
    if (!prevline){
      for(int i = 0; i < length; i+=4){
        if (!scanline[i + 3]) {
          *(unsigned*)&scanline[i] = 0;
        }
      }
    }
    else{
      for(int i = 0; i < length; i+=4){
        if (!scanline[i + 3]) {
          scanline[i] = prevline[i];
          scanline[i + 1] = prevline[i + 1];
          scanline[i + 2] = prevline[i + 2];
        }
      }
    }
  }
  else if(filterType == 3){
    if (!prevline){
      if (!scanline[3]) {
        *(unsigned*)scanline = 0;
      }
      for(int i = 4; i < length; i+=4){
        if (!scanline[i + 3]) {
          scanline[i] = scanline[i - 4] / 2;
          scanline[i + 1] = scanline[i - 3] / 2;
          scanline[i + 2] = scanline[i - 2] / 2;
        }
      }
    }
    else{
      if (!scanline[3]) {
        scanline[0] = prevline[0] / 2;
        scanline[1] = prevline[1] / 2;
        scanline[2] = prevline[2] / 2;
      }
      for(int i = 4; i < length; i+=4){
        if (!scanline[i + 3]) {
          scanline[i] = (scanline[i - 4] + prevline[i]) / 2;
          scanline[i + 1] = (scanline[i - 3] + prevline[i + 1]) / 2;
          scanline[i + 2] = (scanline[i - 2] + prevline[i + 2]) / 2;
        }
      }
    }
  }
  else if(filterType == 4 && forReal){
    if (!prevline){
      if (!scanline[3]) {
        *(unsigned*)scanline = 0;
      }
      for(int i = 4; i < length; i+=4){
        if (!scanline[i + 3]) {
          scanline[i] = scanline[i - 4];
          scanline[i + 1] = scanline[i - 3];
          scanline[i + 2] = scanline[i - 2];
        }
      }
    }
    else{
      if (!scanline[3]) {
        scanline[0] = prevline[0];
        scanline[1] = prevline[1];
        scanline[2] = prevline[2];
      }
      for(int i = 4; i < length; i+=4){
        if (!scanline[i + 3]) {
          scanline[i] = paethPredictor(scanline[i - 4], prevline[i], prevline[i - 4]);
          scanline[i + 1] = paethPredictor(scanline[i - 3], prevline[i], prevline[i - 3]);
          scanline[i + 2] = paethPredictor(scanline[i - 2], prevline[i], prevline[i - 2]);
        }

      }
    }
  }
}

extern "C" size_t ZopfliLZ77LazyLauncher(const unsigned char* in,
                              size_t instart, size_t inend, unsigned fs);

static void initRandomUInt64(uint64_t* s) {
  /* xorshift+ requires 128 bits of state */
  s[0] = 1;
  s[1] = 2;
}

/* xorshift+ pseudorandom number generator */
static uint64_t randomUInt64(uint64_t* s) {
  uint64_t x = s[0];
  uint64_t const y = s[1];
  s[0] = y;
  x ^= x << 23;
  s[1] = x ^ y ^ (x >> 17) ^ (y >> 26);
  return s[1] + y;
}

/* generate random number between 0 and 1 */
static double randomDecimal(uint64_t* s) {
  return double(randomUInt64(s)) / UINT64_MAX;
}

#include <signal.h>
static int signaled = 0;
static void sig_handler(int signo)
{
  if (signo == SIGINT){
    printf("received SIGINT, will stop after this iteration\n");
    signaled = 1;
  }
}

static char windowbits(unsigned long len){
  int result = 0;
#ifdef __GNUC__
  result = __builtin_clzl(len) ^ (8 * sizeof(unsigned long) - 1);
#else
  while (len >>= 1){result++;}
#endif

  result++;
  if (result < 9){
    result = 9;
  }
  else if (result > 15){
    result = 15;
  }
  return -result;
}

static unsigned filter(unsigned char* out, unsigned char* in, unsigned w, unsigned h,
                       const LodePNGColorMode* info, LodePNGEncoderSettings* settings)
{
  /*
  For PNG filter method 0
  out must be a buffer with as size: h + (w * h * bpp + 7) / 8, because there are
  the scanlines with 1 extra byte per scanline
  */

  unsigned bpp = lodepng_get_bpp(info);
  /*the width of a scanline in bytes, not including the filter type*/
  size_t linebytes = (w * bpp + 7) / 8;
  /*bytewidth is used for filtering, is 1 when bpp < 8, number of bytes per pixel otherwise*/
  size_t bytewidth = (bpp + 7) / 8;
  const unsigned char* prevline = 0;
  unsigned x, y;
  LodePNGFilterStrategy strategy = settings->filter_strategy;

  if(bpp == 0) return 31; /*error: invalid color type*/

  if(strategy < LFS_BRUTE_FORCE)
  {
    for(y = 0; y != h; ++y)
    {
      size_t outindex = (1 + linebytes) * y; /*the extra filterbyte added to each row*/
      size_t inindex = linebytes * y;
      out[outindex] = strategy; /*filter type byte*/
      filterScanline(&out[outindex + 1], &in[inindex], prevline, linebytes, bytewidth, strategy);
      prevline = &in[inindex];
    }
  }
  else if(strategy == LFS_PREDEFINED)
  {
    for(y = 0; y != h; ++y)
    {
      size_t outindex = (1 + linebytes) * y; /*the extra filterbyte added to each row*/
      size_t inindex = linebytes * y;
      unsigned char type = settings->predefined_filters[y];
      out[outindex] = type; /*filter type byte*/
      filterScanline(&out[outindex + 1], &in[inindex], prevline, linebytes, bytewidth, type);
      prevline = &in[inindex];
    }
  }
  else
  {
    unsigned clean = settings->clean_alpha && info->colortype == LCT_RGBA && info->bitdepth == 8 && !info->key_defined;
    unsigned char* in2 = 0;
    unsigned char* rem = 0;
    if(clean){
      in2 = (unsigned char*)malloc(linebytes * h);
      if (!in2){
        exit(1);
      }
      memcpy(in2, in, linebytes * h);
      rem = (unsigned char*)malloc(linebytes);
    }
    if(strategy == LFS_BRUTE_FORCE)
  {
    /*brute force filter chooser.
     deflate the scanline after every filter attempt to see which one deflates best.*/
    size_t size[5];
    ucvector attempt[5]; /*five filtering attempts, one for each filter type*/
    size_t smallest = 0;
    unsigned type = 0, bestType = 0;

    z_stream stream;
    stream.zalloc = 0;
    stream.zfree = 0;
    stream.opaque = 0;

    int err = deflateInit2(&stream, 3, Z_DEFLATED, windowbits(linebytes), 3, Z_FILTERED);
    if (err != Z_OK) exit(1);

    for(type = 0; type != 5; ++type)
    {
      ucvector_init(&attempt[type]);
      if(!ucvector_resize(&attempt[type], linebytes)) return 83; /*alloc fail*/
    }

    for(y = 0; y != h; ++y) /*try the 5 filter types*/
    {
      memcpy(rem, &in2[y * linebytes], linebytes * clean);
      for(type = 0; type != 5; ++type)
      {
        if(clean){
          filterScanline2(&in2[y * linebytes], prevline, linebytes, type, 0);
          filterScanline(attempt[type].data, &in2[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        else{
          filterScanline(attempt[type].data, &in[y * linebytes], prevline, linebytes, bytewidth, type);
        }

        if(settings->filter_style < 2 || 1){
          deflateTune(&stream, 258, 258, 258, 550 + (settings->filter_style) * 100);
          stream.next_in = (z_const unsigned char *)attempt[type].data;
          stream.avail_in = linebytes;
          stream.avail_out = UINT_MAX;
          stream.next_out = (unsigned char *)1;

          deflate_nooutput(&stream, Z_FINISH);

          size[type] = stream.total_out;
          deflateReset(&stream);
        }
        else{
          size[type] = ZopfliLZ77LazyLauncher(attempt[type].data, 0, linebytes, settings->filter_style);
        }

        /*check if this is smallest size (or if type == 0 it's the first case so always store the values)*/
        if(type == 0 || size[type] < smallest)
        {
          bestType = type;
          smallest = size[type];
        }
        if(clean){
          memcpy(&in2[y * linebytes], rem, linebytes);
        }

      }
      out[y * (linebytes + 1)] = bestType; /*the first byte of a scanline will be the filter type*/
      for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType].data[x];
      if(clean){
        filterScanline2(&in2[y * linebytes], prevline, linebytes, bestType, 0);
        prevline = &in2[y * linebytes];
      }
      else{
        prevline = &in[y * linebytes];
      }
    }

    deflateEnd(&stream);
    for(type = 0; type != 5; ++type) ucvector_cleanup(&attempt[type]);

  }
  else if(strategy == LFS_INCREMENTAL || strategy == LFS_INCREMENTAL2 || strategy == LFS_INCREMENTAL3){
    /*Incremental brute force filter chooser.
     Keep a buffer of each tested scanline and deflate the entire buffer after every filter attempt to see which one deflates best.
     Now implemented with streaming, which reduces complexity to O(n)
     This is slow.*/
    size_t size[5];
    unsigned char* attempt[5]; /*five filtering attempts, one for each filter type*/
    size_t smallest;
    unsigned type, bestType = 0;

    z_stream dstream;
    z_stream teststream;

    dstream.zalloc = 0;
    dstream.zfree = 0;
    dstream.opaque = 0;

    size_t testsize = linebytes + 1;
    int err = deflateInit2(&dstream, strategy == LFS_INCREMENTAL3 ? 1 : 2, Z_DEFLATED, windowbits(testsize * h), 8, Z_FILTERED);
    if (err != Z_OK) exit(1);
    if(strategy == LFS_INCREMENTAL){
      deflateTune(&dstream, 16, 258, 258, 200);
    }
    else if (strategy == LFS_INCREMENTAL2){
      deflateTune(&dstream, 50, 258, 258, 1100);
    }
    deflateCopy(&teststream, &dstream, 1);

    unsigned char* dummy = (unsigned char*)1; //Not used, but must not be NULL

    unsigned char* prevline2 = 0;
    unsigned char* prevlinebuf = 0;
    unsigned char* linebuf;
    if(clean){
      prevlinebuf = (unsigned char*)malloc(linebytes);
      linebuf = (unsigned char*)malloc(linebytes);
    }

    for(type = 0; type != 5; ++type)
    {
      attempt[type] = (unsigned char*)lodepng_malloc(linebytes);
      if(!attempt[type]) return 83; /*alloc fail*/
    }

    for(y = 0; y != h; ++y) /*try the 5 filter types*/
    {
      smallest = SIZE_MAX;
      for(type = 4; type + 1 != 0; --type) /*type 0 is most likely, so end with that to reduce copying*/
      {
        if(clean){
          memcpy(linebuf, &in[y * linebytes], linebytes);
          filterScanline2(linebuf, prevline2, linebytes, type, 0);
          filterScanline(attempt[type], linebuf, prevline2, linebytes, bytewidth, type);
        }
        else{
          filterScanline(attempt[type], &in[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        /*copy result to output buffer temporarily to include compression test*/
        out[y * (linebytes + 1)] = type; /*the first byte of a scanline will be the filter type*/
        for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[type][x];
        size[type] = 0;

        deflateCopy(&teststream, &dstream, 0);
        teststream.next_in = (z_const unsigned char *)(out + y * testsize);
        teststream.avail_in = testsize;
        teststream.avail_out = UINT_MAX;
        teststream.next_out = dummy;
        deflate_nooutput(&teststream, Z_FINISH);

        size[type] = teststream.total_out;

        /*check if this is smallest size (or if type == 4 it's the first case so always store the values)*/
        if(size[type] < smallest)
        {
          bestType = type;
          smallest = size[type];
        }
      }

      if(clean){
        memcpy(linebuf, &in[y * linebytes], linebytes);
        filterScanline2(linebuf, prevline2, linebytes, bestType, 0);
        filterScanline(attempt[bestType], linebuf, prevline2, linebytes, bytewidth, bestType);
      }
      else{
        filterScanline(attempt[bestType], &in[y * linebytes], prevline, linebytes, bytewidth, bestType);
      }
      /*copy result to output buffer temporarily to include compression test*/
      out[y * (linebytes + 1)] = bestType; /*the first byte of a scanline will be the filter type*/
      for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];

      dstream.next_in = (z_const unsigned char *)(out + y * testsize);
      dstream.avail_in = testsize;
      dstream.avail_out = UINT_MAX;
      dstream.next_out = dummy;
      deflate_nooutput(&dstream, Z_NO_FLUSH);

      prevline = &in[y * linebytes];
      if(clean){
        memcpy(linebuf, &in[y * linebytes], linebytes);
        filterScanline2(linebuf, prevline2, linebytes, bestType, 0);
        memcpy(prevlinebuf, linebuf, linebytes);
        prevline2 = prevlinebuf;
      }
      out[y * (linebytes + 1)] = bestType; /*the first byte of a scanline will be the filter type*/
      if (type) /*last attempt is type 0, so no copying necessary*/
      {
        for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];
      }
    }
    if(clean){
      free(prevlinebuf);
      free(linebuf);
    }
    deflateEnd(&dstream);
    deflateEnd(&teststream);
    for(type = 0; type != 5; ++type) free(attempt[type]);
  }
  else if(strategy == LFS_MINSUM)
  {
    /*adaptive filtering*/
    size_t sum[5];
    unsigned char* attempt[5]; /*five filtering attempts, one for each filter type*/
    size_t smallest = 0;
    unsigned char type, bestType = 0;

    for(type = 0; type != 5; ++type)
    {
      attempt[type] = (unsigned char*)lodepng_malloc(linebytes);
      if(!attempt[type]) return 83; /*alloc fail*/
    }
    for(y = 0; y != h; ++y)
    {
      memcpy(rem, &in2[y * linebytes], linebytes * clean);
      /*try the 5 filter types*/
      for(type = 0; type != 5; ++type)
      {
        if(clean){
          filterScanline2(&in2[y * linebytes], prevline, linebytes, type, 0);
          filterScanline(attempt[type], &in2[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        else{
          filterScanline(attempt[type], &in[y * linebytes], prevline, linebytes, bytewidth, type);
        }

        /*calculate the sum of the result*/
        sum[type] = 0;
        if(type == 0)
        {
          for(x = 0; x != linebytes; ++x) sum[type] += (unsigned char)(attempt[type][x]);
        }
        else
        {
          for(x = 0; x != linebytes; ++x)
          {
            /*For differences, each byte should be treated as signed, values above 127 are negative
             (converted to signed char). Filtertype 0 isn't a difference though, so use unsigned there.
             This means filtertype 0 is almost never chosen, but that is justified.*/
            unsigned char s = attempt[type][x];
            sum[type] += s < 128 ? s : (255U - s);
          }
        }

        /*check if this is smallest sum (or if type == 0 it's the first case so always store the values)*/
        if(type == 0 || sum[type] < smallest)
        {
          bestType = type;
          smallest = sum[type];
        }
        if(clean){
          memcpy(&in2[y * linebytes], rem, linebytes);
        }
      }

      /*now fill the out values*/
      out[y * (linebytes + 1)] = bestType; /*the first byte of a scanline will be the filter type*/
      for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];
      if(clean){
        filterScanline2(&in2[y * linebytes], prevline, linebytes, bestType, 0);
        prevline = &in2[y * linebytes];
      }
      else{
        prevline = &in[y * linebytes];
      }
    }

    for(type = 0; type != 5; ++type) free(attempt[type]);
  }
  else if(strategy == LFS_DISTINCT_BYTES)
  {
    size_t sum[5];
    unsigned char* attempt[5]; /*five filtering attempts, one for each filter type*/
    size_t smallest;
    unsigned type, bestType = 0;
    unsigned char count[256];

    for(type = 0; type != 5; ++type)
    {
      attempt[type] = (unsigned char*)lodepng_malloc(linebytes);
      if(!attempt[type]) return 83; /*alloc fail*/
    }

    for(y = 0; y != h; ++y)
    {
      memcpy(rem, &in2[y * linebytes], linebytes * clean);
      smallest = SIZE_MAX;
      /*try the 5 filter types*/
      for(type = 0; type != 5; ++type)
      {
        if(clean){
          filterScanline2(&in2[y * linebytes], prevline, linebytes, type, 0);
          filterScanline(attempt[type], &in2[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        else{
          filterScanline(attempt[type], &in[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        memset(count, 0, 256);
        for(x = 0; x != linebytes; ++x) count[attempt[type][x]] = 1;
        count[type] = 1; /*the filter type itself is part of the scanline*/
        sum[type] = 0;
        for(x = 0; x != 256; ++x)
        {
          if(count[x]) ++sum[type];
        }
        /*check if this is smallest sum (or if type == 0 it's the first case so always store the values)*/
        if(sum[type] < smallest)
        {
          bestType = type;
          smallest = sum[type];
        }
        if(clean){
          memcpy(&in2[y * linebytes], rem, linebytes);
        }
      }

      /*now fill the out values*/
      out[y * (linebytes + 1)] = bestType; /*the first byte of a scanline will be the filter type*/
      for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];
      if(clean){
        filterScanline2(&in2[y * linebytes], prevline, linebytes, bestType, 0);
        prevline = &in2[y * linebytes];
      }
      else{
        prevline = &in[y * linebytes];
      }
    }

    for(type = 0; type != 5; ++type) free(attempt[type]);
  }
  else if(strategy == LFS_DISTINCT_BIGRAMS)
  {
    size_t sum[5];
    unsigned char* attempt[5]; /*five filtering attempts, one for each filter type*/
    size_t smallest;
    unsigned type, bestType = 0;
    unsigned char count[65536];

    for(type = 0; type != 5; ++type)
    {
      attempt[type] = (unsigned char*)lodepng_malloc(linebytes);
      if(!attempt[type]) return 83; /*alloc fail*/
    }

    for(y = 0; y != h; ++y)
    {
      memcpy(rem, &in2[y * linebytes], linebytes * clean);
      smallest = SIZE_MAX;
      /*try the 5 filter types*/
      for(type = 0; type != 5; ++type)
      {
        if(clean){
          filterScanline2(&in2[y * linebytes], prevline, linebytes, type, 0);
          filterScanline(attempt[type], &in2[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        else{
          filterScanline(attempt[type], &in[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        memset(count, 0, 65536);
        for(x = 1; x != linebytes; ++x) count[(attempt[type][x - 1] << 8) + attempt[type][x]] = 1;
        count[type] = 1; /*the filter type itself is part of the scanline*/
        sum[type] = 0;
        for(x = 0; x != 65536; ++x)
        {
          if(count[x] != 0) ++sum[type];
        }
        /*check if this is smallest sum (or if type == 0 it's the first case so always store the values)*/
        if(sum[type] < smallest)
        {
          bestType = type;
          smallest = sum[type];
        }
        if(clean){
          memcpy(&in2[y * linebytes], rem, linebytes);
        }
      }

      /*now fill the out values*/
      out[y * (linebytes + 1)] = bestType; /*the first byte of a scanline will be the filter type*/
      for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];
      if(clean){
        filterScanline2(&in2[y * linebytes], prevline, linebytes, bestType, 0);
        prevline = &in2[y * linebytes];
      }
      else{
        prevline = &in[y * linebytes];
      }
    }

    for(type = 0; type != 5; ++type) free(attempt[type]);
  }
  else if(strategy == LFS_ENTROPY)
  {
    float sum[5];
    unsigned char* attempt[5]; /*five filtering attempts, one for each filter type*/
    float smallest = 0;
    unsigned type, bestType = 0;
    unsigned count[256];

    for(type = 0; type != 5; ++type)
    {
      attempt[type] = (unsigned char*)lodepng_malloc(linebytes);
      if(!attempt[type]) return 83; /*alloc fail*/
    }

    for(y = 0; y != h; ++y)
    {
      memcpy(rem, &in2[y * linebytes], linebytes * clean);
      /*try the 5 filter types*/
      for(type = 0; type != 5; ++type)
      {
        if(clean){
          filterScanline2(&in2[y * linebytes], prevline, linebytes, type, 0);
          filterScanline(attempt[type], &in2[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        else{
          filterScanline(attempt[type], &in[y * linebytes], prevline, linebytes, bytewidth, type);
        }
        for(x = 0; x != 256; ++x) count[x] = 0;
        for(x = 0; x != linebytes; ++x) ++count[attempt[type][x]];
        ++count[type]; /*the filter type itself is part of the scanline*/
        sum[type] = 0;
        for(x = 0; x != 256; ++x)
        {
          float p = count[x] / (float)(linebytes + 1);
          sum[type] += count[x] == 0 ? 0 : log2f(1 / p) * p;
        }
        /*check if this is smallest sum (or if type == 0 it's the first case so always store the values)*/
        if(type == 0 || sum[type] < smallest)
        {
          bestType = type;
          smallest = sum[type];
        }
        if(clean){
          memcpy(&in2[y * linebytes], rem, linebytes);
        }
      }

      /*now fill the out values*/
      out[y * (linebytes + 1)] = bestType; /*the first byte of a scanline will be the filter type*/
      for(x = 0; x != linebytes; ++x) out[y * (linebytes + 1) + 1 + x] = attempt[bestType][x];
      if(clean){
        filterScanline2(&in2[y * linebytes], prevline, linebytes, bestType, 0);
        prevline = &in2[y * linebytes];
      }
      else{
        prevline = &in[y * linebytes];
      }
    }

    for(type = 0; type != 5; ++type) free(attempt[type]);
  }
  else if(strategy == LFS_GENETIC || strategy == LFS_ALL_CHEAP)
  {
    if (strategy == LFS_GENETIC){
      printf("warning: You have decided to enable genetic filtering, which may take a very long time.\n"
             "the current generation and number of bytes is displayed.\n"
             "you can stop the genetic filtering anytime by pressing ctrl-c\n"
             "it will automatically stop after 500 generations without progress\n");
      signaled = 0;
    }

    unsigned char* prevlinebuf = 0;
    unsigned char* linebuf;
    if(clean){
      prevlinebuf = (unsigned char*)malloc(linebytes);
      linebuf = (unsigned char*)malloc(linebytes);
    }

    uint64_t r[2];
    initRandomUInt64(r);

    const int Strategies = strategy == LFS_ALL_CHEAP ? 3 : 0;
    /*Genetic algorithm filter finder. Attempts to find better filters through mutation and recombination.*/
    const size_t population_size = strategy == LFS_ALL_CHEAP ? Strategies : 19;
    const size_t last = population_size - 1;
    unsigned char* population = (unsigned char*)lodepng_malloc(h * population_size);
    size_t* size = (size_t*)lodepng_malloc(population_size * sizeof(size_t));
    unsigned* ranking = (unsigned*)lodepng_malloc(population_size * sizeof(int));
    unsigned g, i, j, e, t, c, type, crossover1, crossover2, selection_size, size_sum;
    unsigned best_size = UINT_MAX;
    unsigned total_size = 0;
    unsigned e_since_best = 0;

    z_stream stream;
    stream.zalloc = 0;
    stream.zfree = 0;
    stream.opaque = 0;
#define TUNE deflateTune(&stream, 16, 258, 258, 200);
    int err = deflateInit2(&stream, 3, Z_DEFLATED, windowbits(h * (linebytes + 1)), 8, Z_FILTERED);
    if (err != Z_OK) exit(1);
    unsigned char* dummy = (unsigned char *)1;
    size_t popcnt;
    uint64_t r2[2];
    initRandomUInt64(r2);
    signal(SIGINT, sig_handler);
    for(popcnt = 0; popcnt < h * (population_size - Strategies); ++popcnt) population[popcnt] = randomUInt64(r2) % 5;

    for(g = 0; g <= last; ++g)
    {
      if (strategy == LFS_ALL_CHEAP){
        settings->filter_strategy = (LodePNGFilterStrategy)(g + 11);
        filter(out, in, w, h, info, settings);
        settings->filter_strategy = LFS_ALL_CHEAP;
        for(size_t k = 0; k < h * (linebytes + 1); k += (linebytes + 1))
        {
          population[popcnt++] = out[k];
        }
      }
      prevline = 0;
      for(y = 0; y < h; ++y)
      {
        type = population[g * h + y];
        out[y * (linebytes + 1)] = type;
        if(clean){
          memcpy(linebuf, &in[y * linebytes], linebytes);
          filterScanline2(linebuf, prevline, linebytes, type, 0);
          filterScanline(&out[y * (linebytes + 1) + 1], linebuf, prevline, linebytes, bytewidth, type);
          memcpy(prevlinebuf, linebuf, linebytes);
          prevline = prevlinebuf;
        }
        else{
          filterScanline(&out[y * (linebytes + 1) + 1], &in[y * linebytes], prevline, linebytes, bytewidth, type);
          prevline = &in[y * linebytes];
        }
      }
      TUNE
      stream.next_in = (z_const unsigned char *)out;
      stream.avail_in = h * (linebytes + 1);
      stream.avail_out = UINT_MAX;
      stream.next_out = dummy;

      deflate_nooutput(&stream, Z_FINISH);

      size[g] = stream.total_out;
      deflateReset(&stream);
      total_size += size[g];
      ranking[g] = g;
    }
    for(i = 0; strategy == LFS_ALL_CHEAP && i < population_size; i++)
    {
      if(size[i] < best_size){
        ranking[0] = i;
        best_size = size[i];
      }
    }
    //ctrl-c signals last iteration
    for(e = 0; strategy == LFS_GENETIC && e_since_best < 500 && !signaled; ++e)
    {
      /*resort rankings*/
      for(i = 1; i < population_size; ++i)
      {
        t = ranking[i];
        for(j = i - 1; j + 1 > 0 && size[ranking[j]] > size[t]; --j) ranking[j + 1] = ranking[j];
        ranking[j + 1] = t;
      }
      if(size[ranking[0]] < best_size)
      {
        best_size = size[ranking[0]];
        e_since_best = 0;
        printf("Generation %d: %d bytes\n", e, best_size);
      }
      else ++e_since_best;
      /*generate offspring*/
      for(c = 0; c < 3; ++c)
      {
        /*tournament selection*/
        /*parent 1*/
        selection_size = UINT_MAX;
        for(t = 0; t < 2; ++t) selection_size = std::min(unsigned(randomDecimal(r) * total_size), selection_size);
        size_sum = 0;
        for(j = 0; size_sum <= selection_size; ++j) size_sum += size[ranking[j]];
        unsigned char* parent1 = &population[ranking[j - 1] * h];
        /*parent 2*/
        selection_size = UINT_MAX;
        for(t = 0; t < 2; ++t) selection_size = std::min(unsigned(randomDecimal(r) * total_size), selection_size);
        size_sum = 0;
        for(j = 0; size_sum <= selection_size; ++j) size_sum += size[ranking[j]];
        unsigned char* parent2 = &population[ranking[j - 1] * h];
        /*two-point crossover*/
        unsigned char* child = &population[(ranking[last - c]) * h];
        if(randomDecimal(r) < 0.9)
        {
          crossover1 = randomUInt64(r) % h;
          crossover2 = randomUInt64(r) % h;
          if(crossover1 > crossover2)
          {
            crossover1 ^= crossover2;
            crossover2 ^= crossover1;
            crossover1 ^= crossover2;
          }
          if(child != parent1)
          {
            memcpy(child, parent1, crossover1);
            memcpy(&child[crossover2], &parent1[crossover2], h - crossover2);
          }
          if(child != parent2) memcpy(&child[crossover1], &parent2[crossover1], crossover2 - crossover1);
        }
        else if(randomUInt64(r) & 1) memcpy(child, parent1, h);
        else memcpy(child, parent2, h);
        /*mutation*/
        for(y = 0; y < h; ++y)
        {
          if(randomDecimal(r) < 0.01) child[y] = randomUInt64(r) % 5;
        }
        /*evaluate new genome*/
        total_size -= size[ranking[last - c]];
        prevline = 0;
        for(y = 0; y < h; ++y)
        {
          type = child[y];
          out[y * (linebytes + 1)] = type;
          if(clean){
            memcpy(linebuf, &in[y * linebytes], linebytes);
            filterScanline2(linebuf, prevline, linebytes, type, 0);
            filterScanline(&out[y * (linebytes + 1) + 1], linebuf, prevline, linebytes, bytewidth, type);
            memcpy(prevlinebuf, linebuf, linebytes);
            prevline = prevlinebuf;
          }
          else{
            filterScanline(&out[y * (linebytes + 1) + 1], &in[y * linebytes], prevline, linebytes, bytewidth, type);
            prevline = &in[y * linebytes];
          }
        }
        TUNE

        stream.next_in = (z_const unsigned char *)out;
        stream.avail_in = h * (linebytes + 1);
        stream.avail_out = UINT_MAX;
        stream.next_out = dummy;

        deflate_nooutput(&stream, Z_FINISH);

        size[ranking[last - c]] = stream.total_out;
        deflateReset(&stream);
        total_size += size[ranking[last - c]];
      }
    }
    /*final choice*/
    prevline = 0;
    for(y = 0; y < h; ++y)
    {
      type = population[ranking[0] * h + y];
      out[y * (linebytes + 1)] = type;
      if(clean){
        memcpy(linebuf, &in[y * linebytes], linebytes);
        filterScanline2(linebuf, prevline, linebytes, type, 0);
        filterScanline(&out[y * (linebytes + 1) + 1], linebuf, prevline, linebytes, bytewidth, type);
        memcpy(prevlinebuf, linebuf, linebytes);
        prevline = prevlinebuf;
      }
      else{
        filterScanline(&out[y * (linebytes + 1) + 1], &in[y * linebytes], prevline, linebytes, bytewidth, type);
        prevline = &in[y * linebytes];
      }
    }
    deflateEnd(&stream);
    free(population);
    free(size);
    free(ranking);
    if(clean){
      free(prevlinebuf);
      free(linebuf);
    }
  }
  else return 88; /* unknown filter strategy */
    free(rem);
    free(in2);
  }

  return 0;
}

static void addPaddingBits(unsigned char* out, const unsigned char* in,
                           size_t olinebits, size_t ilinebits, unsigned h)
{
  /*The opposite of the removePaddingBits function
  olinebits must be >= ilinebits*/
  unsigned y;
  size_t diff = olinebits - ilinebits;
  size_t obp = 0, ibp = 0; /*bit pointers*/
  for(y = 0; y != h; ++y)
  {
    size_t x;
    for(x = 0; x < ilinebits; ++x)
    {
      unsigned char bit = readBitFromReversedStream(&ibp, in);
      setBitOfReversedStream(&obp, out, bit);
    }
    /*obp += diff; --> no, fill in some value in the padding bits too, to avoid
    "Use of uninitialised value of size ###" warning from valgrind*/
    for(x = 0; x != diff; ++x) setBitOfReversedStream(&obp, out, 0);
  }
}

/*
in: non-interlaced image with size w*h
out: the same pixels, but re-ordered according to PNG's Adam7 interlacing, with
 no padding bits between scanlines, but between reduced images so that each
 reduced image starts at a byte.
bpp: bits per pixel
there are no padding bits, not between scanlines, not between reduced images
in has the following size in bits: w * h * bpp.
out is possibly bigger due to padding bits between reduced images
NOTE: comments about padding bits are only relevant if bpp < 8
*/
static void Adam7_interlace(unsigned char* out, const unsigned char* in, unsigned w, unsigned h, unsigned bpp)
{
  unsigned passw[7], passh[7];
  size_t filter_passstart[8], padded_passstart[8], passstart[8];
  unsigned i;

  Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

  if(bpp >= 8)
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      size_t bytewidth = bpp / 8;
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        size_t pixelinstart = ((ADAM7_IY[i] + y * ADAM7_DY[i]) * w + ADAM7_IX[i] + x * ADAM7_DX[i]) * bytewidth;
        size_t pixeloutstart = passstart[i] + (y * passw[i] + x) * bytewidth;
        for(b = 0; b < bytewidth; ++b)
        {
          out[pixeloutstart + b] = in[pixelinstart + b];
        }
      }
    }
  }
  else /*bpp < 8: Adam7 with pixels < 8 bit is a bit trickier: with bit pointers*/
  {
    for(i = 0; i != 7; ++i)
    {
      unsigned x, y, b;
      unsigned ilinebits = bpp * passw[i];
      unsigned olinebits = bpp * w;
      size_t obp, ibp; /*bit pointers (for out and in buffer)*/
      for(y = 0; y < passh[i]; ++y)
      for(x = 0; x < passw[i]; ++x)
      {
        ibp = (ADAM7_IY[i] + y * ADAM7_DY[i]) * olinebits + (ADAM7_IX[i] + x * ADAM7_DX[i]) * bpp;
        obp = (8 * passstart[i]) + (y * ilinebits + x * bpp);
        for(b = 0; b < bpp; ++b)
        {
          unsigned char bit = readBitFromReversedStream(&ibp, in);
          setBitOfReversedStream(&obp, out, bit);
        }
      }
    }
  }
}

/*out must be buffer big enough to contain uncompressed IDAT chunk data, and in must contain the full image.
return value is error**/
static unsigned preProcessScanlines(unsigned char** out, size_t* outsize, unsigned char* in,
                                    unsigned w, unsigned h,
                                    const LodePNGInfo* info_png, LodePNGEncoderSettings* settings)
{
  /*
  This function converts the pure 2D image with the PNG's colortype, into filtered-padded-interlaced data. Steps:
  *) if no Adam7: 1) add padding bits (= posible extra bits per scanline if bpp < 8) 2) filter
  *) if adam7: 1) Adam7_interlace 2) 7x add padding bits 3) 7x filter
  */
  unsigned bpp = lodepng_get_bpp(&info_png->color);
  unsigned error = 0;

  if(info_png->interlace_method == 0)
  {
    *outsize = h + (h * ((w * bpp + 7) / 8)); /*image size plus an extra byte per scanline + possible padding bits*/
    *out = (unsigned char*)lodepng_malloc(*outsize);
    if(!(*out) && (*outsize)) error = 83; /*alloc fail*/

    if(!error)
    {
      /*non multiple of 8 bits per scanline, padding bits needed per scanline*/
      if(bpp < 8 && w * bpp != ((w * bpp + 7) / 8) * 8)
      {
        unsigned char* padded = (unsigned char*)lodepng_malloc(h * ((w * bpp + 7) / 8));
        if(!padded) error = 83; /*alloc fail*/
        if(!error)
        {
          addPaddingBits(padded, in, ((w * bpp + 7) / 8) * 8, w * bpp, h);
          error = filter(*out, padded, w, h, &info_png->color, settings);
        }
        free(padded);
      }
      else
      {
        /*we can immediatly filter into the out buffer, no other steps needed*/
        error = filter(*out, in, w, h, &info_png->color, settings);
      }
    }
  }
  else /*interlace_method is 1 (Adam7)*/
  {
    unsigned passw[7], passh[7];
    size_t filter_passstart[8], padded_passstart[8], passstart[8];
    unsigned char* adam7;

    Adam7_getpassvalues(passw, passh, filter_passstart, padded_passstart, passstart, w, h, bpp);

    *outsize = filter_passstart[7]; /*image size plus an extra byte per scanline + possible padding bits*/
    *out = (unsigned char*)lodepng_malloc(*outsize);
    if(!(*out)) error = 83; /*alloc fail*/

    adam7 = (unsigned char*)lodepng_malloc(passstart[7]);
    if(!adam7 && passstart[7]) error = 83; /*alloc fail*/

    if(!error)
    {
      unsigned i;

      Adam7_interlace(adam7, in, w, h, bpp);
      for(i = 0; i != 7; ++i)
      {
        if(bpp < 8)
        {
          unsigned char* padded = (unsigned char*)lodepng_malloc(padded_passstart[i + 1] - padded_passstart[i]);
          if(!padded) ERROR_BREAK(83); /*alloc fail*/
          addPaddingBits(padded, &adam7[passstart[i]],
                         ((passw[i] * bpp + 7) / 8) * 8, passw[i] * bpp, passh[i]);
          error = filter(&(*out)[filter_passstart[i]], padded,
                         passw[i], passh[i], &info_png->color, settings);
          free(padded);
        }
        else
        {
          error = filter(&(*out)[filter_passstart[i]], &adam7[padded_passstart[i]],
                         passw[i], passh[i], &info_png->color, settings);
        }

        if(error) break;
      }
    }

    free(adam7);
  }

  return error;
}

/*
palette must have 4 * palettesize bytes allocated, and given in format RGBARGBARGBARGBA...
returns 0 if the palette is opaque,
returns 1 if the palette has a single color with alpha 0 ==> color key
returns 2 if the palette is semi-translucent.
*/
static unsigned getPaletteTranslucency(const unsigned char* palette, size_t palettesize)
{
  size_t i;
  unsigned key = 0;
  unsigned r = 0, g = 0, b = 0; /*the value of the color with alpha 0, so long as color keying is possible*/
  for(i = 0; i != palettesize; ++i)
  {
    if(!key && palette[4 * i + 3] == 0)
    {
      r = palette[4 * i]; g = palette[4 * i + 1]; b = palette[4 * i + 2];
      key = 1;
      i = (size_t)(-1); /*restart from beginning, to detect earlier opaque colors with key's value*/
    }
    else if(palette[4 * i + 3] != 255) return 2;
    /*when key, no opaque RGB may have key's RGB*/
    else if(key && r == palette[i * 4] && g == palette[i * 4 + 1] && b == palette[i * 4 + 2]) return 2;
  }
  return key;
}

#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
static unsigned addUnknownChunks(ucvector* out, unsigned char* data, size_t datasize)
{
  unsigned char* inchunk = data;
  while((size_t)(inchunk - data) < datasize)
  {
    CERROR_TRY_RETURN(lodepng_chunk_append(&out->data, &out->size, inchunk));
    out->allocsize = out->size; /*fix the allocsize again*/
    inchunk = lodepng_chunk_next(inchunk);
  }
  return 0;
}
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/

static ColorTree ct;
static unsigned lodepng_encode(unsigned char** out, size_t* outsize,
                        unsigned char* image, unsigned w, unsigned h,
                        LodePNGState* state, LodePNGPaletteSettings palset)
{
  LodePNGInfo info;
  ucvector outv;
  unsigned char* data = 0; /*uncompressed version of the IDAT chunk data*/
  size_t datasize = 0;

  /*provide some proper output values if error will happen*/
  *out = 0;
  *outsize = 0;
  state->error = 0;

  lodepng_info_init(&info);
  lodepng_info_copy(&info, &state->info_png);

  if((info.color.colortype == LCT_PALETTE || state->encoder.force_palette)
      && (info.color.palettesize == 0 || info.color.palettesize > 256))
  {
    state->error = 68; /*invalid palette size, it is only allowed to be 1-256*/
    return state->error;
  }

  if(state->encoder.auto_convert)
  {
    state->error = lodepng_auto_choose_color(&info.color, image, w, h, &state->info_raw, state->div);
    if(info.color.colortype == LCT_PALETTE && palset.order != LPOS_NONE)
    {
      if (palset._first & 1) {
        color_tree_init(&ct);
      }
      optimize_palette(&info.color, (uint32_t*)image, w, h, palset.priority, palset.direction,
                       palset.trans, palset.order);

      unsigned crc = crc32(0, info.color.palette, info.color.palettesize);
      if (!color_tree_inc(&ct, crc & 0xFF, crc & 0xFF00, crc & 0xFF0000, crc & 0xFF000000)) {
      }
      else{
        if (palset._first & 2) {
          color_tree_cleanup(&ct);
        }
        lodepng_info_cleanup(&info);
        return 96;
      }
      if (palset._first & 2) {
        color_tree_cleanup(&ct);
      }
    }
    lodepng_color_mode_init(&state->out_mode);
    lodepng_color_mode_copy(&state->out_mode, &info.color);

  }
  if(state->error) return state->error;

  if(state->encoder.zlibsettings.btype > 2)
  {
    CERROR_RETURN_ERROR(state->error, 61); /*error: unexisting btype*/
  }
  if(state->info_png.interlace_method > 1)
  {
    CERROR_RETURN_ERROR(state->error, 71); /*error: unexisting interlace mode*/
  }

  state->error = checkColorValidity(info.color.colortype, info.color.bitdepth);
  if(state->error) return state->error; /*error: unexisting color type given*/
  state->error = checkColorValidity(state->info_raw.colortype, state->info_raw.bitdepth);
  if(state->error) return state->error; /*error: unexisting color type given*/

  if(!lodepng_color_mode_equal(&state->info_raw, &info.color))
  {
    unsigned char* converted;
    size_t size = ((unsigned long long)w * h * lodepng_get_bpp(&info.color) + 7) / 8;

    converted = (unsigned char*)lodepng_malloc(size);
    if(!converted && size) state->error = 83; /*alloc fail*/
    if(!state->error)
    {
      state->error = lodepng_convert(converted, image, &info.color, &state->info_raw, w, h);
    }
    if(!state->error) preProcessScanlines(&data, &datasize, converted, w, h, &info, &state->encoder);
    free(converted);
  }
  else preProcessScanlines(&data, &datasize, image, w, h, &info, &state->encoder);

  ucvector_init(&outv);
  while(!state->error) /*while only executed once, to break on error*/
  {
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    size_t i;
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    /*write signature and chunks*/
    writeSignature(&outv);
    /*IHDR*/
    addChunk_IHDR(&outv, w, h, info.color.colortype, info.color.bitdepth, info.interlace_method);
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    /*unknown chunks between IHDR and PLTE*/
    if(info.unknown_chunks_data[0])
    {
      state->error = addUnknownChunks(&outv, info.unknown_chunks_data[0], info.unknown_chunks_size[0]);
      if(state->error) break;
    }
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    /*PLTE*/
    if(info.color.colortype == LCT_PALETTE)
    {
      addChunk_PLTE(&outv, &info.color);
    }
    if(state->encoder.force_palette && (info.color.colortype == LCT_RGB || info.color.colortype == LCT_RGBA))
    {
      addChunk_PLTE(&outv, &info.color);
    }
    /*tRNS*/
    if(info.color.colortype == LCT_PALETTE && getPaletteTranslucency(info.color.palette, info.color.palettesize) != 0)
    {
      addChunk_tRNS(&outv, &info.color);
    }
    if((info.color.colortype == LCT_GREY || info.color.colortype == LCT_RGB) && info.color.key_defined)
    {
      addChunk_tRNS(&outv, &info.color);
    }
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    /*bKGD (must come between PLTE and the IDAt chunks*/
    if(info.background_defined) addChunk_bKGD(&outv, &info);

    /*unknown chunks between PLTE and IDAT*/
    if(info.unknown_chunks_data[1])
    {
      state->error = addUnknownChunks(&outv, info.unknown_chunks_data[1], info.unknown_chunks_size[1]);
      if(state->error) break;
    }
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    /*IDAT (multiple IDAT chunks must be consecutive)*/
    //Work around problem w/ nonstandard malloc's. This can most likely be disabled.
    data = (unsigned char*)realloc(data, datasize + 8);
    state->error = addChunk_IDAT(&outv, data, datasize, &state->encoder.zlibsettings);
    if(state->error) break;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    /*tEXt and/or zTXt*/
    for(i = 0; i != info.text_num; ++i)
    {
      if(strlen(info.text_keys[i]) > 79)
      {
        state->error = 66; /*text chunk too large*/
        break;
      }
      if(strlen(info.text_keys[i]) < 1)
      {
        state->error = 67; /*text chunk too small*/
        break;
      }
      if(state->encoder.text_compression)
      {
        addChunk_zTXt(&outv, info.text_keys[i], info.text_strings[i], &state->encoder.zlibsettings);
      }
      else
      {
        addChunk_tEXt(&outv, info.text_keys[i], info.text_strings[i]);
      }
    }
    /*iTXt*/
    for(i = 0; i != info.itext_num; ++i)
    {
      if(strlen(info.itext_keys[i]) > 79)
      {
        state->error = 66; /*text chunk too large*/
        break;
      }
      if(strlen(info.itext_keys[i]) < 1)
      {
        state->error = 67; /*text chunk too small*/
        break;
      }
      addChunk_iTXt(&outv, state->encoder.text_compression,
                    info.itext_keys[i], info.itext_langtags[i], info.itext_transkeys[i], info.itext_strings[i],
                    &state->encoder.zlibsettings);
    }

    /*unknown chunks between IDAT and IEND*/
    if(info.unknown_chunks_data[2])
    {
      state->error = addUnknownChunks(&outv, info.unknown_chunks_data[2], info.unknown_chunks_size[2]);
      if(state->error) break;
    }
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
    addChunk_IEND(&outv);

    break; /*this isn't really a while loop; no error happened so break out now!*/
  }

  lodepng_info_cleanup(&info);
  free(data);
  /*instead of cleaning the vector up, give it to the output*/
  *out = outv.data;
  *outsize = outv.size;

  return state->error;
}

void lodepng_encoder_settings_init(LodePNGEncoderSettings* settings)
{
  lodepng_compress_settings_init(&settings->zlibsettings);
  settings->filter_strategy = LFS_ENTROPY;
  settings->auto_convert = 1;
  settings->clean_alpha = 1;
  settings->force_palette = 0;
  settings->predefined_filters = 0;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
  settings->text_compression = 1;
#endif /*LODEPNG_COMPILE_ANCILLARY_CHUNKS*/
}

#endif /*LODEPNG_COMPILE_ENCODER*/
#endif /*LODEPNG_COMPILE_PNG*/

/*
This returns the description of a numerical error code in English. This is also
the documentation of all the error codes.
*/
const char* lodepng_error_text(unsigned code)
{
  switch(code)
  {
    case 0: return "no error, everything went ok";
    case 1: return "nothing done yet"; /*the Encoder/Decoder has done nothing yet, error checking makes no sense yet*/
    case 10: return "end of input memory reached without huffman end code"; /*while huffman decoding*/
    case 11: return "error in code tree made it jump outside of huffman tree"; /*while huffman decoding*/
    case 13: return "problem while processing dynamic deflate block";
    case 14: return "problem while processing dynamic deflate block";
    case 15: return "problem while processing dynamic deflate block";
    case 16: return "unexisting code while processing dynamic deflate block";
    case 17: return "end of out buffer memory reached while inflating";
    case 18: return "invalid distance code while inflating";
    case 19: return "end of out buffer memory reached while inflating";
    case 20: return "invalid deflate block BTYPE encountered while decoding";
    case 21: return "NLEN is not ones complement of LEN in a deflate block";
     /*end of out buffer memory reached while inflating:
     This can happen if the inflated deflate data is longer than the amount of bytes required to fill up
     all the pixels of the image, given the color depth and image dimensions. Something that doesn't
     happen in a normal, well encoded, PNG image.*/
    case 22: return "end of out buffer memory reached while inflating";
    case 23: return "end of in buffer memory reached while inflating";
    case 24: return "invalid FCHECK in zlib header";
    case 25: return "invalid compression method in zlib header";
    case 26: return "FDICT encountered in zlib header while it's not used for PNG";
    case 27: return "PNG file is smaller than a PNG header";
    /*Checks the magic file header, the first 8 bytes of the PNG file*/
    case 28: return "incorrect PNG signature, it's no PNG or corrupted";
    case 29: return "first chunk is not the header chunk";
    case 30: return "chunk length too large, chunk broken off at end of file";
    case 31: return "illegal PNG color type or bpp";
    case 32: return "illegal PNG compression method";
    case 33: return "illegal PNG filter method";
    case 34: return "illegal PNG interlace method";
    case 35: return "chunk length of a chunk is too large or the chunk too small";
    case 36: return "illegal PNG filter type encountered";
    case 37: return "illegal bit depth for this color type given";
    case 38: return "the palette is too big"; /*more than 256 colors*/
    case 39: return "more palette alpha values given in tRNS chunk than there are colors in the palette";
    case 40: return "tRNS chunk has wrong size for greyscale image";
    case 41: return "tRNS chunk has wrong size for RGB image";
    case 42: return "tRNS chunk appeared while it was not allowed for this color type";
    case 43: return "bKGD chunk has wrong size for palette image";
    case 44: return "bKGD chunk has wrong size for greyscale image";
    case 45: return "bKGD chunk has wrong size for RGB image";
    /*the input data is empty, maybe a PNG file doesn't exist or is in the wrong path*/
    case 48: return "empty input or file doesn't exist";
    case 49: return "jumped past memory while generating dynamic huffman tree";
    case 50: return "jumped past memory while generating dynamic huffman tree";
    //case 51: return "jumped past memory while inflating huffman block";
    case 52: return "jumped past memory while inflating";
    case 53: return "size of zlib data too small";
    case 54: return "repeat symbol in tree while there was no value symbol yet";
    /*jumped past tree while generating huffman tree, this could be when the
    tree will have more leaves than symbols after generating it out of the
    given lenghts. They call this an oversubscribed dynamic bit lengths tree in zlib.*/
    case 55: return "jumped past tree while generating huffman tree";
    case 56: return "given output image colortype or bitdepth not supported for color conversion";
    case 57: return "invalid CRC encountered (checking CRC can be disabled)";
    case 58: return "invalid ADLER32 encountered (checking ADLER32 can be disabled)";
    //case 59: return "requested color conversion not supported";
    //case 60: return "invalid window size given in the settings of the encoder (must be 0-32768)";
    case 61: return "invalid BTYPE given in the settings of the encoder (only 0, 1 and 2 are allowed)";
    /*LodePNG leaves the choice of RGB to greyscale conversion formula to the user.*/
    //case 62: return "conversion from color to greyscale not supported";
    case 63: return "length of a chunk too long, max allowed for PNG is 2147483647 bytes per chunk"; /*(2^31-1)*/
    /*this would result in the inability of a deflated block to ever contain an end code. It must be at least 1.*/
    case 64: return "the length of the END symbol 256 in the Huffman tree is 0";
    case 66: return "the length of a text chunk keyword given to the encoder is longer than the maximum of 79 bytes";
    case 67: return "the length of a text chunk keyword given to the encoder is smaller than the minimum of 1 byte";
    case 68: return "tried to encode a PLTE chunk with a palette that has less than 1 or more than 256 colors";
    case 69: return "unknown chunk type with 'critical' flag encountered by the decoder";
    case 71: return "unexisting interlace mode given to encoder (must be 0 or 1)";
    case 72: return "while decoding, unexisting compression method encountering in zTXt or iTXt chunk (it must be 0)";
    //case 73: return "invalid tIME chunk size";
    //case 74: return "invalid pHYs chunk size";
    /*length could be wrong, or data chopped off*/
    case 75: return "no null termination char found while decoding text chunk";
    case 76: return "iTXt chunk too short to contain required bytes";
    case 77: return "integer overflow in buffer size";
    case 78: return "failed to open file for reading"; /*file doesn't exist or couldn't be opened for reading*/
    case 79: return "failed to open file for writing";
    //case 80: return "tried creating a tree of 0 symbols";
    //case 81: return "lazy matching at pos 0 is impossible";
    case 82: return "color conversion to palette requested while a color isn't in palette";
    case 83: return "memory allocation failed";
    case 84: return "given image too small to contain all pixels to be encoded";
    //case 86: return "impossible offset in lz77 encoding (internal bug)";
    case 87: return "must provide custom zlib function pointer if LODEPNG_COMPILE_ZLIB is not defined";
    case 88: return "invalid filter strategy given for LodePNGEncoderSettings.filter_strategy";
    case 89: return "text chunk keyword too short or long: must have size 1-79";
    /*the windowsize in the LodePNGCompressSettings. Requiring POT(==> & instead of %) makes encoding 12% faster.*/
    //case 90: return "windowsize must be a power of two";
    case 91: return "invalid decompressed idat size";
    case 92: return "too many pixels, not supported";
    case 93: return "zero width or height is invalid";
    case 94: return "header chunk must have a size of 13 bytes";
    case 95: return "decoding error";
  }
  return "unknown error code";
}

/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */
/* // C++ Wrapper                                                          // */
/* ////////////////////////////////////////////////////////////////////////// */
/* ////////////////////////////////////////////////////////////////////////// */

#ifdef LODEPNG_COMPILE_CPP
namespace lodepng
{

#ifdef LODEPNG_COMPILE_DISK
void load_file(std::vector<unsigned char>& buffer, const std::string& filename)
{
  std::ifstream file(filename.c_str(), std::ios::in|std::ios::binary|std::ios::ate);

  /*get filesize*/
  std::streamsize size = 0;
  if(file.seekg(0, std::ios::end).good()) size = file.tellg();
  if(file.seekg(0, std::ios::beg).good()) size -= file.tellg();

  /*read contents of the file into the vector*/
  buffer.resize(size_t(size));
  if(size > 0) file.read((char*)(&buffer[0]), size);
}

/*write given buffer to the file, overwriting the file, it doesn't append to it.*/
void save_file(const std::vector<unsigned char>& buffer, const std::string& filename)
{
  std::ofstream file(filename.c_str(), std::ios::out|std::ios::binary);
  file.write(buffer.empty() ? 0 : (char*)&buffer[0], std::streamsize(buffer.size()));
}
#endif //LODEPNG_COMPILE_DISK

#ifdef LODEPNG_COMPILE_PNG

State::State()
{
  lodepng_state_init(this);
}

State::~State()
{
  lodepng_state_cleanup(this);
}

#ifdef LODEPNG_COMPILE_DECODER
static unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h, const unsigned char* in,
                size_t insize, LodePNGColorType colortype, unsigned bitdepth)
{
  unsigned char* buffer;
  unsigned error = lodepng_decode_memory(&buffer, &w, &h, in, insize, colortype, bitdepth);
  if(buffer && !error)
  {
    State state;
    state.info_raw.colortype = colortype;
    state.info_raw.bitdepth = bitdepth;
    size_t buffersize = lodepng_get_raw_size(w, h, &state.info_raw);
    out.insert(out.end(), &buffer[0], &buffer[buffersize]);
    free(buffer);
  }
  return error;
}

unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                const std::vector<unsigned char>& in, LodePNGColorType colortype, unsigned bitdepth)
{
  return decode(out, w, h, in.empty() ? 0 : &in[0], (unsigned)in.size(), colortype, bitdepth);
}

static unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                State& state,
                const unsigned char* in, size_t insize)
{
  unsigned char* buffer = 0;
  unsigned error = lodepng_decode(&buffer, &w, &h, &state, in, insize);
  if(buffer && !error)
  {
    size_t buffersize = lodepng_get_raw_size(w, h, &state.info_raw);
    out.insert(out.end(), &buffer[0], &buffer[buffersize]);
  }
  free(buffer);
  return error;
}

unsigned decode(std::vector<unsigned char>& out, unsigned& w, unsigned& h,
                State& state,
                const std::vector<unsigned char>& in)
{
  return decode(out, w, h, state, in.empty() ? 0 : &in[0], in.size());
}

#endif //LODEPNG_COMPILE_DISK

#ifdef LODEPNG_COMPILE_ENCODER
unsigned encode(std::vector<unsigned char>& out,
                std::vector<unsigned char>& in, unsigned w, unsigned h,
                State& state, LodePNGPaletteSettings p)
{
  state.note = 0;
  if(lodepng_get_raw_size(w, h, &state.info_raw) > in.size()) return 84;
  unsigned char* buffer;
  size_t buffersize;

  unsigned error = lodepng_encode(&buffer, &buffersize, in.empty() ? 0 : &in[0], w, h, &state, p);
  if (error == 96) {
    error = 0;
    state.note = 1;
  }
  if(buffer)
  {
    out.insert(out.end(), &buffer[0], &buffer[buffersize]);
    free(buffer);
  }
  return error;
}
#endif //LODEPNG_COMPILE_ENCODER
#endif //LODEPNG_COMPILE_PNG
} //namespace lodepng
#endif /*LODEPNG_COMPILE_CPP*/
