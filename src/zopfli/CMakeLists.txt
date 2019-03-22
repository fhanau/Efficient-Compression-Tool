cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(zopfli LANGUAGES C CXX)

add_library(zopfli
	blocksplitter.c
	deflate.cpp
	katajainen.cpp
	lz77.c
	squeeze.c
	util.c
	zlib_container.c
	zopfli_gzip.cpp
	
	blocksplitter.h
	deflate.h
	katajainen.h
	lz77.h
	match.h
	squeeze.h
	util.h
	zlib_container.h
	zopfli.h)

add_library(zopfli::zopfli ALIAS zopfli)

target_compile_features(zopfli
	PRIVATE
		cxx_std_11)

if(NOT ECT_MULTITHREADING)
	target_compile_definitions(zopfli
		PRIVATE
			NOMULTI=1)
else()
	find_package(Threads REQUIRED)
	target_link_libraries(zopfli
		Threads::Threads)
endif()
