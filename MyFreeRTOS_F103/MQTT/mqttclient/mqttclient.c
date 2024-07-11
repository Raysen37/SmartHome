/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-09 21:31:25
 * @LastEditTime: 2021-01-14 09:52:39
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */

#define __MQTT_CLIENT_CLASS_IMPLEMENT
#include "mqttclient.h"

#define MQTT_MIN_PAYLOAD_SIZE 2
#define MQTT_MAX_PAYLOAD_SIZE 268435455 // MQTT imposes a maximum payload size of 268435455 bytes.

static void default_msg_handler(void *client, message_data_t *msg)
{
    MQTT_LOG_I("%s:%d %s()...\ntopic: %s, qos: %d, \nmessage:%s", __FILE__, __LINE__, __FUNCTION__,
               msg->topic_name, msg->message->qos, (char *)msg->message->payload);
}

/**
 * @brief 获取 MQTT 客户端当前状态。
 *
 * @param c MQTT 客户端结构体指针。
 * @return client_state_t 客户端当前状态。
 */
static client_state_t mqtt_get_client_state(mqtt_client_t *c)
{
    return c->mqtt_client_state;
}

/**
 * @brief 设置 MQTT 客户端状态。
 *
 * @param c MQTT 客户端结构体指针。
 * @param state 要设置的状态。
 */
static void mqtt_set_client_state(mqtt_client_t *c, client_state_t state)
{
    platform_mutex_lock(&c->mqtt_global_lock);
    c->mqtt_client_state = state;
    platform_mutex_unlock(&c->mqtt_global_lock);
}

/**
 * @brief 检查 MQTT 客户端是否已连接。
 *
 * @param c MQTT 客户端结构体指针。
 * @return int 如果客户端已连接返回 MQTT_SUCCESS_ERROR，否则返回错误码。
 */
static int mqtt_is_connected(mqtt_client_t *c)
{
    client_state_t state;

    state = mqtt_get_client_state(c);
    if (CLIENT_STATE_CLEAN_SESSION == state)
    {
        RETURN_ERROR(MQTT_CLEAN_SESSION_ERROR);
    }
    else if (CLIENT_STATE_CONNECTED != state)
    {
        RETURN_ERROR(MQTT_NOT_CONNECT_ERROR);
    }
    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 设置 PUBLISH 消息的 DUP（重复发布标志）。
 *
 * @param c MQTT 客户端结构体指针。
 * @param dup DUP 标志。
 * @return int 设置成功返回 MQTT_SUCCESS_ERROR，否则返回 MQTT_SET_PUBLISH_DUP_FAILED_ERROR。
 */
static int mqtt_set_publish_dup(mqtt_client_t *c, uint8_t dup)
{
    uint8_t *read_data = c->mqtt_write_buf;
    uint8_t *write_data = c->mqtt_write_buf;
    MQTTHeader header = {0};

    if (NULL == c->mqtt_write_buf)
        RETURN_ERROR(MQTT_SET_PUBLISH_DUP_FAILED_ERROR);

    header.byte = readChar(&read_data); /* read header */

    if (header.bits.type != PUBLISH)
        RETURN_ERROR(MQTT_SET_PUBLISH_DUP_FAILED_ERROR);

    header.bits.dup = dup;
    writeChar(&write_data, header.byte); /* write header */

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 检查 ACK 处理程序数量是否已达到最大值。
 *
 * @param c MQTT 客户端结构体指针。
 * @return int 如果已达到最大值返回 1，否则返回 0。
 */
static int mqtt_ack_handler_is_maximum(mqtt_client_t *c)
{
    return (c->mqtt_ack_handler_number >= MQTT_ACK_HANDLER_NUM_MAX) ? 1 : 0;
}

/**
 * @brief 增加 ACK 处理程序数量。
 *
 * @param c MQTT 客户端结构体指针。
 */
static void mqtt_add_ack_handler_num(mqtt_client_t *c)
{
    platform_mutex_lock(&c->mqtt_global_lock);
    c->mqtt_ack_handler_number++;
    platform_mutex_unlock(&c->mqtt_global_lock);
}

/**
 * @brief 减少 ACK 处理程序数量。
 *
 * @param c MQTT 客户端结构体指针。
 * @return int 如果成功减少返回 MQTT_SUCCESS_ERROR，否则返回错误码。
 */
static int mqtt_subtract_ack_handler_num(mqtt_client_t *c)
{
    int rc = MQTT_SUCCESS_ERROR;
    platform_mutex_lock(&c->mqtt_global_lock);
    if (c->mqtt_ack_handler_number <= 0)
    {
        goto exit;
    }

    c->mqtt_ack_handler_number--;

exit:
    platform_mutex_unlock(&c->mqtt_global_lock);
    RETURN_ERROR(rc);
}

/**
 * @brief 获取下一个 MQTT 数据包的 ID。
 *
 * @param c MQTT 客户端结构体指针。
 * @return uint16_t 下一个数据包的 ID。
 */
static uint16_t mqtt_get_next_packet_id(mqtt_client_t *c)
{
    platform_mutex_lock(&c->mqtt_global_lock);
    c->mqtt_packet_id = (c->mqtt_packet_id == MQTT_MAX_PACKET_ID) ? 1 : c->mqtt_packet_id + 1;
    platform_mutex_unlock(&c->mqtt_global_lock);
    return c->mqtt_packet_id;
}

/**
 * @brief 解码 MQTT 数据包。
 *
 * @param c MQTT 客户端结构体指针。
 * @param value 解码后的数值。
 * @param timeout 超时时间。
 * @return int 解码的字节数。
 */
static int mqtt_decode_packet(mqtt_client_t *c, int *value, int timeout)
{
    uint8_t i;
    int multiplier = 1;
    int len = 0;
    const int MAX_NO_OF_REMAINING_LENGTH_BYTES = 4;

    *value = 0;
    do
    {
        int rc = MQTTPACKET_READ_ERROR;

        if (++len > MAX_NO_OF_REMAINING_LENGTH_BYTES)
        {
            rc = MQTTPACKET_READ_ERROR; /* bad data */
            goto exit;
        }
        rc = network_read(c->mqtt_network, &i, 1, timeout); /* read network data */
        if (rc != 1)
            goto exit;
        *value += (i & 127) * multiplier; /* decode data length according to mqtt protocol */
        multiplier *= 128;
    } while ((i & 128) != 0);
exit:
    return len;
}

/**
 * @brief 清空 MQTT 数据包。
 *
 * @param c MQTT 客户端结构体指针。
 * @param timer 平台定时器结构体指针。
 * @param packet_len 数据包长度。
 */
static void mqtt_packet_drain(mqtt_client_t *c, platform_timer_t *timer, int packet_len)
{
    int total_bytes_read = 0, read_len = 0, bytes2read = 0;

    if (packet_len < c->mqtt_read_buf_size)
    {
        bytes2read = packet_len;
    }
    else
    {
        bytes2read = c->mqtt_read_buf_size;
    }

    do
    {
        read_len = network_read(c->mqtt_network, c->mqtt_read_buf, bytes2read, platform_timer_remain(timer));
        if (0 != read_len)
        {
            total_bytes_read += read_len;
            if ((packet_len - total_bytes_read) >= c->mqtt_read_buf_size)
            {
                bytes2read = c->mqtt_read_buf_size;
            }
            else
            {
                bytes2read = packet_len - total_bytes_read;
            }
        }
    } while ((total_bytes_read < packet_len) && (0 != read_len)); /* read and discard all corrupted data */
}
/**
 * @brief 从网络读取 MQTT 数据包
 *
 * @param c MQTT 客户端实例
 * @param packet_type 返回的数据包类型
 * @param timer 计时器实例
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_read_packet(mqtt_client_t *c, int *packet_type, platform_timer_t *timer)
{
    MQTTHeader header = {0};
    int rc;
    int len = 1;
    int remain_len = 0;

    if (NULL == packet_type)
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    platform_timer_cutdown(timer, c->mqtt_cmd_timeout);

    /* 1. 读取头部字节，其中包含数据包类型 */
    rc = network_read(c->mqtt_network, c->mqtt_read_buf, len, platform_timer_remain(timer));
    if (rc != len)
        RETURN_ERROR(MQTT_NOTHING_TO_READ_ERROR);

    /* 2. 读取剩余长度字段，这个字段本身是可变的 */
    mqtt_decode_packet(c, &remain_len, platform_timer_remain(timer));

    /* 将原始的剩余长度字段放回缓冲区 */
    len += MQTTPacket_encode(c->mqtt_read_buf + len, remain_len);

    if ((len + remain_len) > c->mqtt_read_buf_size)
    {

        /* MQTT 缓冲区太短，读取并丢弃所有损坏的数据 */
        mqtt_packet_drain(c, timer, remain_len);

        RETURN_ERROR(MQTT_BUFFER_TOO_SHORT_ERROR);
    }

    /* 3. 使用回调读取网络中剩余的数据 */
    if ((remain_len > 0) && ((rc = network_read(c->mqtt_network, c->mqtt_read_buf + len, remain_len, platform_timer_remain(timer))) != remain_len))
        RETURN_ERROR(MQTT_NOTHING_TO_READ_ERROR);

    header.byte = c->mqtt_read_buf[0];
    *packet_type = header.bits.type;

    /* 设置最后接收时间，用于保活检测 */
    platform_timer_cutdown(&c->mqtt_last_received, (c->mqtt_keep_alive_interval * 1000));

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 发送 MQTT 数据包到网络
 *
 * @param c MQTT 客户端实例
 * @param length 要发送的数据包长度
 * @param timer 计时器实例
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_send_packet(mqtt_client_t *c, int length, platform_timer_t *timer)
{
    int len = 0;
    int sent = 0;

    platform_timer_cutdown(timer, c->mqtt_cmd_timeout);

    /* 在阻塞模式下发送 MQTT 数据包，或在定时器超时时退出 */
    while ((sent < length) && (!platform_timer_is_expired(timer)))
    {
        len = network_write(c->mqtt_network, &c->mqtt_write_buf[sent], length, platform_timer_remain(timer));
        if (len <= 0) // 发送数据出错
            break;
        sent += len;
    }

    if (sent == length)
    {
        /* 更新最后发送时间，用于保活检测 */
        platform_timer_cutdown(&c->mqtt_last_sent, (c->mqtt_keep_alive_interval * 1000));
        RETURN_ERROR(MQTT_SUCCESS_ERROR);
    }

    RETURN_ERROR(MQTT_SEND_PACKET_ERROR);
}

/**
 * @brief 检查两个 MQTT 主题是否相等
 *
 * @param topic_filter 主题过滤器
 * @param topic 主题名
 * @return int 返回1表示相等，0表示不相等
 */
static int mqtt_is_topic_equals(const char *topic_filter, const char *topic)
{
    int topic_len = 0;

    topic_len = strlen(topic);
    if (strlen(topic_filter) != topic_len)
    {
        return 0;
    }

    if (strncmp(topic_filter, topic, topic_len) == 0)
    {
        return 1;
    }

    return 0;
}

/**
 * @brief 检查 MQTT 主题是否与过滤器匹配
 *
 * @param topic_filter 主题过滤器
 * @param topic_name 主题名
 * @return char 返回1表示匹配，0表示不匹配
 */
static char mqtt_topic_is_matched(char *topic_filter, MQTTString *topic_name)
{
    char *curf = topic_filter;
    char *curn = topic_name->lenstring.data;
    char *curn_end = curn + topic_name->lenstring.len;

    while (*curf && curn < curn_end)
    {
        if (*curn == '/' && *curf != '/')
            break;

        /* 支持 MQTT 主题的通配符，如 '#' '+' */
        if (*curf != '+' && *curf != '#' && *curf != *curn)
            break;

        if (*curf == '+')
        {
            char *nextpos = curn + 1;
            while (nextpos < curn_end && *nextpos != '/')
                nextpos = ++curn + 1;
        }
        else if (*curf == '#')
            curn = curn_end - 1;
        curf++;
        curn++;
    };

    return (curn == curn_end) && (*curf == '\0');
}
/**
 * @brief 创建一个新的消息数据结构
 *
 * @param md 消息数据结构指针
 * @param topic_name MQTT 主题名
 * @param message MQTT 消息结构指针
 */
static void mqtt_new_message_data(message_data_t *md, MQTTString *topic_name, mqtt_message_t *message)
{
    int len;
    len = (topic_name->lenstring.len < MQTT_TOPIC_LEN_MAX - 1) ? topic_name->lenstring.len : MQTT_TOPIC_LEN_MAX - 1;
    memcpy(md->topic_name, topic_name->lenstring.data, len);
    md->topic_name[len] = '\0'; /* 主题名太长，将被截断 */
    md->message = message;
}

/**
 * @brief 获取匹配的消息处理器
 *
 * @param c MQTT 客户端实例
 * @param topic_name MQTT 主题名
 * @return message_handlers_t* 返回匹配的消息处理器，如果未找到则返回 NULL
 */
static message_handlers_t *mqtt_get_msg_handler(mqtt_client_t *c, MQTTString *topic_name)
{
    mqtt_list_t *curr, *next;
    message_handlers_t *msg_handler;

    /* 遍历消息处理器列表以查找匹配的消息处理器 */
    LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_msg_handler_list)
    {
        msg_handler = LIST_ENTRY(curr, message_handlers_t, list);

        /* 判断主题名是否相等或匹配，支持通配符 '#' '+' */
        if ((NULL != msg_handler->topic_filter) && ((MQTTPacket_equals(topic_name, (char *)msg_handler->topic_filter)) ||
                                                    (mqtt_topic_is_matched((char *)msg_handler->topic_filter, topic_name))))
        {
            return msg_handler;
        }
    }
    return NULL;
}

/**
 * @brief 传递 MQTT 消息到相应的消息处理器或拦截器
 *
 * @param c MQTT 客户端实例
 * @param topic_name MQTT 主题名
 * @param message MQTT 消息结构指针
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_deliver_message(mqtt_client_t *c, MQTTString *topic_name, mqtt_message_t *message)
{
    int rc = MQTT_FAILED_ERROR;
    message_handlers_t *msg_handler;

    /* 获取匹配的消息处理器 */
    msg_handler = mqtt_get_msg_handler(c, topic_name);

    if (NULL != msg_handler)
    {
        message_data_t md;
        mqtt_new_message_data(&md, topic_name, message); /* 创建消息数据 */
        msg_handler->handler(c, &md);                    /* 传递消息 */
        rc = MQTT_SUCCESS_ERROR;
    }
    else if (NULL != c->mqtt_interceptor_handler)
    {
        message_data_t md;
        mqtt_new_message_data(&md, topic_name, message); /* 创建消息数据 */
        c->mqtt_interceptor_handler(c, &md);
        rc = MQTT_SUCCESS_ERROR;
    }

    memset(message->payload, 0, strlen((const char *)message->payload));
    memset(topic_name->lenstring.data, 0, topic_name->lenstring.len);

    RETURN_ERROR(rc);
}

