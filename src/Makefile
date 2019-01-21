CC = gcc
CXX = g++
UCFLAGS = -Ofast -std=gnu11 -fsigned-char $(CFLAGS)
UCXXFLAGS = -pthread -Ofast -std=gnu++11 -fsigned-char $(CXXFLAGS)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
INSTALL ?= install
MAKEDIR ?= install -d
CMAKE = cmake

ifeq ($(OS),Windows_NT)
	CXXFLAGS += -mno-ms-bitfields
	CMAKE += -G "MSYS Makefiles"
endif
OBJECTS = blocksplitter.o codec.o image.o lz77.o opngreduc.o squeeze.o util.o LzFind.o miniz.o
CXXSRC = support.cpp zopflipng.cpp zopfli/deflate.cpp zopfli/zopfli_gzip.cpp zopfli/katajainen.cpp \
lodepng/lodepng.cpp lodepng/lodepng_util.cpp optipng/optipng.cpp jpegtran.cpp gztools.cpp \
leanify/zip.cpp leanify/leanify.cpp

.PHONY: libpng mozjpeg deps bin all install
all: deps bin

bin: deps
	$(CC) -c $(UCFLAGS) optipng/codec.c optipng/image.c zopfli//util.c zopfli/squeeze.c zopfli/lz77.c \
	zopfli/blocksplitter.c optipng/opngreduc/opngreduc.c LzFind.c miniz/miniz.c
	$(CXX) $(UCXXFLAGS) main.cpp $(OBJECTS) $(CXXSRC) mozjpeg/libjpeg.a libpng/libpng.a libz.a -o ../ect $(LDFLAGS)
clean:
	rm -f *.o *.a zlib/*.o libpng/*.o libpng/*.a libpng/pngusr.h libpng/pnglibconf.h
	make -C mozjpeg clean
deps: libz.a libpng mozjpeg
libz.a:
	cd zlib/; \
	$(CC) $(UCFLAGS) -c adler32.c crc32.c deflate.c inffast.c inflate.c inftrees.c trees.c zutil.c gzlib.c gzread.c; \
	ar rcs ../libz.a adler32.o crc32.o deflate.o inffast.o inflate.o inftrees.o trees.o zutil.o gzlib.o gzread.o
libpng:
	cp pngusr.h libpng/pngusr.h
	make -C libpng/ -f scripts/makefile.linux-opt CC="$(CC)" CFLAGS="$(UCFLAGS) -DPNG_USER_CONFIG -Wno-macro-redefined" libpng.a
mozjpeg:
	cd mozjpeg/; \
	export CC="$(CC)"; \
	$(CMAKE) .; \
	make
install: all
	$(MAKEDIR) $(DESTDIR)$(BINDIR)
	$(INSTALL) ../ect $(DESTDIR)$(BINDIR)
