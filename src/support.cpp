//
//  support.cpp
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 09.04.15.
//  Copyright (c) 2015 Felix Hanau.
//

#include "support.h"
#include <sys/stat.h>
#include <unistd.h>

long long filesize (const char * Input) {
    struct stat stats;
    //Todo: 
    if (stat(Input, &stats) != 0){}
    return stats.st_size;
}

bool exists(const char * Input) {
    struct stat stats;
    return stat(Input, &stats) == 0;
}

bool writepermission (const char * Input) {
    return !access (Input, W_OK);
}
