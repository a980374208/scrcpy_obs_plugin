# OBS scrcpy 投屏控制插件 (plugin-srccpy)

## 简介
`plugin-srccpy` 是一个专为 OBS Studio 开发的插件，旨在将 `scrcpy` 的音视频投屏（音频暂未实现）与控制功能直接集成到 OBS 中。支持 Android 设备的有线 (USB) 和无线 (TCP/IP WiFi) 投屏，并可在 OBS 内直接进行点击与控制。

本插件基于 `scrcpy` 3.x 核心协议进行 C++ 重构，以支持高性能的音视频同步采集和低延迟的互动控制。

---

## 功能特性

- **双模式连接支持**：
  - **USB 模式**：即插即用，通过 USB 接口高速采集设备画面与音频。
  - **WiFi 模式 (TCP/IP)**：支持直接连接指定 `IP:Port` 目标地址，或从 USB 自适应切换/激活无线 TCP/IP 连接模式。
- **自动进程清理**：每次重新连接或切换设备时，主动终止手机后台残留的 `scrcpy-server` 孤儿进程，并提供 500ms 的硬件释放缓冲时延，彻底解决 `Camera device in error state` (相机硬件被锁定/占用) 的问题。
- **可靠的 TCP/IP 传输隧道**：当检测到 TCP/IP 无线设备连接时，自动且强制启用正向端口转发 (`adb forward`)。避开无线环境下不稳定的反向代理 (`adb reverse`)，确保 100% 成功建立网络握手。
- **现代规整的项目结构**：所有核心源码及子模块均整合于 `src/` 目录下，根目录保持极其整洁。
- **低延迟控制**：支持键盘、鼠标及触控事件回传（包含坐标自动缩放与转换）。

---

## 目录结构说明

为了保持项目清晰可维护，所有源码均规整存放于 `src/` 目录中：

```text
plugin-srccpy/
├── CMakeLists.txt              # CMake 编译配置文件
├── CMakePresets.json           # 预设编译器配置
├── LICENSE                     # 开源许可证
├── README.md                   # 本说明文件
├── data/                       # 本地化多语言资源文件
└── src/                        # 核心源码目录
    ├── adb/                    # ADB 连接与协议解析工具 (包含 TCP/IP 命令适配)
    ├── android/                # Android 键码与输入规范定义
    ├── codec/                  # FFmpeg 音视频流解包与解码组件 (Demuxer/Sink/Merge)
    ├── event/                  # 线程与系统事件调度
    ├── server/                 # scrcpy-server 配置及命令行组装器
    ├── util/                   # 底层实用网络套接字(Socket)、流读取、线程和日志等库
    ├── device.cpp / .h         # 设备状态与数据定义
    ├── controller.cpp / .h     # 控制通道与事件传输控制器
    ├── control_msg.cpp / .h    # Android 交互控制报文封装
    ├── srccpy.cpp / .hpp       # OBS 投屏源核心逻辑实现
    └── srccpy-plugin-main.cpp  # OBS 插件入口及模块注册定义
```

---

## 支持的构建平台

| 操作系统 | 推荐构建工具/编译器 |
|:---|:---|
| Windows | Visual Studio 17 2022 / Visual Studio 18 2026 |

---

## 快速开始

本插件基于 `obs-studio` 源码树内编译进行调试与开发。若想单独作为外部模块编译，可参考 [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) 进行简单 CMake 改造。

### 1. 安装与路径配置
1. 将本项目源码文件夹置于 `obs-studio/plugins/` 目录中。
2. 打开 `obs-studio/plugins/CMakeLists.txt`，在合适的位置加入：
   ```cmake
   add_obs_plugin(plugin-srccpy)
   ```

### 2. 编译 OBS Studio
使用 CMake 重新配置并编译 `obs-studio` 项目。以 Windows x64 为例：
```bash
# 仅编译本插件目标
cmake --build <您的构建目录> --target plugin-srccpy
```

### 3. 配置运行
确保您的 `obs64.exe` 运行路径下能够找到 `adb` 可执行程序，或在系统环境变量中将 `ADB` 环境变量指向您的 `adb.exe` 路径。

---

## 常见问题排查与技术细节

### Q1: 在 WiFi 模式下切换设备或重连时，提示 "Camera device is currently in the error state"？
* **原因**：由于无线网络偶发断开，手机端原有的 Java 服务进程没有及时收到退出信号，在手机后台变成了孤儿进程并继续强行霸占着物理相机资源。
* **解决**：本插件在建立连接时会自动发送 `pkill` 命令强杀手机端的旧服务进程，并挂起 500ms 以等 Android 系统彻底释放硬件句柄。如果依然报错，请在手机端检查是否有其他相机应用在后台运行。

