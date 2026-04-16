# rframe 使用文档

## 1. 目标

`rframe` 是基于 `tty_driver` 的一层轻量帧协议封装，用来在串口、`/dev/ttyRPMSG*`、`/dev/pts/*` 等字节流设备上收发固定格式的数据帧。

它的职责很简单：

- 发送时，把业务数据组织成一帧并交给 `tty_driver` 发送
- 接收时，从连续字节流里重新拼出完整帧，然后回调给业务层

## 2. 帧格式

当前帧格式如下：

```text
header      : 2 字节，固定值 0xAA55
cmd         : 2 字节，命令字
data_length : 1 字节，payload 数据长度
data        : N 字节，最大 256 字节
```

整体结构对应 `rframe_payload_t`：

```c
typedef struct {
	uint16_t header;
	uint16_t cmd;
	uint8_t data_length;
	uint8_t data[256];
} rframe_payload_t;
```

### 2.1 发送原理

`rframe_send_payload(...)` 会做两件事：

1. 自动把 `header` 填成 `0xAA55`
2. 按结构体内存布局将 `header + cmd + data_length + data` 一次性发给底层 `tty_driver`

因此，业务层只需要填写 `cmd`、`data_length` 和 `data`。

### 2.2 接收原理

`rframe` 内部维护一个简单状态机，把连续字节流拆解为以下状态：

- `WAIT_HEADER`：等待帧头
- `WAIT_CMD`：收集命令字
- `WAIT_LENGTH`：读取数据长度
- `WAIT_DATA`：收集完整数据区

当收到完整帧后，会组装出一个 `rframe_payload_t`，并调用初始化时传入的回调函数。

## 3. 对外接口

```c
typedef void (*rframe_rx_payload_handler_t)(rframe_payload_t payload, void *user_ctx);

tty_driver_t *rframe_init(char *device_path,
						  rframe_rx_payload_handler_t rx_payload_handler,
						  void *user_ctx);
uint8_t rframe_close(tty_driver_t *drv);
uint8_t rframe_send_payload(tty_driver_t *drv, rframe_payload_t *payload_p);
```

### 3.1 `rframe_init(...)`

初始化 `rframe` 并绑定接收回调。

参数说明：

- `device_path`：设备路径，例如 `/dev/ttyRPMSG0` 或 `/dev/pts/7`
- `rx_payload_handler`：收到完整帧后的业务回调
- `user_ctx`：透传给回调的用户上下文指针。它不属于 `rframe` 自己，而是你业务层附带进去的一块状态，回调触发时会原样传回。

返回值：

- 成功：返回已打开的 `tty_driver_t *`
- 失败：返回 `NULL`

### 3.2 `rframe_send_payload(...)`

发送一个 `rframe_payload_t`。

约束：

- `payload_p` 不能为空
- `payload_p->data_length` 不能超过 256

### 3.3 `rframe_close(...)`

关闭底层 `tty_driver` 并释放资源。

## 4. 最小使用示例

### 4.1 接收示例

```c
#include <stdio.h>
#include <string.h>

#include "rframe.h"

static void on_rframe_payload(rframe_payload_t payload, void *user_ctx)
{
	const char *tag = (const char *)user_ctx;

	printf("[%s] rx frame: header=0x%04X cmd=0x%04X len=%u\n",
		   tag,
		   payload.header,
		   payload.cmd,
		   payload.data_length);

	printf("data:");
	for (uint8_t i = 0; i < payload.data_length; i++) {
		printf(" %02X", payload.data[i]);
	}
	printf("\n");
}

typedef struct {
	int session_id;
	const char *name;
} app_ctx_t;

int main(void)
{
	app_ctx_t app_ctx = {
		.session_id = 1,
		.name = "rframe-demo",
	};

	tty_driver_t *drv = rframe_init("/dev/ttyRPMSG0", on_rframe_payload, &app_ctx);
	if (drv == NULL) {
		perror("rframe_init");
		return 1;
	}

	getchar();

	rframe_close(drv);
	return 0;
}
```

### 4.2 发送示例

```c
#include <stdio.h>
#include <string.h>

#include "rframe.h"

static void on_rframe_payload(rframe_payload_t payload, void *user_ctx)
{
	(void)payload;
	(void)user_ctx;
}

int main(void)
{
	tty_driver_t *drv = rframe_init("/dev/ttyRPMSG0", on_rframe_payload, NULL);
	if (drv == NULL) {
		perror("rframe_init");
		return 1;
	}

	rframe_payload_t payload = {
		.cmd = 0x0102,
		.data_length = 4,
		.data = {0xDE, 0xAD, 0xBE, 0xEF},
	};

	if (rframe_send_payload(drv, &payload) != 0) {
		perror("rframe_send_payload");
	}

	rframe_close(drv);
	return 0;
}
```

## 5. 使用流程建议

推荐的调用顺序是：

1. 准备业务回调函数
2. 调用 `rframe_init(...)` 打开设备并注册回调
3. 使用 `rframe_send_payload(...)` 发帧
4. 在回调里处理收到的完整 payload
5. 结束时调用 `rframe_close(...)`

## 6. 注意事项

- 回调函数会在底层接收线程中触发，建议只做轻量处理。
- 如果业务逻辑较重，建议在回调中把 `payload` 投递到消息队列，再由主线程处理。
- `data_length` 不能超过 `256`，否则发送接口会返回错误。
- `rframe_init(...)` 需要传入非空回调；如果你不想处理接收数据，也可以传一个空实现。

## 7. 结合现有示例

仓库里的 `tests/test_rframe.c` 已经展示了如何初始化 `rframe` 并验证发送路径；你可以在此基础上补一个接收方向的回归测试，验证外部设备发来的帧能被正确拆包。
