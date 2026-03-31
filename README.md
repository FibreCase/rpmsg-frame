# C 语言通用项目模板（CMake + CTest）

这个项目是一个面向 C 语言开发的通用模板，目标是帮助你快速搭建可维护、可扩展、可测试的工程。

它已经实现了：

- 基于 CMake 的模块化组织
- 基于 CTest 的自动化测试入口
- 基于 Unity 的单元测试框架集成

适用于以下场景：

- 快速启动一个新的 C 项目
- 将代码按模块拆分成独立库
- 在本地或 CI 中统一执行测试

## 1. 项目特性

- 模块化构建：每个功能模块都可以独立维护自己的 CMakeLists.txt
- 主程序与模块解耦：主程序通过链接库使用模块能力
- 测试独立目录：测试代码集中在 tests 目录
- 统一测试入口：使用 ctest 统一执行所有测试
- 第三方测试框架内置：已集成 Unity（位于 3rdparty/unity）

## 2. 目录结构

```text
c-test/
├─ CMakeLists.txt            # 根构建入口，汇总 src/、tests/、3rdparty/
├─ src/
│  ├─ CMakeLists.txt         # 模块构建配置（示例模块 add）
│  ├─ add.h
│  ├─ add.c
│  └─ main.c                 # 示例主程序
├─ tests/
│  ├─ CMakeLists.txt         # 测试构建与 add_test 注册
│  └─ test_add.c             # add 模块单元测试
└─ 3rdparty/
 └─ unity/                 # Unity 测试框架源码
```

## 3. 构建与运行

### 3.1 配置工程

在项目根目录执行：

```bash
cmake -S . -B build
```

### 3.2 编译

```bash
cmake --build build
```

### 3.3 运行主程序

```bash
./build/c_test_demo
```

Windows 上通常为：

```powershell
.\build\Debug\c_test_demo.exe
```

说明：可执行文件路径会因生成器（Ninja、Visual Studio、MinGW）和配置（Debug/Release）不同而变化。

## 4. 执行测试（CTest）

### 4.1 在 build 目录运行

```bash
cd build
ctest
```

### 4.2 查看详细日志

```bash
ctest --output-on-failure
```

### 4.3 指定构建配置（多配置生成器，如 Visual Studio）

```bash
ctest -C Debug --output-on-failure
```

## 5. 当前示例说明

- src/add.c 提供示例函数 add(int x, int y)
- tests/test_add.c 使用 Unity 对 add 函数做断言测试
- tests/CMakeLists.txt 中通过 add_test(test_add test_add) 注册到 CTest

这意味着，只要你继续按同样方式添加测试，就能通过 ctest 一键统一执行。

## 6. 如何新增一个业务模块

以新增模块 math_utils 为例：

1. 在 src 下新增源码与头文件

- src/math_utils.c
- src/math_utils.h

1. 在 src/CMakeLists.txt 中新增库或并入现有库

- 可选方案 A：新增独立 add_library(math_utils ...)
- 可选方案 B：并入已有库（按项目规划决定）

1. 在需要使用该模块的目标中链接该库

- 如主程序 target_link_libraries(... math_utils)
- 如测试程序 target_link_libraries(... math_utils unity)

1. 为该模块添加测试并注册到 CTest

- 新建 tests/test_math_utils.c
- 在 tests/CMakeLists.txt 中新增 add_executable + target_link_libraries + add_test

## 7. 如何新增一个测试文件（推荐流程）

1. 新建测试源码，如 tests/test_xxx.c
2. 编写 Unity 测试用例（RUN_TEST）
3. 在 tests/CMakeLists.txt 中添加：

- add_executable(test_xxx test_xxx.c)
- target_link_libraries(test_xxx PRIVATE 你的模块库 unity)
- add_test(test_xxx test_xxx)

1. 重新配置并编译：

```bash
cmake -S . -B build
cmake --build build
```

1. 执行测试：

```bash
cd build
ctest --output-on-failure
```

## 8. 推荐开发实践

- 头文件与实现文件分离，明确模块边界
- 库目标设置 PUBLIC/PRIVATE include/link 作用域
- 每个模块至少配套一个测试文件
- 在 CI 中执行 cmake + build + ctest，保证变更质量

## 9. 后续可扩展方向

- 增加代码覆盖率（gcov/lcov）
- 增加静态检查（clang-tidy / cppcheck）
- 增加格式化工具（clang-format）
- 引入更细粒度的模块目录结构（每模块独立子目录与 CMakeLists.txt）
