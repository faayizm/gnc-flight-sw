/* =============================================================================
 *  FreeRTOSConfig.h -- the kernel, configured for this spacecraft.
 *
 *  Mostly a list of things switched off. The flight software is one task
 *  running the rate-group scheduler (fsw/core/scheduler.hpp explains why one),
 *  so FreeRTOS is used for exactly three things: starting that task on its
 *  own stack, the tick that gives it time, and blocking it between ticks.
 *  No queues, no timers, no heap: every kernel object is allocated statically,
 *  and with configSUPPORT_DYNAMIC_ALLOCATION 0 the kernel's own allocator is
 *  not even compiled.
 * ===========================================================================*/
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configCPU_CLOCK_HZ                      25000000UL
#define configTICK_RATE_HZ                      1000
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  0
#define configMAX_PRIORITIES                    4
#define configMINIMAL_STACK_SIZE                128      /* words */
#define configMAX_TASK_NAME_LEN                 8
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1

#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0
#define configKERNEL_PROVIDED_STATIC_MEMORY     1        /* the idle task's stack */

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_TIMERS                        0
/* V11.1.0 compiles its timer-task stack provider even with timers off, and
 * it names this. Nothing calls it, so the linker drops it and its stack. */
#define configTIMER_TASK_STACK_DEPTH            configMINIMAL_STACK_SIZE
#define configUSE_MUTEXES                       0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            0
#define configUSE_TRACE_FACILITY                0
#define configGENERATE_RUN_TIME_STATS           0
#define configUSE_CO_ROUTINES                   0

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetSchedulerState          1

/* Interrupt priorities. The Cortex-M7 in the AN500 implements 3 priority bits.
 * Interrupts more urgent than configMAX_SYSCALL_INTERRUPT_PRIORITY are never
 * masked by the kernel and must not call it -- the UART handlers are such
 * interrupts. */
#define configPRIO_BITS                         3
#define configKERNEL_INTERRUPT_PRIORITY         (7 << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (2 << (8 - configPRIO_BITS))

/* A broken kernel assumption stops the board, where a debugger can see it,
 * and the watchdog resets it. */
#define configASSERT(x) do { if (!(x)) { __asm volatile("cpsid i"); for (;;) {} } } while (0)

#endif /* FREERTOS_CONFIG_H */
