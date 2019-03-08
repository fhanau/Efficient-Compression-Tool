cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(lodepng LANGUAGES CXX)

add_library(lodepng
	lodepng.cpp
	lodepng_util.cpp

	lodepng.h
	lodepng_util.h)

add_library(lodepng::lodepng ALIAS lodepng)
