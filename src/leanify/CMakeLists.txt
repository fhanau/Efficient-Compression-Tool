cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(leanify LANGUAGES CXX)

add_library(leanify
	leanify.cpp
	zip.cpp

	fileio.h
	zip.h)

add_library(leanify::leanify ALIAS leanify)

target_compile_features(leanify
	PRIVATE
		cxx_std_11)
