cmake_minimum_required(VERSION 3.8 FATAL_ERROR)
project(ect VERSION 0.9.1 LANGUAGES CXX C)

include(GNUInstallDirs)
set (CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# Check that submodules are present only if source was downloaded with git
if(EXISTS "${CMAKE_SOURCE_DIR}/../.git" AND NOT EXISTS "${CMAKE_SOURCE_DIR}/../src/libpng/README")
    message (FATAL_ERROR "Submodules are not initialized. Run \n\tgit submodule update --init --recursive\n within the repository")
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
	support.h
	mozjpeg/transupp.c)

add_executable(ect::ect ALIAS ect)

if(MINGW)
	add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-mno-ms-bitfields>)
	add_compile_options($<$<COMPILE_LANGUAGE:C>:-mno-ms-bitfields>)
endif()



# Use -Ofast in release builds
foreach(var CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO)
	if(${var} MATCHES "-O(2|3)")
		string(REGEX REPLACE "-O(2|3)" "-Ofast" ${var} "${${var}}")
	endif()
endforeach()

# Enable ZLIB_CONST for everything
add_definitions(-DZLIB_CONST)

option(ECT_MULTITHREADING "Enable multithreaded processing support" ON)
option(ECT_MP3_SUPPORT "Enable MP3 support (not currently working)" OFF)
option(ECT_FOLDER_SUPPORT "Enable folder support (requires Boost)" OFF)

add_subdirectory(leanify EXCLUDE_FROM_ALL)
add_subdirectory(lodepng EXCLUDE_FROM_ALL)
add_subdirectory(miniz EXCLUDE_FROM_ALL)
add_subdirectory(zlib EXCLUDE_FROM_ALL)
add_subdirectory(zopfli EXCLUDE_FROM_ALL)
file(COPY ${CMAKE_SOURCE_DIR}/pngusr.h DESTINATION ${CMAKE_SOURCE_DIR}/libpng/)
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

if(NOT ECT_MULTITHREADING)
	target_compile_definitions(ect
		PRIVATE
			NOMULTI=1)
else()
	find_package(Threads REQUIRED)
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

install(TARGETS ect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
