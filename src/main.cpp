//  main.cpp
//  Efficient Compression Tool
//  Created by Felix Hanau on 19.12.14.
//  Copyright (c) 2014-2015 Felix Hanau.

#include "main.h"
#include "support.h"

static unsigned long processedfiles;
static long long bytes;
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
            " -quiet         Print only error messages\n"
            " -help          Print this help\n"
            "Advanced Options:\n"
#ifdef BOOST_SUPPORTED
            " --disable-png  Disable PNG optimization\n"
            " --disable-jpg  Disable JPEG optimization\n"
#endif
            " --strict       Enable strict losslessness\n"
            //" --arithmetic   Use arithmetic encoding for JPEGs, incompatible with most software\n"
#ifdef __DATE__
            ,__DATE__
#endif
            );
}

static void ECT_ReportSavings(){
    if (processedfiles!=0){
    int bk=0;
    int k=0;
    double smul=savings;
    double bmul=bytes;
    while (smul>1024){smul/=1024;k++;}
    while (bmul>1024){bmul/=1024;bk++;}
    char *counter;
    if (k==1){counter=(char *)"K";}
    else if (k==2){counter=(char *)"M";}
    else if (k==3){counter=(char *)"G";}
    else {counter=(char *)"";}
    char *counter2;
    if (bk==1){counter2=(char *)"K";}
    else if (bk==2){counter2=(char *)"M";}
    else if (bk==3){counter2=(char *)"G";}
    else {counter2=(char *)"";}
    printf("Processed %lu file%s\n"
           "Saved ", processedfiles, processedfiles>1 ? "s":"");
    if (k==0){printf("%0.0f", smul);}
    else{printf("%0.2f", smul);}
    printf("%sB out of ", counter);
    if (bk==0){printf("%0.0f", bmul);}
    else{printf("%0.2f", bmul);}
    printf("%sB (%0.3f%%)\n", counter2, (100.0 * savings)/bytes);}
    else {printf("No compatible files found\n");}
}

#ifdef PNG_SUPPORTED
static void OptimizePNG(const char * Filepath, const ECTOptions& Options){
    int x=1;
    long long size = filesize(Filepath);
    if(Options.Mode==5){
        x=Zopflipng(1, Options.Metadata, Filepath, Options.Strict, 2, 0);
    }
    //Disabled as using this causes libpng warnings
    //if (Options.mode>2)
    //int filter = Optipng(Options.Mode, Filepath, Options.Mode!=1, false);
    int filter = Optipng(Options.Mode, Filepath, false, false);
    if (filter == -1){
        return;
    }
    if (Options.Mode!=1){
        if (Options.Mode == 5){
            Zopflipng(60, Options.Metadata, Filepath, Options.Strict, 5, filter);}
        else {
            int PNGiterations;
            if (Options.Mode==2){PNGiterations=1;}
            else if (Options.Mode==3){PNGiterations=5;}
            else {PNGiterations=15;}
            x=Zopflipng(PNGiterations, Options.Metadata, Filepath, Options.Strict, Options.Mode, filter);}
    }
    long long size2 = filesize(Filepath);
    if (size2<=size&&size2>1){unlink(((std::string)Filepath).append(".bak").c_str());}
    else {unlink(Filepath);rename(((std::string)Filepath).append(".bak").c_str(), Filepath);}
    if(Options.Metadata && x==1){Optipng(0, Filepath, false, false);}
}
#endif

#ifdef JPEG_SUPPORTED
static int OptimizeJPEG(const char * Filepath, bool metadata, bool progressive, bool arithmetic){
    return mozjpegtran(arithmetic, progressive, metadata, Filepath, Filepath);
}
#endif

