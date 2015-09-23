//  main.cpp
//  Efficient Compression Tool
//  Created by Felix Hanau on 19.12.14.
//  Copyright (c) 2014-2015 Felix Hanau.

#include "main.h"
#include "support.h"

#ifndef NOMULTI
#include <thread>
#endif

#ifdef MP3_SUPPORTED
#include <id3/tag.h>
#endif

static unsigned long processedfiles;
static size_t bytes;
static long long savings;

static void Usage() {
    printf (
            "Efficient Compression Tool\n"
            "(C) 2014-2015 Felix Hanau.\n"
            "Version 0.1"
#ifdef __DATE__
            " compiled on %s\n"
#endif
            "Folder support "
#ifdef BOOST_SUPPORTED
            "enabled\n"
#else
            "disabled\n"
#endif

            "Losslessly optimizes JPEG and PNG images\n"
            "Usage: ECT [Options] File"
#ifdef BOOST_SUPPORTED
            "/Folder"
#endif
            "\n"
            "Options:\n"
            " -M1 to -M5     Set compression level (Default: 1)\n"
            " -strip         Strip metadata\n"
            " -progressive   Use progressive encoding for JPEGs\n"
#ifdef BOOST_SUPPORTED
            " -recurse       Recursively search directories\n"
#endif
            " -gzip          Compress file with GZIP algorithm\n"
            " -quiet         Print only error messages\n"
            " -help          Print this help\n"
            "Advanced Options:\n"
#ifdef BOOST_SUPPORTED
            " --disable-png  Disable PNG optimization\n"
            " --disable-jpg  Disable JPEG optimization\n"
#endif
            " --strict       Enable strict losslessness\n"
#ifndef NOMULTI
            " --mt-deflate   Use per block multithreading in Deflate\n"
            " --mt-deflate=i Use per block multithreading in Deflate, use i threads\n"
#endif
            //" --arithmetic   Use arithmetic encoding for JPEGs, incompatible with most software\n"
#ifdef __DATE__
            ,__DATE__
#endif
            );
}

static void ECT_ReportSavings(){
    if (processedfiles){
        printf("Processed %lu file%s\n", processedfiles, processedfiles > 1 ? "s":"");
        if (savings < 0){
            printf("Result is bigger\n");
            return;
        }

        int bk = 0;
        int k = 0;
        double smul = savings;
        double bmul = bytes;
        while (smul > 1024) {smul /= 1024; k++;}
        while (bmul > 1024) {bmul /= 1024; bk++;}
        char *counter;
        if (k == 1) {counter = (char *)"K";}
        else if (k == 2) {counter = (char *)"M";}
        else if (k == 3) {counter = (char *)"G";}
        else {counter = (char *)"";}
        char *counter2;
        if (bk == 1){counter2 = (char *)"K";}
        else if (bk == 2){counter2 = (char *)"M";}
        else if (bk == 3){counter2 = (char *)"G";}
        else {counter2 = (char *)"";}
        printf("Saved ");
        if (k == 0){printf("%0.0f", smul);}
        else{printf("%0.2f", smul);}
        printf("%sB out of ", counter);
        if (bk == 0){printf("%0.0f", bmul);}
        else{printf("%0.2f", bmul);}
        printf("%sB (%0.4f%%)\n", counter2, (100.0 * savings)/bytes);}
    else {printf("No compatible files found\n");}
}

static int ECTGzip(const char * Infile, const unsigned Mode, unsigned char multithreading, long long fs){
    if (!fs){
        printf("%s: Compression of empty files is currently not supported\n", Infile);
        return 2;
    }
    if (!IsGzip(Infile)){
        if (exists(((std::string)Infile).append(".gz").c_str())){
            return 2;
        }
        ZopfliGzip(Infile, 0, Mode, multithreading);
        return 1;
    }
    else {
        if (exists(((std::string)Infile).append(".ungz").c_str())){
            return 2;
        }
        if (exists(((std::string)Infile).append(".ungz.gz").c_str())){
            return 2;
        }
        ungz(Infile, ((std::string)Infile).append(".ungz").c_str());
        ZopfliGzip(((std::string)Infile).append(".ungz").c_str(), 0, Mode, multithreading);
        if (filesize(((std::string)Infile).append(".ungz.gz").c_str()) < filesize(Infile)){
            unlink(Infile);
            rename(((std::string)Infile).append(".ungz.gz").c_str(), Infile);
        }
        else {
            unlink(((std::string)Infile).append(".ungz.gz").c_str());
        }
        unlink(((std::string)Infile).append(".ungz").c_str());
        return 0;
    }
}

