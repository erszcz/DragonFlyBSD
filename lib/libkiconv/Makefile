# $FreeBSD: src/lib/libkiconv/Makefile,v 1.4.8.1 2009/04/15 03:14:26 kensmith Exp $

LIB=		kiconv
SRCS=		xlat16_iconv.c xlat16_sysctl.c
SRCS+=		quirks.c
WARNS?=		3

SHLIB_MAJOR=	3

MAN=		kiconv.3

MLINKS+=	kiconv.3 kiconv_add_xlat16_cspair.3 \
		kiconv.3 kiconv_add_xlat16_cspairs.3 \
		kiconv.3 kiconv_add_xlat16_table.3

CFLAGS+=	-I${.CURDIR}/../../sys

.include <bsd.lib.mk>
