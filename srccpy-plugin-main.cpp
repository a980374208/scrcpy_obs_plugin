#include <obs-module.h>
#include <util/windows/win-version.h>
#include "srccpy.hpp"
#include "adb/adb.h"
#include "util/dstr.hpp"
#include "util/str_util.h"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("pulgin-srccpy", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "srccpy sources";
}



static void srccpy_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "choose_res", "1920x1080");
	obs_data_set_default_int(settings, "choose_src", SC_VIDEO_SOURCE_DISPLAY);
	obs_data_set_default_int(settings, "max_fps", 60);
}

static const sc_adb_device_info get_scrcpy_device_info(scrcpy *sc, obs_properties_t *props, obs_data_t *settings)
{
	static const sc_adb_device_info empty_info{};
	const char *device_serial = obs_data_get_string(settings, "device_list");
	// 如果当前没有选择的设备，则默认选用列表中的第一项
	if (!device_serial || device_serial[0] == '\0') {
		obs_property_t *d_p = obs_properties_get(props, "device_list");
		if (d_p && obs_property_list_item_count(d_p) > 0) {
			device_serial = obs_property_list_item_string(d_p, 0);
		}
	}
	if (!device_serial || device_serial[0] == '\0') {
		return empty_info;
	}
	const auto &device_infos = sc->get_device_infos();
	auto it = device_infos.find(device_serial);
	return (it != device_infos.end()) ? it->second : empty_info;
}

static auto on_choose_capture_changed(void *ptr, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int src = static_cast<int>(obs_data_get_int(settings, "choose_src"));
	scrcpy *sc = static_cast<scrcpy *>(ptr);
	obs_property_t *fps_p = obs_properties_get(props, "choose_fps");
	obs_property_t *res_p = obs_properties_get(props, "choose_res");
	if (fps_p)
		obs_property_list_clear(fps_p);
	if (res_p)
		obs_property_list_clear(res_p);
	const auto info = get_scrcpy_device_info(sc, props, settings);
	std::string capture_id = obs_data_get_string(settings, "choose_capture");
	if (src == SC_VIDEO_SOURCE_CAMERA) {
		auto cam_it = info.cameras.find(capture_id);
		if (cam_it != info.cameras.end()) {
			const auto &camera = cam_it->second;
			if (fps_p) {
				for (const auto &fps : camera.suport_fps) {
					std::string name = std::to_string(fps);
					obs_property_list_add_int(fps_p, name.c_str(), fps);
				}
			}
			if (res_p) {
				for (const auto &size : camera.suport_sizes) {
					obs_property_list_add_string(res_p, size.c_str(), size.c_str());
				}
			}
		}
	} else {
		int disp_id = capture_id.empty() ? 0 : std::atoi(capture_id.c_str());
		auto disp_it = info.displays.find(disp_id);
		if (disp_it != info.displays.end()) {
			const auto &display = disp_it->second;
			const float scales[] = {1.0f, 0.75f, 0.5f, 0.25f};
			if (fps_p) {
				for (auto item : scales) {
					int fps = static_cast<int>(display.fps * item);
					std::string name = std::to_string(fps);
					obs_property_list_add_int(fps_p, name.c_str(), fps);
				}
			}
			if (res_p) {
				int cx = 0, cy = 0;
				if (ResolutionValid(display.physical_size, cx, cy)) {
					for (float s : scales) {
						int w = static_cast<int>(cx * s), h = static_cast<int>(cy * s);
						std::string r = std::to_string(w) + "x" + std::to_string(h);
						obs_property_list_add_string(res_p, r.c_str(), r.c_str());
					}
				}
			}
		}
	}
	return true;
};

