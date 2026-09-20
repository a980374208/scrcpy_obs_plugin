# 源码知识库使用规则

- 处理本项目任务先读 `knowledge/README.md` 和 `knowledge/BASELINE.md`。
- 按当前问题选择相关章节，不一次性加载整个知识库。
- 使用结论前检查相关源码、配置及未提交/未跟踪文件是否变化；不能仅比较 HEAD。
- 哈希未变的证据复用；有变化时沿受影响接口、调用方及依赖增量核对。
- 知识库只作导航和分析记录，过期文档不能覆盖当前源码事实。
- 后续修改实现时同步更新受影响的知识库章节、覆盖状态和 `knowledge/manifest.json`。

以上补充不替代适用的上层规则或用户当轮工作边界。

## Server 构建与部署

### 路径与有效证据

- 插件根：`E:\vsSource\obs-studio\plugins\plugin-srccpy`。
- 配套 Android 根：`E:\AndroidSource\scrcpy_obs_plugin_Server`；OBS 运行根：`E:\vsSource\obs-studio\build_x64\rundir\<配置>`。
- 本机已验证工具链：`C:\Program Files\Android\Android Studio1\jbr`（JBR 21），`C:\Users\Jiangbinbin\AppData\Local\Android\Sdk`，SDK 36 / build-tools 36.0.0。路径、版本及可执行性按当前环境核对。
- 构建/部署案例和实际边界见 [幽灵屏幕修复记录](knowledge/reviews/2026-09-20-phantom-display-fix.md)及同名 JSON。历史产物在 Android `server/build/phantom-display-deploy-20260920/`，其 `build-receipt.json`、`host-deployment.json`、`device-deployment.json` 记录输入、备份、前后哈希与设备查询结果；这些 build 文件可能被清理，不能当作永久存在的工具。

### 构建流程

1. 核对两个工作区、生产源文件集合及 SHA-256、生成 AIDL/BuildConfig 和工具链。已通过 gate 的输入和编译输出均未变时，复用 javac/test 结果；不能只核对旧清单而漏掉新增/删除的源文件，也不能混入其他任务的旧 class。
2. 当前 Gradle 配置为 compileSdk/targetSdk 37；曾因 client/daemon loopback 在配置前失败。环境和输入未变时复用阻断证据，不机械重试；环境已改变或任务要求正式 APK 时再检查 Gradle。手工 SDK 36 编译/D8 成功不等于 Gradle assemble、SDK 37 或签名 Release 包成功。
3. 手工编译的已有入口为 Android `tests/run-phantom-display-focused.ps1`，参数为 `-JavaDirectory`、`-AndroidJar`、`-JunitJar`、`-HamcrestJar`。它编译生产源与现有 debug AIDL/BuildConfig，并执行该问题的专项 JUnit，输出 `inputs.json`、`classes/`、`gate.log`。其他问题应选择相应最小 gate；仅打包且输入未变时不重跑此脚本。
4. 从已验证 classes 中，按生产源的 package、顶层类及其内部类白名单生成 `production-classes.jar`。排除测试类、JUnit、探针及注入版本；不要直接把同时含测试输出的整个 classes 目录打包。生成文件只由构建工具产生，不手工改写既有 generated 输入。
5. 使用 D8 生成 DEX；以下变量须先设为本次确认的绝对路径，`$dexOutput` 使用本次独立输出目录：

   ```powershell
   & "$jbrRoot/bin/java.exe" -cp "$sdkRoot/build-tools/36.0.0/lib/d8.jar" `
       com.android.tools.r8.D8 --release --min-api 21 `
       --lib "$sdkRoot/platforms/android-36/android.jar" `
       --output $dexOutput $productionJar
   if ($LASTEXITCODE -ne 0) { throw 'D8 failed' }
   ```

6. 将 DEX 放在 ZIP 根目录，产物命名为 `scrcpy-server`（无扩展名），供 `CLASSPATH` + `app_process` 使用，不通过 `adb install` 安装。本项目已验证产物只含 `classes.dex`；若本次出现多个 DEX，不可静默漏包。检查 ZIP 完整性、生产入口/修复标记、测试类排除及 SHA-256。记录实际 BuildConfig：本次复用的是 `DEBUG=true / VERSION_NAME=3.3.4`；D8 的 `--release` 不会把 BuildConfig.DEBUG 改成 false。

### 部署流程

1. 当前任务已明确授权构建部署时继续执行，不重复索要同一授权。仅修改说明/分析时不触发部署。先确认 OBS 实例、手机 serial、活动会话及 `SCRCPY_SERVER_PATH`；按目标会话归属处理旧进程，不按 Java 主类名批量 pkill。
2. 实际 server 选择顺序：`SCRCPY_SERVER_PATH` 覆盖最高；Debug 使用 OBS 插件 data；Release/RelWithDebInfo 使用 exe 相对路径。以运行日志 `Using server ...` / `Using SCRCPY_SERVER_PATH ...` 确认实际命中位置。

   | 用途 | 路径 |
   |---|---|
   | 插件打包资源 | `<插件根>/data/server/scrcpy-server` |
   | 各配置的插件资源 | `<OBS运行根>/data/obs-plugins/plugin-srccpy/server/scrcpy-server` |
   | exe 相对资源 | `<OBS运行根>/bin/64bit/server/scrcpy-server` |
   | 手机目标 | `/data/local/tmp/scrcpy-server` |

3. 本机全配置部署覆盖插件资源及 Debug/Release/RelWithDebInfo 的上述两类路径，共 7 处；只部署指定配置时按用户范围执行。先按旧 SHA-256 备份已有文件，记录原本不存在的目标；复制到临时文件、核对哈希后替换，再核对最终文件。无需为 server 资源复制重建 C++ DLL。
4. `adb devices -l` 确认本次目标，后续始终使用 `-s $deviceSerial`。`4b07c922` 仅是已验设备，不硬编码为永久目标。推送到 `/data/local/tmp/scrcpy-server.<本次标识>-new`，`sha256sum` 匹配后 rename 到正式路径，避免直接覆盖活动进程可能仍映射的文件；旧进程不会因文件替换自动升级。
5. 最小部署验证使用新 server 查询设备信息，版本号必须匹配本次 BuildConfig：

   ```powershell
   & $adbPath -s $deviceSerial shell `
       'CLASSPATH=/data/local/tmp/scrcpy-server app_process / com.genymobile.scrcpy.Server 3.3.4 list_device_infos=true cleanup=false'
   if ($LASTEXITCODE -ne 0) { throw 'Device query failed' }
   & $adbPath -s $deviceSerial shell sha256sum /data/local/tmp/scrcpy-server
   ```

