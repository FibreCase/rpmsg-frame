# FibreCase RPMSG Frame (frf) C Library

该仓库是一个面向 Linux 的 C 项目，核心目标是 RPMSG 风格帧传输。

主线运行链路为：

- `tty_driver`：串口打开、配置、发送、接收基础能力
- `rframe`：帧封装与负载发送
- `frf`：Unix socket 守护进程，接收请求并转发为 RPMSG 帧

`pts` 在本项目中定位为辅助测试与联调组件，用于构造虚拟串口场景（loopback/bridge），不属于主要产品路径。

项目集成了 CTest 与 Unity，便于持续回归验证。

## 1. 构建产物

根 CMake 会生成以下可执行文件：

- `frf`：RPMSG 帧发送主程序（守护进程）
- `frf_pts`：主要用于测试与联调的 PTY 工具

`src` 下还会构建以下库目标：

- `rframe`：基于 `tty_driver` 的帧层
- `pts_runtime`：可复用 PTY 运行时库（编译时定义 `PTS_NO_MAIN`）

## 2. 目录结构

```text
c-arm/
|- CMakeLists.txt
|- src/
|  |- CMakeLists.txt
|  |- main.c
|  |- tty_driver.h
|  |- tty_driver.c
|  |- rframe.h
|  |- rframe.c
|  |- pts.h
|  \- pts.c
|- tests/
|  |- CMakeLists.txt
|  |- test_rframe.c
|  \- test_pts.c
|- 3rdparty/
|  \- unity/
\- cmake/
   \- toolchains/
      \- rk3506-arm-linux-gnueabihf.cmake
```

## 3. 本机构建与运行

### 3.1 配置与编译

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 3.2 启动 frf（主流程）

`frf` 会启动 Unix socket 守护进程（默认 `/tmp/frf.sock`），并将收到的请求发送到指定串口设备。

```bash
./build/frf /dev/pts/7 /tmp/frf.sock
```

若省略参数，默认使用 `/dev/pts/7` 与 `/tmp/frf.sock`。

### 3.3 可选：启动 frf_pts（测试接线）

当你需要本地虚拟串口验证时使用。

1. loopback 模式：

```bash
./build/frf_pts
```

程序会打印 PTY 路径，例如 `/dev/pts/7`。

2. bridge 模式（例如桥接 RPMSG TTY）：

```bash
./build/frf_pts /dev/ttyRPMSG0
```

## 4. Socket 请求协议

客户端通过 `SOCK_SEQPACKET` 连接，每个请求发送一帧：

- `cmd`：2 字节，网络字节序
- `data_length`：1 字节
- `data`：`data_length` 字节

服务端返回固定 5 字节：

- `status`：1 字节，`0` 表示成功
- `errno`：4 字节，网络字节序，仅在 `status != 0` 时有效

请求示例：

```text
cmd = 0x0102
data_length = 4
data = DE AD BE EF
```

## 5. 测试（CTest + Unity）

默认 `BUILD_TESTING=ON` 时注册以下测试：

- `test_rframe`：验证 `rframe_send_payload` 生成字节流与期望帧格式一致
- `test_pts`：验证 PTY 会话行为（辅助路径）

执行方式：

```bash
cmake -S . -B build
cmake --build build -j
cd build
ctest --output-on-failure
```

## 6. 模块说明

### tty_driver

- 负责设备打开/关闭与 raw 模式设置
- 提供发送锁与后台接收线程
- 回调触发原因：
  - 空闲超时（`TTY_RX_REASON_IDLE`）
  - 缓冲区满（`TTY_RX_REASON_BUFFER_FULL`）

### rframe

- 接口：`rframe_init`、`rframe_send_payload`、`rframe_close`
- 固定帧头：`0xAA55`
- payload 格式：`header(2B) + cmd(2B) + data_length(1B) + data(NB)`

### pts（辅助组件）

- 接口：`pts_init`、`pts_take_rx_data`、`pts_release`
- 支持 PTY 对创建与后台转发
- 适合 loopback 测试与桥接联调

## 7. RK3506 交叉编译

工具链文件：`cmake/toolchains/rk3506-arm-linux-gnueabihf.cmake`

### 7.1 前置要求

- 已安装交叉工具链（默认前缀 `arm-linux-gnueabihf-`）
- 建议提供 sysroot（来自 SDK）

### 7.2 配置与编译

```bash
cmake -S . -B build-rk3506 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3506-arm-linux-gnueabihf.cmake \
  -DCMAKE_SYSROOT=/path/to/rk3506/sysroot \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF

cmake --build build-rk3506 -j
```

常见产物：

- `build-rk3506/frf`
- `build-rk3506/frf_pts`

### 7.3 部署到板端

```bash
scp build-rk3506/frf build-rk3506/frf_pts root@<board-ip>:/tmp/
ssh root@<board-ip> "chmod +x /tmp/frf /tmp/frf_pts"
```

## 8. 常见问题

1. `frf` 启动失败（设备打开失败）：

- 检查设备路径是否存在（默认 `/dev/pts/7`）
- 先运行 `frf_pts`，使用其打印出的 PTY 路径

2. `ctest` 没有测试项：

- 确认未关闭 `BUILD_TESTING`
- 重新执行 CMake 配置与编译

3. 交叉编译找不到头文件或库：

- 检查 `CMAKE_SYSROOT`
- 检查交叉编译器前缀与本机安装是否一致