static auto on_src_changed(void *ptr, obs_properties_t *props, obs_property_t *p, obs_data_t *settings)
{
	int src = static_cast<int>(obs_data_get_int(settings, "choose_src"));
	scrcpy *sc = static_cast<scrcpy *>(ptr);
	obs_property_t *c_c = obs_properties_get(props, "choose_capture");
	if (!c_c)
		return true;
	obs_property_list_clear(c_c);
	const auto &info = get_scrcpy_device_info(sc, props, settings);
	if (src == SC_VIDEO_SOURCE_CAMERA) {
		for (const auto &camera_map : info.cameras) {
			const auto &camera = camera_map.second;
			const char *facing_raw = sc_facing_get_name(camera.facing);
			const char *format_template = obs_module_text("Camera.Format%s(%s)");
			struct dstr name_dstr = {0};
			dstr_printf(&name_dstr, format_template, camera.id.c_str(), facing_raw);
			obs_property_list_add_string(c_c, name_dstr.array, camera.id.c_str());
			dstr_free(&name_dstr);
		}
	} else {
		for (const auto &display_map : info.displays) {
			const auto &display = display_map.second;
			const char *name = obs_module_text("Camera.Display%s");
			std::string id_str = std::to_string(display.id);
			const char *id = id_str.c_str();
			struct dstr name_dstr = {0};
			dstr_printf(&name_dstr, name, id);
			obs_property_list_add_string(c_c, name_dstr.array, id);
		}
	}
	on_choose_capture_changed(ptr, props, c_c, settings);
	return true;
};

static auto on_device_changed(void *ptr, obs_properties_t *props, obs_property_t *p, obs_data_t *settings) {
	scrcpy *sc = static_cast<scrcpy *>(ptr);
	obs_property_t *fps_p = obs_properties_get(props, "choose_fps");
	obs_property_t *r_p = obs_properties_get(props, "choose_res");
	obs_property_t *c_c = obs_properties_get(props, "choose_capture");
	if (fps_p)
		obs_property_list_clear(fps_p);
	if (r_p)
		obs_property_list_clear(r_p);
	const auto &info = get_scrcpy_device_info(sc, props, settings);
	std::string capture_id = obs_data_get_string(settings, "choose_capture");
	// 更新帧率列表
	auto cam_it = info.cameras.find(capture_id);
	if (cam_it != info.cameras.end() && fps_p) {
		for (auto fps : cam_it->second.suport_fps) {
			obs_property_list_add_int(fps_p, std::to_string(fps).c_str(), fps);
		}
	}
	int src = static_cast<int>(obs_data_get_int(settings, "choose_src"));
	if (src == SC_VIDEO_SOURCE_CAMERA) {
		if (c_c) {
			// 判定外置摄像头可用性，并安全置灰禁用
			if (static_cast<size_t>(SC_CAMERA_FACING_EXTERNAL) < obs_property_list_item_count(c_c)) {
				obs_property_list_item_disable(c_c, SC_CAMERA_FACING_EXTERNAL, !info.has_external);
			}
		}
		// 填充摄像头分辨率
		if (cam_it != info.cameras.end() && r_p) {
			for (const auto &res : cam_it->second.suport_sizes) {
				obs_property_list_add_string(r_p, res.c_str(), res.c_str());
			}
		}
	} else {
		// 填充显示器分辨率
		int d_key = capture_id.empty() ? 0 : std::atoi(capture_id.c_str());
		auto disp_it = info.displays.find(d_key);
		int cx = 0, cy = 0;
		if (disp_it != info.displays.end() && ResolutionValid(disp_it->second.physical_size, cx, cy) && r_p) {
			const float scales[] = {1.0f, 0.75f, 0.5f, 0.25f};
			for (float s : scales) {
				int w = static_cast<int>(cx * s);
				int h = static_cast<int>(cy * s);
				std::string r = std::to_string(w) + "x" + std::to_string(h);
				obs_property_list_add_string(r_p, r.c_str(), r.c_str());
			}
		}
	}
	on_src_changed(ptr, props, c_c, settings);
	return true;
};






