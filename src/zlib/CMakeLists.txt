cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(zlib LANGUAGES C)

add_library(zlib
	adler32.c
	adler32_simd.c
	crc32.c
	crc32_simd.c
	deflate.c
	gzlib.c
	gzread.c
	inffast.c
	inffast_chunk.c
	inflate.c
	inftrees.c
	trees.c
	zutil.c

	adler32_simd.h
	chunkcopy.h
	crc32.h
	crc32_simd.h
	deflate.h
	gzguts.h
	inffast.h
	inffast_chunk.h
	inffixed.h
	inflate.h
	inftrees.h
	trees.h
	zconf.h
	zlib.h
	zutil.h)

add_library(zlib::zlib ALIAS zlib)
