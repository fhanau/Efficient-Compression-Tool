//
//  main.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 02.01.15.
//

#include <cstdio>
#include <cstdlib>
#include <string>
#include <cstring>
#include <vector>

#include "gztools.h"

//Compile support for folder input. Requires linking of Boost::filesystem and Boost::system.
//TODO: Native filesystem support is missing from e.g. Xcode, but this will be the preferable solution eventually
/*#if __cplusplus >= 201703L
#define FS_SUPPORTED
#include <filesystem>
//#include <experimental/filesystem>
using std::fs;
#else*/
#ifdef BOOST_SUPPORTED
#include <boost/filesystem.hpp>
#endif

struct ECTOptions{
  unsigned Mode;
  unsigned palette_sort;
  bool strip;
  bool Progressive;
  unsigned Autorotate;
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
  bool Allfilterscheap;
#ifdef BOOST_SUPPORTED
  bool Recurse;
#endif
  unsigned DeflateMultithreading;
  unsigned FileMultithreading;
  bool keep;
};

int Optipng(unsigned level, const char * Infile, bool force_no_palette, unsigned clean_alpha);
int Zopflipng(bool strip, const char * Infile, bool strict, unsigned Mode, int filter, unsigned multithreading, unsigned quiet);
int mozjpegtran (bool arithmetic, bool progressive, bool strip, unsigned autorotate, const char * Infile, const char * Outfile, size_t* stripped_outsize);
int ZopfliGzip(const char* filename, const char* outname, unsigned mode, unsigned multithreading, unsigned ZIP, unsigned char isGZ);
void ZopfliBuffer(unsigned mode, unsigned multithreading, const unsigned char* in, size_t insize, unsigned char** out, size_t* outsize);
unsigned fileHandler(const char * Infile, const ECTOptions& Options, int internal);
unsigned zipHandler(std::vector<int> args, const char * argv[], int files, const ECTOptions& Options);
void ReZipFile(const char* file_path, const ECTOptions& Options, size_t* files);
