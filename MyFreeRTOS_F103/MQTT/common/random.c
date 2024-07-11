/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2020-01-09 19:25:05
 * @LastEditTime: 2020-06-16 14:50:33
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include <stdlib.h>
#include "random.h"
#include "platform_timer.h"
#include "platform_memory.h"

static unsigned int last_seed = 1;

/**
 * @brief 使用指定种子生成随机数。
 *
 * @param seed 随机数种子。
 * @return int 生成的随机数。
 */
static int do_random(unsigned int seed)
{
    srand(seed);
    return rand();
}

/**
 * @brief 生成一个随机数。
 *
 * @return int 生成的随机数。
 */
int random_number(void)
{
    unsigned int seed = (unsigned int)platform_timer_now();
    last_seed += (seed >> ((seed ^ last_seed) % 3));
    return do_random(last_seed ^ seed);
}

/**
 * @brief 生成一个指定范围内的随机数。
 *
 * @param min 最小值（包含）。
 * @param max 最大值（不包含）。
 * @return int 生成的随机数。
 */
int random_number_range(unsigned int min, unsigned int max)
{
    return (random_number() % (max - min)) + min;
}

/**
 * @brief 生成指定长度的随机字符串。
 *
 * @param len 字符串长度。
 * @return char* 生成的随机字符串，需要手动释放内存。
 */
char *random_string(unsigned int len)
{
    unsigned int i, flag, seed, random;

    char *str = platform_memory_alloc((size_t)(len + 1));
    if (NULL == str)
        return NULL;

    seed = (unsigned int)random_number();
    seed += (unsigned int)((size_t)str ^ seed);

    random = (unsigned int)do_random(seed);

    for (i = 0; i < len; i++)
    {
        random = do_random(seed ^ random);
        flag = (unsigned int)random % 3;
        switch (flag)
        {
        case 0:
            str[i] = 'A' + do_random(random ^ (i & flag)) % 26;
            break;
        case 1:
            str[i] = 'a' + do_random(random ^ (i & flag)) % 26;
            break;
        case 2:
            str[i] = '0' + do_random(random ^ (i & flag)) % 10;
            break;
        default:
            str[i] = 'x';
            break;
        }
        random += ((0xb433e5c6 ^ random) << (i & flag));
    }

    str[len] = '\0';
    return str;
}
