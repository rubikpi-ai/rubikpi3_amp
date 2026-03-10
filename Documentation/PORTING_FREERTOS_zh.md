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
  - 增加了 `CONFIG_FREERTOS` Kconfig 选项和 `make menuconfig` 支持。
  - 仅在 `CONFIG_FREERTOS=y` 时才把 `src/freertos/` 加入 `core-y`。
  - 当工程里还没有 `.config` 时，保持默认启用 FreeRTOS 的历史行为。
  - `conf`/`mconf` 现在由仓库内置的 `scripts/kconfig` 源码构建，不再复用 `$(LINUX_KDIR)/scripts/kconfig`。
  - 定义了 `CONFIG_64BIT` 宏，并包含了交叉编译器的系统头文件路径 (用于 `stddef.h`, `stdint.h`)。
- **子 Makefile**:
  - 更新了 `src/freertos/Makefile`，让 FreeRTOS 头文件路径只作用于 FreeRTOS 相关对象。
  - 更新了 `src/kernel/Makefile`，仅在启用 FreeRTOS 时向 `kernel.c` 传递 `CONFIG_FREERTOS` 宏和 FreeRTOS 头文件路径。
  - 创建了 `src/freertos/portable/GCC/ARM_AARCH64_SRE/Makefile`。
  - 创建了 `src/freertos/portable/MemMang/Makefile`。

## 代码集成
### 内核入口 (`src/kernel/kernel.c`)
- `kernel_main` 现在仍然只负责通用硬件初始化，运行时分支通过同一个文件中的 `#ifdef CONFIG_FREERTOS` 完成。
- 当 `CONFIG_FREERTOS=y` 时，`kernel.c` 会：
  - 创建演示任务；
  - 初始化 FreeRTOS 端口需要的本地 GIC/定时器状态；
  - 调用 `vTaskStartScheduler()`。
- 当关闭 `CONFIG_FREERTOS` 时，`kernel.c` 会进入一个简单的裸机轮询循环。
- `vApplicationIRQHandler(uint32_t ulICCIAR)` 现在也放在 `kernel.c` 的 `CONFIG_FREERTOS` 分支内：
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
- 增加了工程级包装头文件 (`include/stdint.h`、`include/stdlib.h`)，确保关闭 FreeRTOS 后标准整数类型头文件依然可用。

## 配置方法

```bash
# 生成默认配置
make defconfig

# 通过菜单启用/关闭 FreeRTOS
make menuconfig
```

- 配置结果保存在工程根目录的 `.config` 中。
- `make menuconfig` 不再依赖外部 Linux 源码树。
- `CONFIG_FREERTOS=y` 时启用 FreeRTOS 及调度器启动流程。
- 关闭 `CONFIG_FREERTOS` 后，固件仍可构建，只是不再拉入 FreeRTOS 源码。

## 验证
- 默认配置下执行 `make baremetal` 可成功构建项目。
- 可通过 `make menuconfig` 修改 `.config` 中的 `CONFIG_FREERTOS`。