/**
 * @brief 创建一个 ACK 处理器
 *
 * @param c MQTT 客户端实例
 * @param type ACK 类型
 * @param packet_id 包 ID
 * @param payload_len 负载长度
 * @param handler 消息处理器指针
 * @return ack_handlers_t* 返回创建的 ACK 处理器实例，如果内存分配失败则返回 NULL
 */
static ack_handlers_t *mqtt_ack_handler_create(mqtt_client_t *c, int type, uint16_t packet_id, uint16_t payload_len, message_handlers_t *handler)
{
    ack_handlers_t *ack_handler = NULL;

    ack_handler = (ack_handlers_t *)platform_memory_alloc(sizeof(ack_handlers_t) + payload_len);
    if (NULL == ack_handler)
        return NULL;

    mqtt_list_init(&ack_handler->list);

    platform_timer_cutdown(&ack_handler->timer, c->mqtt_cmd_timeout); /* 如果超时未响应，则会被销毁或重新发送 */

    ack_handler->type = type;
    ack_handler->packet_id = packet_id;
    ack_handler->payload_len = payload_len;
    ack_handler->payload = (uint8_t *)ack_handler + sizeof(ack_handlers_t);
    memcpy(ack_handler->payload, c->mqtt_write_buf, payload_len); /* 将数据保存在 ACK 处理器中 */

    return ack_handler;
}

/**
 * @brief 销毁 ACK 处理器
 *
 * @param ack_handler ACK 处理器实例指针
 */
static void mqtt_ack_handler_destroy(ack_handlers_t *ack_handler)
{
    if (NULL != &ack_handler->list)
    {
        mqtt_list_del(&ack_handler->list);
        platform_memory_free(ack_handler); /* 从列表中删除 ACK 处理器，并释放内存 */
    }
}

/**
 * @brief 重新发送 ACK 处理器中的数据
 *
 * @param c MQTT 客户端实例
 * @param ack_handler ACK 处理器实例指针
 */
