CC = clang
CXX = clang++
CFLAGS = -O3 -std=gnu11 -Wall -Wextra
CXXFLAGS= -O3 -std=gnu++11 -Wall -Wextra

.PHONY:
all: bin

bin:
	$(CC) -c $(CFLAGS) util.c squeeze.c lz77.c blocksplitter.c LzFind.c
	$(CXX) -c $(CXXFLAGS) deflate.cpp zopfli_gzip.cpp katajainen.cpp
	ar rcs libzopfli2.a blocksplitter.o lz77.o squeeze.o util.o LzFind.o deflate.o zopfli_gzip.o katajainen.o