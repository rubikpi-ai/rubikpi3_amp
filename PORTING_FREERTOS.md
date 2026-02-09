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
  - Added `src/freertos/` to `core-y`.
  - Added FreeRTOS include paths to `TARGETINCLUDE`.
  - Defined `CONFIG_64BIT` and included compiler's internal include path.
  - Defined `GUEST`: Configures FreeRTOS for EL1 execution (SVC for yield, VBAR_EL1 for vectors).
  - Defined `QEMU`: Bypasses strict interrupt priority assertions in `FreeRTOS_Tick_Handler` which may fail on some GIC implementations or emulators.
- **Sub-Makefiles**:
  - Created `src/freertos/Makefile` to build core FreeRTOS files and descend into portable directories.
  - Created `src/freertos/portable/GCC/ARM_AARCH64_SRE/Makefile`.
  - Created `src/freertos/portable/MemMang/Makefile`.

## Code Integration
### Kernel Entry (`src/kernel/kernel.c`)
- Replaced the infinite loop in `kernel_main` with FreeRTOS initialization:
  - Creates demo tasks.
  - Calls `vTaskStartScheduler()`.
- Implemented `vApplicationIRQHandler(uint32_t ulICCIAR)`:
  - This function is called by the FreeRTOS assembly entry point when an IRQ occurs.
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
- Created a wrapper `src/freertos/include/stdint.h` and `stdlib.h` to satisfy FreeRTOS dependencies using the toolchain's headers.

## Verification
- The project builds successfully (`make baremetal`).
- The binary `build/rubikpi3_amp.bin` is generated.