static void mqtt_ack_handler_resend(mqtt_client_t *c, ack_handlers_t *ack_handler)
{
    platform_timer_t timer;

    platform_timer_cutdown(&timer, c->mqtt_cmd_timeout);
    platform_timer_cutdown(&ack_handler->timer, c->mqtt_cmd_timeout); /* 超时，重新计时 */

    platform_mutex_lock(&c->mqtt_write_lock);
    memcpy(c->mqtt_write_buf, ack_handler->payload, ack_handler->payload_len); /* 从 ACK 处理器中复制数据到写缓冲区 */

    mqtt_send_packet(c, ack_handler->payload_len, &timer); /* 重新发送数据 */
    platform_mutex_unlock(&c->mqtt_write_lock);
    MQTT_LOG_W("%s:%d %s()... 重新发送 %d 包, 包 ID 是 %d ", __FILE__, __LINE__, __FUNCTION__, ack_handler->type, ack_handler->packet_id);
}
/**
 * @brief 检查 ACK 处理器列表中是否存在指定的节点
 *
 * @param c MQTT 客户端实例
 * @param type ACK 类型
 * @param packet_id 包 ID
 * @return int 如果存在返回 1，否则返回 0
 */
static int mqtt_ack_list_node_is_exist(mqtt_client_t *c, int type, uint16_t packet_id)
{
    mqtt_list_t *curr, *next;
    ack_handlers_t *ack_handler;

    if (mqtt_list_is_empty(&c->mqtt_ack_handler_list))
        return 0;

    LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_ack_handler_list)
    {
        ack_handler = LIST_ENTRY(curr, ack_handlers_t, list);

        /* 对于 MQTT QoS1 和 QoS2 的数据包，可以使用包 ID 和类型作为唯一标识符来确定节点是否已存在，避免重复添加 */
        if ((packet_id == ack_handler->packet_id) && (type == ack_handler->type))
            return 1;
    }

    return 0;
}

/**
 * @brief 记录 ACK 处理器到列表中
 *
 * @param c MQTT 客户端实例
 * @param type ACK 类型
 * @param packet_id 包 ID
 * @param payload_len 负载长度
 * @param handler 消息处理器指针
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_ack_list_record(mqtt_client_t *c, int type, uint16_t packet_id, uint16_t payload_len, message_handlers_t *handler)
{
    int rc = MQTT_SUCCESS_ERROR;
    ack_handlers_t *ack_handler = NULL;

    /* 判断节点是否已存在 */
    if (mqtt_ack_list_node_is_exist(c, type, packet_id))
        RETURN_ERROR(MQTT_ACK_NODE_IS_EXIST_ERROR);

    /* 创建一个 ACK 处理器节点 */
    ack_handler = mqtt_ack_handler_create(c, type, packet_id, payload_len, handler);
    if (NULL == ack_handler)
        RETURN_ERROR(MQTT_MEM_NOT_ENOUGH_ERROR);

    mqtt_add_ack_handler_num(c);

    mqtt_list_add_tail(&ack_handler->list, &c->mqtt_ack_handler_list);

    RETURN_ERROR(rc);
}

/**
 * @brief 从 ACK 处理器列表中删除记录
 *
 * @param c MQTT 客户端实例
 * @param type ACK 类型
 * @param packet_id 包 ID
 * @param handler 消息处理器指针地址
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_ack_list_unrecord(mqtt_client_t *c, int type, uint16_t packet_id, message_handlers_t **handler)
{
    mqtt_list_t *curr, *next;
    ack_handlers_t *ack_handler;

    if (mqtt_list_is_empty(&c->mqtt_ack_handler_list))
        RETURN_ERROR(MQTT_SUCCESS_ERROR);

    LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_ack_handler_list)
    {
        ack_handler = LIST_ENTRY(curr, ack_handlers_t, list);

        if ((packet_id != ack_handler->packet_id) || (type != ack_handler->type))
            continue;

        if (handler)
            *handler = ack_handler->handler;

        /* 销毁一个 ACK 处理器节点 */
        mqtt_ack_handler_destroy(ack_handler);
        mqtt_subtract_ack_handler_num(c);
    }
    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 创建一个消息处理器实例
 *
 * @param topic_filter MQTT 主题过滤器
 * @param qos MQTT QoS 等级
 * @param handler 消息处理器回调函数
 * @return message_handlers_t* 返回创建的消息处理器实例，如果内存分配失败则返回 NULL
 */
static message_handlers_t *mqtt_msg_handler_create(const char *topic_filter, mqtt_qos_t qos, message_handler_t handler)
{
    message_handlers_t *msg_handler = NULL;

    msg_handler = (message_handlers_t *)platform_memory_alloc(sizeof(message_handlers_t));
    if (NULL == msg_handler)
        return NULL;

    mqtt_list_init(&msg_handler->list);

    msg_handler->qos = qos;
    msg_handler->handler = handler; /* 注册回调处理器 */
    msg_handler->topic_filter = topic_filter;

    return msg_handler;
}

/**
 * @brief 销毁消息处理器实例
 *
 * @param msg_handler 消息处理器实例指针
 */
static void mqtt_msg_handler_destory(message_handlers_t *msg_handler)
{
    if (NULL != &msg_handler->list)
    {
        mqtt_list_del(&msg_handler->list);
        platform_memory_free(msg_handler);
    }
}

/**
 * @brief 检查消息处理器列表中是否已存在指定的消息处理器
 *
 * @param c MQTT 客户端实例
 * @param handler 消息处理器实例指针
 * @return int 如果存在返回 1，否则返回 0
 */
static int mqtt_msg_handler_is_exist(mqtt_client_t *c, message_handlers_t *handler)
{
    mqtt_list_t *curr, *next;
    message_handlers_t *msg_handler;

    if (mqtt_list_is_empty(&c->mqtt_msg_handler_list))
        return 0;

    LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_msg_handler_list)
    {
        msg_handler = LIST_ENTRY(curr, message_handlers_t, list);

        /* 通过 MQTT 主题名判断节点是否已存在，但不支持通配符 */
        if ((NULL != msg_handler->topic_filter) && (mqtt_is_topic_equals(msg_handler->topic_filter, handler->topic_filter)))
        {
            MQTT_LOG_W("%s:%d %s()...msg_handler->topic_filter: %s, handler->topic_filter: %s",
                       __FILE__, __LINE__, __FUNCTION__, msg_handler->topic_filter, handler->topic_filter);
            return 1;
        }
    }

    return 0;
}

/**
 * @brief 安装消息处理器到消息处理器列表中
 *
 * @param c MQTT 客户端实例
 * @param handler 消息处理器实例指针
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_msg_handlers_install(mqtt_client_t *c, message_handlers_t *handler)
{
    if ((NULL == c) || (NULL == handler))
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    if (mqtt_msg_handler_is_exist(c, handler))
    {
        mqtt_msg_handler_destory(handler);
        RETURN_ERROR(MQTT_SUCCESS_ERROR);
    }

    /* 安装到消息处理器列表 */
    mqtt_list_add_tail(&handler->list, &c->mqtt_msg_handler_list);

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}
/**
 * @brief 清理 MQTT 客户端会话状态，释放 ACK 处理器列表和消息处理器列表的内存资源
 *
 * @param c MQTT 客户端实例
 */
static void mqtt_clean_session(mqtt_client_t *c)
{
    mqtt_list_t *curr, *next;
    ack_handlers_t *ack_handler;
    message_handlers_t *msg_handler;

    /* 释放所有 ACK 处理器列表的内存资源 */
    if (!(mqtt_list_is_empty(&c->mqtt_ack_handler_list)))
    {
        LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_ack_handler_list)
        {
            ack_handler = LIST_ENTRY(curr, ack_handlers_t, list);
            mqtt_list_del(&ack_handler->list);
            //@lchnu, 2020-10-08, 避免在等待 suback/unsuback 时断开连接...
            if (NULL != ack_handler->handler)
            {
                mqtt_msg_handler_destory(ack_handler->handler);
                ack_handler->handler = NULL;
            }
            platform_memory_free(ack_handler);
        }
        mqtt_list_del_init(&c->mqtt_ack_handler_list);
    }
    /* 需要清除 mqtt_ack_handler_number 的值，由 @lchnu 发现的 bug */
    c->mqtt_ack_handler_number = 0;

    /* 释放所有消息处理器列表的内存资源 */
    if (!(mqtt_list_is_empty(&c->mqtt_msg_handler_list)))
    {
        LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_msg_handler_list)
        {
            msg_handler = LIST_ENTRY(curr, message_handlers_t, list);
            mqtt_list_del(&msg_handler->list);
            msg_handler->topic_filter = NULL;
            platform_memory_free(msg_handler);
        }
        mqtt_list_del_init(&c->mqtt_msg_handler_list);
    }

    mqtt_set_client_state(c, CLIENT_STATE_INVALID);
}

/**
 * @brief 扫描 ACK 处理器列表，处理等待服务器响应的消息
 *
 * @param c MQTT 客户端实例
 * @param flag 标志位，0 表示不需要等待超时立即处理，1 表示需要等待超时后处理
 */