void PerFileWrapper(const char * Input, const ECTOptions& Options){
    std::string Ext = Input;
    std::string x =Ext.substr(Ext.find_last_of(".") + 1);
    bool supported=false;
#if defined JPEG_SUPPORTED && defined PNG_SUPPORTED
    if ((Options.PNG_ACTIVE && (x== "PNG" || x == "png"))||(Options.JPEG_ACTIVE && (x== "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg"))){supported=true;}
#elif defined JPEG_SUPPORTED
    if (Options.JPEG_ACTIVE && (x== "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg")){supported=true;}
#elif defined PNG_SUPPORTED
    if (Options.PNG_ACTIVE && (x== "PNG" || x == "png")){supported=true;}
#endif
    if (supported){
        long long size = filesize(Input);
        if (size<100000000 && size>0){
            int y=0;
#ifdef PNG_SUPPORTED
            if (x== "PNG" || x == "png"){OptimizePNG(Input, Options);}
#ifdef JPEG_SUPPORTED
            else
#endif
#endif
#ifdef JPEG_SUPPORTED
                if (x== "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg"){
                if (Options.Progressive){
                    if(OptimizeJPEG(Input, Options.Metadata, true, Options.Arithmetic)!=0){y++;}
                    if((Options.Mode==1&&size<6142)||(Options.Mode==2&&size<8192)||(Options.Mode==3&&size<15360)||(Options.Mode==4&&size<30720)||(Options.Mode==5&&size<51200)){
                        if(OptimizeJPEG(Input, Options.Metadata, false, Options.Arithmetic)!=0){y++;}}
                    else if ((Options.Mode==2&&size<26142)||(Options.Mode==3&&size<45360)||(Options.Mode==4&&size<70720)||(Options.Mode==5&&size<101200)){
                        long long procs = filesize(Input);
                        if((Options.Mode==1&&procs<6142)||(Options.Mode==2&&procs<8192)||(Options.Mode==3&&procs<15360)||(Options.Mode==4&&procs<30720)||(Options.Mode==5&&procs<51200)){
                            if(OptimizeJPEG(Input, Options.Metadata, false, Options.Arithmetic)!=0){y++;}}
                        else{y++;}
                    }
                    else{y++;}
                }
                else{if(OptimizeJPEG(Input, Options.Metadata, false, Options.Arithmetic)!=0){y++;}y++;}
            }
#endif
            if(Options.SavingsCounter){
                processedfiles++;
                bytes+=size;
                if(y!=2){
                    savings=savings+size-filesize(Input);
                }
            }
        }
        else{printf("File too big\n");}
    }
}

int main(int argc, const char * argv[]) {
    ECTOptions Options;
    Options.Metadata = false;
    Options.Progressive = false;
    Options.Mode = 1;
#ifdef BOOST_SUPPORTED
    Options.Recurse = false;
#endif
    Options.PNG_ACTIVE = true;
    Options.JPEG_ACTIVE = true;
    Options.Arithmetic = false;
    Options.SavingsCounter = true;
    Options.Strict = false;
    if (argc>=2){
        for (int i = 1; i < argc-1; i++) {
            std::string arg = argv[i];
            if (arg == "-strip" || arg == "-s"){Options.Metadata=true;}
            else if (arg == "-progressive" || arg == "-p") {Options.Progressive=true;}
            else if (arg == "-M1") {Options.Mode=1;}
            else if (arg == "-M2") {Options.Mode=2;}
            else if (arg == "-M3") {Options.Mode=3;}
            else if (arg == "-M4") {Options.Mode=4;}
            else if (arg == "-M5") {Options.Mode=5;}
            else if (arg == "-h" || arg == "-help") {Usage(); return 0;}
            else if (arg == "-quiet" || arg == "-q") {Options.SavingsCounter=false;}
#ifdef BOOST_SUPPORTED
            else if (arg == "--disable-jpeg" || arg == "--disable-jpg"){Options.JPEG_ACTIVE=false;}
            else if (arg == "--disable-png"){Options.PNG_ACTIVE=false;}
            else if (arg == "-recurse" || arg == "-r")  {Options.Recurse=1;}
#endif
            else if (arg == "--strict") {Options.Strict=true;}
//            else if (arg == "--arithmetic") {Options.Arithmetic=true;}
            else {printf("Unknown flag: %s\n", argv[i]); return 0;}
        }
#ifdef BOOST_SUPPORTED
        if (boost::filesystem::is_regular_file(argv[argc-1])){
            PerFileWrapper(argv[argc-1], Options);
        }
        else if (boost::filesystem::is_directory(argv[argc-1])){
            if(Options.Recurse){boost::filesystem::recursive_directory_iterator a(argv[argc-1]), b;
                std::vector<boost::filesystem::path> paths(a, b);
                for(unsigned long i=0;i<paths.size();i++){PerFileWrapper(paths[i].c_str(), Options);}
            }
            else{
                boost::filesystem::directory_iterator a(argv[argc-1]), b;
                std::vector<boost::filesystem::path> paths(a, b);
                for(unsigned long i=0;i<paths.size();i++){PerFileWrapper(paths[i].c_str(), Options);}
            }
        }
#else
        PerFileWrapper(argv[argc-1], Options);
#endif
        if(Options.SavingsCounter){ECT_ReportSavings();}
    }
    else {Usage();}
}
