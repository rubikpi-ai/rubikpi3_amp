obj-m += amp.o
KDIR:=/home/tsdl/Qualcomm/linux
PWD?=$(shell pwd)
all:
	make -C $(KDIR) M=$(PWD) modules
	echo $(PWD)
clean:
	rm -f *.ko *.o *.mod *.mod.o *.mod.c *.symvers *.order
