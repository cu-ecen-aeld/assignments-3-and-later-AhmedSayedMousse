#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=${OUTDIR:=/tmp/aeld}
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-
CONFIG_PREFIX="${OUTDIR}/rootfs"
CROSS_COMPILE_BUSY=$(dirname $(which "aarch64-none-linux-gnu-gcc"))/aarch64-none-linux-gnu-
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}
	echo "Fixing the yalloc issue in this version of kernel"
	sed -i 's/^YYLTYPE yylloc/extern YYLTYPE yylloc/g' ${OUTDIR}/linux-stable/scripts/dtc/dtc-lexer.l
    # TODO: Add your kernel build steps here

    	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
	make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs

fi

echo "Adding the Image in outdir"
	cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/Image
echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
	mkdir ${OUTDIR}/rootfs
	cd ${OUTDIR}/rootfs
	mkdir bin dev etc home lib lib64 proc sbin sys tmp usr var conf
	mkdir usr/bin usr/lib usr/sbin
	mkdir -p var/log

	 
cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox 
	make distclean
	make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
  
   # mkdir -pv ${OUTDIR}/rootfs/bin/busybox
sudo make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE_BUSY}
sudo make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE_BUSY} CONFIG_PREFIX=${CONFIG_PREFIX} install

# adding the modules
    #make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} INSTALL_MOD_PATH="${OUTDIR}/rootfs" modules_install

cd "${OUTDIR}/rootfs"
echo "Library dependencies"
${CROSS_COMPILE}readelf -a bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a bin/busybox | grep "Shared library"
#ls -l "${SYSROOT}/lib/ld-linux-aarch64.so.1"
# TODO: Add library dependencies to rootfs
	cp  ${SYSROOT}/lib/ld-linux-aarch64.so.1 ${OUTDIR}/rootfs/lib	
	cp  ${SYSROOT}/lib64/ld-2.31.so ${OUTDIR}/rootfs/lib64		
	cp  ${SYSROOT}/lib64/libc.so.6 ${OUTDIR}/rootfs/lib64	
	cp  ${SYSROOT}/lib64/libc-2.31.so ${OUTDIR}/rootfs/lib64		
	cp  ${SYSROOT}/lib64/libm.so.6 ${OUTDIR}/rootfs/lib64	 
	cp  ${SYSROOT}/lib64/libm-2.31.so ${OUTDIR}/rootfs/lib64		
	cp  ${SYSROOT}/lib64/libresolv.so.2 ${OUTDIR}/rootfs/lib64	
	cp  ${SYSROOT}/lib64/libresolv-2.31.so ${OUTDIR}/rootfs/lib64	 
# TODO: Make device nodes
	sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
	sudo mknod -m 600 ${OUTDIR}/rootfs/dev/console c 5 1
# TODO: Clean and build the writer utility
	cd "${FINDER_APP_DIR}"
	make clean
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} writer
# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
	cp -r ../conf/* ${OUTDIR}/rootfs/conf
	cp -a conf	${OUTDIR}/rootfs/home/
	cp finder.sh ${OUTDIR}/rootfs/home/
	cp finder-test.sh ${OUTDIR}/rootfs/home/
	cp autorun-qemu.sh ${OUTDIR}/rootfs/home/
	cp writer ${OUTDIR}/rootfs/home/
# TODO: Chown the root directory
	cd ${OUTDIR}/rootfs
	sudo chown -R root:root *
# TODO: Create initramfs.cpio.gz
	find . | cpio -H newc -ov --owner root:root > ../initramfs.cpio
	cd ..
	gzip initramfs.cpio
	#mkimage ARCH=${ARCH} -O linux -T ramdisk -d initramfs.cpio.gz uRamdisk

