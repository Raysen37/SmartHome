（未完成），该案例来自《**毕设级项目：基于STM32F103-MINI开发板-使用MQTT实现智能家居**》- 韦东山

1. ## 整体思路

1. **应用层（platform_net_socket.c）**：
   - **职责**：负责高层业务逻辑，实现通过AT指令连接网络、发送和接收数据。
   - 功能：
     - 调用AT指令进行网络连接。
     - 通过AT指令发送和接收网络数据。
     - 处理网络相关的任务和状态管理。
2. **ESP8266驱动层（ESP8266）**：
   - **职责**：提供具体的AT指令函数，用于控制ESP8266模块。
   - 功能：
     - 提供函数接口，用于发送AT指令（如连接网络、发送数据、接收数据等）。
     - 解析AT指令的响应，设置相应的状态。
3. **UART驱动抽象层**：
   - **职责**：提供通用的UART读写接口，抽象底层硬件操作。（如`USART2_Write`和`USART2_Read`
   - 功能：
     - 实现UART数据的写入函数。
     - 实现UART数据从环形缓冲区的读取函数。
     - 管理UART的中断和互斥锁。
4. **UART硬件驱动层**：
   - **职责**：直接与硬件交互，执行具体的UART数据发送和接收操作。（如`UART_Send`和`UART_Receive`）
   - 功能：
     - 发送UART数据到ESP8266模块。
     - 接收UART数据并存入环形缓冲区。
     - 处理UART中断，管理缓冲区的数据读写。

通过这种分层结构，可以清晰地将不同层次的功能进行分离，使得代码结构更加清晰，便于维护和扩展。

2. ## 现实现功能

我们将实现一个系统，其中有两个任务：一个任务用于发送 AT 指令并等待响应，另一个任务用于解析接收到的 AT 指令响应。系统还包含串口通信的初始化和处理。

### 任务和功能说明

1. **AT 指令发送任务 (`Task_ATTest`)**：
   - 持续发送 AT 指令并等待响应。
   - 使用 `ATSendCmd` 函数发送 AT 指令并获取响应。

2. **AT 指令响应解析任务 (`ATRecvParser`)**：
   - 持续接收并解析从 WIFI 模块发来的 AT 指令响应。
   - 使用 `HAL_AT_Recv` 阻塞式接收数据。
   - 解析数据并设置相应状态，释放互斥锁以通知发送任务。

3. **串口初始化和中断处理**：
   - 初始化串口及互斥锁。
   - 使用互斥锁同步串口数据的读取和处理。

### 详细代码

#### 1. 任务创建

```c
osThreadNew(ATRecvParser, NULL, &defaultTask_attributes);
osThreadNew(Task_ATTest, NULL, &defaultTask_attributes);
```

#### 2. 发送 AT 指令并等待响应 (`ATSendCmd`)

```c
/**
 * @brief 发送 AT 指令并等待响应
 *
 * @param buf AT 指令缓冲区
 * @param len AT 指令长度
 * @param resp 响应数据缓冲区，用于存储从 WIFI 模块返回的响应数据
 * @param resp_len 响应数据缓冲区长度
 * @param timeout 超时时间，单位毫秒
 * @return int 返回执行状态，可以是 AT_OK, AT_ERR 或 AT_TIMEOUT
 */
int ATSendCmd(char *buf, int len, char *resp, int resp_len, int timeout)
{
    int ret;
    int err;

    /* 发送AT命令 */
    HAL_AT_Send(buf, len);  // 调用 HAL 层发送 AT 指令
    HAL_AT_Send("\r\n", 2); // 发送回车换行符结束指令

    /**
     * 1 : 成功得到mutex
     * 0 : 超时返回
     */
    ret = platform_mutex_lock_timeout(&at_ret_mutex, AT_CMD_TIMEOUT); // 获取互斥锁，设置超时时间

    if (ret)
    {
        /* 判断返回值 */
        err = GetATStatus(); // 获取当前 AT 指令执行状态
        if (!err && resp)
        {
            memcpy(resp, g_at_resp, resp_len); // 将全局响应数据复制到用户指定的响应缓冲区
        }
        return err; // 返回执行状态
    }
    else
    {
        return AT_TIMEOUT; // 获取互斥锁超时，返回超时状态
    }
}
```

#### 3. AT 指令响应解析任务 (`ATRecvParser`)

