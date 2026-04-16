# frf: 串口帧收发与 PTY/RPMSG 桥接示例（CMake + CTest + Unity）

该仓库是一个面向 Linux 的 C 项目，包含两条能力线：

- rframe/tty_driver：对串口设备进行帧发送和异步接收
- pts：创建虚拟串口（PTY），可用于 loopback 或桥接到外部设备（如 RPMSG TTY）

当前 `frf` 可作为 Unix socket 守护进程运行，把其他进程发来的请求转换为 RPMSG 帧发送。

项目同时集成了 CTest 与 Unity，便于持续回归验证。

## 1. 当前构建产物

根 CMake 会生成以下可执行文件：

- frf：主程序，使用 rframe 发送示例 payload 到指定串口
- frf_pts：PTY 工具，可在 loopback 或 bridge 模式运行

另外，src 中还会构建以下库目标：

- rframe：基于 tty_driver 的帧封装
- pts_runtime：pts 逻辑库（编译时定义 PTS_NO_MAIN）

## 2. 目录结构

```text
c-arm/
├─ CMakeLists.txt
├─ src/
│  ├─ CMakeLists.txt
│  ├─ main.c
│  ├─ tty_driver.h
│  ├─ tty_driver.c
│  ├─ rframe.h
│  ├─ rframe.c
│  ├─ pts.h
│  └─ pts.c
├─ tests/
│  ├─ CMakeLists.txt
│  ├─ test_rframe.c
│  └─ test_pts.c
├─ 3rdparty/
│  └─ unity/
└─ cmake/
   └─ toolchains/
      └─ rk3506-arm-linux-gnueabihf.cmake
```

## 3. 本机构建与运行

### 3.1 配置与编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 3.2 运行 frf_pts（推荐先做）

1) loopback 模式（不接外部桥接设备）：

```bash
./build/frf_pts
```

程序会打印一个虚拟串口路径（如 /dev/pts/7）。

2) bridge 模式（桥接到实际设备，例如 RPMSG TTY）：

```bash
./build/frf_pts /dev/ttyRPMSG0
```

### 3.3 运行 frf

`frf` 现在会启动一个 Unix socket 守护进程，默认监听 `/tmp/frf.sock`，并把收到的请求发送到指定串口设备。

```bash
./build/frf /dev/pts/7 /tmp/frf.sock
```

如果省略参数，则默认使用 `/dev/pts/7` 和 `/tmp/frf.sock`。

#### 请求格式

客户端使用 `SOCK_SEQPACKET` 连接 socket，每个请求发送一帧：

- `cmd`：2 字节，网络字节序
- `data_length`：1 字节
- `data`：`data_length` 字节

服务端返回 5 字节响应：

- `status`：1 字节，`0` 表示发送成功
- `errno`：4 字节，网络字节序，`status != 0` 时表示失败原因

示例请求：

```text
cmd = 0x0102
data_length = 4
data = DE AD BE EF
```

## 4. 测试（CTest + Unity）

默认开启 BUILD_TESTING 时会构建并注册以下测试：

- test_rframe
- test_pts

执行方式：

```bash
cmake -S . -B build
cmake --build build -j
cd build
ctest --output-on-failure
```

测试要点：

- test_pts：验证 PTY 会话可收集从 slave 侧写入的数据
- test_rframe：验证 rframe_send_payload 的字节流与期望帧格式一致

## 5. 模块说明

### tty_driver

- 负责设备打开/关闭、raw 模式设置、发送锁、后台接收线程
- 接收回调支持两种触发原因：
  - 空闲超时（TTY_RX_REASON_IDLE）
  - 缓冲区满（TTY_RX_REASON_BUFFER_FULL）

### rframe

- 提供 rframe_init / rframe_send_payload / rframe_close
- 发送前会填充固定帧头 0xAA55
- payload 格式：header(2B) + cmd(2B) + data_length(1B) + data(NB)

### pts

- 提供可复用库接口：pts_init / pts_take_rx_data / pts_release
- 支持创建 PTY 对并启动后台收发
- 可选桥接到外部串口设备，便于联调

## 6. RK3506 交叉编译

工具链文件：cmake/toolchains/rk3506-arm-linux-gnueabihf.cmake

### 6.1 前置要求

- 已安装对应交叉工具链（默认前缀 arm-linux-gnueabihf-）
- 建议提供 sysroot（按你的 SDK 路径）

### 6.2 配置与编译

```bash
cmake -S . -B build-rk3506 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3506-arm-linux-gnueabihf.cmake \
  -DCMAKE_SYSROOT=/path/to/rk3506/sysroot \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF

cmake --build build-rk3506 -j
```

生成产物通常为：

- build-rk3506/frf
- build-rk3506/frf_pts

### 6.3 部署到板端

```bash
scp build-rk3506/frf build-rk3506/frf_pts root@<board-ip>:/tmp/
ssh root@<board-ip> "chmod +x /tmp/frf /tmp/frf_pts"
```

## 7. 常见问题

1) frf 启动失败，提示打开设备失败：

- 检查 main.c 中设备路径是否存在（默认 /dev/pts/7）
- 先运行 frf_pts，确认打印出来的 PTY 路径

2) ctest 无测试项：

- 确认配置时未关闭 BUILD_TESTING
- 重新执行 cmake 配置后再编译

3) 交叉编译找不到库/头文件：

- 检查 CMAKE_SYSROOT 是否正确
- 检查交叉工具链前缀是否与本机安装一致
