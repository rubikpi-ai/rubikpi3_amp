obj-y := croutine.o \
         event_groups.o \
         list.o \
         queue.o \
         stream_buffer.o \
         tasks.o \
         timers.o \
         portable/GCC/ARM_AARCH64_SRE/ \
         portable/MemMang/

freertos-includes := -I$(srctree)/src/freertos/include \
		     -I$(srctree)/src/freertos/portable/GCC/ARM_AARCH64_SRE \
		     -I$(srctree)/src/freertos

EXTRA_CFLAGS += $(freertos-includes)
EXTRA_AFLAGS += $(freertos-includes)
EXTRA_CPPFLAGS += $(freertos-includes)
