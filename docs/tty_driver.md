# tty_driver 使用文档

## 1. 目标

`tty_driver` 用于 Linux 字符设备（例如 `/dev/pts/8`、`/dev/ttyRPMSG0`）的收发。

它对外暴露两个核心接口：

- `tty_driver_send(...)`：发送任意长度数据
- `tty_driver_set_rx_callback(...)`：注册接收回调

回调触发时机满足两类场景：

- 接收空闲（`IDLE`）：一段时间内没有新字节到达
- 缓冲区写满（`BUFFER_FULL`）：接收缓冲区达到上限

## 2. 架构原理

### 2.1 打开与初始化

`tty_driver_open(...)` 会完成以下动作：

- 校验配置参数
- 打开设备文件（非阻塞模式）
- 将设备设置为 `raw` 模式（关闭行规约、回显等）
- 分配 RX 缓冲区
- 启动后台接收线程

### 2.2 发送路径（任意长度）

`tty_driver_send(...)` 的行为：

- 内部使用发送互斥锁，保证多线程并发发送时数据完整
- 循环调用 `write(...)`，直到把 `len` 字节全部写完
- 若设备暂时不可写（`EAGAIN/EWOULDBLOCK`），通过 `poll(POLLOUT)` 等待后继续写

因此调用者可以传入任意长度缓冲区，不需要手动分片。

### 2.3 接收路径（回调触发）

后台线程循环做两件事：

- `poll(POLLIN)` 等待设备可读
- 读取数据写入内部 RX 缓冲区

当满足以下条件之一时触发回调：

1. 缓冲区满：立即触发，`reason = TTY_RX_REASON_BUFFER_FULL`
2. 接收空闲：从最后一次收到数据开始，超过 `idle_timeout_ms` 无新数据，触发，`reason = TTY_RX_REASON_IDLE`

回调原型：

```c
typedef void (*tty_rx_callback_t)(const uint8_t *data,
                                  size_t len,
                                  tty_rx_reason_t reason,
                                  void *user_ctx);
```

回调参数说明：

- `data`：本次聚合后的数据起始地址
- `len`：本次数据长度
- `reason`：触发原因（空闲或满）
- `user_ctx`：用户上下文，来自注册时的参数

## 3. 对外 API

```c
int tty_driver_open(tty_driver_t *drv, const tty_driver_config_t *cfg);
void tty_driver_close(tty_driver_t *drv);
ssize_t tty_driver_send(tty_driver_t *drv, const void *data, size_t len);
int tty_driver_set_rx_callback(tty_driver_t *drv,
                               tty_rx_callback_t cb,
                               void *user_ctx);
```

### 3.1 配置结构

```c
typedef struct tty_driver_config {
    const char *device_path;      // 设备路径，如 /dev/ttyRPMSG0
    size_t rx_buffer_size;         // 接收缓存大小
    unsigned int idle_timeout_ms;  // 空闲触发阈值（毫秒）
} tty_driver_config_t;
```

## 4. 最小使用示例

```c
#include <stdio.h>
#include <string.h>
#include "tty_driver.h"

static void on_rx(const uint8_t *data,
                  size_t len,
                  tty_rx_reason_t reason,
                  void *user_ctx)
{
    (void)user_ctx;
    printf("RX callback: len=%zu, reason=%s\n",
           len,
           reason == TTY_RX_REASON_IDLE ? "IDLE" : "BUFFER_FULL");

    // 示例：按文本打印（实际项目可按二进制处理）
    fwrite(data, 1, len, stdout);
    printf("\n");
}

int main(void)
{
    tty_driver_t drv;
    tty_driver_config_t cfg = {
        .device_path = "/dev/ttyRPMSG0",   // 或 /dev/pts/8
        .rx_buffer_size = 1024,
        .idle_timeout_ms = 20,
    };

    if (tty_driver_open(&drv, &cfg) != 0) {
        perror("tty_driver_open");
        return 1;
    }

    if (tty_driver_set_rx_callback(&drv, on_rx, NULL) != 0) {
        perror("tty_driver_set_rx_callback");
        tty_driver_close(&drv);
        return 1;
    }

    const char *msg = "hello tty driver";
    if (tty_driver_send(&drv, msg, strlen(msg)) < 0) {
        perror("tty_driver_send");
    }

    // 这里仅示例，实际可换成事件循环或主业务逻辑
    getchar();

    tty_driver_close(&drv);
    return 0;
}
```

## 5. 使用建议

- `rx_buffer_size` 决定一次回调可聚合的数据上限，太小会更频繁触发 `BUFFER_FULL`。
- `idle_timeout_ms` 越小，回调延迟越低，但回调次数通常会增加。
- 回调运行在后台接收线程中，建议：
  - 不要做长时间阻塞
  - 不要在回调中做重 I/O
  - 复杂处理可投递到业务线程/队列
- 若设备是串口/TTY，确保外部配置与 raw 模式预期一致（波特率、流控等根据实际场景补充配置）。

## 6. 生命周期与线程安全

- `tty_driver_open(...)` 成功后才可发送或注册回调。
- `tty_driver_close(...)` 会停止线程并释放资源，关闭后不可继续调用发送接口。
- 发送接口内部已加锁，支持多线程并发发送。
- 回调注册接口会更新内部回调指针；建议在业务层保证回调函数生命周期有效。

## 7. 常见问题

### 7.1 为什么有时一次收到很多字节才回调？

因为驱动采用“聚合后回调”策略，满足以下任一条件才触发：

- 空闲超时
- 缓冲区满

这能减少高频小包回调开销。

### 7.2 如果对端持续高速发送会怎样？

在空闲不出现的情况下，会主要由 `BUFFER_FULL` 驱动回调节奏。建议适当增大 `rx_buffer_size`。
