Efficient Compression Tool
============================

Efficient Compression Tool (or ECT) is a C++ file optimizer. 
It supports PNG, JPEG, GZIP and ZIP files.

Performance(v0.6)
-------------------------
All tests were run on OS X 10.11 using an i5-2500S and clang.

File: enwik8, 100,000,000 bytes, compressed into gzip format

|  Compressor    |  File Size    | Time       |
|  ----------    |  -----        | ---------- |
|  gzip -9       |  36,475,811B  |      6.2s  |
| [zopfli] -i1   |  35,139,225B  |  1m  6.4s  | 
|  ECT -2        |  35,019,083B  |     16.9s  |
|  zopfli -i5    |  35,014,777B  |  1m 56.2s  |
|  ECT -3        |  35,012,378B  |     18.3s  |
|  zopfli -i15   |  34,987,258B  |  4m  3.2s  |
|  ECT -4        |  34,964,155B  |     22.5s  |
|  ECT -5        |  34,942,811B  |     28.9s  |
|  ECT -6        |  34,940,747B  |     49.7s  |
|  ECT -7        |  34,938,252B  |  1m  7.9s  |
|  ECT -8        |  34,937,694B  |  3m 54.8s  |


[zopfli]: https://github.com/google/zopfli

## Building

### Command line
ECT is built with `cmake`
```bash
mkdir build
cd build
cmake ../src
make
```

In addition, you can add the following arguments to the cmake call to turn various features on and off:
- `-DECT_MULTITHREADING=OFF`: Turn off multithreading support
- `-DECT_FOLDER_SUPPORT=ON`: Turn on the ability to recursively search folders (requires [boost::filesystem](https://www.boostcpp.org/))

### With Xcode
You can use cmake to generate an Xcode project.  Just add `-G Xcode` to the end of the cmake command:
```bash
mkdir build
cd build
cmake ../src -G Xcode
make
```
You will run into a slight issue in that Xcode doesn't know how to compile some of the asm files within mozjpeg project.  To fix this, locate your copy of `nasm` (`/usr/local/bin/nasm` in the example) navigate to the Build Rules of the `simd` target, and add a custom rule to process source files matching `*.asm` with the following script:
```sh
/usr/local/bin/nasm "-I${PROJECT_DIR}/mozjpeg" -DMACHO -D__x86_64__ "-I${PROJECT_DIR}/mozjpeg/simd/nasm/" "-I${PROJECT_DIR}/mozjpeg/simd/x86_64/" -f macho64 -o "${BUILT_PRODUCTS_DIR}/x86_64/${INPUT_FILE_BASE}.o" "${INPUT_FILE_PATH}"
```
and set `$(BUILT_PRODUCTS_DIR)/x86_64/${INPUT_FILE_BASE}.o` as the output files.

If you are using Xcode for development and do not need maximum speed, you can also disable the asm files by adding `-DWITH_SIMD=OFF` to the cmake.
