/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-23 19:26:27
 * @LastEditTime: 2020-09-23 08:53:43
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_thread.h"
#include "platform_memory.h"

/**
 * @brief 初始化并创建一个线程。
 *
 * @param name 线程名称。
 * @param entry 线程入口函数。
 * @param param 传递给线程入口函数的参数。
 * @param stack_size 线程的栈大小。
 * @param priority 线程的优先级。
 * @param tick 线程的时间片大小（未使用）。
 * @return platform_thread_t* 指向创建的线程对象的指针，失败时返回NULL。
 */
platform_thread_t *platform_thread_init(const char *name,
                                        void (*entry)(void *),
                                        void *const param,
                                        unsigned int stack_size,
                                        unsigned int priority,
                                        unsigned int tick)
{
    BaseType_t err;
    platform_thread_t *thread;

    // 分配内存用于存储线程对象
    thread = platform_memory_alloc(sizeof(platform_thread_t));

    (void)tick; // 未使用的参数

    // 创建FreeRTOS任务
    err = xTaskCreate(entry, name, stack_size, param, priority, &thread->thread);

    if (pdPASS != err)
    {
        // 创建失败，释放内存
        platform_memory_free(thread);
        return NULL;
    }

    return thread;
}

/**
 * @brief 启动线程。
 *
 * @param thread 指向要启动的线程对象的指针。
 */
void platform_thread_startup(platform_thread_t *thread)
{
    (void)thread;
}

/**
 * @brief 停止线程。
 *
 * @param thread 指向要停止的线程对象的指针。
 */
void platform_thread_stop(platform_thread_t *thread)
{
    vTaskSuspend(thread->thread);
}

/**
 * @brief 启动已经停止的线程。
 *
 * @param thread 指向要启动的线程对象的指针。
 */
void platform_thread_start(platform_thread_t *thread)
{
    vTaskResume(thread->thread);
}

/**
 * @brief 销毁线程。
 *
 * @param thread 指向要销毁的线程对象的指针。
 */
void platform_thread_destroy(platform_thread_t *thread)
{
    if (NULL != thread)
        vTaskDelete(thread->thread);

    // 释放线程对象的内存
    platform_memory_free(thread);
}
