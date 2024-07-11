/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-09 21:30:54
 * @LastEditTime: 2020-06-05 17:17:48
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include "platform_timer.h"
#include "platform_memory.h"
#include "nettype_tcp.h"

#ifndef MQTT_NETWORK_TYPE_NO_TLS
#include "nettype_tls.h"
#endif

/**
 * @brief 从网络读取数据。
 *
 * @param n 指向网络对象的指针。
 * @param buf 存储读取数据的缓冲区。
 * @param len 要读取的数据长度。
 * @param timeout 超时时间，单位为毫秒。
 * @return int 成功时返回读取的字节数，失败时返回错误码。
 */
int network_read(network_t *n, unsigned char *buf, int len, int timeout)
{
#ifndef MQTT_NETWORK_TYPE_NO_TLS
    if (n->channel)
        return nettype_tls_read(n, buf, len, timeout);
#endif
    return nettype_tcp_read(n, buf, len, timeout);
}

/**
 * @brief 向网络写入数据。
 *
 * @param n 指向网络对象的指针。
 * @param buf 要写入的数据缓冲区。
 * @param len 要写入的数据长度。
 * @param timeout 超时时间，单位为毫秒。
 * @return int 成功时返回写入的字节数，失败时返回错误码。
 */
int network_write(network_t *n, unsigned char *buf, int len, int timeout)
{
#ifndef MQTT_NETWORK_TYPE_NO_TLS
    if (n->channel)
        return nettype_tls_write(n, buf, len, timeout);
#endif
    return nettype_tcp_write(n, buf, len, timeout);
}

/**
 * @brief 连接到服务器。
 *
 * @param n 指向网络对象的指针。
 * @return int 成功时返回MQTT_SUCCESS_ERROR，失败时返回错误码。
 */
int network_connect(network_t *n)
{
#ifndef MQTT_NETWORK_TYPE_NO_TLS
    if (n->channel)
        return nettype_tls_connect(n);
#endif
    return nettype_tcp_connect(n);
}

/**
 * @brief 断开与服务器的连接。
 *
 * @param n 指向网络对象的指针。
 */
void network_disconnect(network_t *n)
{
#ifndef MQTT_NETWORK_TYPE_NO_TLS
    if (n->channel)
        nettype_tls_disconnect(n);
    else
#endif
        nettype_tcp_disconnect(n);
}

/**
 * @brief 初始化网络对象。
 *
 * @param n 指向网络对象的指针。
 * @param host 主机地址。
 * @param port 端口号。
 * @param ca 证书信息（如果使用TLS）。
 * @return int 成功时返回MQTT_SUCCESS_ERROR，失败时返回错误码。
 */
int network_init(network_t *n, const char *host, const char *port, const char *ca)
{
    if (NULL == n)
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    n->socket = -1;
    n->host = host;
    n->port = port;

#ifndef MQTT_NETWORK_TYPE_NO_TLS
    n->channel = 0;

    if (NULL != ca)
    {
        network_set_ca(n, ca);
    }
#endif
    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 释放网络对象。
 *
 * @param n 指向网络对象的指针。
 */
void network_release(network_t *n)
{
    if (n->socket >= 0)
        network_disconnect(n);

    memset(n, 0, sizeof(network_t));
}

/**
 * @brief 设置网络通道（是否使用TLS）。
 *
 * @param n 指向网络对象的指针。
 * @param channel 通道类型。
 */
void network_set_channel(network_t *n, int channel)
{
#ifndef MQTT_NETWORK_TYPE_NO_TLS
    n->channel = channel;
#endif
}

/**
 * @brief 设置CA证书信息。
 *
 * @param n 指向网络对象的指针。
 * @param ca CA证书信息。
 * @return int 成功时返回MQTT_SUCCESS_ERROR，失败时返回错误码。
 */
int network_set_ca(network_t *n, const char *ca)
{
#ifndef MQTT_NETWORK_TYPE_NO_TLS
    if ((NULL == n) || (NULL == ca))
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    n->ca_crt = ca;
    n->ca_crt_len = strlen(ca);
    n->channel = NETWORK_CHANNEL_TLS;
    n->timeout_ms = MQTT_TLS_HANDSHAKE_TIMEOUT;
#endif
    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 设置主机和端口信息。
 *
 * @param n 指向网络对象的指针。
 * @param host 主机地址。
 * @param port 端口号。
 * @return int 成功时返回MQTT_SUCCESS_ERROR，失败时返回错误码。
 */
int network_set_host_port(network_t *n, char *host, char *port)
{
    if (!(n && host && port))
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    n->host = host;
    n->port = port;

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}
