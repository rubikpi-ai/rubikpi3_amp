echo 0 > /sys/devices/system/cpu/cpu7/online

insmod ampcpu7.ko

cat rubikpi3_amp.bin > /dev/ampcpu7

echo 1 > /sys/kernel/debug/ampcpu7/start

cat /sys/kernel/debug/ampcpu7/status

echo 0x10000 > /sys/kernel/debug/ampcpu7/flush
