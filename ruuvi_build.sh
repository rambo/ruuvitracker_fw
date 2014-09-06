#!/bin/bash
git submodule init
git submodule update
if [ ! -d ChibiOS/ext/fatfs ]
then
    CUR_PWD=`pwd`
    cd ChibiOS/ext/
    unzip fatfs-*.zip
    cd $CUR_PWD
fi
grep -q 'swreset.patch applied' ChibiOS/os/ports/GCC/ARMCMx/crt0.c || patch ChibiOS/os/ports/GCC/ARMCMx/crt0.c swreset.patch

make

