#	@(#)Makefile	8.2 (Berkeley) 4/2/94
# $FreeBSD: src/usr.bin/chpass/Makefile,v 1.30 2004/12/29 08:13:20 ache Exp $
# $DragonFly: src/usr.bin/chpass/Makefile,v 1.4 2007/08/27 16:50:52 pavalos Exp $

.PATH: ${.CURDIR}/../../usr.sbin/pwd_mkdb ${.CURDIR}/../../lib/libc/gen

PROG=	chpass
SRCS=	chpass.c edit.c field.c pw_scan.c table.c util.c
BINOWN=	root
BINMODE=4555
.if !defined(NO_NIS)
CFLAGS+= -DYP
.endif
#Some people need this, uncomment to activate
#CFLAGS+=-DRESTRICT_FULLNAME_CHANGE
CFLAGS+=-I${.CURDIR}/../../usr.sbin/pwd_mkdb -I${.CURDIR}/../../lib/libc/gen -I.

DPADD=	${LIBCRYPT} ${LIBUTIL}
LDADD=	-lcrypt -lutil
.if !defined(NO_NIS)
DPADD+=	${LIBYPCLNT}
LDADD+=	-lypclnt
.endif

LINKS=	${BINDIR}/chpass ${BINDIR}/chfn
LINKS+=	${BINDIR}/chpass ${BINDIR}/chsh
.if !defined(NO_NIS)
LINKS+=	${BINDIR}/chpass ${BINDIR}/ypchpass
LINKS+=	${BINDIR}/chpass ${BINDIR}/ypchfn
LINKS+=	${BINDIR}/chpass ${BINDIR}/ypchsh
.endif

MLINKS=	chpass.1 chfn.1 chpass.1 chsh.1
.if !defined(NO_NIS)
MLINKS+= chpass.1 ypchpass.1 chpass.1 ypchfn.1 chpass.1 ypchsh.1
.endif

beforeinstall:
.for i in chpass chfn chsh ypchpass ypchfn ypchsh
	[ ! -e ${DESTDIR}${BINDIR}/$i ] || \
		chflags noschg ${DESTDIR}${BINDIR}/$i || true
.endfor

.if !defined(NOFSCHG)
afterinstall:
	-chflags schg ${DESTDIR}${BINDIR}/chpass
.endif

.include <bsd.prog.mk>
