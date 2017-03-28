//
//  support.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 09.04.15.
//  Copyright (c) 2015 Felix Hanau.
//

#ifndef __Efficient_Compression_Tool__support__
#define __Efficient_Compression_Tool__support__

#include <unistd.h>

// Returns Filesize of Infile
size_t filesize (const char * Infile);

bool exists(const char * Infile);

bool writepermission (const char * Infile);

bool isDirectory(const char *path);

#endif /* defined(__Efficient_Compression_Tool__support__) */
