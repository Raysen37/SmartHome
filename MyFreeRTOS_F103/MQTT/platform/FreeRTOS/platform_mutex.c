/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-15 18:27:19
 * @LastEditTime: 2020-04-27 22:22:27
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_mutex.h"

/**
 * @brief 初始化互斥锁。
 *
 * @param m 指向平台互斥锁对象的指针。
 * @return int 成功时返回0，失败时返回非0。
 */
int platform_mutex_init(platform_mutex_t *m)
{
    m->mutex = xSemaphoreCreateMutex();
    return 0;
}

/**
 * @brief 加锁互斥锁。如果互斥锁已被占用，调用者将阻塞直到互斥锁可用。
 *
 * @param m 指向平台互斥锁对象的指针。
 * @return int 成功时返回pdTRUE，失败时返回pdFALSE。
 */
int platform_mutex_lock(platform_mutex_t *m)
{
    return xSemaphoreTake(m->mutex, portMAX_DELAY);
}

/**
 * @brief 尝试加锁互斥锁。如果互斥锁已被占用，立即返回。
 *
 * @param m 指向平台互斥锁对象的指针。
 * @return int 成功时返回pdTRUE，失败时返回pdFALSE。
 */
int platform_mutex_trylock(platform_mutex_t *m)
{
    return xSemaphoreTake(m->mutex, 0);
}

/**
 * @brief 解锁互斥锁。
 *
 * @param m 指向平台互斥锁对象的指针。
 * @return int 成功时返回pdTRUE，失败时返回pdFALSE。
 */
int platform_mutex_unlock(platform_mutex_t *m)
{
    return xSemaphoreGive(m->mutex);
}

/**
 * @brief 销毁互斥锁。
 *
 * @param m 指向平台互斥锁对象的指针。
 * @return int 成功时返回0，失败时返回非0。
 */
int platform_mutex_destroy(platform_mutex_t *m)
{
    vSemaphoreDelete(m->mutex);
    return 0;
}