static void OptimizePNG(const char * Infile, const ECTOptions& Options){
    int x = 1;
    long long size = filesize(Infile);
    if(Options.Mode == 5){
        x = Zopflipng(Options.strip, Infile, Options.Strict, 2, 0, Options.DeflateMultithreading);
    }
    //Disabled as using this causes libpng warnings
    //int filter = Optipng(Options.Mode, Infile, true);
    int filter = Optipng(Options.Mode, Infile, false);

    if (filter == -1){
        return;
    }
    if (Options.Mode != 1){
        if (Options.Mode == 5){
            Zopflipng(Options.strip, Infile, Options.Strict, 5, filter, Options.DeflateMultithreading);}
        else {
            x = Zopflipng(Options.strip, Infile, Options.Strict, Options.Mode, filter, Options.DeflateMultithreading);}
    }
    else {
        if (filesize(Infile) <= size){
            unlink(((std::string)Infile).append(".bak").c_str());
        }
        else {
            unlink(Infile);
            rename(((std::string)Infile).append(".bak").c_str(), Infile);
        }
    }

    if(Options.strip && x){
        Optipng(0, Infile, false);
    }
}

static void OptimizeJPEG(const char * Infile, const ECTOptions& Options){
    mozjpegtran(Options.Arithmetic, Options.Progressive, Options.strip, Infile, Infile);
    if (Options.Progressive){
        long long fs = filesize(Infile);
        if((Options.Mode == 1 && fs < 6142) || (Options.Mode == 2 && fs < 8192) || (Options.Mode == 3 && fs < 15360) || (Options.Mode == 4 && fs < 30720) || (Options.Mode == 5 && fs < 51200)){
            mozjpegtran(Options.Arithmetic, false, Options.strip, Infile, Infile);
        }
    }
}

#ifdef MP3_SUPPORTED
static void OptimizeMP3(const char * Infile, const ECTOptions& Options){
    ID3_Tag orig (Infile);
    size_t start = orig.Size();
    ID3_Frame* picFrame = orig.Find(ID3FID_PICTURE);
    if (picFrame)
    {
        ID3_Field* mime = picFrame->GetField(ID3FN_MIMETYPE);
        if (mime){
            char mimetxt[20];
            mime->Get(mimetxt, 19);
            //printf("%s", mimetxt);
            ID3_Field* pic = picFrame->GetField(ID3FN_DATA);
            bool ispng = memcmp(mimetxt, "image/png", 9) == 0 || memcmp(mimetxt, "PNG", 3) == 0;
            if (pic && (memcmp(mimetxt, "image/jpeg", 10) == 0 || ispng)){
                pic->ToFile("out.jpg");
                if (ispng){
                    OptimizePNG("out.jpg", Options);
                }
                else{
                    OptimizeJPEG("out.jpg", Options);
                }
                pic->FromFile("out.jpg");
                unlink("out.jpg");
                orig.SetPadding(false);
                //orig.SetCompression(true);
                if (orig.Size() < start){
                    orig.Update();
                }
            }
        }
    }
}
#endif

static void PerFileWrapper(const char * Infile, const ECTOptions& Options){
    std::string Ext = Infile;
    std::string x = Ext.substr(Ext.find_last_of(".") + 1);

    if ((Options.PNG_ACTIVE && (x == "PNG" || x == "png")) || (Options.JPEG_ACTIVE && (x == "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg")) || Options.Gzip){
        long long size = filesize(Infile);
        if (size < 0){
            printf("%s: bad file\n", Infile);
            return;
        }
        bool statcompressedfile = 0;
        if (size < 1200000000) {//completely random value
            if (x == "PNG" || x == "png"){
                OptimizePNG(Infile, Options);
            }
            else if (x == "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg"){
                OptimizeJPEG(Infile, Options);
            }
            else if (Options.Gzip){
                //if (!size)
                statcompressedfile = ECTGzip(Infile, Options.Mode, Options.DeflateMultithreading, size);
            }
            if(Options.SavingsCounter){
                processedfiles++;
                bytes += size;
                if (!statcompressedfile){
                savings = savings + size - filesize(Infile);
                }
                else if (statcompressedfile){
                    savings += (size - filesize(((std::string)Infile).append(".gz").c_str()));
                }
            }
        }
        else{printf("File too big\n");}
    }
#ifdef MP3_SUPPORTED
    else if(x == "mp3"){
        OptimizeMP3(Infile, Options);
    }
#endif
}

