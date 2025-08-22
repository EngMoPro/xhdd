LDLIBS := $(LDLIBS) -ldialog $(shell pkg-config --libs ncursesw menuw) -lm -lrt
LDFLAGS := $(LDFLAGS) -pthread
CFLAGS := $(CFLAGS) -std=gnu99 -pthread -D_GNU_SOURCE -I./ -I./cui/ -I./libdevcheck/ -I/usr/include/dialog $(shell pkg-config --cflags ncursesw menuw)
DEBUG_CFLAGS := -g3 -ggdb3 -O0
INSTALL ?= install
DESTDIR ?= /usr/local

source_files := $(wildcard *.c) $(wildcard cui/*.c) $(wildcard libdevcheck/*.c)

XHDD: version.h $(source_files) Makefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(source_files) $(LDLIBS)

XHDD_g: version.h ${source_files} Makefile
	${CC} ${CFLAGS} ${DEBUG_CFLAGS} ${LDFLAGS} -o $@ ${source_files} ${LDLIBS}

version.h: FORCE
	./version.sh . version.h

install: XHDD
	$(INSTALL) -D XHDD $(DESTDIR)/bin/XHDD

FORCE:
