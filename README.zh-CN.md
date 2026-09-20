# OBS Android 画面与音频捕获插件

[English](README.md) | **简体中文**

`plugin-srccpy` 将 Android 屏幕、摄像头及音频接入 OBS Studio。安装后，在 OBS 来源列表中添加 **安卓设备（Android Device）** 即可配置捕获；自动化接口的来源类型为 `srccpy_source`。

本项目参照 [scrcpy 官方源码](https://github.com/Genymobile/scrcpy) 编写，并针对 OBS 来源集成进行适配。感谢 scrcpy 项目及其贡献者。

## 功能与适用范围

- 捕获手机屏幕或摄像头，设置采集项、分辨率和请求帧率，默认请求 30 FPS。
- 勾选“启用音频捕获”后，屏幕模式采集设备声音（OUTPUT），摄像头模式采集麦克风（MIC）；音频来源随画面模式切换。
- 支持同一设备创建多个 OBS 来源，各来源独立管理捕获会话。
- 提供 USB 连接和无线调试配对入口。当前已验证环境为 Windows、OBS 32.1.2、Redmi 22081212C / Android 15、USB、H.264 + Opus。

## 快速开始（Windows / USB）

### 1. 准备环境

- 使用已配置完成的 OBS 源码树，以及匹配的 OBS、Qt 6 Widgets 和 FFmpeg 依赖。当前插件通过 OBS 宿主构建，尚无已验证的独立构建入口。
- 安装 Android SDK Platform-Tools，通过 `PATH` 或 `ADB` 环境变量提供 `adb.exe`。设置环境变量后，从该环境启动 OBS。
- 手机开启 USB 调试、连接 USB，并在手机上允许本机调试授权。首次验证屏幕捕获时保持手机解锁、亮屏。
- 使用本项目配套 Android server，确保与插件版本匹配；server 构建步骤见[构建与验证](knowledge/BUILD_AND_VALIDATION.md#server-构建与部署)。

### 2. 构建插件

将本目录放在 OBS 源码树的 `plugins/plugin-srccpy`，由宿主 `plugins/CMakeLists.txt` 注册 `add_obs_plugin(plugin-srccpy)`。使用已包含该目标的宿主 CMake 缓存构建：

```powershell
cmake --build <OBS构建目录> --target plugin-srccpy --config Release
```

将 `<OBS构建目录>` 替换为实际路径。工具链、测试和本机命令见[构建与验证](knowledge/BUILD_AND_VALIDATION.md)。

### 3. 核对 server 资源

插件 DLL、语言资源和配套 `scrcpy-server` 必须与所运行的 OBS 配置对应。server 查找顺序如下：

| 条件 | server 位置 |
|---|---|
| 设置了 `SCRCPY_SERVER_PATH` | 优先使用该变量指定的文件 |
| Debug | `<OBS运行根>/data/obs-plugins/plugin-srccpy/server/scrcpy-server` |
| Release / RelWithDebInfo | `<obs64.exe所在目录>/server/scrcpy-server` |

OBS 日志中的 `Using server ...` 或 `Using SCRCPY_SERVER_PATH ...` 可确认实际命中位置。手机端载荷通过 `app_process` 启动，无需 `adb install`。资源打包、备份与部署流程见[部署流程](knowledge/BUILD_AND_VALIDATION.md#部署流程)。

### 4. 添加来源并验证捕获

1. 启动对应配置的 OBS，在“来源”中添加“安卓设备”。
2. 选择已授权的 USB 设备；列表为空时点击“刷新设备列表”。USB 场景保持“启用无线连接”关闭。
3. 在“选择画面源”中选择屏幕或摄像头，再选择采集项、分辨率和帧率。当前下拉项分别显示为 `VIDEO_SOURCE_DISPLAY` 和 `VIDEO_SOURCE_CAMERA`。
4. 需要声音时勾选“启用音频捕获”，播放手机音频或向手机麦克风讲话，核对 OBS 混音器电平与实际录制声音。
5. 确认预览出现预期画面，再按需要调整参数。设备或摄像头出现在列表中，只代表枚举成功，仍需验证实际捕获。

## 常见问题

| 现象 | 优先检查 |
|---|---|
| OBS 中没有“安卓设备”来源 | 插件是否部署到当前 OBS 配置；OBS 日志是否成功加载 `plugin-srccpy` |
| 没有设备或显示未授权 | OBS 是否能找到 `adb.exe`；USB 调试、连接和手机端授权是否完成 |
| 设备可见但无法出画面 | 日志中的 server 路径与版本、手机亮屏状态、所选屏幕或摄像头是否实际可用 |
| 没有声音 | 是否勾选音频捕获、当前模式对应 OUTPUT 还是 MIC、OBS 来源是否静音 |
| 请求帧率与观察值不同 | 请求值不等于实际帧率；静态屏幕可能稀疏出帧，测量方法见[FPS 与听感验收](knowledge/reviews/2026-09-20-fps-listening-acceptance.md) |

## 开发与文档导航

| 入口 | 用途 |
|---|---|
| [知识库](knowledge/README.md) | 按任务查阅当前基线、操作指南与验收记录 |
| [架构](knowledge/ARCHITECTURE.md) | 源码入口、协议、线程与所有权 |
| [测试与覆盖](knowledge/COVERAGE.md) | 选择最小验证范围、定位回归测试 |
| [项目工作规则](AGENTS.md) | 工作区边界、证据复用和维护要求 |
| [src/](src/) / [data/](data/) / [cmake/](cmake/) | 生产实现、语言与 server 资源、构建辅助 |

文档一致性检查在插件根执行 `python knowledge/_tools/validate.py`。

## 许可证

采用 [GNU GPL v3](LICENSE)。
