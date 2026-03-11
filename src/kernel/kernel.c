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
#include <printk.h>
#include <uart_geni.h>
#include <clock.h>
#include <clock_qcs6490.h>
#include <i2c_geni.h>
#include <spi_geni.h>
#ifdef CONFIG_FREERTOS
#include <gic_v3.h>
#include <timer.h>
#include "FreeRTOS.h"
#include "task.h"
#else
#define AMP_CMD_IDX   32
#define AMP_CMD_RESET 0x52534554ULL
#endif

#define PSCI_0_2_FN_CPU_OFF 0x84000002UL
#define PSCI_POWER_STATE_TYPE_POWER_DOWN (1UL << 16)
#define AMP_CMD_IDX   32
#define AMP_CMD_RESET 0x52534554ULL

static inline unsigned long read_mpidr_el1(void)
{
	unsigned long v;

	__asm__ volatile ("mrs %0, mpidr_el1" : "=r"(v));
	return v;
}

static inline void psci_cpu_off(void)
{
	register unsigned long x0 asm("x0") = PSCI_0_2_FN_CPU_OFF;
	register unsigned long x1 asm("x1") = PSCI_POWER_STATE_TYPE_POWER_DOWN;
	register unsigned long x2 asm("x2") = 0;
	register unsigned long x3 asm("x3") = 0;

	asm volatile ("smc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3) : "memory");
	for (;;)
		asm volatile ("wfi");
}

#ifdef CONFIG_FREERTOS
extern void FreeRTOS_Tick_Handler(void);

void vApplicationIRQHandler(uint32_t ulICCIAR)
{
	uint32_t ulInterruptID = ulICCIAR & 0x3FFUL;
	volatile u64 *shm = (volatile u64 *)SHM_BASE;

	shm[1]++;
	shm[2] = ulInterruptID;

	if (ulInterruptID == TIMER_PPI_ID) {
		shm[3]++;
		FreeRTOS_Tick_Handler();
	}
}

static void vTask1(void *pvParameters)
{
	(void) pvParameters;
	volatile u64 *shm = (volatile u64 *)SHM_BASE;

	printk("Task1 (GPIO Toggle) started\n");

	for (;;) {
		gpio_direction_output(44, 0);
		vTaskDelay(pdMS_TO_TICKS(500));
		gpio_direction_output(44, 1);
		vTaskDelay(pdMS_TO_TICKS(500));

		shm[4]++;
	}
}

static void task_sleep(uint32_t seconds)
{
	vTaskDelay(pdMS_TO_TICKS(seconds * 1000));
}

static void vTask2(void *pvParameters)
{
	(void) pvParameters;
	int count = 0;

	printk("Task2 (Logger) started\n");

	for (;;) {
		printk("Task2: Keeping Alive... count=%d (Tick=%d)\n",
		       count++, xTaskGetTickCount());
		task_sleep(1);
	}
}

static void vTask3(void *pvParameters)
{
	(void) pvParameters;
	int a = 1;
	int b = 1;

	printk("Task3 (Fibonacci) started\n");

	for (;;) {
		int next = a + b;

		printk("Task3: Fibonacci next=%d\n", next);
		a = b;
		b = next;

		if (a > 1000) {
			a = 1;
			b = 1;
			printk("Task3: Reset sequence\n");
		}

		task_sleep(2);
	}
}

static void vTaskResetMonitor(void *pvParameters)
{
	volatile u64 *shm = (volatile u64 *)SHM_BASE;

	(void) pvParameters;

	for (;;) {
		if (shm[AMP_CMD_IDX] == AMP_CMD_RESET) {
			shm[AMP_CMD_IDX] = 0;
			taskDISABLE_INTERRUPTS();
			psci_cpu_off();
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}

static void amp_runtime_prepare(void)
{
	u64 gicr = 0x17b40000ULL;

	gicv3_init_for_cpu();
	enable_ppi(gicr, TIMER_PPI_ID, 0x40);
	write_cntv_ctl_el0(0);
}

static void amp_runtime_start(void)
{
	printk("Starting FreeRTOS Scheduler...\n");

	xTaskCreate(vTask1, "Task1", 1024, NULL, 1, NULL);
	xTaskCreate(vTask2, "Task2", 1024, NULL, 1, NULL);
	xTaskCreate(vTask3, "Task3", 1024, NULL, 1, NULL);
	xTaskCreate(vTaskResetMonitor, "ResetMon", 512, NULL, 3, NULL);

	vTaskStartScheduler();

	for (;;)
		__asm__ volatile ("yield");
}
#else
static void amp_runtime_prepare(void)
{
}

static void amp_runtime_start(void)
{
	volatile u64 *shm = (volatile u64 *)SHM_BASE;

	printk("FreeRTOS disabled, entering bare-metal polling loop\n");

	for (;;) {
		if (shm[AMP_CMD_IDX] == AMP_CMD_RESET) {
			shm[AMP_CMD_IDX] = 0;
			arch_local_irq_disable();
			psci_cpu_off();
		}

		__asm__ volatile ("yield");
	}
}
#endif

void kernel_main(void)
{
	volatile u64 *shm = (volatile u64 *)SHM_BASE;
	unsigned long *test = (unsigned long *)SHM_BASE;
	u8 tx_data[] = {0x01, 0x02, 0x03};

	arch_local_irq_disable();
	shm[1] = read_mpidr_el1();

	mem_init(0, 0);
	paging_init();

	uart2_init();
	uart2_puts("uart2 hello rubikpi 123456\n");

//	i2c1_init(I2C_STANDARD_MODE_FREQ);
//	i2c1_write(0x50, (u8 *)"Hello I2C EEPROM", 16);

	spi12_init(SPI_50MHZ);
	spi12_set_mode(0, SPI_MODE_0);
	spi12_write(0, tx_data, sizeof(tx_data));

	printk("SPI12 transfer done.\n");

	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 1);
	gpio_pinmux_set(44, mux_gpio);
	gpio_direction_output(44, 0);

	test[20] = 0x2020208;

	amp_runtime_prepare();
	amp_runtime_start();
}
