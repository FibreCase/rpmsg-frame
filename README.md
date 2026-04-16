# FibreCase RPMSG Frame (frf) C Library

This repository is a Linux C project centered on RPMSG-style frame transport.

The core runtime path is:

- `tty_driver`: serial open/config/send/receive foundation
- `rframe`: frame packing and payload transmission
- `frf`: Unix socket daemon that accepts requests and sends RPMSG frames

The `pts` part is kept as a supplementary test and integration utility. It provides a virtual serial endpoint for loopback and bridge scenarios, but it is not the primary product flow.

The project integrates CTest and Unity for regression testing.

## 1. Build Outputs

Root CMake generates these executables:

- `frf`: main runtime daemon for RPMSG frame sending
- `frf_pts`: PTY helper tool used mainly for test and integration setup

Library targets under `src`:

- `rframe`: frame layer built on top of `tty_driver`
- `pts_runtime`: reusable PTY runtime library (compiled with `PTS_NO_MAIN`)

## 2. Project Layout

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

## 3. Build and Run (Host)

### 3.1 Configure and Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### 3.2 Start frf (Primary Runtime)

`frf` starts a Unix socket daemon (default: `/tmp/frf.sock`) and forwards incoming requests to the specified serial device as RPMSG frames.

```bash
./build/frf /dev/pts/7 /tmp/frf.sock
```

If arguments are omitted, defaults are `/dev/pts/7` and `/tmp/frf.sock`.

### 3.3 Optional: Start frf_pts for Test Wiring

Use this when you need a virtual endpoint for local validation.

1. Loopback mode:

```bash
./build/frf_pts
```

It prints a PTY path such as `/dev/pts/7`.

2. Bridge mode (for example to RPMSG TTY):

```bash
./build/frf_pts /dev/ttyRPMSG0
```

## 4. Socket Request Protocol

Clients connect with `SOCK_SEQPACKET` and send one frame per request:

- `cmd`: 2 bytes, network byte order
- `data_length`: 1 byte
- `data`: `data_length` bytes

Server reply is 5 bytes:

- `status`: 1 byte, `0` means success
- `errno`: 4 bytes, network byte order, valid when `status != 0`

Example request:

```text
cmd = 0x0102
data_length = 4
data = DE AD BE EF
```

## 5. Testing (CTest + Unity)

With `BUILD_TESTING=ON` (default), these tests are registered:

- `test_rframe`: validates generated frame bytes from `rframe_send_payload`
- `test_pts`: validates PTY session behavior (supplementary path)

Run tests:

```bash
cmake -S . -B build
cmake --build build -j
cd build
ctest --output-on-failure
```

## 6. Module Summary

### tty_driver

- Opens/closes serial devices and configures raw mode
- Provides TX locking and asynchronous RX thread
- RX callback reasons:
  - idle timeout (`TTY_RX_REASON_IDLE`)
  - buffer full (`TTY_RX_REASON_BUFFER_FULL`)

### rframe

- APIs: `rframe_init`, `rframe_send_payload`, `rframe_close`
- Uses fixed header `0xAA55`
- Payload format: `header(2B) + cmd(2B) + data_length(1B) + data(NB)`

### pts (supplementary)

- APIs: `pts_init`, `pts_take_rx_data`, `pts_release`
- Creates PTY pairs and background forwarding
- Useful for test loopback and integration bridge scenarios

## 7. RK3506 Cross Compilation

Toolchain file: `cmake/toolchains/rk3506-arm-linux-gnueabihf.cmake`

### 7.1 Prerequisites

- Cross toolchain installed (default prefix: `arm-linux-gnueabihf-`)
- Sysroot recommended (from your SDK)

### 7.2 Configure and Build

```bash
cmake -S . -B build-rk3506 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/rk3506-arm-linux-gnueabihf.cmake \
  -DCMAKE_SYSROOT=/path/to/rk3506/sysroot \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF

cmake --build build-rk3506 -j
```

Expected artifacts:

- `build-rk3506/frf`
- `build-rk3506/frf_pts`

### 7.3 Deploy to Board

```bash
scp build-rk3506/frf build-rk3506/frf_pts root@<board-ip>:/tmp/
ssh root@<board-ip> "chmod +x /tmp/frf /tmp/frf_pts"
```

## 8. Troubleshooting

1. `frf` fails to start because device open fails:

- Verify the device path exists (default `/dev/pts/7`)
- Start `frf_pts` first and use the printed PTY path

2. No tests in `ctest`:

- Ensure `BUILD_TESTING` is not disabled
- Re-run CMake configure and build

3. Missing headers/libraries in cross build:

- Check `CMAKE_SYSROOT`
- Check cross compiler prefix matches your installed toolchain
