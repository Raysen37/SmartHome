/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-14 22:02:07
 * @LastEditTime: 2020-04-27 16:32:58
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_memory.h"
#include "string.h"
#include "FreeRTOS.h"

/**
 * @brief 分配内存并初始化为零。
 *
 * @param size 要分配的内存大小（字节）。
 * @return void* 指向分配的内存块的指针，若分配失败则返回NULL。
 */
void *platform_memory_alloc(size_t size)
{
    char *ptr;
    // 使用FreeRTOS的pvPortMalloc分配内存
    ptr = pvPortMalloc(size);
    // 将分配的内存块初始化为零
    memset(ptr, 0, size);
    return (void *)ptr;
}

/**
 * @brief 分配内存块，不初始化。
 *
 * @param num 内存块的数量。
 * @param size 每个内存块的大小（字节）。
 * @return void* 指向分配的内存块的指针，若分配失败则返回NULL。
 */
void *platform_memory_calloc(size_t num, size_t size)
{
    // 使用FreeRTOS的pvPortMalloc分配内存，但不初始化
    return pvPortMalloc(num * size);
}

/**
 * @brief 释放之前分配的内存。
 *
 * @param ptr 指向要释放的内存块的指针。
 */
void platform_memory_free(void *ptr)
{
    // 使用FreeRTOS的vPortFree释放内存
    vPortFree(ptr);
}