static void mqtt_ack_list_scan(mqtt_client_t *c, uint8_t flag)
{
    mqtt_list_t *curr, *next;
    ack_handlers_t *ack_handler;

    if ((mqtt_list_is_empty(&c->mqtt_ack_handler_list)) || (CLIENT_STATE_CONNECTED != mqtt_get_client_state(c)))
        return;

    LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_ack_handler_list)
    {
        ack_handler = LIST_ENTRY(curr, ack_handlers_t, list);

        if ((!platform_timer_is_expired(&ack_handler->timer)) && (flag == 1))
            continue;

        if ((ack_handler->type == PUBACK) || (ack_handler->type == PUBREC) || (ack_handler->type == PUBREL) || (ack_handler->type == PUBCOMP))
        {

            /* 已发生超时，对于 QoS1 和 QoS2 的数据包，需要重新发送 */
            mqtt_ack_handler_resend(c, ack_handler);
            continue;
        }
        else if ((ack_handler->type == SUBACK) || (ack_handler->type == UNSUBACK))
        {

            /*@lchnu, 2020-10-08, 如果 suback/unsuback 已过期，则释放处理器内存 */
            if (NULL != ack_handler->handler)
            {
                mqtt_msg_handler_destory(ack_handler->handler);
                ack_handler->handler = NULL;
            }
        }
        /* 如果不是 QoS1 或 QoS2 消息，则在每次处理中销毁 */
        mqtt_ack_handler_destroy(ack_handler);
        mqtt_subtract_ack_handler_num(c); /*@lchnu, 2020-10-08 */
    }
}

/**
 * @brief 尝试重新订阅 MQTT 客户端中断前的所有主题
 *
 * @param c MQTT 客户端实例
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_try_resubscribe(mqtt_client_t *c)
{
    int rc = MQTT_RESUBSCRIBE_ERROR;
    mqtt_list_t *curr, *next;
    message_handlers_t *msg_handler;

    MQTT_LOG_W("%s:%d %s()... mqtt try resubscribe ...", __FILE__, __LINE__, __FUNCTION__);

    if (mqtt_list_is_empty(&c->mqtt_msg_handler_list))
        RETURN_ERROR(MQTT_SUCCESS_ERROR);

    LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_msg_handler_list)
    {
        msg_handler = LIST_ENTRY(curr, message_handlers_t, list);

        /* 重新订阅主题 */
        if ((rc = mqtt_subscribe(c, msg_handler->topic_filter, msg_handler->qos, msg_handler->handler)) == MQTT_ACK_HANDLER_NUM_TOO_MUCH_ERROR)
            MQTT_LOG_W("%s:%d %s()... mqtt ack handler num too much ...", __FILE__, __LINE__, __FUNCTION__);
    }

    RETURN_ERROR(rc);
}

/**
 * @brief 尝试执行 MQTT 客户端重新连接操作
 *
 * @param c MQTT 客户端实例
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_try_do_reconnect(mqtt_client_t *c)
{
    int rc = MQTT_CONNECT_FAILED_ERROR;

    if (CLIENT_STATE_CONNECTED != mqtt_get_client_state(c))
        rc = mqtt_connect(c); /* 重新连接 */

    if (MQTT_SUCCESS_ERROR == rc)
    {
        rc = mqtt_try_resubscribe(c); /* 重新订阅 */
        /* 在重新连接后立即处理这些 ACK 消息 */
        mqtt_ack_list_scan(c, 0);
    }

    MQTT_LOG_D("%s:%d %s()... mqtt try connect result is -0x%04x", __FILE__, __LINE__, __FUNCTION__, -rc);

    RETURN_ERROR(rc);
}

/**
 * @brief 尝试执行 MQTT 客户端重新连接操作，包括连接失败后的重试机制
 *
 * @param c MQTT 客户端实例
 * @return int 返回状态码，MQTT_SUCCESS_ERROR 表示成功
 */
static int mqtt_try_reconnect(mqtt_client_t *c)
{
    int rc = MQTT_SUCCESS_ERROR;

    /* 在连接之前调用重连处理器，可以用于更新 MQTT 密码，例如：OneNet 平台需要 */
    if (NULL != c->mqtt_reconnect_handler)
    {
        c->mqtt_reconnect_handler(c, c->mqtt_reconnect_data);
    }

    rc = mqtt_try_do_reconnect(c);

    if (MQTT_SUCCESS_ERROR != rc)
    {
        /* 连接失败必须延迟重新连接尝试持续时间，并让 CPU 时间消耗尽可能少，以便最低优先级任务可以运行 */
        mqtt_sleep_ms(c->mqtt_reconnect_try_duration);
        RETURN_ERROR(MQTT_RECONNECT_TIMEOUT_ERROR);
    }

    RETURN_ERROR(rc);
}

/**
 * @brief 发布消息的确认报文处理函数
 *
 * @param c MQTT 客户端结构体指针
 * @param packet_id 报文标识符
 * @param packet_type 报文类型（PUBREC 或 PUBREL）
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
static int mqtt_publish_ack_packet(mqtt_client_t *c, uint16_t packet_id, int packet_type)
{
    int len = 0;
    int rc = MQTT_SUCCESS_ERROR;
    platform_timer_t timer;

    platform_mutex_lock(&c->mqtt_write_lock);

    switch (packet_type)
    {
    case PUBREC:
        len = MQTTSerialize_ack(c->mqtt_write_buf, c->mqtt_write_buf_size, PUBREL, 0, packet_id); /* 构造 PUBREL 确认报文 */
        rc = mqtt_ack_list_record(c, PUBCOMP, packet_id, len, NULL);                              /* 记录确认报文，期望接收 PUBCOMP */
        if (MQTT_SUCCESS_ERROR != rc)
            goto exit;
        break;

    case PUBREL:
        len = MQTTSerialize_ack(c->mqtt_write_buf, c->mqtt_write_buf_size, PUBCOMP, 0, packet_id); /* 构造 PUBCOMP 确认报文 */
        break;

    default:
        rc = MQTT_PUBLISH_ACK_TYPE_ERROR;
        goto exit;
    }

    if (len <= 0)
    {
        rc = MQTT_PUBLISH_ACK_PACKET_ERROR;
        goto exit;
    }

    rc = mqtt_send_packet(c, len, &timer); // 发送确认报文

exit:
    platform_mutex_unlock(&c->mqtt_write_lock);

    RETURN_ERROR(rc); // 返回处理结果
}

/**
 * @brief 处理 PUBACK 和 PUBCOMP 报文的函数
 *
 * @param c MQTT 客户端结构体指针
 * @param timer 超时计时器
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
static int mqtt_puback_and_pubcomp_packet_handle(mqtt_client_t *c, platform_timer_t *timer)
{
    int rc = MQTT_FAILED_ERROR;
    uint16_t packet_id;
    uint8_t dup, packet_type;

    rc = mqtt_is_connected(c); // 检查 MQTT 连接状态
    if (MQTT_SUCCESS_ERROR != rc)
        RETURN_ERROR(rc);

    if (MQTTDeserialize_ack(&packet_type, &dup, &packet_id, c->mqtt_read_buf, c->mqtt_read_buf_size) != 1)
        rc = MQTT_PUBREC_PACKET_ERROR; // 反序列化 PUBACK 或 PUBCOMP 报文失败

    (void)dup;
    rc = mqtt_ack_list_unrecord(c, packet_type, packet_id, NULL); /* 取消记录确认处理程序 */

    RETURN_ERROR(rc); // 返回处理结果
}

/**
 * @brief 处理 SUBACK 报文的函数
 *
 * @param c MQTT 客户端结构体指针
 * @param timer 超时计时器
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
static int mqtt_suback_packet_handle(mqtt_client_t *c, platform_timer_t *timer)
{
    int rc = MQTT_FAILED_ERROR;
    int count = 0;
    int granted_qos = 0;
    uint16_t packet_id;
    int is_nack = 0;
    message_handlers_t *msg_handler = NULL;

    rc = mqtt_is_connected(c); // 检查 MQTT 连接状态
    if (MQTT_SUCCESS_ERROR != rc)
        RETURN_ERROR(rc);

    /* 反序列化 SUBACK 报文 */
    if (MQTTDeserialize_suback(&packet_id, 1, &count, (int *)&granted_qos, c->mqtt_read_buf, c->mqtt_read_buf_size) != 1)
        RETURN_ERROR(MQTT_SUBSCRIBE_ACK_PACKET_ERROR);

    is_nack = (granted_qos == SUBFAIL);

    rc = mqtt_ack_list_unrecord(c, SUBACK, packet_id, &msg_handler); /* 取消记录确认处理程序 */

    if (!msg_handler)
        RETURN_ERROR(MQTT_MEM_NOT_ENOUGH_ERROR);

    if (is_nack)
    {
        mqtt_msg_handler_destory(msg_handler); /* 订阅主题失败，销毁消息处理程序 */
        MQTT_LOG_D("订阅主题失败...");
        RETURN_ERROR(MQTT_SUBSCRIBE_NOT_ACK_ERROR);
    }

    rc = mqtt_msg_handlers_install(c, msg_handler); // 安装消息处理程序

    RETURN_ERROR(rc); // 返回处理结果
}

