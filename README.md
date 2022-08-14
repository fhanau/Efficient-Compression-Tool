Efficient Compression Tool
============================

Efficient Compression Tool (or ECT) is a C++ file optimizer.  
It supports PNG, JPEG, GZIP and ZIP files.

Performance (v0.9.2)
-------------------------
All tests were run on macOS 12.5 using an Intel i7-7700HQ and clang.  
File: enwik8, 100,000,000 bytes, compressed into gzip format

|  Compressor    |  File Size   | Time       |
|  ----------    |  -----       | ---------- |
|  ECT -1        |  36,493,257  |      3.5s  |
|  gzip -9       |  36,475,811  |      5.8s  |
| [zopfli] -i1   |  35,102,371  |  1m 30.2s  |
|  ECT -2        |  35,019,440  |     14.8s  |
|  zopfli -i5    |  34,983,757  |  2m 12.0s  |
|  ECT -3        |  35,014,543  |     16.2s  |
|  zopfli -i15   |  34,966,078  |  3m 59.9s  |
|  ECT -4        |  34,963,581  |     19.8s  |
|  zopfli -i30   |  34,961,453  |  6m 30.6s  |
|  ECT -5        |  34,942,796  |     25.1s  |
|  ECT -6        |  34,943,943  |     41.9s  |
|  ECT -7        |  34,942,348  |     59.7s  |
|  ECT -8        |  34,941,125  |  2m 25.6s  |
|  ECT -9        |  34,937,781  |  3m 17.9s  |


[zopfli]: https://github.com/google/zopfli

## Building
To build ECT, you need to recursively clone it, just downloading isn’t enough, i. e. `git clone --recursive https://github.com/fhanau/Efficient-Compression-Tool.git`  
You may also need to install `nasm` if it is not available already.

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

### With Xcode
You can use cmake to generate an Xcode project.  Just add `-G Xcode` to the end of the cmake command:
```bash
mkdir build
cd build
cmake ../src -G Xcode
make
```
You will run into a slight issue in that Xcode doesn't know how to compile some of the asm files within mozjpeg.  To fix this, locate your copy of `nasm` (`/usr/local/bin/nasm` in the example) navigate to the Build Rules of the `simd` target, and add a custom rule to process source files matching `*.asm` with the following script:
```sh
/usr/local/bin/nasm "-I${PROJECT_DIR}/mozjpeg" -DMACHO -D__x86_64__ "-I${PROJECT_DIR}/mozjpeg/simd/nasm/" "-I${PROJECT_DIR}/mozjpeg/simd/x86_64/" -f macho64 -o "${BUILT_PRODUCTS_DIR}/x86_64/${INPUT_FILE_BASE}.o" "${INPUT_FILE_PATH}"
```
and set `$(BUILT_PRODUCTS_DIR)/x86_64/${INPUT_FILE_BASE}.o` as the output files.

If you are using Xcode for development and do not need maximum speed, you can also disable the asm files by adding `-DWITH_SIMD=OFF` to the cmake.
