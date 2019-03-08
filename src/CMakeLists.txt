cmake_minimum_required(VERSION 3.0 FATAL_ERROR)
project(ect VERSION 0.8.2 LANGUAGES CXX C)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(ect
	main.cpp
	gztools.cpp
	jpegtran.cpp
	LzFind.c
	support.cpp
	zopflipng.cpp
	# Add headers so they get added to things like Xcode projects
	gztools.h
	LzFind.h
	main.h
	pngusr.h
	support.h)

add_executable(ect::ect ALIAS ect)

if(MINGW)
	add_compile_options(-mno-ms-bitfields)
endif()

# Use -Ofast in release builds
foreach(var CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	if(${var} MATCHES "-O(2|3)")
		string(REGEX REPLACE "-O(2|3)" "-Ofast" ${var} "${${var}}")
	endif()
endforeach()

# Enable ZLIB_CONST for everything
add_definitions(-DZLIB_CONST)

add_subdirectory(leanify EXCLUDE_FROM_ALL)
add_subdirectory(lodepng EXCLUDE_FROM_ALL)
add_subdirectory(miniz EXCLUDE_FROM_ALL)
add_subdirectory(zlib EXCLUDE_FROM_ALL)
add_subdirectory(zopfli EXCLUDE_FROM_ALL)
add_subdirectory(optipng EXCLUDE_FROM_ALL)
# Mozjpeg changes the install prefix if it thinks the current is defaulted
set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)
add_subdirectory(mozjpeg EXCLUDE_FROM_ALL)

target_link_libraries(ect
	leanify::leanify
	lodepng::lodepng
	miniz::miniz
	optipng::optipng
	zlib::zlib
	zopfli::zopfli
	jpeg-static)

# mozjpeg generates some header files that we need to be able to include
target_include_directories(ect
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/mozjpeg)

option(ECT_MULTITHREADING "Enable multithreaded processing support" ON)
option(ECT_MP3_SUPPORT "Enable MP3 support (not currently working)" OFF)
option(ECT_FOLDER_SUPPORT "Enable folder support (requires Boost)" OFF)

if(NOT ECT_MULTITHREADING)
	target_compile_definitions(ect
		PRIVATE
			NOMULTI=1)
else()
	find_package(Threads)
	target_link_libraries(ect
		Threads::Threads)
endif()

if(ECT_MP3_SUPPORT)
	target_compile_definitions(ect
		PRIVATE
			MP3_SUPPORTED=1)
endif()

if(ECT_FOLDER_SUPPORT)
	find_package(Boost 1.56 REQUIRED COMPONENTS filesystem)
	target_link_libraries(ect
		Boost::filesystem)
	target_compile_definitions(ect
		PRIVATE
			BOOST_SUPPORTED=1)
endif()

install(TARGETS ect RUNTIME DESTINATION {CMAKE_INSTALL_BINDIR})