/**
 * @brief 处理 UNSUBACK 报文的函数
 *
 * @param c MQTT 客户端结构体指针
 * @param timer 超时计时器
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
static int mqtt_unsuback_packet_handle(mqtt_client_t *c, platform_timer_t *timer)
{
    int rc = MQTT_FAILED_ERROR;
    message_handlers_t *msg_handler;
    uint16_t packet_id = 0;

    rc = mqtt_is_connected(c); // 检查 MQTT 连接状态
    if (MQTT_SUCCESS_ERROR != rc)
        RETURN_ERROR(rc);

    if (MQTTDeserialize_unsuback(&packet_id, c->mqtt_read_buf, c->mqtt_read_buf_size) != 1)
        RETURN_ERROR(MQTT_UNSUBSCRIBE_ACK_PACKET_ERROR);

    rc = mqtt_ack_list_unrecord(c, UNSUBACK, packet_id, &msg_handler); /* 取消记录确认处理程序，并获取消息处理程序 */

    if (!msg_handler)
        RETURN_ERROR(MQTT_MEM_NOT_ENOUGH_ERROR);

    mqtt_msg_handler_destory(msg_handler); /* 销毁消息处理程序 */

    RETURN_ERROR(rc); // 返回处理结果
}

static int mqtt_publish_packet_handle(mqtt_client_t *c, platform_timer_t *timer)
{
    int len = 0, rc = MQTT_SUCCESS_ERROR;
    MQTTString topic_name;
    mqtt_message_t msg;
    int qos;
    msg.payloadlen = 0; 
    
    rc = mqtt_is_connected(c);
    if (MQTT_SUCCESS_ERROR != rc)
        RETURN_ERROR(rc);

    if (MQTTDeserialize_publish(&msg.dup, &qos, &msg.retained, &msg.id, &topic_name,
        (uint8_t**)&msg.payload, (int*)&msg.payloadlen, c->mqtt_read_buf, c->mqtt_read_buf_size) != 1)
        RETURN_ERROR(MQTT_PUBLISH_PACKET_ERROR);
    
    msg.qos = (mqtt_qos_t)qos;

    /* for qos1 and qos2, you need to send a ack packet */
    if (msg.qos != QOS0) {
        platform_mutex_lock(&c->mqtt_write_lock);
        
        if (msg.qos == QOS1)
            len = MQTTSerialize_ack(c->mqtt_write_buf, c->mqtt_write_buf_size, PUBACK, 0, msg.id);
        else if (msg.qos == QOS2)
            len = MQTTSerialize_ack(c->mqtt_write_buf, c->mqtt_write_buf_size, PUBREC, 0, msg.id);

        if (len <= 0)
            rc = MQTT_SERIALIZE_PUBLISH_ACK_PACKET_ERROR;
        else
            rc = mqtt_send_packet(c, len, timer);
        
        platform_mutex_unlock(&c->mqtt_write_lock);
    }

    if (rc < 0)
        RETURN_ERROR(rc);

    if (msg.qos != QOS2)
        mqtt_deliver_message(c, &topic_name, &msg);
    else {
        /* record the received of a qos2 message and only processes it when the qos2 message is received for the first time */
        if ((rc = mqtt_ack_list_record(c, PUBREL, msg.id, len, NULL)) != MQTT_ACK_NODE_IS_EXIST_ERROR)
            mqtt_deliver_message(c, &topic_name, &msg);
    }
    
    RETURN_ERROR(rc);
}


static int mqtt_pubrec_and_pubrel_packet_handle(mqtt_client_t *c, platform_timer_t *timer)
{
    int rc = MQTT_FAILED_ERROR;
    uint16_t packet_id;
    uint8_t dup, packet_type;
    
    rc = mqtt_is_connected(c);
    if (MQTT_SUCCESS_ERROR != rc)
        RETURN_ERROR(rc);

    if (MQTTDeserialize_ack(&packet_type, &dup, &packet_id, c->mqtt_read_buf, c->mqtt_read_buf_size) != 1)
        RETURN_ERROR(MQTT_PUBREC_PACKET_ERROR);

    (void) dup;
    rc = mqtt_publish_ack_packet(c, packet_id, packet_type);    /* make a ack packet and send it */
    rc = mqtt_ack_list_unrecord(c, packet_type, packet_id, NULL);

    RETURN_ERROR(rc);
}

static int mqtt_packet_handle(mqtt_client_t* c, platform_timer_t* timer)
{
    int rc = MQTT_SUCCESS_ERROR;
    int packet_type = 0;
    
    rc = mqtt_read_packet(c, &packet_type, timer);

    switch (packet_type) {
        case 0: /* timed out reading packet or an error occurred while reading data*/
            if (MQTT_BUFFER_TOO_SHORT_ERROR == rc) {
                MQTT_LOG_E("the client read buffer is too short, please call mqtt_set_read_buf_size() to reset the buffer size");
                /* don't return directly, you need to stay active, because there is data readable now, but the buffer is too small */
            }
            break;

        case CONNACK: /* has been processed */
            goto exit;

        case PUBACK:
        case PUBCOMP:
            rc = mqtt_puback_and_pubcomp_packet_handle(c, timer);
            break;

        case SUBACK:
            rc = mqtt_suback_packet_handle(c, timer);
            break;
            
        case UNSUBACK:
            rc = mqtt_unsuback_packet_handle(c, timer);
            break;

        case PUBLISH:
            rc = mqtt_publish_packet_handle(c, timer);
            break;

        case PUBREC:
        case PUBREL:
            rc = mqtt_pubrec_and_pubrel_packet_handle(c, timer);
            break;

        case PINGRESP:
            c->mqtt_ping_outstanding = 0;    /* keep alive ping success */
            break;

        default:
            break;
    }

    rc = mqtt_keep_alive(c);

exit:
    if (rc == MQTT_SUCCESS_ERROR)
        rc = packet_type;

    RETURN_ERROR(rc);
}

static int mqtt_wait_packet(mqtt_client_t* c, int packet_type, platform_timer_t* timer)
{
    int rc = MQTT_FAILED_ERROR;

    do {
        if (platform_timer_is_expired(timer))
            break; 
        rc = mqtt_packet_handle(c, timer);
    } while (rc != packet_type && rc >= 0);

    RETURN_ERROR(rc);
}

static int mqtt_yield(mqtt_client_t* c, int timeout_ms)
{
    int rc = MQTT_SUCCESS_ERROR;
    client_state_t state;
    platform_timer_t timer;

    if (NULL == c)
        RETURN_ERROR(MQTT_FAILED_ERROR);

    if (0 == timeout_ms)
        timeout_ms = c->mqtt_cmd_timeout;

   
    platform_timer_cutdown(&timer, timeout_ms);
    
    while (!platform_timer_is_expired(&timer)) {
        state = mqtt_get_client_state(c);
        if (CLIENT_STATE_CLEAN_SESSION ==  state) {
            RETURN_ERROR(MQTT_CLEAN_SESSION_ERROR);
        } else if (CLIENT_STATE_CONNECTED != state) {
            /* mqtt not connect, need reconnect */
            rc = mqtt_try_reconnect(c);

            if (MQTT_RECONNECT_TIMEOUT_ERROR == rc)
                RETURN_ERROR(rc);
            continue;
        }
        
        /* mqtt connected, handle mqtt packet */
        rc = mqtt_packet_handle(c, &timer);

        if (rc >= 0) {
            /* scan ack list, destroy ack handler that have timed out or resend them */
            mqtt_ack_list_scan(c, 1);

        } else if (MQTT_NOT_CONNECT_ERROR == rc) {
            MQTT_LOG_E("%s:%d %s()... mqtt not connect", __FILE__, __LINE__, __FUNCTION__);
        } else {
            break;
        }
    }

    RETURN_ERROR(rc);
}

static void mqtt_yield_thread(void *arg)
{
    int rc;
    client_state_t state;
    mqtt_client_t *c = (mqtt_client_t *)arg;
    platform_thread_t *thread_to_be_destoried = NULL;
    
    state = mqtt_get_client_state(c);
        if (CLIENT_STATE_CONNECTED !=  state) {
            MQTT_LOG_W("%s:%d %s()..., mqtt is not connected to the server...",  __FILE__, __LINE__, __FUNCTION__);
            platform_thread_stop(c->mqtt_thread);    /* mqtt is not connected to the server, stop thread */
    }

    while (1) {
        rc = mqtt_yield(c, c->mqtt_cmd_timeout);
        if (MQTT_CLEAN_SESSION_ERROR == rc) {
            MQTT_LOG_W("%s:%d %s()..., mqtt clean session....", __FILE__, __LINE__, __FUNCTION__);
            network_disconnect(c->mqtt_network);
            mqtt_clean_session(c);
            goto exit;
        } else if (MQTT_RECONNECT_TIMEOUT_ERROR == rc) {
            MQTT_LOG_W("%s:%d %s()..., mqtt reconnect timeout....", __FILE__, __LINE__, __FUNCTION__);
        }
    }
    
exit:
    thread_to_be_destoried = c->mqtt_thread;
    c->mqtt_thread = (platform_thread_t *)0;
    platform_thread_destroy(thread_to_be_destoried);
}

