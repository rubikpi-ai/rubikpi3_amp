obj-y := heap_4.o

freertos-includes := -I$(srctree)/src/freertos/include \
		     -I$(srctree)/src/freertos/portable/GCC/ARM_AARCH64_SRE \
		     -I$(srctree)/src/freertos

EXTRA_CFLAGS += $(freertos-includes)
EXTRA_AFLAGS += $(freertos-includes)
EXTRA_CPPFLAGS += $(freertos-includes)
