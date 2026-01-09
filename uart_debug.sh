#!/bin/sh

echo on > /sys/bus/platform/devices/988000.serial/power/control
stty -F /dev/ttyHS1 ispeed 115200 ospeed 115200

cd /root
insmod amp.ko
echo 0 > /sys/devices/system/cpu/cpu7/online
cat rubikpi3_amp.bin > /dev/ampcpu7
echo 1 > /sys/kernel/debug/ampcpu7/start
