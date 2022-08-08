//
//  ungz.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 08.05.15.
//  Copyright (c) 2015 Felix Hanau. All rights reserved.
//

#ifndef __Efficient_Compression_Tool__ungz__
#define __Efficient_Compression_Tool__ungz__

#include <stdio.h>

int ungz(const char * Infile, const char * Outfile);
int IsGzip(const char * Infile, char** gzip_name);
int IsZIP(const char * Infile);
#endif /* defined(__Efficient_Compression_Tool__ungz__) */
