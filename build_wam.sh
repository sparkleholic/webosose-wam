#!/bin/bash

if [ -z $WLD ]; then
	echo "install path as WLD not specified"
	exit 124
fi

export CHROMIUM_CBE=$WLD/image/usr/lib
export CHROMIUM_SRC=$WLD/image/usr/include/chromium79

export LD_LIBRARY_PATH=${CHROMIUM_CBE}:$LD_LIBRARY_PATH

cmake -G 'Ninja' -DCMAKE_MAKE_PROGRAM=ninja \
	      -DCMAKE_BUILD_TYPE=Debug \
	      -DCMAKE_INSTALL_PREFIX:PATH=$WLD \
	      -DPLATFORM_NAME=POKY_AGL \
	      -DCHROMIUM_SRC_DIR=${CHROMIUM_SRC} ..
	      #-DCHROMIUM_CBE=${CHROMIUM_CBE} \
	      #-DCMAKE_INSTALL_SO_NO_EXE=0 \
	      #-DCMAKE_NO_SYSTEM_FROM_IMPORTED=1 \
	      #-Wno-dev ..
	      #-DCMAKE_INSTALL_BINDIR:PATH=bin \
	      #-DCMAKE_INSTALL_SBINDIR:PATH=sbin \
	      #-DCMAKE_INSTALL_LIBEXECDIR:PATH=libexec \
	      #-DCMAKE_INSTALL_SYSCONFDIR:PATH=/etc \
	      #-DCMAKE_INSTALL_SHAREDSTATEDIR:PATH=../com \
	      #-DCMAKE_INSTALL_LOCALSTATEDIR:PATH=/var \
	      #-DCMAKE_INSTALL_LIBDIR:PATH=lib \
	      #-DCMAKE_INSTALL_INCLUDEDIR:PATH=include \
	      #-DCMAKE_INSTALL_DATAROOTDIR:PATH=share \
