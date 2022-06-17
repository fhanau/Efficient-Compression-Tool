//  main.cpp
//  Efficient Compression Tool
//  Created by Felix Hanau on 12/19/14.
//  Copyright (c) 2014-2022 Felix Hanau.

#include "main.h"
#include "support.h"
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

static std::atomic<size_t> processedfiles;
static std::atomic<size_t> bytes;
static std::atomic<long long> savings;

static void Usage() {
    printf (
            "Efficient Compression Tool\n"
            "(c) 2014-2022 Felix Hanau.\n"
            "Version 0.9.1"
#ifdef __DATE__
            " compiled on %s\n"
#endif
            "Folder support "
#ifdef BOOST_SUPPORTED
            "enabled\n"
#else
            "disabled\n"
#endif

            "Losslessly optimizes GZIP, ZIP, JPEG and PNG images\n"
            "Usage: ECT [Options] Files"
#ifdef BOOST_SUPPORTED
            "/Folders"
#endif
            "...\n"
            "Options:\n"
            " -1 to -9          Set compression level (Default: 3)\n"
            " -strip            Strip metadata\n"
            " -progressive      Use progressive encoding for JPEGs\n"
            " -autorotate       Automatically rotate JPEGs, when perfectly transformable\n"
            " -autorotate=force Automatically rotate JPEGs, dropping non-transformable edge blocks\n"
#ifdef BOOST_SUPPORTED
            " -recurse          Recursively search directories\n"
#endif
            " -zip              Compress file(s) with  ZIP algorithm\n"
            " -gzip             Compress file with GZIP algorithm\n"
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

static void RenameAndReplace(const char * Infile, const char * Outfile){
#ifdef _WIN32
    MoveFileExA(Infile, Outfile, MOVEFILE_REPLACE_EXISTING);
#else
    rename(Infile, Outfile);
#endif
}

static void ECT_ReportSavings(){
    size_t localProcessedFiles = processedfiles.load(std::memory_order_seq_cst);
    size_t localBytes = bytes.load(std::memory_order_seq_cst);
    long long localSavings = savings.load(std::memory_order_seq_cst);
    if (localProcessedFiles){
        printf("Processed %zu file%s\n", localProcessedFiles, localProcessedFiles > 1 ? "s":"");
        if (localSavings < 0){
            printf("Result is bigger\n");
            return;
        }

        int bk = 0;
        int k = 0;
        double smul = localSavings;
        double bmul = localBytes;
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
        printf("%sB (%0.4f%%)\n", counter2, (100.0 * localSavings)/localBytes);}
    else {printf("No compatible files found\n");}
}

static int ECTGzip(const char * Infile, const unsigned Mode, unsigned char multithreading, long long fs, unsigned ZIP, int strict){
    if (!fs){
        printf("%s: Compression of empty files is currently not supported\n", Infile);
        return 2;
    }
    int isGZ = IsGzip(Infile);
    if(isGZ == 2){
        return 2;
    }
    if(isGZ == 3 && strict){
        printf("%s: File includes extra field, file name or comment, can't be optimized in strict mode\n", Infile);
        return 2;
    }
    if (ZIP || !isGZ){
        if (exists(((std::string)Infile).append(ZIP ? ".zip" : ".gz").c_str())){
            printf("%s: Compressed file already exists\n", Infile);
            return 2;
        }
        ZopfliGzip(Infile, 0, Mode, multithreading, ZIP, 0);
        return 1;
    }
    else {
      if (exists(((std::string)Infile).append(".tmp").c_str())){
        return 2;
      }
      ZopfliGzip(Infile, 0, Mode, multithreading, ZIP, 1);
      if (filesize(((std::string)Infile).append(".tmp").c_str()) < filesize(Infile)){
          RenameAndReplace(((std::string)Infile).append(".tmp").c_str(), Infile);
      }
      else {
          unlink(((std::string)Infile).append(".tmp").c_str());
      }
      return 0;
    }
}

static unsigned char OptimizePNG(const char * Infile, const ECTOptions& Options){
    unsigned _mode = Options.Mode;
    unsigned mode = (Options.Mode % 10000) > 9 ? 9 : (Options.Mode % 10000);
    if (mode == 1 && Options.Reuse){
        mode++;
    }
    unsigned quiet = !Options.SavingsCounter;

    int x = 1;
    long long size = filesize(Infile);
    if(size < 0){
        printf("Can't read from %s\n", Infile);
        return 1;
    }
    if(mode == 9 && !Options.Reuse && !Options.Allfilters){
        x = Zopflipng(Options.strip, Infile, Options.Strict, 3, 0, Options.DeflateMultithreading, quiet);
        if(x < 0){
            return 1;
        }
    }
    //Disabled as using this causes libpng warnings
    //int filter = Optipng(Options.Mode, Infile, true, Options.Strict || Options.Mode > 1);
    int filter = 0;
    if (!Options.Allfilters){
        filter = Options.Reuse ? 6 : Optipng(mode, Infile, false, Options.Strict || mode > 1);
    }

    if (filter == -1){
        return 1;
    }
    if(filter && !Options.Allfilters && Options.Allfilterscheap && !Options.Reuse){
        filter = 15;
    }
    if (mode != 1){
        if (Options.Allfilters){
            x = Zopflipng(Options.strip, Infile, Options.Strict, _mode, 6 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            if(x < 0){
                return 1;
            }
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 5 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 1 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 2 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 3 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 4 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 7 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 8 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 11 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 12 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, 13 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            if (Options.Allfiltersbrute){
                Zopflipng(Options.strip, Infile, Options.Strict, _mode, 9 + Options.palette_sort, Options.DeflateMultithreading, quiet);
                Zopflipng(Options.strip, Infile, Options.Strict, _mode, 10 + Options.palette_sort, Options.DeflateMultithreading, quiet);
                Zopflipng(Options.strip, Infile, Options.Strict, _mode, 14 + Options.palette_sort, Options.DeflateMultithreading, quiet);
            }
        }
        else if (mode == 9){
            Zopflipng(Options.strip, Infile, Options.Strict, _mode, filter + Options.palette_sort, Options.DeflateMultithreading, quiet);
        }
        else {
            x = Zopflipng(Options.strip, Infile, Options.Strict, _mode, filter + Options.palette_sort, Options.DeflateMultithreading, quiet);
            if(x < 0){
                return 1;
            }
        }
    }
    else {
        if (filesize(Infile) <= size){
            unlink(((std::string)Infile).append(".bak").c_str());
        }
        else {
            RenameAndReplace(((std::string)Infile).append(".bak").c_str(), Infile);
        }
    }

    if(Options.strip && x){
        Optipng(0, Infile, false, 0);
    }
    return 0;
}

static unsigned char OptimizeJPEG(const char * Infile, const ECTOptions& Options){
    size_t stsize = 0;

    int res = mozjpegtran(Options.Arithmetic, Options.Progressive && (Options.Mode > 1 || filesize(Infile) > 5000), Options.strip, Options.Autorotate, Infile, Infile, &stsize);
    if (Options.Progressive && Options.Mode > 1 && res != 2){
        if(res == 1 || (Options.Mode == 2 && stsize < 6500) || (Options.Mode == 3 && stsize < 10000) || (Options.Mode == 4 && stsize < 15000) || (Options.Mode > 4 && stsize < 20000)){
            res = mozjpegtran(Options.Arithmetic, false, Options.strip, Options.Autorotate, Infile, Infile, &stsize);
        }
    }
    return res == 2;
}

#ifdef MP3_SUPPORTED
#error MP3 code may corrupt metadata.
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

unsigned fileHandler(const char * Infile, const ECTOptions& Options, int internal){
    std::string Ext = Infile;
    std::string x = Ext.substr(Ext.find_last_of(".") + 1);
    time_t t;
    unsigned error = 0;

    if ((Options.PNG_ACTIVE && (x == "PNG" || x == "png")) || (Options.JPEG_ACTIVE && (x == "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg")) || (Options.Gzip && !internal)){
        if(Options.keep){
            t = get_file_time(Infile);
        }
        long long size = filesize(Infile);
        if (size < 0){
            printf("%s: bad file\n", Infile);
            return 1;
        }
        int statcompressedfile = 0;
        if (size < 1200000000) {//completely random value
            if (x == "PNG" || x == "png"){
                error = OptimizePNG(Infile, Options);
            }
            else if (x == "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg"){
                error = OptimizeJPEG(Infile, Options);
            }
            else if (Options.Gzip && !internal){
                statcompressedfile = ECTGzip(Infile, Options.Mode, Options.DeflateMultithreading, size, Options.Zip, Options.Strict);
                if (statcompressedfile == 2){
                    return 1;
                }
            }
            if(Options.SavingsCounter && !internal){
                processedfiles.fetch_add(1);
                bytes.fetch_add(size);
                if (!statcompressedfile){
                savings.fetch_add(size - filesize(Infile));
                }
                else if (statcompressedfile){
                    savings.fetch_add((size - filesize(((std::string)Infile).append(Options.Zip ? ".zip" : ".gz").c_str())));
                }
            }
        }
        else{printf("File too big\n");}
        if(Options.keep && !statcompressedfile){
            set_file_time(Infile, t);
        }
    }
#ifdef MP3_SUPPORTED
    else if(x == "mp3"){
        OptimizeMP3(Infile, Options);
    }
#endif
    return error;
}

unsigned zipHandler(std::vector<int> args, const char * argv[], int files, const ECTOptions& Options){
#ifdef _WIN32
#define EXTSEP "\\"
#else
#define EXTSEP "/"
#endif
    std::string extension = ((std::string)argv[args[0]]).substr(((std::string)argv[args[0]]).find_last_of(".") + 1);
    std::string zipfilename = argv[args[0]];
    size_t local_bytes = 0;
    unsigned i = 0;
    time_t t = -1;
    if((extension=="zip" || extension=="ZIP" || IsZIP(argv[args[0]])) && !isDirectory(argv[args[0]])){
        i++;
        if(exists(argv[args[0]])){
            local_bytes += filesize(zipfilename.c_str());
            if(Options.keep){
                t = get_file_time(argv[args[0]]);
            }
        }
    }
    else{
        //Construct name
        if(!isDirectory(argv[args[0]])
#ifdef BOOST_SUPPORTED
           && boost::filesystem::is_regular_file(argv[args[0]])
#endif
           ){
            if(zipfilename.find_last_of(".") > zipfilename.find_last_of("/\\")) {
                zipfilename = zipfilename.substr(0, zipfilename.find_last_of("."));
            }
        }
        else if(zipfilename.back() == '/' || zipfilename.back() == '\\'){
            zipfilename.pop_back();
        }

        zipfilename += ".zip";
        if(exists(zipfilename.c_str())){
            printf("Error: ZIP file for chosen file/folder already exists, but you didn't list it.\n");
            return 1;
        }
    }

    int error = 0;
    for(; error == 0 && i < files; i++){
        if(isDirectory(argv[args[i]])){
#ifdef BOOST_SUPPORTED
            std::string fold = boost::filesystem::canonical(argv[args[i]]).string();
            int substr = boost::filesystem::path(fold).has_parent_path() ? boost::filesystem::path(fold).parent_path().string().length() + 1 : 0;

            boost::filesystem::recursive_directory_iterator a(fold), b;
            std::vector<boost::filesystem::path> paths(a, b);
            for(unsigned j = 0; j < paths.size(); j++){
                std::string newfile = paths[j].string();
                const char* name = newfile.erase(0, substr).c_str();

                if(isDirectory(paths[j].string().c_str())){
                    //Only add dir if it is empty to minimize filesize
                    std::string next = paths[j + 1].string();
                    if ((next.compare(0, paths[j].string().size() + 1, paths[j].string() + "/") != 0 || next.compare(0, paths[j].string().size() + 1, paths[j].string() + "/") != 0)&& !mz_zip_add_mem_to_archive_file_in_place(zipfilename.c_str(), ((std::string)name + EXTSEP).c_str(), 0, 0, 0, 0, paths[j].string().c_str())) {
                        printf("can't add directory '%s'\n", argv[args[i]]);
                    }
                }
                else{
                    long long f = filesize(paths[j].string().c_str());
                    if(f > UINT_MAX){
                        printf("%s: file too big\n", paths[j].string().c_str());
                        continue;
                    }
                    if(f < 0){
                        printf("%s: can't read file\n", paths[j].string().c_str());
                        continue;
                    }
                    char* file = (char*)malloc(f);
                    if(!file){
                        exit(1);
                    }
                    FILE * stream = fopen (paths[j].string().c_str(), "rb");
                    if (!stream){
                        free(file); error = 1; continue;
                    }
                    if (fread(file, 1, f, stream) != f){
                        fclose(stream); free(file); error = 1; continue;
                    }
                    fclose(stream);
                    if(!mz_zip_add_mem_to_archive_file_in_place(zipfilename.c_str(), name, file, f, 0, 0, paths[j].string().c_str())){
                        printf("can't add file '%s'\n", paths[j].string().c_str());
                        free(file); error = 1; continue;
                    }
                    else{
                        local_bytes += filesize(paths[j].string().c_str());
                    }
                    free(file);
                }
            }
            if(!paths.size()){
                if (!mz_zip_add_mem_to_archive_file_in_place(zipfilename.c_str(), (fold.erase(0, substr) + EXTSEP).c_str(), 0, 0, 0, 0, argv[args[i]])) {
                    printf("can't add directory '%s'\n", argv[args[i]]);
                }
            }
#else
            printf("%s: Zipping folders is not supported\n", argv[args[i]]);
#endif
        }
        else{

            const char* fname = argv[args[i]];
            long long f = filesize(fname);
            if(f > UINT_MAX){
                printf("%s: file too big\n", fname);
                continue;
            }
            if(f < 0){
                printf("%s: can't read file\n", fname);
                continue;
            }
            char* file = (char*)malloc(f);
            if(!file){
                exit(1);
            }

            FILE * stream = fopen (fname, "rb");
            if (!stream){
                free(file); error = 1; continue;
            }
            if (fread(file, 1, f, stream) != f){
                fclose(stream); free(file); error = 1; continue;
            }

            fclose(stream);
            if (!mz_zip_add_mem_to_archive_file_in_place(zipfilename.c_str(), ((std::string)argv[args[i]]).substr(((std::string)argv[args[i]]).find_last_of("/\\") + 1).c_str(), file, f, 0, 0, argv[args[i]])
                ) {
                printf("can't add file '%s'\n", argv[0]);
                free(file); error = 1; continue;
            }
            local_bytes += filesize(argv[args[i]]);

            free(file);

        }
    }
    size_t localProcessedFiles = 0;
    ReZipFile(zipfilename.c_str(), Options, &localProcessedFiles);
    processedfiles.fetch_add(localProcessedFiles);
    if(t >= 0){
        set_file_time(zipfilename.c_str(), t);
    }

    bytes.fetch_add(local_bytes);
    savings.fetch_add(local_bytes - filesize(zipfilename.c_str()));
    return error;
}

static void multithreadFileLoop(const std::vector<std::string> &fileList, std::atomic<size_t> *pos, const ECTOptions &options, std::atomic<unsigned> *error) {
    while (true) {
        size_t nextPos = pos->fetch_add(1);
        if (nextPos >= fileList.size()) {
            break;
        }
        unsigned localError = fileHandler(fileList[nextPos].c_str(), options, 0);
        error->fetch_or(localError);
    }
}

int main(int argc, const char * argv[]) {
    std::atomic<unsigned> error(0);
    ECTOptions Options;
    Options.strip = false;
    Options.Progressive = false;
    Options.Autorotate = 0;
    Options.Mode = 3;
#ifdef BOOST_SUPPORTED
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
#ifdef BOOST_SUPPORTED
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
#ifdef BOOST_SUPPORTED
                if (boost::filesystem::is_regular_file(argv[args[j]])){
                    fileList.push_back(argv[args[j]]);
                }
                else if (boost::filesystem::is_directory(argv[args[j]])){
                    if(Options.Recurse){boost::filesystem::recursive_directory_iterator a(argv[args[j]]), b;
                        std::vector<boost::filesystem::path> paths(a, b);
                        for(unsigned i = 0; i < paths.size(); i++){
                            fileList.push_back(paths[i].string());
                        }
                    }
                    else{
                        boost::filesystem::directory_iterator a(argv[args[j]]), b;
                        std::vector<boost::filesystem::path> paths(a, b);
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
