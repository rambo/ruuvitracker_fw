#!/bin/bash

function wait_for_dfu() {
    while ! dfu-util -l |grep "Found DFU">/dev/null;do
	sleep 0.1
    done
}

BINARY=build/ruuvitracker.bin

echo "Waiting for DFU device..."
wait_for_dfu

dfu-util -i 0 -a 0 -D $BINARY -s 0x08000000:leave