static obs_properties_t *scrcpy_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	scrcpy *bs = static_cast<scrcpy *>(data);
	// 1. 刷新设备按钮
	obs_properties_add_button2(
		props, "refresh_devices", obs_module_text("RefreshDeviceList"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			// static_cast<scrcpy *>(data)->Refresh();
			return false;
		},
		bs);
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);
	// 2. 设备改变回调

	obs_property_t *dev_prop = obs_properties_add_list(props, "device_list", obs_module_text("DEVICE"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback2(dev_prop, on_device_changed, bs);
	// 获取并填充已连接设备
	sc_vec_adb_devices devices;
	sc_adb_list_devices(bs->server.m_intr, 0, devices);
	sc_vec_adb_device_infos device_infos;

	if (!devices.empty()) {
		bs->get_device_infos(device_infos, devices.at(0).serial);
	}
	for (const auto &device_info : device_infos) {
		AddDevice(dev_prop, device_info.second);
	}
	bs->update_device_infos(device_infos);
	// 3. 画面来源切换（Display 或 Camera）

	obs_property_t *src_prop = obs_properties_add_list(props, "choose_src", obs_module_text("ChooseUsedScreen"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_set_modified_callback2(src_prop, on_src_changed, bs);
	obs_property_list_add_int(src_prop, "SC_VIDEO_SOURCE_DISPLAY", SC_VIDEO_SOURCE_DISPLAY);
	obs_property_list_add_int(src_prop, "SC_VIDEO_SOURCE_CAMERA", SC_VIDEO_SOURCE_CAMERA);
	// 4. 采集具体项切换（Camera ID 或 Display ID 变更）

	obs_property_t *cap_pror = obs_properties_add_list(props, "choose_capture",
							   obs_module_text("ChooseUsedCapture"), OBS_COMBO_TYPE_LIST,
							   OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback2(cap_pror, on_choose_capture_changed, bs);
	obs_properties_add_list(props, "choose_res", obs_module_text("ChooseResolution"), OBS_COMBO_TYPE_LIST,
				OBS_COMBO_FORMAT_STRING);
	obs_properties_add_list(props, "choose_fps", obs_module_text("ChooseFPS"), OBS_COMBO_TYPE_LIST,
				OBS_COMBO_FORMAT_INT);
	return props;
}

void register_srccpy()
{
	struct obs_source_info info{};

	info.id = "srccpy_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE |
			    OBS_SOURCE_CONTROLLABLE_MEDIA,
	info.get_properties = scrcpy_source_get_properties;
	info.get_defaults = srccpy_source_get_defaults;
	info.icon_type = OBS_ICON_TYPE_BROWSER;

	info.get_name = [](void *) {
		return obs_module_text("ScrcpySource");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		return new scrcpy(settings, source);
	};
	info.destroy = [](void *data) {
		//static_cast<BrowserSource *>(data)->Destroy();
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<scrcpy *>(data)->update(settings);
	};
	info.get_width = [](void *data) {
		uint32_t w = static_cast<scrcpy *>(data)->width;
		return w > 0 ? w : (uint32_t)1080;
	};
	info.get_height = [](void *data) {
		uint32_t h = static_cast<scrcpy *>(data)->height;
		return h > 0 ? h : (uint32_t)1920;
	};

	info.show = [](void *data) {
		//static_cast<BrowserSource *>(data)->SetShowing(true);
	};
	info.hide = [](void *data) {
		//static_cast<BrowserSource *>(data)->SetShowing(false);
	};
	info.activate = [](void *data) {
		//BrowserSource *bs = static_cast<BrowserSource *>(data);
		//if (bs->restart)
		//	bs->Refresh();
		//bs->SetActive(true);
	};
	info.deactivate = [](void *data) {
		//static_cast<BrowserSource *>(data)->SetActive(false);
	};

	obs_register_source(&info);
}

extern struct obs_source_info srccpy_source_info;
bool obs_module_load(void)
{

	register_srccpy();
	return true;
}

void obs_module_unload(void)
{

}