6. 记录查询退出码/JSON、本地和远端最终哈希、备份位置。捕获启动的默认 cleanup 可能删除手机上的 server，不能在文件已删后声称远端哈希通过。属性查询进程与捕获进程须使用同一新版本；重建目标会话后新代码才生效。加载/查询 PASS 与真实画面、声音、活动镜像过滤分别记录；未运行的场景标 `NOT_RUN`。

## 使用 OBS WebSocket 操作与验收

### 连接与认证

- 支持的 OBS 操作优先使用本机 obs-websocket v5；UI 仅补足 WebSocket 未覆盖的交互。先确认已有 OBS 实例和配置，再连接，避免启动重复实例。原验收使用 OBS 32.1.2 / WebSocket 5.7.3，后续用 `GetVersion` 核对实际版本及可用请求。
- portable 配置文件为 `<OBS运行根>/config/obs-studio/plugin_config/obs-websocket/config.json`；字段为 `server_enabled`、`auth_required`、`server_password`、`server_port`。当前 Debug 持久端口为 4455；历史验收通过 `--websocket_port 4460 --websocket_ipv4_only` 覆盖为 4460。读取当前进程参数/日志和配置决定 `ws://127.0.0.1:<实际端口>`，不能照抄历史端口；`--websocket_ipv4_only` 仅限制地址族，不表示只监听 loopback。
- 运行任务需要启动 OBS 时，使用所需配置的 `bin/64bit/obs64.exe` 和匹配工作目录；portable 验收使用 `--portable`。不默认添加 `--multi`；需要并行实例时明确隔离配置与端口。启动参数中的端口覆盖不会自动启用已禁用的 server。
- 用户此前明确要求保留 WebSocket 启用状态，验收结束不要顺手关闭它。保留现有认证；密码仅在内存中从本机配置读取，不打印整个配置、不写进 AGENTS/日志或提交文件。
- 可复用 PowerShell 7 的 `System.Net.WebSockets.ClientWebSocket`。现有示例 [obs-ws-runtime.ps1](build/bug014-runtime/obs-ws-runtime.ps1) 包含连接、认证、分片接收、requestId 匹配及 source 操作。它是 BUG-014 完整运行序列，直接执行会创建/切换/删除指定 source 并留下退出测试 source；按当前场景提取 helper 和握手代码，不把整个脚本 dot-source 为无副作用库。该文件位于 ignored build，缺失时按下述协议重建所需 helper。幽灵屏幕的窄范围入口是 `tests/verify-phantom-display-runtime.ps1`：它使用唯一名称临时 source，等待 `GetSourceActive(sourceName)` 为真，读取 `choose_capture` 并在 finally 中删除临时 source；结果与脚本哈希写入本次 runtime receipt。
- 握手：接收 `Hello(op=0)`；若有 authentication，计算 `secret=Base64(SHA256(UTF8(password+salt)))`，再计算 `authentication=Base64(SHA256(UTF8(secret+challenge)))`。发送 `Identify(op=1, d={rpcVersion:1,eventSubscriptions:0,authentication:...})`；无认证时省略 authentication。确认收到 `Identified(op=2)` 后才发请求。
- 请求格式：`{op:6,d:{requestType,requestId,requestData}}`。等待同一 requestId 的 `op=7`，检查 `requestStatus.result`，失败保留 code/comment。接收至 `EndOfMessage` 才解析 JSON，跳过无关事件；连接设有界超时，每个请求使用整体截止时间，避免被持续事件重置超时。属性查询可能同步等待设备，超时须覆盖已知查询预算（当前 10 秒）；不要对失败变更无限重发。

