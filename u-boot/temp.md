setenv loadaddr 0xd0800000
ext4load scsi 6:1 ${loadaddr} /rubikpi3_amp.bin
ampcpu7 ${loadaddr}

0xd7c00000

echo 0 > /sys/devices/system/cpu/cpu7/online

systemctl mask sleep.target suspend.target hibernate.target hybrid-sleep.target
