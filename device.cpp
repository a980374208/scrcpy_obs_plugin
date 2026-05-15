#include "device.h"
#include "str_util.h"
#include <stdio.h>

// 初始化设备信息
int device_init(DeviceInfo *info, const char *serial)
{
	if (info == NULL) {
		return -1;
	}

	LOGI("DEVICE", "device_init: info=%p, serial=%p", (void *)info, (const void *)serial);

	// 复制序列号
	if (serial != NULL) {
		size_t serial_len = strlen(serial) + 1;
		info->serial = (char *)malloc(serial_len);
		if (info->serial == NULL) {
			LOGE("DEVICE", "Failed to allocate memory for serial");
			// 清理已分配的资源
			if (info->device_name != NULL) {
				free(info->device_name);
				info->device_name = NULL;
			}
			return -1;
		}
		memcpy(info->serial, serial, serial_len); // 使用memcpy而不是strcpy更安全
	}

	// 设置默认值
	info->device_name = str_util_strdup("Android Device");
	info->width = 1920;
	info->height = 1080;

	LOGI("DEVICE", "Device initialized: %s", serial ? serial : "unknown");
	return 0;
}

// 获取设备名称
const char *device_get_name(const DeviceInfo *info)
{
	if (info == NULL || info->device_name == NULL) {
		return "Unknown";
	}
	return info->device_name;
}

// 获取设备尺寸
void device_get_size(const DeviceInfo *info, int *width, int *height)
{
	if (info == NULL) {
		return;
	}
	if (width != NULL) {
		*width = info->width;
	}
	if (height != NULL) {
		*height = info->height;
	}
}

// 清理设备资源
void device_cleanup(DeviceInfo *info)
{
	if (info == NULL) {
		return;
	}

	if (info->serial != NULL) {
		free(info->serial);
		info->serial = NULL;
	}

	if (info->device_name != NULL) {
		free(info->device_name);
		info->device_name = NULL;
	}

	LOGI("DEVICE", "Device cleaned up");
}
