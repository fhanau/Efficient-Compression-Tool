cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(optipng LANGUAGES C CXX)

add_library(optipng
	optipng.cpp
	codec.cpp
	image.c
	opngreduc/opngreduc.c

	codec.h
	image.h
	opngcore.h
	opngreduc/opngreduc.h
	trans.h)

add_library(optipng::optipng ALIAS optipng)

add_subdirectory(../libpng libpng EXCLUDE_FROM_ALL)
target_link_libraries(optipng
	png_static)

# libpng generates some header files that we need to be able to include
target_include_directories(optipng
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/libpng)
