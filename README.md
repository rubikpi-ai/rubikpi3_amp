setenv loadaddr 0xd0800000
ext4load scsi 6:1 ${loadaddr} /rubikpi3_amp.bin
ampcpu7 ${loadaddr}

0xd7c00000
