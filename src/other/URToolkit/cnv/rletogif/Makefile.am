
bin_PROGRAMS = rletogif

rletogif_SOURCES = \
	compgif.c \
	gifencod.c \
	rletogif.c

noinst_HEADERS = rletogif.h

rletogif_LDADD = ${RLE}
rletogif_CFLAGS = ${RLE_CPPFLAGS} ${NOWARN}

include $(top_srcdir)/misc/Makefile.defs

