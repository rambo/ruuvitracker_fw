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
cp -f main.c main.c.backup && \
cp -f tracker.c main.c && \
sleep 2 && \
touch main.c && \
make
cp -f main.c.backup main.c 
sleep 2
touch main.c
echo "NOTE: Use ./ruuvi_program.sh instead of 'make flash'"