static int mqtt_connect_with_results(mqtt_client_t* c)
{
    int len = 0;
    int rc = MQTT_CONNECT_FAILED_ERROR;
    platform_timer_t connect_timer;
    mqtt_connack_data_t connack_data = {0};
    MQTTPacket_connectData connect_data = MQTTPacket_connectData_initializer;

    if (NULL == c)
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    if (CLIENT_STATE_CONNECTED == mqtt_get_client_state(c))
        RETURN_ERROR(MQTT_SUCCESS_ERROR);



#ifndef MQTT_NETWORK_TYPE_NO_TLS
    rc = network_init(c->mqtt_network, c->mqtt_host, c->mqtt_port, c->mqtt_ca);
#else
    rc = network_init(c->mqtt_network, c->mqtt_host, c->mqtt_port, NULL);
#endif

    rc = network_connect(c->mqtt_network);
    if (MQTT_SUCCESS_ERROR != rc) {
        if (NULL != c->mqtt_network) {
            network_release(c->mqtt_network);
            RETURN_ERROR(rc);
        }  
    }
    
    MQTT_LOG_I("%s:%d %s()... mqtt connect success...", __FILE__, __LINE__, __FUNCTION__);

    connect_data.keepAliveInterval = c->mqtt_keep_alive_interval;
    connect_data.cleansession = c->mqtt_clean_session;
    connect_data.MQTTVersion = c->mqtt_version;
    connect_data.clientID.cstring= c->mqtt_client_id;
    connect_data.username.cstring = c->mqtt_user_name;
    connect_data.password.cstring = c->mqtt_password;

    if (c->mqtt_will_flag) {
        connect_data.willFlag = c->mqtt_will_flag;
        connect_data.will.message.cstring = c->mqtt_will_options->will_message;
        connect_data.will.qos = c->mqtt_will_options->will_qos;
        connect_data.will.retained = c->mqtt_will_options->will_retained;
        connect_data.will.topicName.cstring = c->mqtt_will_options->will_topic;
    }
    
    platform_timer_cutdown(&c->mqtt_last_received, (c->mqtt_keep_alive_interval * 1000));

    platform_mutex_lock(&c->mqtt_write_lock);

    /* serialize connect packet */
    if ((len = MQTTSerialize_connect(c->mqtt_write_buf, c->mqtt_write_buf_size, &connect_data)) <= 0)
        goto exit;
        
    platform_timer_cutdown(&connect_timer, c->mqtt_cmd_timeout);

    /* send connect packet */
    if ((rc = mqtt_send_packet(c, len, &connect_timer)) != MQTT_SUCCESS_ERROR)
        goto exit;

    if (mqtt_wait_packet(c, CONNACK, &connect_timer) == CONNACK) {
        if (MQTTDeserialize_connack(&connack_data.session_present, &connack_data.rc, c->mqtt_read_buf, c->mqtt_read_buf_size) == 1)
            rc = connack_data.rc;
        else
            rc = MQTT_CONNECT_FAILED_ERROR;
    } else
        rc = MQTT_CONNECT_FAILED_ERROR;

exit:
    if (rc == MQTT_SUCCESS_ERROR) {
        if(NULL == c->mqtt_thread) {

            /* connect success, and need init mqtt thread */
            c->mqtt_thread= platform_thread_init("mqtt_yield_thread", mqtt_yield_thread, c, MQTT_THREAD_STACK_SIZE, MQTT_THREAD_PRIO, MQTT_THREAD_TICK);

            if (NULL != c->mqtt_thread) {
                mqtt_set_client_state(c, CLIENT_STATE_CONNECTED);
                platform_thread_startup(c->mqtt_thread);
                platform_thread_start(c->mqtt_thread);       /* start run mqtt thread */
            } else {
                /*creat the thread fail and disconnect the mqtt socket connect*/
                network_release(c->mqtt_network);
                rc = MQTT_CONNECT_FAILED_ERROR;
                MQTT_LOG_W("%s:%d %s()... mqtt yield thread creat faile...", __FILE__, __LINE__, __FUNCTION__);    
            }
        } else {
            mqtt_set_client_state(c, CLIENT_STATE_CONNECTED);   /* reconnect, mqtt thread is already exists */
        }

        c->mqtt_ping_outstanding = 0;        /* reset ping outstanding */

    } else {
        network_release(c->mqtt_network);
        mqtt_set_client_state(c, CLIENT_STATE_INITIALIZED); /* connect failed */
    }
    
    platform_mutex_unlock(&c->mqtt_write_lock);

    RETURN_ERROR(rc);
}

static uint32_t mqtt_read_buf_malloc(mqtt_client_t* c, uint32_t size)
{
    MQTT_ROBUSTNESS_CHECK(c, 0);
    
    if (NULL != c->mqtt_read_buf)
        platform_memory_free(c->mqtt_read_buf);
    
    c->mqtt_read_buf_size = size;

    /* limit the size of the read buffer */
    if ((MQTT_MIN_PAYLOAD_SIZE >= c->mqtt_read_buf_size) || (MQTT_MAX_PAYLOAD_SIZE <= c->mqtt_read_buf_size))
        c->mqtt_read_buf_size = MQTT_DEFAULT_BUF_SIZE;
    
    c->mqtt_read_buf = (uint8_t*) platform_memory_alloc(c->mqtt_read_buf_size);
    
    if (NULL == c->mqtt_read_buf) {
        MQTT_LOG_E("%s:%d %s()... malloc read buf failed...", __FILE__, __LINE__, __FUNCTION__);
        RETURN_ERROR(MQTT_MEM_NOT_ENOUGH_ERROR);
    }
    return c->mqtt_read_buf_size;
}

static uint32_t mqtt_write_buf_malloc(mqtt_client_t* c, uint32_t size)
{
    MQTT_ROBUSTNESS_CHECK(c, 0);
    
    if (NULL != c->mqtt_write_buf)
        platform_memory_free(c->mqtt_write_buf);
    
    c->mqtt_write_buf_size = size;

    /* limit the size of the read buffer */
    if ((MQTT_MIN_PAYLOAD_SIZE >= c->mqtt_write_buf_size) || (MQTT_MAX_PAYLOAD_SIZE <= c->mqtt_write_buf_size))
        c->mqtt_write_buf_size = MQTT_DEFAULT_BUF_SIZE;
    
    c->mqtt_write_buf = (uint8_t*) platform_memory_alloc(c->mqtt_write_buf_size);
    
    if (NULL == c->mqtt_write_buf) {
        MQTT_LOG_E("%s:%d %s()... malloc write buf failed...", __FILE__, __LINE__, __FUNCTION__);
        RETURN_ERROR(MQTT_MEM_NOT_ENOUGH_ERROR);
    }
    return c->mqtt_write_buf_size;
}

