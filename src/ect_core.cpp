//  main.cpp
//  Efficient Compression Tool
//  Created by Felix Hanau on 12/19/14.
//  Copyright (c) 2014-2025 Felix Hanau.

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

static std::atomic<size_t> processedfiles;
static std::atomic<size_t> bytes;
static std::atomic<long long> savings;

void ECT_ReportSavings(){
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

int ECTGzip(const char * Infile, const unsigned Mode, unsigned char multithreading, long long fs, unsigned ZIP, int strict){
    if (!fs){
      printf("%s: Compression of empty files is currently not supported\n", Infile);
      return 2;
    }
    char* gzip_name = 0;
    int isGZ = IsGzip(Infile, &gzip_name);
    if(isGZ == 2){
      if (gzip_name) {
        free(gzip_name);
      }
      return 2;
    }
    if(isGZ == 3 && strict){
      if (gzip_name) {
        free(gzip_name);
      }
      fprintf(stderr, "%s: File includes extra field or comment, can't be optimized in strict mode\n", Infile);
      return 2;
    }

    std::string out_str = ((std::string)Infile).append(ZIP ? ".zip" : isGZ ? ".tmp" : ".gz");
    const char* out_name = out_str.c_str();
    if (ZIP || !isGZ){
      if (exists(out_name)) {
        fprintf(stderr, "%s: Compressed file already exists\n", Infile);
        return 2;
      }
      if (ZopfliGzip(Infile, out_name, Mode, multithreading, ZIP, 0, Infile)) {return 2;}
      return 1;
    }
    else {
      if (exists(out_name) || ZopfliGzip(Infile, out_name, Mode, multithreading, ZIP, 1, gzip_name)) {
        if (gzip_name) {
          free(gzip_name);
        }
        return 2;
      }
      if (gzip_name) {
        free(gzip_name);
      }
      if (filesize(out_name) < filesize(Infile)){
        RenameAndReplace(out_name, Infile);
      }
      else {
        unlink(out_name);
      }
      return 0;
    }
}

unsigned char OptimizePNG(const char * Infile, const ECTOptions& Options){
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

    if(Options.strip && x){
        Optipng(0, Infile, false, 0);
    }
    return 0;
}

unsigned char OptimizeJPEG(const char * Infile, const ECTOptions& Options){
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
#error MP3 code may corrupt metadata and has been disabled.
void OptimizeMP3(const char * Infile, const ECTOptions& Options){
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
    time_t t = 0;
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
            if (Options.Gzip && !internal) {
                statcompressedfile = ECTGzip(Infile, Options.Mode, Options.DeflateMultithreading, size, Options.Zip, Options.Strict);
                if (statcompressedfile == 2){
                    return 1;
                }
            } else if (x == "PNG" || x == "png") {
                error = OptimizePNG(Infile, Options);
            } else if (x == "jpg" || x == "JPG" || x == "JPEG" || x == "jpeg") {
                error = OptimizeJPEG(Infile, Options);
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
    std::string extension = ((std::string)argv[args[0]]).substr(((std::string)argv[args[0]]).find_last_of(".") + 1);
    std::string zipfilename = argv[args[0]];
    size_t local_bytes = 0;
    unsigned i = 0;
    time_t t = -1;
    if((extension=="zip" || extension=="ZIP" || IsZIP(argv[args[0]]) == 1) && !isDirectory(argv[args[0]])){
        i++;
        if(exists(argv[args[0]])){
            local_bytes += filesize(zipfilename.c_str());
            if(Options.keep){
                t = get_file_time(argv[args[0]]);
            }
        }
    } else {
        //Construct name
        if (!isDirectory(argv[args[0]])
#ifdef FS_SUPPORTED
           && std::filesystem::is_regular_file(argv[args[0]])
#endif
           ) {
            // Cut off file extension, but handle file names beginning with a dot correctly
            if(zipfilename.find_last_of(".") > zipfilename.find_last_of("/\\") + 1) {
                zipfilename = zipfilename.substr(0, zipfilename.find_last_of("."));
            }
        } else {
            // Work around relative directory names ending in '.' or '..'
            // TODO: Implement a proper file name parser and use the absolute path in all cases.
#ifndef _WIN32
            char abs_path[PATH_MAX];
            if (!realpath(argv[args[0]], abs_path)) {
#else
            char abs_path[MAX_PATH];
            if (!GetFullPathNameA(argv[args[0]], MAX_PATH, abs_path, 0)) {
#endif
                printf("Error: Could not find directory\n");
                return 1;
            }
            zipfilename = abs_path;
            if(zipfilename.back() == '/' || zipfilename.back() == '\\') {
                zipfilename.pop_back();
            }
        }
        zipfilename += ".zip";
        if(exists(zipfilename.c_str())){
            printf("Error: ZIP file for chosen file/folder already exists, but is not listed.\n");
            return 1;
        }
    }

    int error = 0;
    for(; error == 0 && i < files; i++){
        if(isDirectory(argv[args[i]])){
#ifdef FS_SUPPORTED
            std::string fold = std::filesystem::canonical(argv[args[i]]).generic_string();
            int substr = std::filesystem::path(fold).has_parent_path() ? std::filesystem::path(fold).parent_path().generic_string().length() + 1 : 0;

            std::filesystem::recursive_directory_iterator a(fold), b;
            std::vector<std::filesystem::path> paths(a, b);
            for(unsigned j = 0; j < paths.size(); j++){
                std::string newfile = paths[j].generic_string();
                const char* name = newfile.erase(0, substr).c_str();
                std::string file_string = paths[j].generic_string();
                const char* file_path = file_string.c_str();

                if(isDirectory(file_path)){
                    //Only add dir if it is empty to minimize filesize
                    if (j + 1 < files) {
                        std::string next = paths[j + 1].generic_string();
                        if (next.compare(0, file_string.size(), file_string) == 0) {
                            continue;
                        }
                    }
                    if (!mz_zip_add_mem_to_archive_file_in_place(zipfilename.c_str(), (((std::string)name) + "/").c_str(), 0, 0, 0, 0, file_path)) {
                        printf("can't add directory '%s'\n", file_path);
                    }
                }
                else{
                    long long f = filesize(file_path);
                    if(f > UINT_MAX){
                        printf("%s: file too big\n", file_path);
                        continue;
                    }
                    if(f < 0){
                        printf("%s: can't read file\n", file_path);
                        continue;
                    }
                    char* file = (char*)malloc(f);
                    if(!file){
                        exit(1);
                    }
                    FILE* stream = fopen(file_path, "rb");
                    if (!stream){
                        free(file); error = 1; continue;
                    }
                    if (fread(file, 1, f, stream) != f){
                        fclose(stream); free(file); error = 1; continue;
                    }
                    fclose(stream);
                    if(!mz_zip_add_mem_to_archive_file_in_place(zipfilename.c_str(), name, file, f, 0, 0, file_path)){
                        printf("can't add file '%s'\n", file_path);
                        free(file); error = 1; continue;
                    }
                    else{
                        local_bytes += filesize(file_path);
                    }
                    free(file);
                }
            }
            if(!paths.size()){
                if (!mz_zip_add_mem_to_archive_file_in_place(zipfilename.c_str(), (fold.erase(0, substr) + "/").c_str(), 0, 0, 0, 0, argv[args[i]])) {
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

void multithreadFileLoop(const std::vector<std::string> &fileList, std::atomic<size_t> *pos, const ECTOptions &options, std::atomic<unsigned> *error) {
    while (true) {
        size_t nextPos = pos->fetch_add(1);
        if (nextPos >= fileList.size()) {
            break;
        }
        unsigned localError = fileHandler(fileList[nextPos].c_str(), options, 0);
        error->fetch_or(localError);
    }
}
