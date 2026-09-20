# OBS Android 画面与音频捕获插件

`plugin-srccpy` 将配套 scrcpy server 的屏幕、摄像头、音频及控制通道接入 OBS Studio，来源类型为 `srccpy_source`。当前默认媒体组合是 H.264 + Opus，Windows 构建依赖 OBS、Qt 6 和 FFmpeg。

2026-09-20 已通过 Gradle SDK 37 Release 构建及一台 Redmi Android 15 的 USB 基础运行：屏幕/摄像头切换、OUTPUT/MIC 收音、音频停启、双来源隔离、同配置故障恢复和正常退出。随后完成屏幕与前后摄像头 15/30 FPS 输入测量、OUTPUT/MIC 人工试听，见[FPS 与听感验收](knowledge/reviews/2026-09-20-fps-listening-acceptance.md)。最新[异步选帧修复](knowledge/reviews/2026-09-20-async-frame-timing-fix.md)将 OBS30 的屏幕渲染恢复至约 30 FPS，前后摄渲染约 29.81–29.96 FPS；新增约 50 ms 视频播放余量，音画同步尚未验收。APK 使用现有 debug 签名，WiFi、跨设备兼容与长稳尚未验收。构建及原始矩阵见[运行记录](knowledge/reviews/2026-09-20-formal-build-basic-runtime.md)。

## 构建与使用

1. 放入 OBS 源码树的 `plugins/plugin-srccpy`，由宿主 `plugins/CMakeLists.txt` 注册 `add_obs_plugin(plugin-srccpy)`，使用匹配的宿主依赖和 CMake 缓存。
2. 编译插件：`cmake --build <OBS构建目录> --target plugin-srccpy --config Release`。根 CMake 依赖宿主目标，不是已验证的独立构建入口。
3. 通过 PATH 或 `ADB` 环境变量提供 `adb.exe`，手机开启 USB 调试并授权；在 OBS 添加来源后选择本次枚举到的设备、屏幕或摄像头。
4. 部署匹配版本的 `scrcpy-server`：Debug 使用插件 data，Release/RelWithDebInfo 使用 `obs64.exe` 相对的 `server/scrcpy-server`；`SCRCPY_SERVER_PATH` 可显式覆盖。手机载荷由 `app_process` 启动，不用 `adb install`。

连接、构建、部署与自动验收步骤统一见[构建与验证](knowledge/BUILD_AND_VALIDATION.md)。WiFi 路径存在但不保证所有无线环境握手成功；会话按 SCID 管理进程与 tunnel。

## 开发导航

- [知识库入口](knowledge/README.md)：当前基线、架构、修复与风险。
- [测试说明](knowledge/COVERAGE.md)：最小 gate、运行边界及测试文件职责。
- `src/`：生产实现；`tests/`：回归测试；`data/`：语言与 server 资源；`cmake/`：构建辅助。