static int mqtt_init(mqtt_client_t* c)
{
    /* network init */
    c->mqtt_network = (network_t*) platform_memory_alloc(sizeof(network_t));

    if (NULL == c->mqtt_network) {
        MQTT_LOG_E("%s:%d %s()... malloc memory failed...", __FILE__, __LINE__, __FUNCTION__);
        RETURN_ERROR(MQTT_MEM_NOT_ENOUGH_ERROR);
    }
    memset(c->mqtt_network, 0, sizeof(network_t));

    c->mqtt_packet_id = 1;
    c->mqtt_clean_session = 0;          //no clear session by default
    c->mqtt_will_flag = 0;
    c->mqtt_cmd_timeout = MQTT_DEFAULT_CMD_TIMEOUT;
    c->mqtt_client_state = CLIENT_STATE_INITIALIZED;
    
    c->mqtt_ping_outstanding = 0;
    c->mqtt_ack_handler_number = 0;
    c->mqtt_client_id_len = 0;
    c->mqtt_user_name_len = 0;
    c->mqtt_password_len = 0;
    c->mqtt_keep_alive_interval = MQTT_KEEP_ALIVE_INTERVAL;
    c->mqtt_version = MQTT_VERSION;
    c->mqtt_reconnect_try_duration = MQTT_RECONNECT_DEFAULT_DURATION;

    c->mqtt_will_options = NULL;
    c->mqtt_reconnect_data = NULL;
    c->mqtt_reconnect_handler = NULL;
    c->mqtt_interceptor_handler = NULL;
    
    mqtt_read_buf_malloc(c, MQTT_DEFAULT_BUF_SIZE);
    mqtt_write_buf_malloc(c, MQTT_DEFAULT_BUF_SIZE);

    mqtt_list_init(&c->mqtt_msg_handler_list);
    mqtt_list_init(&c->mqtt_ack_handler_list);
    
    platform_mutex_init(&c->mqtt_write_lock);
    platform_mutex_init(&c->mqtt_global_lock);

    platform_timer_init(&c->mqtt_last_sent);
    platform_timer_init(&c->mqtt_last_received);

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/********************************************************* mqttclient global function ********************************************************/

/**
 * @brief 定义 MQTT 客户端设置的宏，例如 client_id、user_name、password 等
 *
 * @param name 宏的名称
 * @param type 宏的数据类型
 * @param def_value 宏的默认值
 */
MQTT_CLIENT_SET_DEFINE(client_id, char *, NULL)
MQTT_CLIENT_SET_DEFINE(user_name, char *, NULL)
MQTT_CLIENT_SET_DEFINE(password, char *, NULL)
MQTT_CLIENT_SET_DEFINE(host, char *, NULL)
MQTT_CLIENT_SET_DEFINE(port, char *, NULL)
MQTT_CLIENT_SET_DEFINE(ca, char *, NULL)
MQTT_CLIENT_SET_DEFINE(reconnect_data, void *, NULL)
MQTT_CLIENT_SET_DEFINE(keep_alive_interval, uint16_t, 0)
MQTT_CLIENT_SET_DEFINE(will_flag, uint32_t, 0)
MQTT_CLIENT_SET_DEFINE(clean_session, uint32_t, 0)
MQTT_CLIENT_SET_DEFINE(version, uint32_t, 0)
MQTT_CLIENT_SET_DEFINE(cmd_timeout, uint32_t, 0)
MQTT_CLIENT_SET_DEFINE(reconnect_try_duration, uint32_t, 0)
MQTT_CLIENT_SET_DEFINE(reconnect_handler, reconnect_handler_t, NULL)
MQTT_CLIENT_SET_DEFINE(interceptor_handler, interceptor_handler_t, NULL)

/**
 * @brief 设置 MQTT 客户端读缓冲区大小
 *
 * @param c MQTT 客户端结构体指针
 * @param size 缓冲区大小
 * @return uint32_t 返回实际设置的缓冲区大小
 */
uint32_t mqtt_set_read_buf_size(mqtt_client_t *c, uint32_t size)
{
    return mqtt_read_buf_malloc(c, size);
}

/**
 * @brief 设置 MQTT 客户端写缓冲区大小
 *
 * @param c MQTT 客户端结构体指针
 * @param size 缓冲区大小
 * @return uint32_t 返回实际设置的缓冲区大小
 */
uint32_t mqtt_set_write_buf_size(mqtt_client_t *c, uint32_t size)
{
    return mqtt_write_buf_malloc(c, size);
}

/**
 * @brief 毫秒级延时函数
 *
 * @param ms 延时的毫秒数
 */
void mqtt_sleep_ms(int ms)
{
    platform_timer_usleep(ms * 1000);
}

/**
 * @brief MQTT 客户端保持活跃
 *
 * @param c MQTT 客户端结构体指针
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_keep_alive(mqtt_client_t *c)
{
    int rc = MQTT_SUCCESS_ERROR;

    rc = mqtt_is_connected(c);
    if (MQTT_SUCCESS_ERROR != rc)
        RETURN_ERROR(rc);

    // 检查是否需要发送 PINGREQ 报文
    if (platform_timer_is_expired(&c->mqtt_last_sent) || platform_timer_is_expired(&c->mqtt_last_received))
    {
        if (c->mqtt_ping_outstanding)
        {
            MQTT_LOG_W("%s:%d %s()... ping outstanding", __FILE__, __LINE__, __FUNCTION__);
            network_release(c->mqtt_network);

            mqtt_set_client_state(c, CLIENT_STATE_DISCONNECTED);
            rc = MQTT_NOT_CONNECT_ERROR; /* PINGRESP not received in keepalive interval */
        }
        else
        {
            platform_timer_t timer;
            int len = MQTTSerialize_pingreq(c->mqtt_write_buf, c->mqtt_write_buf_size);
            if (len > 0 && (rc = mqtt_send_packet(c, len, &timer)) == MQTT_SUCCESS_ERROR) // 发送 PINGREQ 报文
                c->mqtt_ping_outstanding++;
        }
    }

    RETURN_ERROR(rc);
}

/**
 * @brief 分配 MQTT 客户端结构体内存并初始化
 *
 * @return mqtt_client_t* 返回分配的 MQTT 客户端结构体指针，初始化失败时返回 NULL
 */
mqtt_client_t *mqtt_lease(void)
{
    int rc;
    mqtt_client_t *c;

    c = (mqtt_client_t *)platform_memory_alloc(sizeof(mqtt_client_t));
    if (NULL == c)
        return NULL;

    memset(c, 0, sizeof(mqtt_client_t));

    rc = mqtt_init(c);
    if (MQTT_SUCCESS_ERROR != rc)
        return NULL;

    return c;
}

/**
 * @brief 释放 MQTT 客户端结构体内存
 *
 * @param c MQTT 客户端结构体指针
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_release(mqtt_client_t *c)
{
    platform_timer_t timer;

    if (NULL == c)
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    // 设置定时器，用于超时控制
    platform_timer_cutdown(&timer, c->mqtt_cmd_timeout);

    /* 等待清理会话完成 */
    while ((CLIENT_STATE_INVALID != mqtt_get_client_state(c)))
    {
        // platform_timer_usleep(1000);            // 1ms 避免编译器优化。
        if (platform_timer_is_expired(&timer))
        {
            MQTT_LOG_E("%s:%d %s()... mqtt release failed...", __FILE__, __LINE__, __FUNCTION__);
            RETURN_ERROR(MQTT_FAILED_ERROR)
        }
    }

    // 释放网络结构体内存
    if (NULL != c->mqtt_network)
    {
        platform_memory_free(c->mqtt_network);
        c->mqtt_network = NULL;
    }

    // 释放读缓冲区内存
    if (NULL != c->mqtt_read_buf)
    {
        platform_memory_free(c->mqtt_read_buf);
        c->mqtt_read_buf = NULL;
    }

    // 释放写缓冲区内存
    if (NULL != c->mqtt_write_buf)
    {
        platform_memory_free(c->mqtt_write_buf);
        c->mqtt_write_buf = NULL;
    }

    platform_mutex_destroy(&c->mqtt_write_lock);
    platform_mutex_destroy(&c->mqtt_global_lock);

    memset(c, 0, sizeof(mqtt_client_t));

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 建立 MQTT 连接
 *
 * @param c MQTT 客户端结构体指针
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_connect(mqtt_client_t *c)
{
    /* 使用阻塞模式连接服务器并等待连接结果 */
    return mqtt_connect_with_results(c);
}

/**
 * @brief 断开 MQTT 连接
 *
 * @param c MQTT 客户端结构体指针
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_disconnect(mqtt_client_t *c)
{
    int rc = MQTT_FAILED_ERROR;
    platform_timer_t timer;
    int len = 0;

    // 设置定时器，用于超时控制
    platform_timer_cutdown(&timer, c->mqtt_cmd_timeout);

    platform_mutex_lock(&c->mqtt_write_lock);

    /* 序列化断开连接报文并发送 */
    len = MQTTSerialize_disconnect(c->mqtt_write_buf, c->mqtt_write_buf_size);
    if (len > 0)
        rc = mqtt_send_packet(c, len, &timer);

    platform_mutex_unlock(&c->mqtt_write_lock);

    // 设置客户端状态为清除会话状态
    mqtt_set_client_state(c, CLIENT_STATE_CLEAN_SESSION);

    RETURN_ERROR(rc);
}

