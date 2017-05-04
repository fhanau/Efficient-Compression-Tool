CC = gcc
CXX = g++
CFLAGS = -Ofast -std=gnu11
CXXFLAGS= -pthread -Ofast -std=gnu++11

ifeq ($(OS),Windows_NT)
	CXXFLAGS += -mno-ms-bitfields
endif
OBJECTS = blocksplitter.o codec.o image.o lz77.o opngreduc.o squeeze.o util.o LzFind.o miniz.o
CXXSRC = support.cpp zopflipng.cpp zopfli/deflate.cpp zopfli/zopfli_gzip.cpp zopfli/katajainen.cpp \
lodepng/lodepng.cpp lodepng/lodepng_util.cpp optipng/optipng.cpp jpegtran.cpp gztools.cpp \
leanify/zip.cpp leanify/leanify.cpp

.PHONY: zlib libpng mozjpeg deps bin all
all: deps bin

bin:
	$(CC) -c $(CFLAGS) optipng/codec.c optipng/image.c zopfli//util.c zopfli/squeeze.c zopfli/lz77.c \
	zopfli/blocksplitter.c optipng/opngreduc/opngreduc.c LzFind.c miniz/miniz.c
	$(CXX) $(CXXFLAGS) main.cpp $(OBJECTS) $(CXXSRC) mozjpeg/.libs/libjpeg.a libpng/libpng.a zlib/libz.a -o ../ECT
clean:
	rm -f *.o zlib/*.o zlib/*.a libpng/*.o libpng/*.a
deps: zlib libpng mozjpeg
zlib:
	cd zlib/; \
	$(CC) $(CFLAGS) -c adler32.c crc32.c deflate.c inffast.c inflate.c inftrees.c trees.c zutil.c gzlib.c gzread.c; \
	ar rcs libz.a adler32.o crc32.o deflate.o inffast.o inflate.o inftrees.o trees.o zutil.o gzlib.o gzread.o
libpng:
	cd libpng/; \
	$(CC) -c -I../zlib $(CFLAGS) -DPNG_USE_ABS png.c pngerror.c pngget.c pngmem.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwutil.c; \
	ar rcs libpng.a png.o pngerror.o pngget.o pngmem.o pngread.o pngrio.o pngrtran.o pngrutil.o pngset.o pngtrans.o pngwio.o pngwrite.o pngwutil.o
mozjpeg:
	cd mozjpeg/; \
	./configure --disable-dependency-tracking --disable-shared --without-turbojpeg --without-java CC="$(CC)" CFLAGS="$(CFLAGS)"; \
	make
