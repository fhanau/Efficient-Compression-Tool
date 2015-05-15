CC = gcc
CXX = g++
CFLAGS = -Ofast -DNDEBUG -std=gnu11
CXXFLAGS= -Ofast -DNDEBUG -std=gnu++11

OBJECTS = blocksplitter.o cache.o codec.o deflate.o hash.o image.o katajainen.o lz77.o opngreduc.o squeeze.o trans.o tree.o util.o
CXXSRC = support.cpp zopfli/src/zopflipng/zopflipng.cpp zopfli/src/zopfli/zopfli_gzip.cpp\
zopfli/src/zopflipng/lodepng/lodepng.cpp zopfli/src/zopflipng/lodepng/lodepng_util.cpp optipng/optipng.cpp jpegtran.cpp gztools.cpp

.PHONY: zlib libpng mozjpeg deps all
all:
	$(CC) -c $(CFLAGS) optipng/codec.c optipng/image.c optipng/trans.c zopfli/src/zopfli/zlib_container.c \
	zopfli/src/zopfli/util.c zopfli/src/zopfli/deflate.c zopfli/src/zopfli/squeeze.c zopfli/src/zopfli/lz77.c zopfli/src/zopfli/katajainen.c \
	zopfli/src/zopfli/blocksplitter.c zopfli/src/zopfli/tree.c zopfli/src/zopfli/cache.c zopfli/src/zopfli/hash.c optipng/opngreduc/opngreduc.c
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