/**
 * @brief 订阅 MQTT 主题
 *
 * @param c MQTT 客户端结构体指针
 * @param topic_filter 订阅的主题过滤器
 * @param qos 订阅的 QoS 等级
 * @param handler 消息处理函数指针
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_subscribe(mqtt_client_t *c, const char *topic_filter, mqtt_qos_t qos, message_handler_t handler)
{
    int rc = MQTT_SUBSCRIBE_ERROR;
    int len = 0;
    uint16_t packet_id;
    platform_timer_t timer;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topic_filter;
    message_handlers_t *msg_handler = NULL;

    if (CLIENT_STATE_CONNECTED != mqtt_get_client_state(c))
        RETURN_ERROR(MQTT_NOT_CONNECT_ERROR);

    // 获取下一个报文 ID
    packet_id = mqtt_get_next_packet_id(c);

    platform_mutex_lock(&c->mqtt_write_lock);

    /* 序列化订阅报文并发送 */
    len = MQTTSerialize_subscribe(c->mqtt_write_buf, c->mqtt_write_buf_size, 0, packet_id, 1, &topic, (int *)&qos);
    if (len <= 0)
        goto exit;

    if ((rc = mqtt_send_packet(c, len, &timer)) != MQTT_SUCCESS_ERROR)
        goto exit;

    // 如果 handler 为 NULL，则使用默认的消息处理函数
    if (NULL == handler)
        handler = default_msg_handler;

    /* 创建消息处理程序并记录 */
    msg_handler = mqtt_msg_handler_create(topic_filter, qos, handler);
    if (NULL == msg_handler)
    {
        rc = MQTT_MEM_NOT_ENOUGH_ERROR;
        goto exit;
    }

    rc = mqtt_ack_list_record(c, SUBACK, packet_id, len, msg_handler);

exit:

    platform_mutex_unlock(&c->mqtt_write_lock);

    RETURN_ERROR(rc);
}

/**
 * @brief 取消订阅 MQTT 主题
 *
 * @param c MQTT 客户端结构体指针
 * @param topic_filter 要取消订阅的主题过滤器
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_unsubscribe(mqtt_client_t *c, const char *topic_filter)
{
    int len = 0;
    int rc = MQTT_FAILED_ERROR;
    uint16_t packet_id;
    platform_timer_t timer;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topic_filter;
    message_handlers_t *msg_handler = NULL;

    if (CLIENT_STATE_CONNECTED != mqtt_get_client_state(c))
        RETURN_ERROR(MQTT_NOT_CONNECT_ERROR);

    // 获取下一个报文 ID
    packet_id = mqtt_get_next_packet_id(c);

    platform_mutex_lock(&c->mqtt_write_lock);

    /* 序列化取消订阅报文并发送 */
    if ((len = MQTTSerialize_unsubscribe(c->mqtt_write_buf, c->mqtt_write_buf_size, 0, packet_id, 1, &topic)) <= 0)
        goto exit;
    if ((rc = mqtt_send_packet(c, len, &timer)) != MQTT_SUCCESS_ERROR)
        goto exit;

    /* 获取已订阅消息处理程序 */
    msg_handler = mqtt_get_msg_handler(c, &topic);
    if (NULL == msg_handler)
    {
        rc = MQTT_MEM_NOT_ENOUGH_ERROR;
        goto exit;
    }

    rc = mqtt_ack_list_record(c, UNSUBACK, packet_id, len, msg_handler);

exit:

    platform_mutex_unlock(&c->mqtt_write_lock);

    RETURN_ERROR(rc);
}

/**
 * @brief 发布 MQTT 消息到指定主题
 *
 * @param c MQTT 客户端结构体指针
 * @param topic_filter 发布的主题过滤器
 * @param msg MQTT 消息结构体指针，包含要发布的消息内容
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_publish(mqtt_client_t *c, const char *topic_filter, mqtt_message_t *msg)
{
    int len = 0;
    int rc = MQTT_FAILED_ERROR;
    platform_timer_t timer;
    MQTTString topic = MQTTString_initializer;
    topic.cstring = (char *)topic_filter;

    if (CLIENT_STATE_CONNECTED != mqtt_get_client_state(c))
    {
        rc = MQTT_NOT_CONNECT_ERROR;
        goto exit;
    }

    // 如果消息的 payload 存在且长度为 0，则根据字符串长度设置 payload 长度
    if ((NULL != msg->payload) && (0 == msg->payloadlen))
        msg->payloadlen = strlen((char *)msg->payload);

    // 如果消息的 payload 长度大于客户端写缓冲区的大小，则报错并返回
    if (msg->payloadlen > c->mqtt_write_buf_size)
    {
        MQTT_LOG_E("publish payload len is greater than client write buffer...");
        RETURN_ERROR(MQTT_BUFFER_TOO_SHORT_ERROR);
    }

    platform_mutex_lock(&c->mqtt_write_lock);

    // 如果 QoS 不为 0，则生成报文 ID，并记录 ack handler
    if (QOS0 != msg->qos)
    {
        if (mqtt_ack_handler_is_maximum(c))
        {
            rc = MQTT_ACK_HANDLER_NUM_TOO_MUCH_ERROR; /* 记录的 ack 处理程序达到最大值 */
            goto exit;
        }
        msg->id = mqtt_get_next_packet_id(c);
    }

    /* 序列化发布报文并发送 */
    len = MQTTSerialize_publish(c->mqtt_write_buf, c->mqtt_write_buf_size, 0, msg->qos, msg->retained, msg->id,
                                topic, (uint8_t *)msg->payload, msg->payloadlen);
    if (len <= 0)
        goto exit;

    // 发送报文，并处理返回结果
    if ((rc = mqtt_send_packet(c, len, &timer)) != MQTT_SUCCESS_ERROR)
        goto exit;

    // 如果 QoS 不为 0，则记录 ack handler，并设置 dup 标志
    if (QOS0 != msg->qos)
    {
        mqtt_set_publish_dup(c, 1); /* 可能会重发此数据，提前设置 dup 标志 */

        if (QOS1 == msg->qos)
        {
            /* 期望接收 PUBACK，否则数据将被重新发送 */
            rc = mqtt_ack_list_record(c, PUBACK, msg->id, len, NULL);
        }
        else if (QOS2 == msg->qos)
        {
            /* 期望接收 PUBREC，否则数据将被重新发送 */
            rc = mqtt_ack_list_record(c, PUBREC, msg->id, len, NULL);
        }
    }

exit:
    msg->payloadlen = 0; // 清空 payload 长度

    platform_mutex_unlock(&c->mqtt_write_lock);

    // 处理内存不足或 ack handler 过多的情况
    if ((MQTT_ACK_HANDLER_NUM_TOO_MUCH_ERROR == rc) || (MQTT_MEM_NOT_ENOUGH_ERROR == rc))
    {
        MQTT_LOG_W("%s:%d %s()... 记录过多重传数据，可能被断开连接，需要重新连接...", __FILE__, __LINE__, __FUNCTION__);

        /* 释放网络连接 zhaoshimin 20200629 */
        network_release(c->mqtt_network);

        /* 记录过多重传数据，可能被断开连接，需要重新连接 */
        mqtt_set_client_state(c, CLIENT_STATE_DISCONNECTED);
    }

    RETURN_ERROR(rc);
}

/**
 * @brief 列出已订阅的 MQTT 主题
 *
 * @param c MQTT 客户端结构体指针
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_list_subscribe_topic(mqtt_client_t *c)
{
    int i = 0;
    mqtt_list_t *curr, *next;
    message_handlers_t *msg_handler;

    if (NULL == c)
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    if (mqtt_list_is_empty(&c->mqtt_msg_handler_list))
        MQTT_LOG_I("%s:%d %s()... 没有订阅的主题...", __FILE__, __LINE__, __FUNCTION__);

    LIST_FOR_EACH_SAFE(curr, next, &c->mqtt_msg_handler_list)
    {
        msg_handler = LIST_ENTRY(curr, message_handlers_t, list);
        /* 通过 MQTT 主题判断节点是否已存在，不支持通配符 */
        if (NULL != msg_handler->topic_filter)
        {
            MQTT_LOG_I("%s:%d %s()...[%d] 订阅主题: %s", __FILE__, __LINE__, __FUNCTION__, ++i, msg_handler->topic_filter);
        }
    }

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}

/**
 * @brief 设置 MQTT 遗嘱消息选项
 *
 * @param c MQTT 客户端结构体指针
 * @param topic 遗嘱主题
 * @param qos 遗嘱消息的 QoS 等级
 * @param retained 是否为保留消息
 * @param message 遗嘱消息内容
 * @return int 返回处理结果，可能是成功或失败的状态码
 */
int mqtt_set_will_options(mqtt_client_t *c, char *topic, mqtt_qos_t qos, uint8_t retained, char *message)
{
    if ((NULL == c) || (NULL == topic))
        RETURN_ERROR(MQTT_NULL_VALUE_ERROR);

    if (NULL == c->mqtt_will_options)
    {
        c->mqtt_will_options = (mqtt_will_options_t *)platform_memory_alloc(sizeof(mqtt_will_options_t));
        MQTT_ROBUSTNESS_CHECK(c->mqtt_will_options, MQTT_MEM_NOT_ENOUGH_ERROR);
    }

    if (0 == c->mqtt_will_flag)
        c->mqtt_will_flag = 1;

    c->mqtt_will_options->will_topic = topic;
    c->mqtt_will_options->will_qos = qos;
    c->mqtt_will_options->will_retained = retained;
    c->mqtt_will_options->will_message = message;

    RETURN_ERROR(MQTT_SUCCESS_ERROR);
}
