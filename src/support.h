//
//  support.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 09.04.15.
//  Copyright (c) 2015 Felix Hanau.
//

#ifndef __Efficient_Compression_Tool__support__
#define __Efficient_Compression_Tool__support__

// Returns Filesize of Infile
long long filesize (const char * Infile);

bool exists(const char * Infile);

bool writepermission (const char * Infile);

#endif /* defined(__Efficient_Compression_Tool__support__) */
