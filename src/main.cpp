//  main.cpp
//  Efficient Compression Tool
//  Created by Felix Hanau on 12/19/14.
//  Copyright (c) 2014-2026 Felix Hanau.

#include "main.h"
#include "support.h"
#include "gztools.h"
#include "miniz/miniz.h"
#include <limits.h>
#include <atomic>

#ifndef NOMULTI
#include <thread>
#endif

#ifdef MP3_SUPPORTED
#include <id3/tag.h>
#endif

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

static void Usage() {
    printf (
            "Efficient Compression Tool\n"
            "(c) 2014-2026 Felix Hanau.\n"
            "Version 0.9.5"
#ifdef __DATE__
            " compiled on %s\n"
#endif
            "Folder support "
#ifdef FS_SUPPORTED
            "enabled\n"
#else
            "disabled\n"
#endif

            "Losslessly optimizes GZIP, ZIP, JPEG and PNG files\n"
            "Usage: ect [Options] files"
#ifdef FS_SUPPORTED
            "/folders"
#endif
            "...\n"
            "Options:\n"
            " -1 to -9          Set compression level (Default: 3)\n"
            " -strip            Strip metadata\n"
            " -progressive      Use progressive encoding for JPEGs\n"
            " -autorotate       Automatically rotate JPEGs, when perfectly transformable\n"
            " -autorotate=force Automatically rotate JPEGs, dropping non-transformable edge blocks\n"
#ifdef FS_SUPPORTED
            " -recurse          Recursively search directories\n"
#endif
            " -zip              Compress file(s) with ZIP algorithm\n"
            " -gzip             Compress file(s) with GZIP algorithm\n"
            " -quiet            Print only error messages\n"
            " -help             Print this help\n"
            " -keep             Keep modification time\n"
            "Advanced Options:\n"
            " --disable-png     Disable PNG optimization\n"
            " --disable-jpg     Disable JPEG optimization\n"
            " --strict          Enable strict losslessness\n"
            " --reuse           Keep PNG filter and colortype\n"
            " --allfilters      Try all PNG filter modes\n"
            " --allfilters-b    Try all PNG filter modes, including brute force strategies\n"
            " --pal_sort=i      Try i different PNG palette filtering strategies (up to 120)\n"
#ifndef NOMULTI
            " --mt-deflate      Use per block multithreading in Deflate\n"
            " --mt-deflate=i    Use per block multithreading in Deflate with i threads\n"
            " --mt-file         Use per file multithreading\n"
            " --mt-file=i       Use per file multithreading with i threads\n"
#endif
            //" --arithmetic   Use arithmetic encoding for JPEGs, incompatible with most software\n"
#ifdef __DATE__
            ,__DATE__
#endif
            );
}