int main(int argc, const char * argv[]) {
    ECTOptions Options;
    Options.strip = false;
    Options.Progressive = false;
    Options.Mode = 2;
#ifdef BOOST_SUPPORTED
    Options.Recurse = false;
#endif
    Options.PNG_ACTIVE = true;
    Options.JPEG_ACTIVE = true;
    Options.Arithmetic = false;
    Options.Gzip = false;
    Options.SavingsCounter = true;
    Options.Strict = false;
    Options.DeflateMultithreading = 0;
    if (argc >= 2){
        for (int i = 1; i < argc-1; i++) {
            if (strncmp(argv[i], "-strip", 2) == 0){Options.strip = true;}
            else if (strncmp(argv[i], "-progressive", 2) == 0) {Options.Progressive = true;}
            else if (strcmp(argv[i], "-M1") == 0) {Options.Mode = 1;}
            else if (strcmp(argv[i], "-M2") == 0) {Options.Mode = 2;}
            else if (strcmp(argv[i], "-M3") == 0) {Options.Mode = 3;}
            else if (strcmp(argv[i], "-M4") == 0) {Options.Mode = 4;}
            else if (strcmp(argv[i], "-M5") == 0) {Options.Mode = 5;}
            else if (strncmp(argv[i], "-gzip", 2) == 0) {Options.Gzip = true;}
            else if (strncmp(argv[i], "-help", 2) == 0) {Usage(); return 0;}
            else if (strncmp(argv[i], "-quiet", 2) == 0) {Options.SavingsCounter = false;}
#ifdef BOOST_SUPPORTED
            else if (strcmp(argv[i], "--disable-jpeg") == 0 || strcmp(argv[i], "--disable-jpg") == 0 ){Options.JPEG_ACTIVE = false;}
            else if (strcmp(argv[i], "--disable-png") == 0){Options.PNG_ACTIVE = false;}
            else if (strncmp(argv[i], "-recurse", 2) == 0)  {Options.Recurse = 1;}
#endif
            else if (strcmp(argv[i], "--strict") == 0) {Options.Strict = true;}
#ifndef NOMULTI
            else if (strncmp(argv[i], "--mt-deflate", 12) == 0) {
                if (strncmp(argv[i], "--mt-deflate=", 13) == 0){
                    Options.DeflateMultithreading = atoi(argv[i] + 13);
                }
                else{
                    Options.DeflateMultithreading = std::thread::hardware_concurrency();
                }
            }
#endif
            //else if (strcmp(argv[i], "--arithmetic") == 0) {Options.Arithmetic = true;}
            else {printf("Unknown flag: %s\n", argv[i]); return 0;}
        }
#ifdef BOOST_SUPPORTED
        if (boost::filesystem::is_regular_file(argv[argc-1])){
            PerFileWrapper(argv[argc-1], Options);
        }
        else if (boost::filesystem::is_directory(argv[argc-1])){
            if(Options.Recurse){boost::filesystem::recursive_directory_iterator a(argv[argc-1]), b;
                std::vector<boost::filesystem::path> paths(a, b);
                for(unsigned i = 0; i < paths.size(); i++){PerFileWrapper(paths[i].c_str(), Options);}
            }
            else{
                boost::filesystem::directory_iterator a(argv[argc-1]), b;
                std::vector<boost::filesystem::path> paths(a, b);
                for(unsigned i = 0; i < paths.size(); i++){
                    PerFileWrapper(paths[i].c_str(), Options);}
            }
        }
#else
        PerFileWrapper(argv[argc-1], Options);
#endif
        if(Options.SavingsCounter){ECT_ReportSavings();}
    }
    else {Usage();}
}
