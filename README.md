# Android Screen, Camera, and Audio Capture for OBS

**English** | [简体中文](README.zh-CN.md)

`plugin-srccpy` brings Android screens, cameras, and audio into OBS Studio. Once installed, add an **Android Device** source in OBS to configure capture. The source type for automation is `srccpy_source`.

This project was developed with reference to the [official scrcpy source code](https://github.com/Genymobile/scrcpy) and adapted for integration as an OBS source. Thanks to the scrcpy project and its contributors.

## Features and scope

- Capture a phone's screen or camera, and choose the capture target, resolution, and requested frame rate. The default request is 30 FPS.
- Select **Enable Audio Capture** to capture device playback audio (OUTPUT) in screen mode or microphone audio (MIC) in camera mode. The audio source follows the video mode.
- Create multiple OBS sources for the same device, with each source managing its own capture session.
- Connect over USB or use the wireless debugging pairing interface. The validated environment is Windows, OBS 32.1.2, a Redmi 22081212C running Android 15, USB, and H.264 + Opus.

## Quick start (Windows / USB)

### 1. Prepare the environment

- Use an already configured OBS source tree with matching OBS, Qt 6 Widgets, and FFmpeg dependencies. The plugin currently builds within OBS; a standalone build has not been validated.
- Install Android SDK Platform-Tools and make `adb.exe` available through `PATH` or the `ADB` environment variable. After setting the variables, launch OBS from that environment.
- Enable USB debugging on the phone, connect it over USB, and authorize the computer on the phone. Keep the phone unlocked and its screen on when first checking screen capture.
- Use this project's companion Android server, with a version that matches the plugin. See [Build and validation](knowledge/BUILD_AND_VALIDATION.md#server-构建与部署) for server build instructions.

### 2. Build the plugin

Place this directory at `plugins/plugin-srccpy` in the OBS source tree and register it with `add_obs_plugin(plugin-srccpy)` in OBS's `plugins/CMakeLists.txt`. Build using an existing OBS CMake build directory that already includes this target:

```powershell
cmake --build <OBS-build-directory> --target plugin-srccpy --config Release
```

Replace `<OBS-build-directory>` with the actual path. See [Build and validation](knowledge/BUILD_AND_VALIDATION.md) for toolchain details, tests, and commands used in the development environment.

### 3. Check the server resources

The plugin DLL, locale files, and companion `scrcpy-server` must match the OBS build configuration you run. The server is located as follows:

| Condition | Server location |
|---|---|
| `SCRCPY_SERVER_PATH` is set | The file specified by this variable takes precedence |
| Debug | `<OBS-runtime-root>/data/obs-plugins/plugin-srccpy/server/scrcpy-server` |
| Release / RelWithDebInfo | `<directory-containing-obs64.exe>/server/scrcpy-server` |

Check `Using server ...` or `Using SCRCPY_SERVER_PATH ...` in the OBS log to confirm the file used. The device-side payload runs through `app_process`; `adb install` is not required. See the [deployment procedure](knowledge/BUILD_AND_VALIDATION.md#部署流程) for packaging, backups, and deployment.

### 4. Add a source and verify capture

1. Start OBS with the corresponding build configuration and add **Android Device** under **Sources**.
2. Select an authorized USB device. If the list is empty, click **Refresh Device List**. Leave **Enable Wireless Connection** unchecked when using USB.
3. Under **Choose Screen/Camera**, select a screen or camera, then set **Choose Capture Device**, **Choose Resolution**, and **Choose FPS**. The video mode options currently appear as `VIDEO_SOURCE_DISPLAY` and `VIDEO_SOURCE_CAMERA`.
4. For audio, select **Enable Audio Capture** and play audio on the phone or speak into its microphone. Check both the OBS mixer levels and the sound in an actual recording.
5. Confirm that the preview shows the expected image, then adjust settings as needed. A device or camera appearing in the list only confirms that it was enumerated; actual capture still needs to be checked.

## Troubleshooting

| Symptom | What to check first |
|---|---|
| **Android Device** is missing from OBS sources | Check that the plugin was deployed to the OBS build configuration you are running and that the OBS log reports a successful load of `plugin-srccpy` |
| No device appears, or the device is unauthorized | Check that OBS can find `adb.exe`, USB debugging is enabled, the USB connection works, and the computer is authorized on the phone |
| The device appears, but no video is shown | Check the server path and version in the log, whether the phone's screen is on, and whether the selected screen or camera is usable |
| No audio | Check that audio capture is enabled, whether the current mode uses OUTPUT or MIC, and whether the OBS source is muted |
| The observed frame rate differs from the requested value | The requested rate is not a guarantee of the actual rate; a static screen may produce frames infrequently. See [FPS and listening validation](knowledge/reviews/2026-09-20-fps-listening-acceptance.md) for the measurement method |

## Development and documentation

The development documentation linked below is currently in Chinese.

| Resource | Purpose |
|---|---|
| [Knowledge base](knowledge/README.md) | Find the current baseline, operating procedures, and validation records by task |
| [Architecture](knowledge/ARCHITECTURE.md) | Source entry points, protocols, threads, and ownership |
| [Tests and coverage](knowledge/COVERAGE.md) | Choose the smallest relevant validation scope and locate regression tests |
| [Project working rules](AGENTS.md) | Workspace boundaries, evidence reuse, and maintenance requirements |
| [src/](src/) / [data/](data/) / [cmake/](cmake/) | Production code, locale and server resources, and build helpers |

Run `python knowledge/_tools/validate.py` from the plugin root to check documentation consistency.

## License

Licensed under the [GNU GPL v3](LICENSE).
