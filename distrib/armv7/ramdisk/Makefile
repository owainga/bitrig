#	$OpenBSD: Makefile,v 1.7 2013/11/01 20:35:52 jasper Exp $

REV=	${OSrev}

TOP=		${.CURDIR}/..

BSD_RD=		bsd.rd
IMAGE=		mr.fs
CBIN?=		instbin
CRUNCHCONF?=	${CBIN}.conf
CRUNCHCONFS?=	${.CURDIR}/../common/crunch.conf
LISTS=		${.CURDIR}/list
UTILS?=		${.CURDIR}/../../miniroot

MOUNT_POINT=	/mnt
MTREE=		${UTILS}/mtree.conf

VND?=		vnd0
VND_DEV=	/dev/${VND}a
VND_RDEV=	/dev/r${VND}a
VND_CRDEV=	/dev/r${VND}c
PID!=		echo $$$$

KERNADDR_OMAP=0x80300000
KERNADDR_IMX=0x10300000
KERNADDR_EXYNOS=0x40800000
KERNADDR_SUNXI=0x40800000

DISKTYPE=       rdroot84
NBLKS=		16640
# minfree, opt, b/i  trks, sects, cpg
NEWFSARGS= -m 0 -o space -i 4096

.ifndef DESTDIR
all ${IMAGE}:
	@echo setenv DESTDIR before making a ramdisk!
	@false
.else

ALLSOC=		IMX OMAP EXYNOS SUNXI
ALLBSD=
ALLBSDRD=
.for SOC in ${ALLSOC}
ALLBSD+=	bsd.${SOC}
ALLBSDRD+=	${BSD_RD}.${SOC} ${BSD_RD}.${SOC}.umg
all:	${ALLBSDRD}
.endfor

.for SOC in ${ALLSOC}
bsd.${SOC}:
	cd ${TOP}/../../sys/arch/armv7/conf && config RAMDISK-${SOC}
	cd ${TOP}/../../sys/arch/armv7/compile/RAMDISK-${SOC} && \
	    ${MAKE} clean && exec ${MAKE}
	cp ${TOP}/../../sys/arch/armv7/compile/RAMDISK-${SOC}/bsd bsd.${SOC}

${BSD_RD}.${SOC}: ${CBIN} ${IMAGE} bsd.${SOC} rdsetroot
	cp bsd.${SOC} ${BSD_RD}.${SOC}
	${.OBJDIR}/rdsetroot ${BSD_RD}.${SOC} ${IMAGE}
	mkuboot -a arm -o linux -e ${KERNADDR_${SOC}} -l ${KERNADDR_${SOC}} \
	    ${BSD_RD}.${SOC} ${BSD_RD}.${SOC}.umg
.endfor

${IMAGE}: rd_setup do_files rd_teardown

.endif

rd_setup:
	dd if=/dev/zero of=${IMAGE} bs=512 count=${NBLKS}
	vnconfig -v -c ${VND} ${IMAGE}
	disklabel -w ${VND} ${DISKTYPE}
	newfs ${NEWFSARGS} ${VND_RDEV}
	fsck ${VND_RDEV}
	mount ${VND_DEV} ${MOUNT_POINT}

rd_teardown:
	@df -i ${MOUNT_POINT}
	-umount ${MOUNT_POINT}
	-vnconfig -u ${VND}

rdsetroot:	${TOP}/../common/elfrdsetroot.c
	${HOSTCC} ${HOSTCFLAGS} -o rdsetroot \
	    ${TOP}/../common/elfrdsetroot.c ${TOP}/../common/elf32.c \
	    ${TOP}/../common/elf64.c

unconfig:
	-umount -f ${MOUNT_POINT}
	-vnconfig -u ${VND}

.PRECIOUS:	${IMAGE}

${CBIN}.mk ${CBIN}.cache ${CBIN}.c: ${CRUNCHCONF}
	crunchgen -E -D ${.CURDIR}/../../.. -L ${DESTDIR}/usr/lib ${.ALLSRC}

${CBIN}: ${CBIN}.mk ${CBIN}.cache ${CBIN}.c
	${MAKE} -f ${CBIN}.mk all

${CRUNCHCONF}: ${LISTS}
	awk -f ${UTILS}/makeconf.awk CBIN=${CBIN} ${LISTS} > ${CBIN}.conf

do_files:
	cat ${MTREE} | mtree -de -p ${MOUNT_POINT}/ -u
	TOPDIR=${TOP} CURDIR=${.CURDIR} OBJDIR=${.OBJDIR} \
	    REV=${REV} TARGDIR=${MOUNT_POINT} UTILS=${UTILS} \
	    RELEASEDIR=${RELEASEDIR} sh ${UTILS}/runlist.sh ${LISTS}
	rm ${MOUNT_POINT}/${CBIN}

clean cleandir:
	/bin/rm -f *.core ${IMAGE} ${CBIN} ${CBIN}.mk ${CBIN}.cache \
	    lib*.a lib*.olist ${CBIN}.map \
	    *.o *.lo *.c ${ALLBSD} ${ALLBSDRD} rdsetroot
	rm -f instbin.conf
	rm -rf cd-dir

.ifdef RELEASEDIR
install:
	cp ${ALLBSDRD} ${RELEASEDIR}
.endif	# RELEASEDIR

.include <bsd.obj.mk>
.include <bsd.subdir.mk>
