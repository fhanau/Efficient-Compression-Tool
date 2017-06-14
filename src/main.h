//
//  main.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 02.01.15.
//  Copyright (c) 2015 Felix Hanau.
//

#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>

#include "gztools.h"

//Compile support for folder input. Requires linking of Boost::filesystem and Boost::system.
#ifdef BOOST_SUPPORTED
#include <boost/filesystem.hpp>
#endif

struct ECTOptions{
  unsigned Mode;
  unsigned palette_sort;
  bool strip;
  bool Progressive;
  bool JPEG_ACTIVE;
  bool PNG_ACTIVE;
  bool SavingsCounter;
  bool Strict;
  bool Arithmetic;
  bool Gzip;
  bool Zip;
  bool Reuse;
  bool Allfilters;
  bool Allfiltersbrute;
#ifdef BOOST_SUPPORTED
  bool Recurse;
#endif
  unsigned DeflateMultithreading;
  bool keep;
};

int Optipng(unsigned filter, const char * Infile, bool force_no_palette, unsigned clean_alpha);
int Zopflipng(bool strip, const char * Infile, bool strict, unsigned Mode, int filter, unsigned multithreading);
int mozjpegtran (bool arithmetic, bool progressive, bool strip, const char * Infile, const char * Outfile, size_t* stripped_outsize);
int ZopfliGzip(const char* filename, const char* outname, unsigned mode, unsigned multithreading, unsigned ZIP);
void ZopfliBuffer(unsigned mode, unsigned multithreading, const unsigned char* in, size_t insize, unsigned char** out, size_t* outsize);
unsigned fileHandler(const char * Infile, const ECTOptions& Options, int internal);
unsigned zipHandler(std::vector<int> args, const char * argv[], int files, const ECTOptions& Options);
void ReZipFile(const char* file_path, const ECTOptions& Options, unsigned long* files);
