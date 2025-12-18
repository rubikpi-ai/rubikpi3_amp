#!/bin/sh

../qtestsign/qtestsign.py -v6 aboot -o u-boot.mbn u-boot.elf
cp u-boot.mbn qdl/
sudo qdl qdl/prog_firehose_ddr.elf qdl/rawprogram*.xml qdl/patch*.xml
# fastboot flash uefi_a u-boot.mbn
# fastboot reboot
