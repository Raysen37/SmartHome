/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-15 13:38:52
 * @LastEditTime: 2020-05-25 10:13:41
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "mqtt_log.h"
#include "nettype_tcp.h"

/**
 * @brief 读取TCP数据。
 *
 * @param n 指向网络对象的指针。
 * @param read_buf 存储读取数据的缓冲区。
 * @param len 要读取的数据长度。
 * @param timeout 超时时间，单位为毫秒。
 * @return int 成功时返回读取的字节数，失败时返回错误码。
 */
int nettype_tcp_read(network_t *n, unsigned char *read_buf, int len, int timeout)
{
    return platform_net_socket_recv_timeout(n->socket, read_buf, len, timeout);
}

/**
 * @brief 写入TCP数据。
 *
 * @param n 指向网络对象的指针。
 * @param write_buf 要写入的数据缓冲区。
 * @param len 要写入的数据长度。
 * @param timeout 超时时间，单位为毫秒。
 * @return int 成功时返回写入的字节数，失败时返回错误码。
 */
int nettype_tcp_write(network_t *n, unsigned char *write_buf, int len, int timeout)
{
    return platform_net_socket_write_timeout(n->socket, write_buf, len, timeout);
}

/**
 * @brief 连接到TCP服务器。
 *
 * @param n 指向网络对象的指针。
 * @return int 成功时返回MQTT_SUCCESS_ERROR，失败时返回错误码。
 */
int nettype_tcp_connect(network_t *n)
{
    n->socket = platform_net_socket_connect(n->host, n->port, PLATFORM_NET_PROTO_TCP);
    if (n->socket < 0)
        RETURN_ERROR(n->socket);

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 断开与TCP服务器的连接。
 *
 * @param n 指向网络对象的指针。
 */
void nettype_tcp_disconnect(network_t *n)
{
    if (NULL != n)
        platform_net_socket_close(n->socket);
    n->socket = -1;
}