int main(int argc, const char * argv[]) {
    std::atomic<unsigned> error(0);
    ECTOptions Options;
    Options.strip = false;
    Options.Progressive = false;
    Options.Autorotate = 0;
    Options.Mode = 3;
#ifdef FS_SUPPORTED
    Options.Recurse = false;
#endif
    Options.PNG_ACTIVE = true;
    Options.JPEG_ACTIVE = true;
    Options.Arithmetic = false;
    Options.Gzip = false;
    Options.Zip = 0;
    Options.SavingsCounter = true;
    Options.Strict = false;
    Options.DeflateMultithreading = 0;
    Options.FileMultithreading = 0;
    Options.Reuse = 0;
    Options.Allfilters = 0;
    Options.Allfiltersbrute = 0;
    Options.Allfilterscheap = 0;
    Options.palette_sort = 0;
    Options.keep = false;
    std::vector<int> args;
    int files = 0;
    if (argc >= 2){
        for (int i = 1; i < argc; i++) {
            int strlen = strnlen(argv[i], 64);  //File names may be longer and are unaffected by this check
            if (strncmp(argv[i], "-", 1) != 0){
                args.push_back(i);
                files++;
            }
            else if (strncmp(argv[i], "-strip", strlen) == 0){Options.strip = true;}
            else if (strncmp(argv[i], "-progressive", strlen) == 0) {Options.Progressive = true;}
            else if (strncmp(argv[i], "-autorotate", strlen) == 0) {Options.Autorotate = 2;} //Transform only if 'perfect'
            else if (strncmp(argv[i], "-autorotate=force", strlen) == 0) {Options.Autorotate = 1;} //Always transform
            else if (argv[i][0] == '-' && isdigit(argv[i][1])) {
                int l = atoi(argv[i] + 1);
                if (!l) {
                    l = 1;
                }
                Options.Mode = l;
            }
            else if (strncmp(argv[i], "-gzip", strlen) == 0) {Options.Gzip = true;}
            else if (strncmp(argv[i], "-zip", strlen) == 0) {Options.Zip = true; Options.Gzip = true;}
            else if (strncmp(argv[i], "-help", strlen) == 0) {Usage(); return 0;}
            else if (strncmp(argv[i], "-quiet", strlen) == 0) {Options.SavingsCounter = false;}
            else if (strncmp(argv[i], "-keep", strlen) == 0) {Options.keep = true;}
            else if (strcmp(argv[i], "--disable-jpeg") == 0 || strcmp(argv[i], "--disable-jpg") == 0 ){Options.JPEG_ACTIVE = false;}
            else if (strcmp(argv[i], "--disable-png") == 0){Options.PNG_ACTIVE = false;}
#ifdef FS_SUPPORTED
            else if (strncmp(argv[i], "-recurse", strlen) == 0)  {Options.Recurse = 1;}
#endif
            else if (strcmp(argv[i], "--strict") == 0) {Options.Strict = true;}
            else if (strcmp(argv[i], "--reuse") == 0) {Options.Reuse = true;}
            else if (strcmp(argv[i], "--allfilters") == 0) {Options.Allfilters = true;}
            else if (strcmp(argv[i], "--allfilters-b") == 0) {Options.Allfiltersbrute = Options.Allfilters = true;}
            else if (strcmp(argv[i], "--allfilters-c") == 0) {Options.Allfilterscheap = true;}
            else if (strncmp(argv[i], "--pal_sort=", 11) == 0){
                Options.palette_sort = atoi(argv[i] + 11) << 8;
                if(Options.palette_sort > 120 << 8){
                    Options.palette_sort = 120 << 8;
                }
            }


#ifndef NOMULTI
            else if (strncmp(argv[i], "--mt-deflate", 12) == 0) {
                if (strncmp(argv[i], "--mt-deflate=", 13) == 0){
                    Options.DeflateMultithreading = atoi(argv[i] + 13);
                }
                else if (strcmp(argv[i], "--mt-deflate") == 0) {
                    Options.DeflateMultithreading = std::thread::hardware_concurrency();
                }
            }
            else if (strncmp(argv[i], "--mt-file", 9) == 0) {
                if (strncmp(argv[i], "--mt-file=", 10) == 0){
                    Options.FileMultithreading = atoi(argv[i] + 10);
                }
                else if (strcmp(argv[i], "--mt-file") == 0) {
                    Options.FileMultithreading = std::thread::hardware_concurrency();
                }
            }
#endif
            else if (strcmp(argv[i], "--arithmetic") == 0) {Options.Arithmetic = true;}
            else {printf("Unknown flag: %s\n", argv[i]); return 0;}
        }
        if(Options.Autorotate > 0) {
            if (!Options.strip) {printf("Flag -autorotate requires -strip\n"); return 0;}
        }
        if(Options.Reuse){
            Options.Allfilters = 0;
        }
        if(Options.Zip && files){
            error |= zipHandler(args, argv, files, Options);
        }
        else {
            std::vector<std::string> fileList;
            for (int j = 0; j < files; j++){
#ifdef FS_SUPPORTED
                if (std::filesystem::is_regular_file(argv[args[j]])){
                    fileList.push_back(argv[args[j]]);
                }
                else if (std::filesystem::is_directory(argv[args[j]])){
                    if(Options.Recurse){std::filesystem::recursive_directory_iterator a(argv[args[j]]), b;
                        std::vector<std::filesystem::path> paths(a, b);
                        for(unsigned i = 0; i < paths.size(); i++){
                            fileList.push_back(paths[i].string());
                        }
                    }
                    else{
                        std::filesystem::directory_iterator a(argv[args[j]]), b;
                        std::vector<std::filesystem::path> paths(a, b);
                        for(unsigned i = 0; i < paths.size(); i++){
                            fileList.push_back(paths[i].string());
                        }
                    }
                }
                else{
                    error = 1;
                }
#else
                fileList.push_back(argv[args[j]]);
#endif
            }
#ifndef NOMULTI
            if (Options.FileMultithreading) {
                std::vector<std::thread> threads;
                std::atomic<size_t> pos(0);
                for (unsigned i = 0; i < Options.FileMultithreading; i++) {
                    threads.emplace_back(multithreadFileLoop, fileList, &pos, Options, &error);
                }
                for (auto &thread : threads) {
                    thread.join();
                }
            }
            else {
                for (const auto& file : fileList) {
                    error |= fileHandler(file.c_str(), Options, 0);
                }
            }
#else
            for (const auto& file : fileList) {
                error |= fileHandler(file.c_str(), Options, 0);
            }
#endif
        }

        if(!files){Usage();}

        if(Options.SavingsCounter){ECT_ReportSavings();}
    }
    else {Usage();}
    return error.load(std::memory_order_seq_cst);
}
