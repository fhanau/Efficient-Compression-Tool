/*
Copyright 2013 Google Inc. All Rights Reserved.
Copyright 2015 Mr_KrzYch00. All Rights Reserved.

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

#include <cstdio>
#include <sys/stat.h>

#include "zopfli.h"
#include "../zlib/zlib.h"
#include "deflate.h"
#include "zopfli.h"
#include "zlib_container.h"
#include "../main.h"
#include <time.h>

#define ZOPFLI_APPEND_DATA(/* T */ value, /* T** */ data, /* size_t* */ size) {\
(*data)[(*size)] = (value);\
(*size)++;\
}

static void ZopfliZipCompress(const ZopfliOptions* options,
                              const unsigned char* in, size_t insize, time_t time, std::string name,
                              unsigned char** out, size_t* outsize) {
  static const unsigned char filePKh[10]     = { 80, 75,  3,  4, 20,  0,  2,  0,  8,  0};
  static const unsigned char CDIRPKh[12]     = { 80, 75,  1,  2, 20,  0, 20,  0,  2,  0,  8,  0};
  static const unsigned char CDIRPKs[12]     = {  0,  0,  0,  0,  0,  0,  0,  0, 32,  0,  0,  0};
  static const unsigned char EndCDIRPKh[12]  = { 80, 75,  5,  6,  0,  0,  0,  0,  1,  0,  1,  0};

  unsigned long crcvalue = crc32(0, in, insize);
  unsigned long i;
  std::string x = name.substr(name.find_last_of('/') + 1);
  const char* infilename = x.c_str();
  unsigned char bp = 0;
  size_t max = x.size();
  *out = (unsigned char*)realloc(*out, 200);

  struct tm* times = localtime(&time);
  unsigned long dostime = times->tm_year < 80 ? 0x00210000 : times->tm_year > 207 ? 0xFF9FBF7D : (
                            (times->tm_year - 80) << 25 | (times->tm_mon + 1) << 21 | times->tm_mday << 16
                            | times->tm_hour << 11 | times->tm_min << 5 | times->tm_sec >> 1
                          );

  /* File PK STATIC DATA + CM */

  for(i=0;i<sizeof(filePKh);++i) ZOPFLI_APPEND_DATA(filePKh[i],out,outsize);

  /* MS-DOS TIME */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((dostime >> (i*8)) % 256, out, outsize);

  /* CRC */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256, out, outsize);

  /* OSIZE NOT KNOWN YET - WILL UPDATE AFTER COMPRESSION */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

  /* ISIZE */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((insize >> (i*8)) % 256, out, outsize);

  /* FNLENGTH */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA((max >> (i*8)) % 256, out, outsize);

  /* NO EXTRA FLAGS */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA(0, out, outsize);

  /* FILENAME */
  for(i=0;i<max;++i) ZOPFLI_APPEND_DATA(infilename[i], out, outsize);
  unsigned long rawdeflsize = *outsize;

  ZopfliDeflate(options, 1, in, insize, &bp, out, outsize);
  *out = (unsigned char*)realloc(*out, 200 + *outsize);

  rawdeflsize = *outsize - rawdeflsize;

  /* C-DIR PK HEADER STATIC DATA */
  unsigned long cdirsize = *outsize;
  for(i=0;i<sizeof(CDIRPKh);++i) ZOPFLI_APPEND_DATA(CDIRPKh[i],out,outsize);

  /* MS-DOS TIME, CRC, OSIZE, ISIZE FROM */

  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((dostime >> (i*8)) % 256, out, outsize);

  /* CRC */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((crcvalue >> (i*8)) % 256,out,outsize);

  /* OSIZE + UPDATE IN PK HEADER */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((rawdeflsize >> (i*8)) % 256,out,outsize);
  for(i=0;i<4;++i) (*out)[18+i]=(rawdeflsize >> (i*8)) % 256;

  /* ISIZE */
  for(i=0;i<25;i+=8) ZOPFLI_APPEND_DATA((insize >> i) % 256,out,outsize);

  /* FILENAME LENGTH */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA((max >> (i*8)) % 256,out,outsize);

  /* C-DIR STATIC DATA */
  for(i=0;i<sizeof(CDIRPKs);++i) ZOPFLI_APPEND_DATA(CDIRPKs[i],out,outsize);

  /* FilePK offset in ZIP file */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA(0,out,outsize);
  unsigned long cdiroffset= rawdeflsize + 30 + max;

  /* FILENAME */
  for(i=0; i<max;++i) ZOPFLI_APPEND_DATA(infilename[i],out,outsize);
  cdirsize = *outsize - cdirsize;

  /* END C-DIR PK STATIC DATA + TOTAL FILES (ALWAYS 1) */
  for(i=0;i<sizeof(EndCDIRPKh);++i) ZOPFLI_APPEND_DATA(EndCDIRPKh[i],out,outsize);

  /* C-DIR SIZE */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((cdirsize >> (i*8)) % 256,out, outsize);

  /* C-DIR OFFSET */
  for(i=0;i<4;++i) ZOPFLI_APPEND_DATA((cdiroffset >> (i*8)) % 256,out, outsize);

  /* NO COMMENTS IN END C-DIR */
  for(i=0;i<2;++i) ZOPFLI_APPEND_DATA(0, out, outsize);
}

