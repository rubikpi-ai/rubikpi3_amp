#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.
 *
 * See http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/

#include "type.h"

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      ( ( unsigned long ) 100000000 ) /* Timer handles its own freq */
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    ( 5 )
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 200 )
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 40960 ) )
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    1
#define configQUEUE_REGISTRY_SIZE               10
#define configUSE_TIME_SLICING                  1

/* Memory allocation related definitions. */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )

/* Software timer definitions. */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1

/* Interrupt handling. */
void timer_cntv_start_hz(u32 hz);
void timer_cntv_reload_hz(u32 hz);
#define configSETUP_TICK_INTERRUPT()            timer_cntv_start_hz(configTICK_RATE_HZ)
#define configCLEAR_TICK_INTERRUPT()            timer_cntv_reload_hz(configTICK_RATE_HZ)

/* SMP related definitions. */
#define configNUM_CORES                         1
#define configTICK_CORE                         0
#define configRUN_MULTIPLE_PRIORITIES           0

/* ARMv8-A / GIC specific definitions. */
#define configMAX_API_CALL_INTERRUPT_PRIORITY   18
#define configUNIQUE_INTERRUPT_PRIORITIES       32
#define configUSE_TASK_FPU_SUPPORT              2

/* Assert */
#include <printk.h>
#define configASSERT( x ) if( ( x ) == 0 ) { printk( "ASSERT: %s:%d\n", __FILE__, __LINE__ ); while(1); }

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          0

#endif /* FREERTOS_CONFIG_H */
