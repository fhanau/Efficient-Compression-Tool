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

int ungz(const char * Input, const char * Output);
int IsGzip(const char * Input);
int IsZIP(const char * Infile);
#endif /* defined(__Efficient_Compression_Tool__ungz__) */
