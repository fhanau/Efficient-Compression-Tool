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
#include <unistd.h>

#include "gztools.h"

//Compile support for folder input. Requires linking of Boost::filesystem and Boost::system.
#ifdef BOOST_SUPPORTED
#include <vector>
#include <boost/filesystem.hpp>
#endif

struct ECTOptions{
    int  Mode;
    int  Filter;
    bool Metadata;
    bool Progressive;
    bool JPEG_ACTIVE;
    bool PNG_ACTIVE;
    bool SavingsCounter;
    bool Strict;
    bool Arithmetic;
    bool Gzip;
#ifdef BOOST_SUPPORTED
    bool Recurse;
#endif
};

int Optipng(int filter, const char * Infile, bool force_palette_if_possible, bool force_no_palette /*, int second_filter, int filterdiff/*/);
int Zopflipng(bool blocksplitboth, const char * Infile, bool strict, int Mode, int best_filter);
int mozjpegtran (bool arithmetic, bool progressive, bool copyoption, const char * Infile, const char * Outfile);
int ZopfliGzip(const char* filename, const char* outname, int mode);