### Source 操作

- 先用 `GetSceneList`、`GetInputList`、`GetInputSettings` 获取目标和当前配置。测试 source 使用本次唯一名称；保存需恢复的原设置和可见性，清理只处理本次创建的 source。`RemoveInput` 会删除整个 input 及其场景引用，不能用它清理用户已有来源。
- 插件 inputKind 的实际拼写是 `srccpy_source`。当前设置字段：`device_list` 为 serial，`choose_src=0` 为屏幕/`1` 为摄像头，`choose_capture` 是字符串 ID，`choose_res` 如 `1920x1080`，`choose_fps` 是整数，`audio_enable` 是布尔值；USB 场景 `wifi_pair=false`。设备/屏幕/摄像头 ID 使用本次枚举结果，不把例子中的 `0` 当作所有设备都可用。

  | 请求 | 关键参数与用途 |
  |---|---|
  | `CreateInput` | `sceneName,inputName,inputKind,inputSettings,sceneItemEnabled`；保存返回的 `sceneItemId` |
  | `SetInputSettings` | `inputName,inputSettings,overlay:true`；仅更新本次需要的键 |
  | `GetInputSettings` | 读取应用后的 settings；不等于远端媒体已生效 |
  | `GetInputPropertiesListPropertyItems` | `inputName,propertyName`；读取 `device_list`、`choose_capture` 等属性候选 |
  | `PressInputPropertiesButton` | `inputName,propertyName:"refresh_devices"`；显式验证刷新按钮时使用 |
  | `SetSceneItemEnabled` | `sceneName,sceneItemId,sceneItemEnabled`；控制对应场景项显示/隐藏 |
  | `GetSourceScreenshot` | 按当前 API 请求 source 截图，验证实际画面时使用 |
  | `RemoveInput` | 仅移除本次创建的测试 input，随后检查会话退出 |

- 音频开关通过 `SetInputSettings` 更新 `audio_enable`；屏幕/摄像头切换更新 `choose_src`、`choose_capture` 及本次需要的参数。同步保存请求编号和时间，结合 OBS 日志、手机进程/显示状态及实际帧/音频结果判断效果。WebSocket 请求成功仅证明 OBS 接受请求，不是手机完成切换 ACK。
- 当前 `GetInputPropertiesListPropertyItems` 会调用 `obs_source_properties()`，后者创建属性并应用 source settings；本插件创建属性时已执行设备刷新。仅获取新候选时直接读列表即可，不机械叠加刷新按钮请求；请求成功仍须检查列表内容及查询错误日志，失败保留的旧缓存不能充当本次新证据。
- 对幽灵屏幕的运行验收，须在活动捕获时读取 `choose_capture` 候选并与手机显示状态对照；设备空闲时只返回 display 0 不能证明内部 mirror 已过滤。只运行当前风险需要的序列，不为使用 WebSocket 自动重跑完整音频/切源/退出矩阵。
- 收口时关闭并释放 WebSocket client，恢复本次临时修改，删除本次测试 input，记录遗留活动 source；保留用户要求的 WebSocket server 启用状态。观察异步会话回收完成，不能以请求返回代替退出证据。

仅更新本文件或运行记录时，不执行 configure/build/test、部署或 OBS/ADB 操作；检查文档 diff、引用及与当前源码/证据的一致性即可。
