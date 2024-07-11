/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Date: 2019-12-11 22:46:33
 * @LastEditTime: 2020-04-27 23:28:12
 * @Description: the following code references TencentOS tiny, please keep the author information and source code according to the license.
 */

#include "mqtt_list.h"

/**
 * @brief 将节点添加到链表中的指定位置。
 *
 * @param node 要添加的节点。
 * @param prev 新节点的前一个节点。
 * @param next 新节点的后一个节点。
 */
static void _mqtt_list_add(mqtt_list_t *node, mqtt_list_t *prev, mqtt_list_t *next)
{
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

/**
 * @brief 从链表中删除指定节点。
 *
 * @param prev 要删除节点的前一个节点。
 * @param next 要删除节点的后一个节点。
 */
static void _mqtt_list_del(mqtt_list_t *prev, mqtt_list_t *next)
{
    next->prev = prev;
    prev->next = next;
}

/**
 * @brief 从链表中删除指定的入口节点。
 *
 * @param entry 要删除的入口节点。
 */
static void _mqtt_list_del_entry(mqtt_list_t *entry)
{
    _mqtt_list_del(entry->prev, entry->next);
}

/**
 * @brief 初始化链表。
 *
 * @param list 要初始化的链表。
 */
void mqtt_list_init(mqtt_list_t *list)
{
    list->next = list;
    list->prev = list;
}

/**
 * @brief 将节点添加到链表的头部。
 *
 * @param node 要添加的节点。
 * @param list 链表头。
 */
void mqtt_list_add(mqtt_list_t *node, mqtt_list_t *list)
{
    _mqtt_list_add(node, list, list->next);
}

/**
 * @brief 将节点添加到链表的尾部。
 *
 * @param node 要添加的节点。
 * @param list 链表头。
 */
void mqtt_list_add_tail(mqtt_list_t *node, mqtt_list_t *list)
{
    _mqtt_list_add(node, list->prev, list);
}

/**
 * @brief 删除链表中的指定节点。
 *
 * @param entry 要删除的节点。
 */
void mqtt_list_del(mqtt_list_t *entry)
{
    _mqtt_list_del(entry->prev, entry->next);
}

/**
 * @brief 删除链表中的指定节点并初始化。
 *
 * @param entry 要删除的节点。
 */
void mqtt_list_del_init(mqtt_list_t *entry)
{
    _mqtt_list_del_entry(entry);
    mqtt_list_init(entry);
}

/**
 * @brief 将节点移动到链表的头部。
 *
 * @param node 要移动的节点。
 * @param list 链表头。
 */
void mqtt_list_move(mqtt_list_t *node, mqtt_list_t *list)
{
    _mqtt_list_del_entry(node);
    mqtt_list_add(node, list);
}

/**
 * @brief 将节点移动到链表的尾部。
 *
 * @param node 要移动的节点。
 * @param list 链表头。
 */
void mqtt_list_move_tail(mqtt_list_t *node, mqtt_list_t *list)
{
    _mqtt_list_del_entry(node);
    mqtt_list_add_tail(node, list);
}

/**
 * @brief 检查链表是否为空。
 *
 * @param list 要检查的链表。
 * @return int 如果链表为空返回1，否则返回0。
 */
int mqtt_list_is_empty(mqtt_list_t *list)
{
    return list->next == list;
}