/*
Compresses the data according to the gzip specification.
*/
static void ZopfliGzipCompress(const ZopfliOptions* options,
                        const unsigned char* in, size_t insize, time_t time,
                        unsigned char** out, size_t* outsize) {
  unsigned crcvalue = crc32(0, in, insize);
  unsigned char bp = 0;

  (*out) = (unsigned char*)malloc(20);
  (*out)[*outsize] = 31; (*outsize)++;  /* ID1 */
  (*out)[*outsize] = 139; (*outsize)++; /* ID2 */
  (*out)[*outsize] = 8; (*outsize)++;   /* CM  */
  (*out)[*outsize] = 0; (*outsize)++;   /* FLG */

  /* MTIME */
  *(unsigned*)(&(*out)[*outsize]) = time & UINT_MAX; (*outsize) += 4;

  (*out)[*outsize] = 2; (*outsize)++;  /* XFL, 2 indicates best compression. */
  (*out)[*outsize] = 3; (*outsize)++;  /* OS follows Unix conventions. */

  //Use zlib-based compression
  if (options->numiterations == -1) {
    z_stream stream;
    stream.zalloc = 0;
    stream.zfree = 0;
    stream.opaque = 0;

    int err = deflateInit2(&stream, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    if (err != Z_OK) exit(EXIT_FAILURE);

    stream.next_in = (z_const unsigned char *)in;
    stream.avail_in = insize;
    stream.avail_out = deflateBound(&stream, insize);
    (*out) = (unsigned char*)realloc(*out, (*outsize) + deflateBound(&stream, insize) + 8);
    stream.next_out = &((*out)[*outsize]);

    deflate(&stream, Z_FINISH);
    deflateEnd(&stream);
    (*outsize) += stream.total_out;
  }
  else {
    ZopfliDeflate(options, 1, in, insize, &bp, out, outsize);
    (*out) = (unsigned char*)realloc(*out, *outsize + 8);
  }

  /* CRC */
  *(unsigned*)(&(*out)[*outsize]) = crcvalue; (*outsize) += 4;

  /* ISIZE */
  *(unsigned*)(&(*out)[*outsize]) = insize % UINT_MAX; (*outsize) += 4;
}

static void ZopfliCompress(const ZopfliOptions* options, ZopfliFormat output_type,
                    const unsigned char* in, size_t insize, time_t time, std::string name,
                    unsigned char** out, size_t* outsize) {
  if (output_type == ZOPFLI_FORMAT_GZIP) {
    ZopfliGzipCompress(options, in, insize, time, out, outsize);
  }
  else if (output_type == ZOPFLI_FORMAT_ZIP) {
    ZopfliZipCompress(options, in, insize, time, name, out, outsize);
  }
  else if (output_type == ZOPFLI_FORMAT_ZLIB) {
    //ZopfliZlibCompress(options, in, insize, out, outsize);
  }
  else if (output_type == ZOPFLI_FORMAT_DEFLATE) {
    unsigned char bp = 0;
    ZopfliDeflate(options, 1, in, insize, &bp, out, outsize);
  }
}

static void LoadGzip(const char* filename, unsigned char** out, long long* outsize_p) {
  gzFile r = gzopen(filename, "rb");
  if (!r) {
    return;
  }
#define GZIP_READ_SIZE 65536
  size_t alloc_size = GZIP_READ_SIZE;
  *out = (unsigned char*)malloc(alloc_size + 8);
  size_t out_size = 0;
  do {
    int bytes = gzread(r, (*out) + out_size, GZIP_READ_SIZE);
    if (bytes) {
      out_size += bytes;
      if (alloc_size - out_size < GZIP_READ_SIZE) {
        alloc_size *= 2;
        (*out) = (unsigned char*)realloc(*out, alloc_size + 8);
      }
    }
    else if (bytes == 0) {
      break;
    }
    else if (bytes < 0) {
      printf("%s: gzip decompression error\n", filename);
      gzclose_r(r);
      *outsize_p = 0;
      return;
    }
  }
  while (!gzeof(r));
  gzclose_r(r);
  *outsize_p = (long long)out_size;
}

/*
 Loads a file into a memory array.
 */
static void LoadFile(const char* filename,
                     unsigned char** out, long long* outsize) {
  FILE* file = fopen(filename, "rb");
  if (!file) return;

  fseek(file , 0 , SEEK_END);
  *outsize = ftell(file);
  rewind(file);

  *out = (unsigned char*)malloc(*outsize + 8);
  if (!*out && *outsize){
    exit(1);
  }
  if (*outsize) {
    size_t testsize = fread(*out, 1, *outsize, file);
    if ((long long)testsize != *outsize) {
      /* It could be a directory */
      free(*out);
      *out = 0;
      *outsize = -1;
    }
  }

  fclose(file);
}

/*
 Saves a file from a memory array, overwriting the file if it existed.
 */
static void SaveFile(const char* filename,
                     const unsigned char* in, size_t insize) {
  FILE* file = fopen(filename, "wb");
  if (!file){
    printf ("Can't write to file");
  }
  else {
    fwrite((char*)in, 1, insize, file);
    fclose(file);
  }
}

/*
 outfilename: filename to write output to, or 0 to write to stdout instead
 */
static void CompressFile(const ZopfliOptions* options,
                         ZopfliFormat output_type,
                         const char* infilename,
                         const char* outfilename,
                         unsigned char isGZ) {
  unsigned char* in;
  long long insize = -1;
  unsigned char* out = 0;
  size_t outsize = 0;
  if (isGZ) {
    LoadGzip(infilename, &in, &insize);
  }
  else {
    LoadFile(infilename, &in, &insize);
  }
  if (insize < 0) {
    fprintf(stderr, "Invalid filename: %s\n", infilename);
    return;
  }

  struct stat st;
  stat(infilename, &st);
  time_t time = st.st_mtime;
  ZopfliCompress(options, output_type, in, insize, time, infilename, &out, &outsize);
  free(in);

  SaveFile(outfilename, out, outsize);

  free(out);
}

int ZopfliGzip(const char* filename, const char* outname, unsigned mode, unsigned multithreading, unsigned ZIP, unsigned char isGZ) {
  ZopfliOptions options;
  //ZopfliFormat output_type = ZOPFLI_FORMAT_GZIP;
  //output_type = ZOPFLI_FORMAT_ZLIB;
  //output_type = ZOPFLI_FORMAT_DEFLATE;

  ZopfliInitOptions(&options, mode, multithreading, 0);
  //Append ".gz" ".zlib" ".deflate"

  CompressFile(&options, ZIP ? ZOPFLI_FORMAT_ZIP : ZOPFLI_FORMAT_GZIP, filename, outname ? outname : ((std::string)filename).append(ZIP ? ".zip" : isGZ ? ".tmp" : ".gz").c_str(), isGZ);
  return 0;
}

void ZopfliBuffer(unsigned mode, unsigned multithreading, const unsigned char* in, size_t insize, unsigned char** out, size_t* outsize) {
  ZopfliOptions options;
  ZopfliInitOptions(&options, mode, multithreading, 0);
  unsigned char bp = 0;
  ZopfliDeflate(&options, 1, in, insize, &bp, out, outsize);
}
