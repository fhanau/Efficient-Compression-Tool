CC = gcc
CXX = g++
CFLAGS = -Ofast -std=gnu11
CXXFLAGS= -Ofast -std=gnu++11

OBJECTS = blocksplitter.o cache.o codec.o hash.o image.o katajainen.o lz77.o opngreduc.o squeeze.o trans.o util.o
CXXSRC = support.cpp zlibWrapper.cpp zopflipng.cpp zopfli/deflate.cpp zopfli/zopfli_gzip.cpp\
lodepng/lodepng.cpp lodepng/lodepng_util.cpp optipng/optipng.cpp jpegtran.cpp gztools.cpp

.PHONY: zlib libpng mozjpeg deps bin all
all: deps bin

bin:
	$(CC) -c $(CFLAGS) optipng/codec.c optipng/image.c optipng/trans.c zopfli//util.c zopfli/squeeze.c zopfli/lz77.c zopfli/katajainen.c \
	zopfli/blocksplitter.c zopfli/cache.c zopfli/hash.c optipng/opngreduc/opngreduc.c
	$(CXX) $(CXXFLAGS) main.cpp $(OBJECTS) $(CXXSRC) mozjpeg/.libs/libjpeg.a libpng/libpng.a zlib/libz.a -o ../ECT
clean:
	rm -f *.o zlib/*.o zlib/*.a libpng*.o libpng*.a
deps: zlib libpng mozjpeg
zlib:
	cd zlib/; \
	$(CC) $(CFLAGS) -DNO_GZCOMPRESS -c adler32.c crc32.c deflate.c infback.c inffast.c inflate.c inftrees.c trees.c zutil.c gzclose.c gzlib.c gzread.c gzwrite.c; \
	ar rcs libz.a adler32.o crc32.o deflate.o infback.o inffast.o inflate.o inftrees.o trees.o zutil.o gzclose.o gzlib.o gzread.o gzwrite.o
libpng:
	cd libpng/; \
	$(CC) -c -I../zlib $(CFLAGS) png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c \
	pngwtran.c pngwutil.c; \
	ar rcs libpng.a png.o pngerror.o pngget.o pngmem.o pngpread.o pngread.o pngrio.o pngrtran.o pngrutil.o pngset.o pngtrans.o pngwio.o pngwrite.o \
	pngwtran.o pngwutil.o
mozjpeg:
	cd mozjpeg/; \
	./configure --disable-shared --without-turbojpeg CC="$(CC)" CFLAGS="$(CFLAGS)"; \
	make
