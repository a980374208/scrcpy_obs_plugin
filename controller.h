#ifndef SCRCPY_CONTROLLER_H
#define SCRCPY_CONTROLLER_H

#include "common.h"

// 控制器结构
typedef struct {
	DeviceInfo *device;
	socket_t control_socket;
	int running;
} Controller;

// 初始化控制器
int controller_init(Controller *ctrl, DeviceInfo *device);

// 启动控制器
int controller_start(Controller *ctrl);

// 停止控制器
void controller_stop(Controller *ctrl);

// 清理控制器
void controller_cleanup(Controller *ctrl);

#endif // SCRCPY_CONTROLLER_H
