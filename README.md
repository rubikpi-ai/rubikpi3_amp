setenv loadaddr 0xd0000000
ext4load scsi 6:1 ${loadaddr} /rubikpi3_amp.bin
ampcpu7 0xD0000000
