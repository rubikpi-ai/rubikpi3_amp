# RubikPi3_amp Makefile - Kbuild style
#
VERSION = 1
PATCHLEVEL = 0
SUBLEVEL = 0
EXTRAVERSION =
NAME = rubikpi3_amp

MAKEFLAGS += -rR --no-print-directory

# Build output directory
O ?= build
export O

# To put more focus on warnings, be less verbose as default
# Use 'make V=1' to see the full commands
ifdef V
  ifeq ("$(origin V)", "command line")
    KBUILD_VERBOSE = $(V)
  endif
endif
ifndef KBUILD_VERBOSE
  KBUILD_VERBOSE = 0
endif

# Beautify output
ifeq ($(KBUILD_VERBOSE),1)
  quiet =
  Q =
else
  quiet=quiet_
  Q = @
endif

export quiet Q KBUILD_VERBOSE

# We process the rest of the Makefile if this is the final invocation of make
PHONY += all
_all: all

srctree		:= $(CURDIR)
objtree		:= $(CURDIR)/$(O)

export srctree objtree

# Architecture settings
ARCH		?= arm64
CROSS_COMPILE	?= aarch64-linux-gnu-

export ARCH CROSS_COMPILE

# SHELL used by kbuild
CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)

HOSTCC       = gcc
HOSTCXX      = g++

# Decide whether to build built-in, modular, or both.
KBUILD_BUILTIN := 1

export KBUILD_BUILTIN CONFIG_SHELL

# Look for make include files relative to root of kernel src
MAKEFLAGS += --include-dir=$(srctree)

# We need some generic definitions.
include $(srctree)/scripts/Kbuild.include

# Make variables (CC, etc...)
AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc
CPP		= $(CROSS_COMPILE)cpp
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
STRIP		= $(CROSS_COMPILE)strip
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump
SIZE		= $(CROSS_COMPILE)size

# Include paths
TARGETINCLUDE   := -I$(srctree)/include \
                   -I$(srctree)/src/freertos/include \
                   -I$(srctree)/src/freertos/portable/GCC/ARM_AARCH64_SRE \
                   -I$(srctree)/src/freertos \
                   -I$(shell $(CC) -print-file-name=include)

# Compiler flags
CPPFLAGS        := -g -D__KERNEL__ -DCONFIG_64BIT -DGUEST -DQEMU $(TARGETINCLUDE)
CFLAGS          := -g -Wall -nostdlib -nostdinc -fno-builtin $(TARGETINCLUDE)
AFLAGS          := -g -D__ASSEMBLY__ $(TARGETINCLUDE)
LDFLAGS         :=
NOSTDINC_FLAGS  := -nostdinc

export VERSION PATCHLEVEL SUBLEVEL EXTRAVERSION NAME
export CONFIG_SHELL HOSTCC HOSTCXX CROSS_COMPILE
export AS LD CC CPP AR NM STRIP OBJCOPY OBJDUMP SIZE MAKE AWK
export CPPFLAGS CFLAGS AFLAGS LDFLAGS NOSTDINC_FLAGS TARGETINCLUDE

# Files to ignore in find ... statements
RCS_FIND_IGNORE := \( -name SCCS -o -name BitKeeper -o -name .svn -o -name CVS -o -name .pc -o -name .hg -o -name .git \) -prune -o
export RCS_TAR_IGNORE := --exclude SCCS --exclude BitKeeper --exclude .svn --exclude CVS --exclude .pc --exclude .hg --exclude .git

# ===========================================================================
# Rules shared between *config targets and build targets

PHONY += scripts_basic
scripts_basic:
	@:

# ===========================================================================
# Build targets only

# Source directories (relative to srctree)
head-y		:= src/arch/arm64/boot.o
core-y		:= src/arch/arm64/
core-y		+= src/kernel/
core-y		+= src/lib/
core-y		+= src/freertos/
drivers-y	:= src/drivers/

# The all: target is the default when no target is given on the command line.
all: $(O)/rubikpi3_amp.bin modules tools

# Build only baremetal firmware
baremetal: $(O)/rubikpi3_amp.bin

