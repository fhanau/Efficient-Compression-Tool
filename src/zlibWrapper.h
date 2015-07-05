//
//  zlibWrapper.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 30.05.15.
//  Copyright (c) 2015 Felix Hanau. All rights reserved.
//

#ifndef Efficient_Compression_Tool_zlibWrapper_h
#define Efficient_Compression_Tool_zlibWrapper_h

int zlibcompress (unsigned char **dest, size_t *destLen, const unsigned char * source, size_t sourceLen, int level);

int zlibuncompress (unsigned char **dest, size_t *destLen, const unsigned char * source, size_t sourceLen);
#endif
