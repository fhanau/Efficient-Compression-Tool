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

long long filesize (const char * Infile) {
    struct stat stats;
    if (stat(Infile, &stats) != 0){
        return -1;
    }
    return stats.st_size;
}

bool exists(const char * Infile) {
    struct stat stats;
    return stat(Infile, &stats) == 0;
}

bool writepermission (const char * Infile) {
    return !access (Infile, W_OK);
}
