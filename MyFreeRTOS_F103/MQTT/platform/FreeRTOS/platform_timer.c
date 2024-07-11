/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-10 22:16:41
 * @LastEditTime: 2020-04-27 22:35:34
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */

#include "platform_timer.h"
#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief 获取系统运行时间（毫秒）。
 *
 * @return uint32_t 系统运行时间，单位为毫秒。
 */
static uint32_t platform_uptime_ms(void)
{
#if (configTICK_RATE_HZ == 1000)
    // 如果系统时钟频率为1000Hz，则直接返回Tick计数
    return (uint32_t)xTaskGetTickCount();
#else
    TickType_t tick = 0u;

    // 否则将Tick计数转换为毫秒
    tick = xTaskGetTickCount() * 1000;
    return (uint32_t)((tick + configTICK_RATE_HZ - 1) / configTICK_RATE_HZ);
#endif
}

/**
 * @brief 初始化定时器。
 *
 * @param timer 指向定时器对象的指针。
 */
void platform_timer_init(platform_timer_t *timer)
{
    timer->time = 0;
}

/**
 * @brief 设置定时器超时时间。
 *
 * @param timer 指向定时器对象的指针。
 * @param timeout 超时时间，单位为毫秒。
 */
void platform_timer_cutdown(platform_timer_t *timer, unsigned int timeout)
{
    timer->time = platform_uptime_ms();
    timer->time += timeout;
}

/**
 * @brief 检查定时器是否已超时。
 *
 * @param timer 指向定时器对象的指针。
 * @return char 超时返回1，否则返回0。
 */
char platform_timer_is_expired(platform_timer_t *timer)
{
    return platform_uptime_ms() > timer->time ? 1 : 0;
}

/**
 * @brief 获取定时器剩余时间。
 *
 * @param timer 指向定时器对象的指针。
 * @return int 剩余时间，单位为毫秒。如果已超时，返回0。
 */
int platform_timer_remain(platform_timer_t *timer)
{
    uint32_t now;

    now = platform_uptime_ms();
    if (timer->time <= now)
    {
        return 0;
    }

    return timer->time - now;
}

/**
 * @brief 获取当前系统时间（毫秒）。
 *
 * @return unsigned long 当前系统时间，单位为毫秒。
 */
unsigned long platform_timer_now(void)
{
    return (unsigned long)platform_uptime_ms();
}

/**
 * @brief 睡眠指定时间（微秒）。
 *
 * @param usec 睡眠时间，单位为微秒。
 */
void platform_timer_usleep(unsigned long usec)
{
    TickType_t tick;

    if (usec != 0)
    {
        tick = usec / portTICK_PERIOD_MS;

        if (tick == 0)
            tick = 1;
    }

    vTaskDelay(tick);
}
