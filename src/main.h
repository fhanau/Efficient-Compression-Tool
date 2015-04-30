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
#include <unistd.h>

//Compile support for folder input. Requires linking of Boost::filesystem and Boost::system
#ifdef BOOST_SUPPORTED
#include <vector>
#include <boost/filesystem.hpp>
#endif

struct ECTOptions{
    int Mode;
    int Filter;
    bool Metadata;
    bool Progressive;
    bool JPEG_ACTIVE;
    bool PNG_ACTIVE;
    bool SavingsCounter;
    bool Strict;
    bool Arithmetic;
#ifdef BOOST_SUPPORTED
    bool Recurse;
#endif
};

//Compile PNG recompression support
#define PNG_SUPPORTED

#ifdef PNG_SUPPORTED
int Optipng(int filter, const char * Input, bool force_palette_if_possible, bool force_no_palette /*, int second_filter, int filterdiff/*/);
int Zopflipng(int iterationnum, bool blocksplitboth, const char * Input, bool strict, int Mode, int best_filter);
#endif

//Compile JPEG recompression support
#define JPEG_SUPPORTED

#ifdef JPEG_SUPPORTED
int mozjpegtran (bool arithmetic, bool progressive, bool copyoption, const char * Input, const char * Output);
#endif
