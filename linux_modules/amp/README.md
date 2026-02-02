# 1) 先让 cpu7 offline（只对 Linux 逻辑态）
echo 0 > /sys/devices/system/cpu/cpu7/online

# 2) 裸机若已在跑，先 reset（让它 CPU_OFF）
echo 1 > /sys/kernel/debug/ampcpu7/reset

# 3) 写新镜像
cat rubikpi3_amp.bin > /dev/ampcpu7

# 4) start
echo 1 > /sys/kernel/debug/ampcpu7/start

# 5) 看状态
cat /sys/kernel/debug/ampcpu7/status


0xd7c00000


  insmod amp.ko                      # 默认使用 CPU 7
  insmod amp.ko target_cpus=7        # 使用 CPU 7
  insmod amp.ko target_cpus=6,7      # 使用 CPU 6 和 7
  insmod amp.ko target_cpus=4,5,6,7  # 使用 CPU 4,5,6,7
