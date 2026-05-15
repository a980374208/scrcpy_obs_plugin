#include "common.h"
#include "net.h"
#include "device.h"
#include "controller.h"
#include "srccpy.hpp"
#include "util/options.h"

int main(int argc, char *argv[])
{
	LOGI("MAIN", "Scrcpy Lite v1.0.0");
	LOGI("MAIN", "Starting application...");

	// 初始化网络
	if (net_init() != 0) {
		LOGE("MAIN", "Failed to initialize network");
		return 1;
	}

	srccpy sr;
	sr.srccpy_init(scrcpy_options_default);
	// 初始化设备信息（使用 calloc 确保内存清零）
	DeviceInfo *device = (DeviceInfo *)calloc(1, sizeof(DeviceInfo));
	if (device == NULL) {
		LOGE("MAIN", "Failed to allocate memory for device");
		net_cleanup();
		return 1;
	}

	const char *serial = (argc > 1) ? argv[1] : NULL;
	if (device_init(device, serial) != 0) {
		LOGE("MAIN", "Failed to initialize device");
		free(device);
		net_cleanup();
		return 1;
	}

	LOGI("MAIN", "Device: %s", device_get_name(device));

	// 初始化控制器
	Controller controller;
	if (controller_init(&controller, device) != 0) {
		LOGE("MAIN", "Failed to initialize controller");
		device_cleanup(device);
		free(device);
		net_cleanup();
		return 1;
	}

	// 启动控制器
	if (controller_start(&controller) != 0) {
		LOGE("MAIN", "Failed to start controller");
		controller_cleanup(&controller);
		device_cleanup(device);
		free(device);
		net_cleanup();
		return 1;
	}

	LOGI("MAIN", "Application running. Press Enter to exit...");

	// 等待用户输入
	getchar();

	// 清理资源
	controller_cleanup(&controller);
	device_cleanup(device);
	free(device);
	net_cleanup();

	LOGI("MAIN", "Application exited successfully");
	return 0;
}
