#include "controller.h"
#include "net.h"

// 初始化控制器
int controller_init(Controller *ctrl, DeviceInfo *device)
{
	if (ctrl == NULL || device == NULL) {
		return -1;
	}

	memset(ctrl, 0, sizeof(Controller));
	ctrl->device = device;
	ctrl->control_socket = INVALID_SOCKET;
	ctrl->running = 0;

	LOGI("CONTROLLER", "Controller initialized");
	return 0;
}

// 启动控制器
int controller_start(Controller *ctrl)
{
	if (ctrl == NULL || ctrl->device == NULL) {
		return -1;
	}

	// 创建控制 socket（示例使用端口 27183）
	ctrl->control_socket = net_create_server_socket(27183);
	if (ctrl->control_socket == INVALID_SOCKET) {
		LOGE("CONTROLLER", "Failed to create control socket");
		return -1;
	}

	ctrl->running = 1;
	LOGI("CONTROLLER", "Controller started on port 27183");
	return 0;
}

// 停止控制器
void controller_stop(Controller *ctrl)
{
	if (ctrl == NULL) {
		return;
	}

	ctrl->running = 0;

	if (ctrl->control_socket != INVALID_SOCKET) {
		//net_close(ctrl->control_socket);
		ctrl->control_socket = INVALID_SOCKET;
	}

	LOGI("CONTROLLER", "Controller stopped");
}

// 清理控制器
void controller_cleanup(Controller *ctrl)
{
	if (ctrl == NULL) {
		return;
	}

	controller_stop(ctrl);
	memset(ctrl, 0, sizeof(Controller));

	LOGI("CONTROLLER", "Controller cleaned up");
}
