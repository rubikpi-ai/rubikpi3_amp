#!/bin/sh

cd /root
insmod amp.ko
echo 0 > /sys/devices/system/cpu/cpu7/online
cat rubikpi3_amp.bin > /dev/ampcpu7
echo 1 > /sys/kernel/debug/ampcpu7/start
