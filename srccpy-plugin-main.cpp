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
	obs_data_set_default_int(settings, "choose_camera", SC_CAMERA_FACING_ANY);
	obs_data_set_default_int(settings, "max_fps", 60);
}

static obs_properties_t *scrcpy_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	scrcpy *bs = static_cast<scrcpy *>(data);

	obs_properties_add_button2(
		props, "refresh_devices", obs_module_text("RefreshDeviceList"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			// static_cast<scrcpy *>(data)->Refresh();
			return false;
		},
		bs);
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	auto on_ui_changed = [](obs_properties_t *props, obs_property_t *p, obs_data_t *settings) {
		obs_property_t *c_p = obs_properties_get(props, "choose_camera");
		obs_property_t *fps_p = obs_properties_get(props, "max_fps");
		obs_property_t *r_p = obs_properties_get(props, "choose_res");
		obs_property_t *d_p = obs_properties_get(props, "device_list");

		obs_property_list_clear(r_p);
		obs_property_list_clear(fps_p);

		const char *tmp = obs_data_get_string(settings, "device_list");
		if ((!tmp || tmp[0] == '\0') && obs_property_list_item_count(d_p) > 0) {
			tmp = obs_property_list_item_string(d_p, 0);
		}

		sc_adb_device_info info{};
		bool decode_success = false;
		if (tmp && tmp[0] != '\0') {
			try {
				info = DecodeDevice(tmp);
				decode_success = true;

				const float scales[] = {1.0f, 0.75f, 0.5f, 0.25f};
				for (float s : scales) {
					int v = static_cast<int>(info.max_fps * s);
					obs_property_list_add_int(fps_p, std::to_string(v).c_str(), v);
				}
			} catch (...) {
			}
		}

		int src = static_cast<int>(obs_data_get_int(settings, "choose_src"));
		if (src == SC_VIDEO_SOURCE_CAMERA) {
			obs_property_set_enabled(c_p, true);
			if (decode_success) {
				obs_property_list_item_disable(c_p, SC_CAMERA_FACING_EXTERNAL, !info.has_external);
			}

			const char *res_list[] = {"3180x2160", "2592x1952", "1920x1080",
						  "1280x720",  "960x540",   "640x360"};
			for (const auto &res : res_list) {
				obs_property_list_add_string(r_p, res, res);
			}
		} else {
			obs_property_set_enabled(c_p, false);

			if (decode_success && !info.physical_size.empty()) {
				int cx = 0, cy = 0;
				if (ResolutionValid(info.physical_size, cx, cy)) {
					const float scales[] = {1.0f, 0.75f, 0.5f, 0.25f};
					for (float s : scales) {
						int w = static_cast<int>(cx * s), h = static_cast<int>(cy * s);
						std::string r = std::to_string(w) + "x" + std::to_string(h);
						obs_property_list_add_string(r_p, r.c_str(), r.c_str());
					}
				}
			}
		}
		return true;
	};

	obs_property_t *dev_prop = obs_properties_add_list(props, "device_list", obs_module_text("DEVICE"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	sc_vec_adb_device_infos device_infos;
	if (sc_adb_list_device_infos(bs->server.m_intr, 0, device_infos) && !device_infos.empty()) {
		for (const auto &device_info : device_infos) {
			AddDevice(dev_prop, device_info);
		}
	}

	obs_property_t *src_prop = obs_properties_add_list(props, "choose_src", obs_module_text("ChooseUsedScreen"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(src_prop, "SC_VIDEO_SOURCE_DISPLAY", SC_VIDEO_SOURCE_DISPLAY);
	obs_property_list_add_int(src_prop, "SC_VIDEO_SOURCE_CAMERA", SC_VIDEO_SOURCE_CAMERA);

	obs_property_t *cam_prop = obs_properties_add_list(props, "choose_camera", obs_module_text("ChooseUsedCamera"),
							   OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(cam_prop, "SC_CAMERA_FACING_ANY", SC_CAMERA_FACING_ANY);
	obs_property_list_add_int(cam_prop, "SC_CAMERA_FACING_FRONT", SC_CAMERA_FACING_FRONT);
	obs_property_list_add_int(cam_prop, "SC_CAMERA_FACING_BACK", SC_CAMERA_FACING_BACK);
	obs_property_list_add_int(cam_prop, "SC_CAMERA_FACING_EXTERNAL", SC_CAMERA_FACING_EXTERNAL);

	obs_properties_add_list(props, "choose_res", obs_module_text("ChooseResolution"), OBS_COMBO_TYPE_LIST,
				OBS_COMBO_FORMAT_STRING);

	obs_properties_add_list(props, "max_fps", obs_module_text("MaxFPS"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_set_modified_callback(dev_prop, on_ui_changed);
	obs_property_set_modified_callback(src_prop, on_ui_changed);

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
