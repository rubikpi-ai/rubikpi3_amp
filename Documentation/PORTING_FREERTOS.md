# FreeRTOS Porting to QCS6490 (AMP Bare-metal)

## Overview
This document describes the process of porting FreeRTOS to the bare-metal core of the QCS6490 AMP system.

## Source Code
- The FreeRTOS source code was copied from `temp/FreeRTOS/FreeRTOS/Source` to `src/freertos`.
- The portable layer used is `GCC/ARM_AARCH64_SRE` (System Register Enable), which supports ARMv8-A with GICv3 using system registers.

## Configuration
- **FreeRTOSConfig.h**: Created in `src/freertos/FreeRTOSConfig.h`.
  - Configured for preemptive scheduling.
  - Tick rate: 1000 Hz.
  - Heap size: 40 KB.
  - Uses `timer_cntv_start_hz` and `timer_cntv_reload_hz` for the tick timer.
  - `configMAX_API_CALL_INTERRUPT_PRIORITY` set to 18 (matching GICv3 priority logic).

## Build System Changes
- **Top-level Makefile**:
  - Added `CONFIG_FREERTOS` Kconfig option and `make menuconfig` support.
  - Adds `src/freertos/` to `core-y` only when `CONFIG_FREERTOS=y`.
  - Keeps bare-metal builds FreeRTOS-enabled by default when no `.config` exists.
  - Builds `conf`/`mconf` from vendored `scripts/kconfig` sources inside this repository instead of reusing `$(LINUX_KDIR)/scripts/kconfig`.
  - Defined `CONFIG_64BIT` and included the compiler's internal include path.
  - Defined `GUEST`: Configures FreeRTOS for EL1 execution (SVC for yield, VBAR_EL1 for vectors).
  - Defined `QEMU`: Bypasses strict interrupt priority assertions in `FreeRTOS_Tick_Handler` which may fail on some GIC implementations or emulators.
- **Sub-Makefiles**:
  - Updated `src/freertos/Makefile` so FreeRTOS-specific include paths are local to FreeRTOS objects.
  - Updated `src/kernel/Makefile` to pass `CONFIG_FREERTOS` and the FreeRTOS include paths only to `kernel.c` when FreeRTOS is enabled.
  - Created `src/freertos/portable/GCC/ARM_AARCH64_SRE/Makefile`.
  - Created `src/freertos/portable/MemMang/Makefile`.

## Code Integration
### Kernel Entry (`src/kernel/kernel.c`)
- Kept `kernel_main` focused on common hardware initialization, then used `#ifdef CONFIG_FREERTOS` in the same file to choose the runtime path.
- When `CONFIG_FREERTOS=y`, `kernel.c`:
  - creates the demo tasks;
  - initializes the local GIC/timer state needed by the port;
  - calls `vTaskStartScheduler()`.
- When `CONFIG_FREERTOS` is disabled, `kernel.c` enters a simple bare-metal polling loop.
- `vApplicationIRQHandler(uint32_t ulICCIAR)` now lives in `kernel.c` under the `CONFIG_FREERTOS` path:
  - It is called by the FreeRTOS assembly entry point when an IRQ occurs.
  - Checks for the tick interrupt (ID 27).
  - Calls `FreeRTOS_Tick_Handler()` for tick interrupts.
  - Updates shared memory counters.

### Vector Table (`src/freertos/portable/GCC/ARM_AARCH64_SRE/portASM.S`)
- The default `portASM.S` did not include a vector table definition (`_freertos_vector_table`).
- Appended a standard AArch64 vector table to `portASM.S` that routes exceptions to FreeRTOS handlers (`FreeRTOS_IRQ_Handler`, `FreeRTOS_SWI_Handler`).
- Fixed the vector table entries for Current EL with SP0 (used by FreeRTOS tasks) to jump to handlers instead of infinite looping.
- Removed the misplaced `.end` directive to ensure the vector table is assembled.

### Type Definitions
- Modified `include/type.h` to include standard headers (`stdint.h`, `stddef.h`) instead of conflicting typedefs, protecting them with `#ifndef __ASSEMBLER__`.
- Added project-level wrapper headers (`include/stdint.h`, `include/stdlib.h`) so standard integer types remain available even when FreeRTOS support is disabled.

## How to Configure

```bash
# Create a persistent default config
make defconfig

# Enable/disable FreeRTOS from the menu
make menuconfig
```

- The configuration is stored in the project-root `.config`.
- `make menuconfig` no longer depends on the external Linux source tree.
- `CONFIG_FREERTOS=y` enables the FreeRTOS port and scheduler startup.
- Disabling `CONFIG_FREERTOS` keeps the firmware buildable without pulling in the FreeRTOS sources.

## Verification
- `make baremetal` builds successfully with the default configuration.
- `make menuconfig` provides a way to toggle `CONFIG_FREERTOS` from `.config`.
