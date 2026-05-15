#ifndef SCRCPY_DEVICE_H
#define SCRCPY_DEVICE_H

#include "common.h"

// 初始化设备信息
int device_init(DeviceInfo *info, const char *serial);

// 获取设备名称
const char *device_get_name(const DeviceInfo *info);

// 获取设备尺寸
void device_get_size(const DeviceInfo *info, int *width, int *height);

// 清理设备资源
void device_cleanup(DeviceInfo *info);

#endif // SCRCPY_DEVICE_H
