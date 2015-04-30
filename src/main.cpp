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
           "Saved "
           ,processedfiles, processedfiles>1 ? "s":"");
    if (k==0){printf("%0.0f", smul);}
    else{printf("%0.2f", smul);}
    printf("%sB out of ", counter);
    if (bk==0){printf("%0.0f", bmul);}
    else{printf("%0.2f", bmul);}
    printf("%sB (%0.3f%%)\n", counter2, (100.0 * savings)/bytes);}
    else {printf("No compatible files found\n");}
}

#ifdef PNG_SUPPORTED
static void OptimizePNG(const char * Filepath, const ECTOptions& ECTopt){
    int x=1;
    long long size = filesize(Filepath);
    if(ECTopt.Mode==5){
        Zopflipng(1,ECTopt.Metadata,Filepath,ECTopt.Strict, 2, 0);
    }
    int filter = Optipng(ECTopt.Mode,Filepath, false, false);
    if (filter == -1){
        return;
    }
    if (ECTopt.Mode!=1){
        int PNGiterations;
        if (ECTopt.Mode==2){PNGiterations=1;}
        else if (ECTopt.Mode==3){PNGiterations=5;}
        else if (ECTopt.Mode==4){PNGiterations=15;}
        else {PNGiterations=60;}
        x=Zopflipng(PNGiterations,ECTopt.Metadata,Filepath,ECTopt.Strict, ECTopt.Mode, filter);
    }
    long long size2 = filesize(Filepath);
    if (size2<=size&&size2>1){unlink(((std::string)Filepath).append(".bak").c_str());}
    else {unlink(Filepath);rename(((std::string)Filepath).append(".bak").c_str(),Filepath);}
    if(ECTopt.Metadata && x==1){Optipng(0,Filepath, false, false);}
}
#endif

#ifdef JPEG_SUPPORTED
static int OptimizeJPEG(const char * Filepath, bool metadata, bool progressive, bool arithmetic){
    return mozjpegtran(arithmetic,progressive,metadata,Filepath,Filepath);
}
#endif

void PerFileWrapper(const char * Input, const ECTOptions& ECTopt){
    std::string Ext = Input;
    std::string x =Ext.substr(Ext.find_last_of(".") + 1);
    bool supported=false;
#if defined JPEG_SUPPORTED && defined PNG_SUPPORTED
    if ((ECTopt.PNG_ACTIVE && (x== "PNG" || x == "png"))||(ECTopt.JPEG_ACTIVE && (x== "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg"))){supported=true;}
#elif defined JPEG_SUPPORTED
    if (ECTopt.JPEG_ACTIVE && (x== "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg")){supported=true;}
#elif defined PNG_SUPPORTED
    if (ECTopt.PNG_ACTIVE && (x== "PNG" || x == "png")){supported=true;}
#endif
    if (supported){
        long long size = filesize(Input);
        if (size<100000000 && size>0){
            int y=0;
#ifdef PNG_SUPPORTED
            if (x== "PNG" || x == "png"){OptimizePNG(Input,ECTopt);}
#ifdef JPEG_SUPPORTED
            else
#endif
#endif
#ifdef JPEG_SUPPORTED
                if (x== "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg"){
                if (ECTopt.Progressive){
                    if(OptimizeJPEG(Input,ECTopt.Metadata,true,ECTopt.Arithmetic)!=0){y++;}
                    if((ECTopt.Mode==1&&size<6142)||(ECTopt.Mode==2&&size<8192)||(ECTopt.Mode==3&&size<15360)||(ECTopt.Mode==4&&size<30720)||(ECTopt.Mode==5&&size<51200)){
                        if(OptimizeJPEG(Input,ECTopt.Metadata,false,ECTopt.Arithmetic)!=0){y++;}}
                    else if ((ECTopt.Mode==2&&size<26142)||(ECTopt.Mode==3&&size<45360)||(ECTopt.Mode==4&&size<70720)||(ECTopt.Mode==5&&size<101200)){
                        long long procs = filesize(Input);
                        if((ECTopt.Mode==1&&procs<6142)||(ECTopt.Mode==2&&procs<8192)||(ECTopt.Mode==3&&procs<15360)||(ECTopt.Mode==4&&procs<30720)||(ECTopt.Mode==5&&procs<51200)){
                            if(OptimizeJPEG(Input,ECTopt.Metadata,false,ECTopt.Arithmetic)!=0){y++;}}
                        else{y++;}
                    }
                    else{y++;}
                }
                else{if(OptimizeJPEG(Input,ECTopt.Metadata,false,ECTopt.Arithmetic)!=0){y++;}y++;}
            }
#endif
            if(ECTopt.SavingsCounter){
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
    ECTOptions ECTopt;
    ECTopt.Metadata = false;
    ECTopt.Progressive = false;
    ECTopt.Mode = 1;
#ifdef BOOST_SUPPORTED
    ECTopt.Recurse = false;
#endif
    ECTopt.PNG_ACTIVE = true;
    ECTopt.JPEG_ACTIVE = true;
    ECTopt.Arithmetic = false;
    ECTopt.SavingsCounter = true;
    ECTopt.Strict = false;
    if (argc>=2){
        for (int i = 1; i < argc-1; i++) {
            std::string arg = argv[i];
            if (arg == "-strip" || arg == "-s"){ECTopt.Metadata=true;}
            else if (arg == "-progressive" || arg == "-p") {ECTopt.Progressive=true;}
            else if (arg == "-M1") {ECTopt.Mode=1;}
            else if (arg == "-M2") {ECTopt.Mode=2;}
            else if (arg == "-M3") {ECTopt.Mode=3;}
            else if (arg == "-M4") {ECTopt.Mode=4;}
            else if (arg == "-M5") {ECTopt.Mode=5;}
            else if (arg == "-h" || arg == "-help") {Usage(); return 0;}
            else if (arg == "-quiet" || arg == "-q") {ECTopt.SavingsCounter=false;}
#ifdef BOOST_SUPPORTED
            else if (arg == "--disable-jpeg" || arg == "--disable-jpg"){ECTopt.JPEG_ACTIVE=false;}
            else if (arg == "--disable-png"){ECTopt.PNG_ACTIVE=false;}
            else if (arg == "-recurse" || arg == "-r")  {ECTopt.Recurse=1;}
#endif
            else if (arg == "--strict") {ECTopt.Strict=true;}
//            else if (arg == "--arithmetic") {ECTopt.Arithmetic=true;}
            else {printf("Unknown flag: %s\n", argv[i]); return 0;}
        }
#ifdef BOOST_SUPPORTED
        if (boost::filesystem::is_regular_file(argv[argc-1])){
            PerFileWrapper(argv[argc-1],ECTopt);
        }
        else if (boost::filesystem::is_directory(argv[argc-1])){
            if(ECTopt.Recurse){boost::filesystem::recursive_directory_iterator a(argv[argc-1]), b;
                std::vector<boost::filesystem::path> paths(a, b);
                for(unsigned long i=0;i<paths.size();i++){PerFileWrapper(paths[i].c_str(),ECTopt);}
            }
            else{
                boost::filesystem::directory_iterator a(argv[argc-1]), b;
                std::vector<boost::filesystem::path> paths(a, b);
                for(unsigned long i=0;i<paths.size();i++){PerFileWrapper(paths[i].c_str(),ECTopt);}
            }
        }
#else
        PerFileWrapper(argv[argc-1],ECTopt);
#endif
        if(ECTopt.SavingsCounter){ECT_ReportSavings();}
    }
    else {Usage();}
}
