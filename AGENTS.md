# 项目工作规则

- 处理任务先读 `knowledge/README.md`、`knowledge/BASELINE.md`，再按问题选章节。
- 知识库只是导航；使用结论前核对相关源码、配置、未提交/未跟踪/ignored 文件的完整集合与 SHA-256，不只比较 HEAD。
- 哈希未变时复用已有证据；变更沿接口、caller/callee、state、owner、thread、持久化和 UI 边界增量核对。
- 修改实现时同步受影响知识章节、`knowledge/COVERAGE.md` 和 `knowledge/manifest.json`；不能只刷新哈希冒充已审阅。
- 保留无关用户改动；未经明确要求不 commit、push、发布、上传、reset、stash 或 checkout，不自动修改 Reference/vendor/generated 或外部仓库。
- 文档/报告整理只做一致性检查；代码变化选择覆盖风险的最小 gate。成功且输入未变时不重复 configure/build/test；未运行的 runtime 标 NOT_RUN/DEFERRED/UNKNOWN。

## 构建与运行操作

执行 server 构建、部署或 OBS 验收前，必须读取并遵循 [构建与验证](knowledge/BUILD_AND_VALIDATION.md) 对应章节；详细流程统一维护在那里。

- 插件根 `E:/vsSource/obs-studio/plugins/plugin-srccpy`，Android 根 `E:/AndroidSource/scrcpy_obs_plugin_Server`，OBS 运行根 `E:/vsSource/obs-studio/build_x64/rundir/<配置>`；每次核对实际工具链、配置及进程。
- 明确授权构建部署后继续执行，不重复询问。仅修改说明不触发部署或 OBS/ADB 操作。
- server 路径：SCRCPY_SERVER_PATH 优先，Debug 用插件 data，Release/RelWithDebInfo 用 exe 相对资源；以运行日志确认命中。
- 本次枚举设备后始终显式 `adb -s <serial>`；进程/tunnel 按会话归属处理，禁止主类名批量 pkill。主机与手机部署须备份、临时文件校验、原子替换、最终哈希和查询留证。
- OBS 操作优先 obs-websocket v5；先检查实例、配置和实际端口，避免重复启动。密码仅在内存读取，保留认证，禁止输出完整配置或写密码到记录。
- 用户已要求保留 WebSocket 启用；收口不关闭 server 配置。临时 source 唯一命名，仅移除本次创建的 input，恢复临时变更，关闭 client 并观察异步回收。
- WebSocket 接受请求不等于设备 ACK；属性候选不等于捕获成功；加载、画面、声音和活动镜像过滤分别留证。

以上补充不替代适用的上层规则及用户当轮工作边界。
