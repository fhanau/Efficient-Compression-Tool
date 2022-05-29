//
//  support.cpp
//  Efficient Compression Tool
//
//  Created by Felix Hanau on 09.04.15.
//  Copyright (c) 2015 Felix Hanau.
//

#include "support.h"
#include <sys/stat.h>
#ifdef _MSC_VER
#include <sys/utime.h>
#include <io.h>
#define access _access
#define W_OK 2
#else
#include <utime.h>
#endif
#include <time.h>
#include <stdio.h>

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

bool isDirectory(const char *path) {
  struct stat sb;
  if (!stat(path, &sb))
    return (sb.st_mode & S_IFDIR) != 0;
  return false;
}

time_t get_file_time(const char* Infile){
  struct stat stats;
  if(stat(Infile, &stats)){
    printf("%s: Could not get time\n", Infile);
    return -1;
  }
  return stats.st_mtime;
}

void set_file_time(const char* Infile, time_t otime){
  struct utimbuf oldtime;
  oldtime.actime = time(0);
  oldtime.modtime = otime;
  if (utime(Infile, &oldtime)){
    printf("%s: Could not set time\n", Infile);
  }
}
