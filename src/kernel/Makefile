# src/kernel/Makefile

obj-y := kernel.o page_alloc.o printk.o timer.o irq.o

freertos-includes := -I$(srctree)/src/freertos/include \
		     -I$(srctree)/src/freertos/portable/GCC/ARM_AARCH64_SRE \
		     -I$(srctree)/src/freertos

ifeq ($(CONFIG_FREERTOS),y)
CFLAGS_kernel.o += -DCONFIG_FREERTOS $(freertos-includes)
endif
