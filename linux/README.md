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
