//
//  support.h
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 09.04.15.
//  Copyright (c) 2015 Felix Hanau.
//

#ifndef __Efficient_Compression_Tool__support__
#define __Efficient_Compression_Tool__support__

// Returns Filesize of Input
long long filesize (const char * Input);

bool exists(const char * Input);

bool writepermission (const char * Input);

#endif /* defined(__Efficient_Compression_Tool__support__) */
