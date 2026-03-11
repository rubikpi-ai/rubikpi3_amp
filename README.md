# RubikPi3 AMP (Asymmetric Multi-Processing)

[中文文档](README_zh.md)

Baremetal firmware for QCS6490 platform, enabling one or more CPU cores to run baremetal code while Linux runs on the remaining cores.

## Features

- **Multi-core Support**: Run baremetal on one or multiple CPU cores
- **PSCI Interface**: Manage CPU cores via PSCI CPU_ON/CPU_OFF
- **Peripheral Drivers**:
  - UART (GENI Serial Engine)
  - I2C (GENI Serial Engine)
  - SPI (GENI Serial Engine)
  - GPIO (TLMM)
  - Clock (GCC)
- **System Features**:
  - GICv3 Interrupt Controller
  - MMU Memory Management
  - Exception Handling with Detailed Output
  - Shared Memory Communication

## Directory Structure

```
rubikpi3_amp/
├── src/                    # Baremetal source code
│   ├── arch/arm64/         # ARM64 architecture
│   │   ├── boot.S          # Boot code
│   │   ├── enter.S         # Exception vectors
│   │   ├── mmu.c           # MMU configuration
│   │   ├── gic_v3.c        # GICv3 driver
│   │   └── linker.ld       # Linker script
│   ├── kernel/             # Kernel features
│   │   ├── kernel.c        # Main function
│   │   ├── irq.c           # Interrupt handling
│   │   ├── timer.c         # Timer
│   │   └── printk.c        # Print output
│   ├── drivers/            # Peripheral drivers
│   │   ├── geni/           # GENI SE common module
│   │   ├── uart/           # UART driver
│   │   ├── i2c/            # I2C driver
│   │   ├── spi/            # SPI driver
│   │   ├── gpio/           # GPIO driver
│   │   └── clk/            # Clock driver
│   └── lib/                # Library functions
├── include/                # Header files
├── linux_modules/          # Linux kernel modules
│   └── amp/                # AMP loader module
├── tools/                  # Tools
│   └── md/                 # Memory dump tool
├── build/                  # Build output directory
└── scripts/                # Build scripts
```

## Building

### Prerequisites

- ARM64 cross-compiler toolchain: `aarch64-linux-gnu-gcc`
- Linux kernel source (for building kernel module only)
- `ncurses` development package (for `make menuconfig`)
- `flex` and `bison` (for building the local Kconfig host tools)

### Build Commands

```bash
# Generate a persistent default .config (optional; plain `make` still defaults
# to FreeRTOS-enabled builds if .config is absent)
make defconfig

# Edit .config with a menu interface
make menuconfig

# Default build: baremetal firmware only
make

# Build only baremetal firmware
make baremetal

# Build baremetal + kernel module + tools
make full

# Build only kernel module
make modules LINUX_KDIR=/path/to/linux

# Build only tools
make tools

# Clean
make clean

# Show help
make help
```

### Build Output

```
build/
├── kconfig/                # Host-side Kconfig helpers for menuconfig
├── rubikpi3_amp.bin        # Baremetal firmware
├── rubikpi3_amp.elf        # ELF file (for debugging)
├── rubikpi3_amp.map        # Link map
├── tools/md/
│   └── md.q                # Memory dump tool (only when `make tools`/`make full`)
└── linux_modules/amp/
    └── amp.ko              # Linux kernel module (only when `make modules`/`make full`)
```

### Configuration

- `make menuconfig` updates the project-root `.config`.
- `make menuconfig` is self-contained in this repository and no longer reuses `$(LINUX_KDIR)/scripts/kconfig`.
- `CONFIG_FREERTOS=y` builds and starts the FreeRTOS scheduler.
- If `CONFIG_FREERTOS` is disabled, the firmware still builds and enters a simple bare-metal polling loop after hardware initialization.
- If no `.config` exists, the build keeps the historical default behavior: FreeRTOS remains enabled.
- `make` builds only the baremetal firmware by default; `linux_modules/` and `tools/` are opt-in via `make modules`, `make tools`, or `make full`.
- `make modules` uses `LINUX_KDIR=/path/to/linux` when provided; otherwise it falls back to `/lib/modules/$(uname -r)/build`.

## Usage

### 1. Deploy Firmware

Copy `build/rubikpi3_amp.bin` to the target device's `/lib/firmware/` directory:

```bash
scp build/rubikpi3_amp.bin root@<device>:/lib/firmware/
```

### 2. Load Kernel Module

```bash
# Build the kernel module first
make modules LINUX_KDIR=/path/to/linux

# Copy kernel module to device
scp build/linux_modules/amp/amp.ko root@<device>:/tmp/

# On the device, load module
ssh root@<device>

# Use default CPU7
insmod /tmp/amp.ko

# Or specify a single CPU
insmod /tmp/amp.ko target_cpus=7

# Or specify multiple CPUs
insmod /tmp/amp.ko target_cpus=6,7
```

### 3. Check Status

```bash
# View debugfs status
cat /sys/kernel/debug/ampcpu/status

# View kernel log
dmesg | grep ampcpu
```

### 4. Control Commands

```bash
# Stop baremetal and return CPU to Linux
echo 1 > /sys/kernel/debug/ampcpu/stop

# Restart baremetal
echo 1 > /sys/kernel/debug/ampcpu/start

# Send reset command
echo 1 > /sys/kernel/debug/ampcpu/reset
```

### 5. Unload Module

```bash
# Automatically stops baremetal and returns CPU to Linux
rmmod amp
```

## Memory Layout

| Address Range | Size | Purpose |
|--------------|------|---------|
| 0xD0800000 - 0xD7BFFFFF | 116 MB | Code and Data |
| 0xD7C00000 - 0xD7FFFFFF | 4 MB | Shared Memory |
| 0xD8000000 - 0xD87FEFFF | ~8 MB | Stack |

## Serial Debugging

The baremetal firmware uses UART2 (SE2) for debug output by default:

```bash
# Use minicom or other serial tools on host
minicom -D /dev/ttyUSB0 -b 115200
```

## Tools

### md.q - Memory Dump Tool

View physical memory contents from Linux:

```bash
# Build the tool first
make tools

# Copy to device
scp build/tools/md/md.q root@<device>:/tmp/

# View shared memory (64 64-bit words)
./md.q 0xD7C00000 64

# View specified address
./md.q <physical_address> [word_count]
```

## Development Guide

### Adding New Drivers

1. Create a new directory under `src/drivers/`
2. Add source files and Makefile
3. Add subdirectory to `src/drivers/Makefile`
4. Add header files under `include/`

### Modifying Memory Layout

Edit address definitions in:
- `src/arch/arm64/linker.ld`
- `linux_modules/amp/amp.c`

### Debugging Exceptions

The baremetal firmware prints detailed information on exceptions:
- ESR_EL1: Exception Syndrome Register
- FAR_EL1: Fault Address Register
- ELR_EL1: Exception Link Register
- General registers X0-X30
- Exception type decoding

## References

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [PSCI Specification](https://developer.arm.com/documentation/den0022/latest)
