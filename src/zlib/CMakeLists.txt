cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(zlib LANGUAGES C)

add_library(zlib
	adler32.c
	crc32.c
	deflate.c
	gzlib.c
	gzread.c
	inffast.c
	inffast_chunk.c
	inflate.c
	inftrees.c
	trees.c
	zutil.c

	crc32.h
	deflate.h
	gzguts.h
	inffast.h
	inffixed.h
	inflate.h
	inftrees.h
	trees.h
	zconf.h
	zlib.h
	zutil.h)

add_library(zlib::zlib ALIAS zlib)
