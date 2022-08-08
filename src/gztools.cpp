//
//  gztools.cpp
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 08.05.15.
//  Copyright (c) 2015 Felix Hanau. All rights reserved.
//

#include "gztools.h"
#include "zlib/zlib.h"
#include "leanify/zip.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

int IsGzip(const char * Infile, char** gzip_name){
    struct stat stats;
    if (stat(Infile, &stats) != 0){
      return 2;
    }
    size_t file_size = stats.st_size;
    FILE * stream = fopen (Infile, "rb");
    if (!stream){
      return 2;
    }

    //header is at least 10 bytes long
    unsigned char buf [10];
    if (fread(&buf, 1, 10, stream) != 10){
      fclose(stream);
      return 2;
    }
    if (buf[0] == 31 && buf[1] == 139 && buf[2] == 8){
      // FHCRC is stripped
      if(buf[3] & 0x20){ //Encrypted
        fprintf(stderr, "%s: File is encrypted, can't be optimized\n", Infile);
        fclose(stream);
        return 2;
      }
      unsigned char fextra = buf[3] & 0x4;
      unsigned char fname = buf[3] & 0x8;
      unsigned char fcomment = buf[3] & 0x10;
      if (fname) {
        //copy file name
        unsigned short extra_size = 0;
        if (fextra) {
          if (fread(&extra_size, 1, sizeof(extra_size), stream) != sizeof(extra_size)) {
            fclose(stream);
            return 2;
          }
          if (12 + extra_size > file_size || fseek(stream, extra_size, SEEK_CUR) != 0) {
            fclose(stream);
            return 2;
          }
        }
        long file_pos = extra_size ? 10 : 12 + extra_size;
        unsigned max_fname_length = 2048;
        if (file_size - file_pos < max_fname_length) {
          max_fname_length = file_size - file_pos;
        }
        (*gzip_name) = (char*)malloc(max_fname_length);
        if (fread((*gzip_name), 1, max_fname_length, stream) != max_fname_length) {
          fclose(stream);
          return 2;
        }
        size_t name_len = strnlen((*gzip_name), max_fname_length);
        //if name_len < max_fname_length, string is null-terminated already, no action needed.
        if (name_len >= max_fname_length) {
          fprintf(stderr, "%s: file name too long\n", Infile);
          fclose(stream);
          return 2;
        }
      }
      fclose(stream);
      if(fextra || fcomment){ //extra field or comment
        return 3;
      }
      return 1;
    }
    fclose(stream);
    return 0;
}

int IsZIP(const char * Infile){
  FILE * stream = fopen (Infile, "rb");
  if (!stream){
    return -1;
  }
  unsigned char buf [4];
  if (fread(&buf, 1, 4, stream) != 4){
    return -1;
  }
  fclose(stream);

  return memcmp(buf, Zip::header_magic, 4) == 0;
}
