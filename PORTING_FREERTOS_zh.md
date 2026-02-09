# FreeRTOS 移植指南 - QCS6490 (AMP 裸机)

## 概述
本文档记录了将 FreeRTOS 移植到 QCS6490 AMP 系统裸机核心的过程。

## 源代码
- FreeRTOS 源代码已从 `temp/FreeRTOS/FreeRTOS/Source` 复制到 `src/freertos`。
- 选用的移植层是 `GCC/ARM_AARCH64_SRE` (System Register Enable)，该层支持基于系统寄存器访问 GICv3 的 ARMv8-A 架构。

## 配置
- **FreeRTOSConfig.h**: 创建于 `src/freertos/FreeRTOSConfig.h`。
  - 配置为抢占式调度 (Preemptive scheduling)。
  - 系统滴答频率 (Tick rate): 1000 Hz。
  - 堆大小 (Heap size): 40 KB。
  - 使用 `timer_cntv_start_hz` 和 `timer_cntv_reload_hz` 宏来实现滴答定时器操作。
  - `configMAX_API_CALL_INTERRUPT_PRIORITY` 设置为 18 (匹配 GICv3 的优先级逻辑)。

## 构建系统更改
- **顶级 Makefile**:
  - 将 `src/freertos/` 添加到 `core-y` 编译列表。
  - 将 FreeRTOS 的头文件路径添加到 `TARGETINCLUDE`。
  - 定义了 `CONFIG_64BIT` 宏，并包含了交叉编译器的系统头文件路径 (用于 `stddef.h`, `stdint.h`)。
- **子 Makefile**:
  - 创建了 `src/freertos/Makefile` 用于构建 FreeRTOS 核心文件并递归编译 portable 目录。
  - 创建了 `src/freertos/portable/GCC/ARM_AARCH64_SRE/Makefile`。
  - 创建了 `src/freertos/portable/MemMang/Makefile`。

## 代码集成
### 内核入口 (`src/kernel/kernel.c`)
- 修改了 `kernel_main` 函数，移除了原有的无限循环，替换为 FreeRTOS 的初始化流程：
  - 创建了一个演示任务 `vTask1` (用于翻转 GPIO 44)。
  - 调用 `vTaskStartScheduler()` 启动调度器。
- 实现了 `vApplicationIRQHandler(uint32_t ulICCIAR)` 函数：
  - 该函数由 FreeRTOS 的汇编入口在发生 IRQ 时调用。
  - 检查中断 ID 是否为 System Tick (ID 27)。
  - 如果是 Tick 中断，调用 `FreeRTOS_Tick_Handler()`。
  - 同时更新共享内存中的计数器用于调试。

### 向量表 (`src/freertos/portable/GCC/ARM_AARCH64_SRE/portASM.S`)
- 官方提供的 `portASM.S` 不包含中断向量表定义 (`_freertos_vector_table`)。
- 在 `portASM.S` 文件末尾追加了标准的 AArch64 向量表，将同步异常、IRQ 等路由到 FreeRTOS 的处理函数 (`FreeRTOS_IRQ_Handler`, `FreeRTOS_SWI_Handler`)。
- 移除了文件中位置错误的 `.end` 指令，确保新增的向量表代码能够被汇编。

### 类型定义适配
- 修改了 `include/type.h`，引入了标准头文件 (`stdint.h`, `stddef.h`) 替代原有的冲突 typedef，并添加 `#ifndef __ASSEMBLER__` 保护。
- 创建了包装头文件 `src/freertos/include/stdint.h` 和 `stdlib.h`，以便 FreeRTOS 能正确引用编译工具链的标准库头文件。

## 验证
- 执行 `make baremetal` 可成功构建项目。
- 编译生成的二进制文件位于 `build/rubikpi3_amp.bin`。
