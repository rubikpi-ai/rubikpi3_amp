#include <pgtable.h>
#include <pgtable_prot.h>
#include <pgtable_hwdef.h>
#include <sysregs.h>
#include <barrier.h>
#include <string.h>
#include <mm.h>
#include <mmu.h>
#include <page_alloc.h>
#include <gpio.h>
#include <ptregs.h>
#include <irq.h>
#include <gic_v3.h>
#include <timer.h>
#include <printk.h>
#include <uart_geni.h>
#include <clock.h>
#include <clock_qcs6490.h>
#include <i2c_geni.h>
#include <spi_geni.h>
#include "FreeRTOS.h"
#include "task.h"

#define AMP_CMD_IDX  32
#define AMP_CMD_RESET 0x52534554ULL /* 'RSET' */

extern char _shared_memory[];
extern char _stack_bottom[];

extern void FreeRTOS_Tick_Handler(void);

void vApplicationIRQHandler(uint32_t ulICCIAR)
{
	uint32_t ulInterruptID = ulICCIAR & 0x3FFUL;
	volatile u64 *shm = (volatile u64 *)SHM_BASE;

	shm[1]++;           /* total irq count */
	shm[2] = ulInterruptID;     /* last intid */

	if (ulInterruptID == 27) {
		shm[3]++;       /* timer tick count */
		FreeRTOS_Tick_Handler();
	}
}

void vTask1(void *pvParameters)
{
	(void) pvParameters;
	volatile u64 *shm = (volatile u64 *)0xD7C00000;

	printk("Task1 (GPIO Toggle) started\n");

	for (;;) {
		// Toggle GPIO 44
		gpio_direction_output(44, 0);
		vTaskDelay(pdMS_TO_TICKS(500));
		gpio_direction_output(44, 1);
		vTaskDelay(pdMS_TO_TICKS(500));

		shm[4]++;
		// printk("Task1 running... tick=%d\n", xTaskGetTickCount());
	}
}

/* Wrapper for standard sleep(seconds) behavior using FreeRTOS Delay */
void sleep(uint32_t seconds)
{
	vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
}

void vTask2(void *pvParameters)
{
	(void) pvParameters;
	int count = 0;

	printk("Task2 (Logger) started\n");

	for (;;) {
		printk("Task2: Keeping Alive... count=%d (Tick=%d)\n", count++, xTaskGetTickCount());
		sleep(1); /* Sleep for 1 second */
	}
}

void vTask3(void *pvParameters)
{
	(void) pvParameters;
	int a = 1, b = 1;

	printk("Task3 (Fibonacci) started\n");

	for (;;) {
		int next = a + b;
		printk("Task3: Fibonacci next=%d\n", next);
		a = b;
		b = next;

		if (a > 1000) { a = 1; b = 1; printk("Task3: Reset sequence\n"); }

		sleep(2); /* Sleep for 2 seconds */
	}
}

static inline unsigned long read_sctlr_el1(void)
{
	unsigned long v;
	asm volatile("mrs %0, sctlr_el1" : "=r"(v));
	return v;
}

static inline unsigned long read_currentel(void)
{
	unsigned long v;
	asm volatile("mrs %0, CurrentEL" : "=r"(v));
	return v;
}

static inline unsigned long read_sp(void) {
	unsigned long v;
	asm volatile("mov %0, sp" : "=r"(v));
	return v;
}

static inline unsigned long read_mpidr_el1(void)
{
	unsigned long v;
	__asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v));
	return v;
}

static inline void psci_cpu_off(void)
{
	register unsigned long x0 asm("x0") = 0x84000002; // CPU_OFF
	asm volatile("smc #0" : : "r"(x0) : "memory");
	while (1) asm volatile("wfi");
}

void kernel_main(void)
{
	volatile u64 *shm = (volatile u64 *)0xD7C00000;
	unsigned long *test = (unsigned long *)SHM_BASE;
	u64 gicr = 0x17b40000ULL;

	arch_local_irq_disable();
	shm[1] = read_mpidr_el1();

	mem_init(0, 0);

	paging_init();

	uart2_init();
	uart2_puts("uart2 hello rubikpi 123456\n");

//	i2c1_init(I2C_STANDARD_MODE_FREQ);
//	i2c1_write(0x50, (u8 *)"Hello I2C EEPROM", 16);

	/* 初始化 SPI12，最大速度 10MHz */
	spi12_init(SPI_50MHZ);

	/* 设置 SPI 模式 0 */
	spi12_set_mode(0, SPI_MODE_0);

	/* 发送数据 */
	u8 tx_data[] = {0x01, 0x02, 0x03};
	spi12_write(0, tx_data, sizeof(tx_data));

	///* 接收数据 */
	//u8 rx_data[4];
	//spi12_read(0, rx_data, sizeof(rx_data));

	///* 全双工传输 */
	//u8 tx[4] = {0xAA, 0xBB, 0xCC, 0xDD};
	//u8 rx[4];
	//spi12_write_read(0, tx, rx, 4);

	///* 先写后读（常用于读取寄存器） */
	//u8 cmd = 0x9F;  /* JEDEC ID 命令 */
	//u8 id[3];
	//spi12_write_then_read(0, &cmd, 1, id, 3);

	//printk("SPI12 JEDEC ID: %02X %02X %02X\n", id[0], id[1], id[2]);
	printk("SPI12 transfer done.\n");

	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

	gicv3_init_for_cpu();
	enable_ppi(gicr, 27, 0x40);
	write_cntv_ctl_el0(0);

	// for (volatile u64 i = 0; i < 1000000; i++) ;

	// arch_local_irq_enable();
	// timer_cntv_start_hz(1000);
	// for (volatile u64 i=0;i<5000000;i++);

	test[20] = 0x2020208;

	//printk("kernel_main: CPU %d, SCTLR_EL1=0x%x, CurrentEL=0x%x, SP=0x%x\n",
	//       read_mpidr_el1() & 0xff,
	//       read_sctlr_el1(),
	//       read_currentel(),
	//       read_sp());

	printk("Starting FreeRTOS Scheduler...\n");

	xTaskCreate(vTask1, "Task1", 1024, NULL, 1, NULL);
	extern void vTask2(void *pvParameters);
	xTaskCreate(vTask2, "Task2", 1024, NULL, 1, NULL);
	extern void vTask3(void *pvParameters);
	xTaskCreate(vTask3, "Task3", 1024, NULL, 1, NULL);

	vTaskStartScheduler();

	/* Should not reach here */
	while (1) {
		__asm__ volatile ("wfi");
	}

#if 0
	while (1) {
		__asm__ volatile ("wfi");
		shm[4] = shm[4] + 1;

		// 阻塞等待一个字符
		int ch = uart2_getc();
		uart2_putc(ch);  // 回显

		/* Check for reset command from Linux */
		if (shm[AMP_CMD_IDX] == AMP_CMD_RESET) {
			/* Disable interrupts before CPU_OFF */
			arch_local_irq_disable();

			/* Call PSCI CPU_OFF to power down this CPU */
			psci_cpu_off();

			/* Should never reach here */
			while (1)
				;
		}
	}
#endif
	return;
}
