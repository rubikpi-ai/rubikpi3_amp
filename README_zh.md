# RubikPi3 AMP (非对称多处理)

[English](README.md)

QCS6490 平台裸机程序，支持在 Linux 运行的同时，将一个或多个 CPU 核心用于运行裸机代码。

## 功能特性

- **多核支持**: 支持指定一个或多个 CPU 核心运行裸机程序
- **PSCI 接口**: 通过 PSCI CPU_ON/CPU_OFF 管理 CPU 核心
- **外设驱动**:
  - UART (GENI Serial Engine)
  - I2C (GENI Serial Engine)
  - SPI (GENI Serial Engine)
  - GPIO (TLMM)
  - Clock (GCC)
- **系统功能**:
  - GICv3 中断控制器
  - MMU 内存管理
  - 异常处理与详细信息打印
  - 共享内存通信

## 目录结构

```
rubikpi3_amp/
├── src/                    # 裸机源码
│   ├── arch/arm64/         # ARM64 架构相关
│   │   ├── boot.S          # 启动代码
│   │   ├── enter.S         # 异常入口
│   │   ├── mmu.c           # MMU 配置
│   │   ├── gic_v3.c        # GICv3 驱动
│   │   └── linker.ld       # 链接脚本
│   ├── kernel/             # 内核功能
│   │   ├── kernel.c        # 主函数
│   │   ├── irq.c           # 中断处理
│   │   ├── timer.c         # 定时器
│   │   └── printk.c        # 打印输出
│   ├── drivers/            # 外设驱动
│   │   ├── geni/           # GENI SE 公共模块
│   │   ├── uart/           # UART 驱动
│   │   ├── i2c/            # I2C 驱动
│   │   ├── spi/            # SPI 驱动
│   │   ├── gpio/           # GPIO 驱动
│   │   └── clk/            # 时钟驱动
│   └── lib/                # 库函数
├── include/                # 头文件
├── linux_modules/          # Linux 内核模块
│   └── amp/                # AMP 加载模块
├── tools/                  # 工具
│   └── md/                 # 内存 dump 工具
├── build/                  # 构建输出目录
└── scripts/                # 构建脚本
```

## 编译

### 依赖

- ARM64 交叉编译工具链: `aarch64-linux-gnu-gcc`
- Linux 内核源码 (用于编译内核模块)

### 构建命令

```bash
# 构建全部 (裸机 + 内核模块 + 工具)
make

# 只构建裸机固件
make baremetal

# 只构建内核模块
make modules

# 只构建工具
make tools

# 清理
make clean

# 查看帮助
make help
```

### 构建产物

```
build/
├── rubikpi3_amp.bin        # 裸机固件
├── rubikpi3_amp.elf        # ELF 文件 (用于调试)
├── rubikpi3_amp.map        # 链接映射
├── linux_modules/amp/
│   └── amp.ko              # Linux 内核模块
└── tools/md/
    └── md.q                # 内存 dump 工具
```

## 使用方法

### 1. 部署固件

将 `build/rubikpi3_amp.bin` 复制到目标设备的 `/lib/firmware/` 目录:

```bash
scp build/rubikpi3_amp.bin root@<device>:/lib/firmware/
```

### 2. 加载内核模块

```bash
# 复制内核模块到设备
scp build/linux_modules/amp/amp.ko root@<device>:/tmp/

# 在设备上加载模块
ssh root@<device>

# 使用默认 CPU7
insmod /tmp/amp.ko

# 或指定单个 CPU
insmod /tmp/amp.ko target_cpus=7

# 或指定多个 CPU
insmod /tmp/amp.ko target_cpus=6,7
```

### 3. 查看状态

```bash
# 查看 debugfs 状态
cat /sys/kernel/debug/ampcpu/status

# 查看内核日志
dmesg | grep ampcpu
```

### 4. 控制命令

```bash
# 停止裸机程序并归还 CPU
echo 1 > /sys/kernel/debug/ampcpu/stop

# 重新启动裸机程序
echo 1 > /sys/kernel/debug/ampcpu/start

# 发送复位命令
echo 1 > /sys/kernel/debug/ampcpu/reset
```

### 5. 卸载模块

```bash
# 卸载时自动停止裸机程序并归还 CPU 给 Linux
rmmod amp
```

## 内存布局

| 地址范围 | 大小 | 用途 |
|---------|------|-----|
| 0xD0800000 - 0xD7BFFFFF | 116 MB | 代码和数据区 |
| 0xD7C00000 - 0xD7FFFFFF | 4 MB | 共享内存区 |
| 0xD8000000 - 0xD87FEFFF | ~8 MB | 栈区 |

## 串口调试

裸机程序默认使用 UART2 (SE2) 输出调试信息:

```bash
# 在主机上使用 minicom 或其他串口工具
./uart_debug.sh
# 或
minicom -D /dev/ttyUSB0 -b 115200
```

## 工具使用

### md.q - 内存 Dump 工具

用于在 Linux 端查看物理内存内容:

```bash
# 复制到设备
scp build/tools/md/md.q root@<device>:/tmp/

# 查看共享内存 (64 个 64 位字)
./md.q 0xD7C00000 64

# 查看指定地址
./md.q <物理地址> [字数]
```

## 开发指南

### 添加新驱动

1. 在 `src/drivers/` 下创建新目录
2. 添加源文件和 Makefile
3. 在 `src/drivers/Makefile` 中添加子目录
4. 在 `include/` 下添加头文件

### 修改内存布局

编辑以下文件中的地址定义:
- `src/arch/arm64/linker.ld`
- `linux_modules/amp/amp.c`

### 调试异常

裸机程序会在异常发生时打印详细信息:
- ESR_EL1: 异常综合寄存器
- FAR_EL1: 错误地址寄存器
- ELR_EL1: 异常链接寄存器
- 通用寄存器 X0-X30
- 异常类型解码

## 参考资料

- [ARM Architecture Reference Manual](https://developer.arm.com/documentation/ddi0487/latest)
- [PSCI Specification](https://developer.arm.com/documentation/den0022/latest)
- [QCS6490 Technical Reference Manual](https://www.qualcomm.com/)

## 许可证

GPL-2.0
