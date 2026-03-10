#!/bin/sh

mkdir -p  build/temp

sudo mount rubikpi_config.img build/temp/
sudo cp rubikpi3_amp.bin build/temp/
sudo umount build/temp

sudo qdl prog_firehose_ddr.elf rawprogram6.xml patch6.xml