KERNELRELEASE = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)
KERNELVERSION = $(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)

export KERNELRELEASE KERNELVERSION

# Default kernel image to build
export KBUILD_IMAGE ?= $(O)/rubikpi3_amp.bin

# Source directories to visit
rubikpi3_amp-dirs	:= $(patsubst %/,%,$(filter %/, $(core-y) $(drivers-y)))

rubikpi3_amp-alldirs	:= $(sort $(rubikpi3_amp-dirs))

# Convert source dirs to output object paths
# src/arch/arm64/ -> $(O)/src/arch/arm64/built-in.o
head-y		:= $(addprefix $(O)/,$(head-y))
core-y		:= $(patsubst %/, $(O)/%/built-in.o, $(core-y))
drivers-y	:= $(patsubst %/, $(O)/%/built-in.o, $(drivers-y))

# Build kernel
# ---------------------------------------------------------------------------
rubikpi3_amp-init := $(head-y)
rubikpi3_amp-main := $(core-y) $(drivers-y)
rubikpi3_amp-all  := $(rubikpi3_amp-init) $(rubikpi3_amp-main)
rubikpi3_amp-lds  := $(srctree)/src/arch/arm64/linker.ld

export KBUILD_OBJS := $(rubikpi3_amp-all)

# The finally linked kernel.
quiet_cmd_link_rubikpi3_amp = LD      $@
      cmd_link_rubikpi3_amp = $(LD) $(LDFLAGS) -o $@ \
	-T $(rubikpi3_amp-lds) -Map $(O)/rubikpi3_amp.map --cref --print-memory-usage \
	$(rubikpi3_amp-init) \
	--start-group $(rubikpi3_amp-main) --end-group \
	-e _start

quiet_cmd_objcopy_bin = OBJCOPY $@
      cmd_objcopy_bin = $(OBJCOPY) -O binary $< $@

# Print memory usage from ELF
quiet_cmd_size = SIZE    $@
      cmd_size = $(SIZE) -A -d $@ | awk ' \
	BEGIN { total=0; } \
	/^\.text/ { text=$$2; total+=$$2; } \
	/^\.rodata/ { rodata=$$2; total+=$$2; } \
	/^\.data/ { data=$$2; total+=$$2; } \
	/^\.bss/ { bss=$$2; total+=$$2; } \
	END { \
		printf "  Memory Usage:\n"; \
		printf "    .text   : %8d bytes\n", text; \
		printf "    .rodata : %8d bytes\n", rodata; \
		printf "    .data   : %8d bytes\n", data; \
		printf "    .bss    : %8d bytes\n", bss; \
		printf "    --------------------------------\n"; \
		printf "    Total   : %8d bytes (%.2f KB)\n", total, total/1024; \
	}'

$(O)/rubikpi3_amp.elf: $(rubikpi3_amp-lds) $(rubikpi3_amp-all) FORCE
	$(Q)mkdir -p $(O)
	$(call cmd,link_rubikpi3_amp)
	@echo ""
	$(call cmd,size)

$(O)/rubikpi3_amp.bin: $(O)/rubikpi3_amp.elf FORCE
	$(call cmd,objcopy_bin)
	@echo ""
	@printf "  Image: $@ (%d bytes)\n" $$(stat -c %s $@)

# The actual objects are generated when descending,
# make sure no implicit rule kicks in
$(sort $(rubikpi3_amp-init) $(rubikpi3_amp-main)): $(rubikpi3_amp-dirs) ;

# Handle descending into subdirectories listed in $(rubikpi3_amp-dirs)
# Pass both src= and obj= to Makefile.build

PHONY += $(rubikpi3_amp-dirs)
$(rubikpi3_amp-dirs): scripts_basic
	$(Q)$(MAKE) $(build)=$(O)/$@ src=$@

###
# Cleaning is done on three levels.
# make clean     Delete most generated files

# Shorthand for $(Q)$(MAKE) -f scripts/Makefile.clean obj=dir
clean := -f $(if $(KBUILD_SRC),$(srctree)/)scripts/Makefile.clean obj