```c
/**
 * @brief AT 指令响应解析函数
 *
 * @param params 参数，通常为 NULL 或保留用途
 */
void ATRecvParser(void *params)
{
    char buf[AT_RESP_LEN]; // 响应数据缓冲区
    int i = 0;

    while (1)
    {
        /* 读取 WIFI 模块发来的数据：使用阻塞方式 */
        HAL_AT_Recv(&buf[i], portMAX_DELAY); // 使用 HAL 层接收数据，阻塞等待

        /* 解析结果 */
        if (i && (buf[i - 1] == '\r') && (buf[i] == '\n'))
        {
            /* 得到回车换行 */
            buf[i + 1] = '\0'; // 添加字符串结束符

            /* 解析响应 */
            if (strstr(buf, "OK\r\n"))
            {
                /* 记录数据 */
                memcpy(g_at_resp, buf, i);            // 将响应数据复制到全局响应缓冲区
                SetATStatus(AT_OK);                   // 设置执行状态为 OK
                platform_mutex_unlock(&at_ret_mutex); // 解锁互斥锁，允许其他线程访问
            }
            else if (strstr(buf, "ERROR\r\n"))
            {
                SetATStatus(AT_ERR);                  // 设置执行状态为 ERROR
                platform_mutex_unlock(&at_ret_mutex); // 解锁互斥锁，允许其他线程访问
            }
            else if (GetSpecialATString(buf))
            {
                ProcessSpecialATString(buf); // 处理特殊的 AT 指令响应
                i = -1;                      // 重置索引，继续接收新的数据
            }
            i++;
        }
    }
}
```

#### 4. AT 指令测试任务 (`Task_ATTest`)

```c
/**
 * @brief AT 指令测试任务
 *
 * @param Param 参数，通常为 NULL 或保留用途
 */
void Task_ATTest(void *Param)
{
    int ret;

    while (1)
    {
        ret = ATSendCmd("AT", 2, NULL, 0, 1000); // 发送 AT 指令 "AT"
        printf("ATSendCmd ret = %d\r\n", ret);   // 打印发送命令的返回状态
    }
}
```

#### 5. 串口互斥锁初始化 (`UART2_Lock_Init`)

```c
/**
 * @brief 初始化 UART2 的互斥锁
 */
void UART2_Lock_Init(void)
{
    platform_mutex_init(&uart_recv_mutex);  // 初始化互斥锁
    platform_mutex_lock(&uart_recv_mutex); // 初始状态锁定互斥锁
}
```

#### 6. 串口数据发送 (`USART2_Write`)

```c
/**
 * @brief 通过 UART2 发送数据
 *
 * @param buf 数据缓冲区
 * @param len 数据长度
 */
void USART2_Write(char *buf, int len)
{
    int i = 0;
    while (i < len)
    {
        while ((huart2.Instance->SR & USART_SR_TXE) == 0); // 等待发送缓冲区为空
        huart2.Instance->DR = buf[i]; // 发送数据
        i++;
    }
}
```

#### 7. 串口数据接收 (`USART2_Read`)

```c
/**
 * @brief 通过 UART2 接收数据
 *
 * @param c 数据缓冲区
 * @param timeout 超时时间
 */
void USART2_Read(char *c, int timeout)
{
    while (1)
    {
        if (0 == ring_buffer_read(c, &uart2_buffer))
            return;
        else
        {
            platform_mutex_lock_timeout(&uart_recv_mutex, timeout); // 获取互斥锁
        }
    }
}
```

#### 8. 串口中断处理 (`USART2_IRQHandler`)

```c
/**
 * @brief UART2 中断处理函数
 */
void USART2_IRQHandler(void)
{
    uint32_t isrflags = READ_REG(huart2.Instance->SR); // 读取状态寄存器
    uint32_t cr1its = READ_REG(huart2.Instance->CR1); // 读取控制寄存器1
    char c;

    if (((isrflags & USART_SR_RXNE) != RESET) && ((cr1its & USART_CR1_RXNEIE) != RESET))
    {
        c = huart2.Instance->DR; // 读取数据寄存器
        ring_buffer_write(c, &uart2_buffer); // 写入环形缓冲区
        platform_mutex_unlock(&uart_recv_mutex); // 释放互斥锁
        return;
    }
}
```

### 总结

1. 初始化互斥锁并创建两个任务。
2. `Task_ATTest` 任务不断发送 AT 指令并等待响应。
3. `ATRecvParser` 任务不断接收并解析 AT 指令响应，更新全局状态并解锁互斥锁。
4. 串口相关的初始化、数据发送、接收及中断处理通过互斥锁进行同步，确保数据的正确性和线程安全。

这样，通过上述步骤，我们构建了一个能够发送 AT 指令并解析响应的系统，并且通过互斥锁保证数据同步和线程安全。