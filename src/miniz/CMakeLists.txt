cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
project(miniz LANGUAGES C)

add_library(miniz
	miniz.c
	miniz.h)

add_library(miniz::miniz ALIAS miniz)