# clean - Delete most generated files
clean: rm-dirs  := $(O)
clean: rm-files :=

PHONY += clean
clean: modules_clean tools_clean
	$(call cmd,rmdirs)
	@find . $(RCS_FIND_IGNORE) \
		\( -name '*.[oas]' -o -name '.*.cmd' \
		-o -name '.*.d' -o -name '.*.tmp' \) \
		-type f -print | xargs rm -f
	@echo "  CLEAN   $(O)"

# Show version information
PHONY += version
version:
	@echo "RubikPi3_amp Kernel Version $(KERNELRELEASE)"
	@echo "Built with ARM GNU Toolchain $(CROSS_COMPILE)"

# Show build information
PHONY += info
info:
	@echo "Version: $(KERNELRELEASE)"
	@echo "Source tree: $(srctree)"
	@echo "Build output: $(O)"
	@echo "Objects to build:"
	@for obj in $(rubikpi3_amp-all); do echo "  $$obj"; done

# Help target
PHONY += help
help:
	@echo 'Cleaning targets:'
	@echo '  clean          - Remove all generated files'
	@echo ''
	@echo 'Build targets:'
	@echo '  all            - Build everything (baremetal + modules + tools)'
	@echo '  baremetal      - Build only $(O)/rubikpi3_amp.bin'
	@echo '  modules        - Build Linux kernel module ($(O)/linux_modules/amp/amp.ko)'
	@echo '  tools          - Build host tools ($(O)/tools/md/md.q)'
	@echo ''
	@echo 'Other targets:'
	@echo '  version        - Show version information'
	@echo '  info           - Show build information'
	@echo '  help           - Show this help'
	@echo ''
	@echo 'Options:'
	@echo '  make V=0|1     - 0 => quiet build (default), 1 => verbose build'
	@echo '  make V=2       - 2 => give reason for rebuild of target'
	@echo '  make O=dir     - Build output in dir (default: build)'

# ===========================================================================
# Linux kernel module build
# ===========================================================================
LINUX_KDIR ?= /home/tsdl/QCOM/linux

PHONY += modules
modules:
	@echo "  Building Linux kernel module..."
	$(Q)$(MAKE) -C $(srctree)/linux_modules/amp KDIR=$(LINUX_KDIR) BUILD_DIR=$(objtree)/linux_modules/amp
	@echo "  Module built: $(O)/linux_modules/amp/amp.ko"

PHONY += modules_clean
modules_clean:
	-$(Q)$(MAKE) -C $(srctree)/linux_modules/amp clean BUILD_DIR=$(objtree)/linux_modules/amp

# ===========================================================================
# Tools build
# ===========================================================================
PHONY += tools
tools:
	@echo "  Building tools..."
	$(Q)$(MAKE) -C $(srctree)/tools/md ARMGNU=$(patsubst %-,%,$(CROSS_COMPILE)) BUILD_DIR=$(objtree)/tools/md
	@echo "  Tools built: $(O)/tools/md/md.q"

PHONY += tools_clean
tools_clean:
	-$(Q)$(MAKE) -C $(srctree)/tools/md clean BUILD_DIR=$(objtree)/tools/md

# FIXME Should go into a make.lib or something
# ===========================================================================

quiet_cmd_rmdirs = $(if $(wildcard $(rm-dirs)),CLEAN   $(wildcard $(rm-dirs)))
      cmd_rmdirs = rm -rf $(rm-dirs)

quiet_cmd_rmfiles = $(if $(wildcard $(rm-files)),CLEAN   $(wildcard $(rm-files)))
      cmd_rmfiles = rm -f $(rm-files)

# read all saved command lines
targets := $(wildcard $(sort $(targets)))
cmd_files := $(wildcard .*.cmd $(foreach f,$(targets),$(dir $(f)).$(notdir $(f)).cmd))

ifneq ($(cmd_files),)
  $(cmd_files): ;	# Do not try to update included dependency files
  include $(cmd_files)
endif

PHONY += FORCE
FORCE:

# Cancel implicit rules on top Makefile
Makefile: ;

# Declare the contents of the .PHONY variable as phony.
.PHONY: $(PHONY)
