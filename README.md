Efficient Compression Tool
============================

Efficient Compression Tool (or ECT) is a C++ file optimizer.  
It supports PNG, JPEG, WebP (non-animated lossless optimization), GZIP and ZIP files.

## Behavior Notes (2026-04-21)
- Type detection uses file signatures (magic bytes) before file extensions.
- ZIP stored entries are recompressed with Deflate by default if this makes output smaller.
- `--strict` keeps ZIP stored entries as stored (no stored->deflate conversion).
- Zip64 single-disk archives are supported for reading and writing.
- Multi-disk ZIP/Zip64 archives are not supported and fail safely.
- Animated WebP files are skipped (not treated as an error).

## CLI Notes
- `--disable-webp`: disable WebP optimization.
- `--`: treat all following arguments as positional paths, even if they start with `-`.

Performance (v0.9.7)
-------------------------
Benchmarked on 2026-04-21.

- OS: Windows 11
- CPU: Intel Core i3-8130U (2C/4T)
- Compiler: MSVC (Release)
- Input: `enwik8` (100,000,000 bytes), `-gzip` mode
- Notes: each level was run once on the same machine

|  Compressor    |  File Size   | Time       |
|  ----------    |  -----       | ---------- |
|  ECT -1        |  36,493,273  |      5.1s  |
|  ECT -3        |  35,014,572  |     32.6s  |
|  ECT -5        |  34,942,674  |     54.4s  |
|  ECT -7        |  34,942,246  |   2m 12.3s |
|  ECT -9        |  34,937,520  |   4m 46.7s |

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
