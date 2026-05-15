#include <obs-module.h>
#include <util/windows/win-version.h>
#include "srccpy.hpp"
#include "adb/adb.h"
#include "util/dstr.hpp"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("pulgin-srccpy", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "srccpy sources";
}

static void srccpy_source_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "url", "https://obsproject.com/browser-source");
	obs_data_set_default_int(settings, "fps", 30);
#ifdef ENABLE_BROWSER_SHARED_TEXTURE
	obs_data_set_default_bool(settings, "fps_custom", false);
#else
	obs_data_set_default_bool(settings, "fps_custom", true);
#endif
	obs_data_set_default_bool(settings, "shutdown", false);
	obs_data_set_default_bool(settings, "restart_when_active", false);
	obs_data_set_default_bool(settings, "reroute_audio", false);
}

static bool AddDevice(obs_property_t *device_list, const sc_adb_device &device)
{
	DStr serial, state, model, device_id, name;

	dstr_from_mbs(serial, device.serial.c_str());
	dstr_from_mbs(state, device.state.c_str());
	dstr_from_mbs(model, device.model.c_str());

	dstr_copy_dstr(device_id, serial);
	dstr_cat(device_id, ":");
	dstr_cat_dstr(device_id, state);
	dstr_cat(device_id, ":");
	dstr_cat_dstr(device_id, model);

	dstr_from_mbs(name, device.best_name.c_str());

	obs_property_list_add_string(device_list, name, device_id);

	return true;
}

static obs_properties_t *browser_source_get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	srccpy *bs = static_cast<srccpy *>(data);
	obs_properties_add_button2(
		props, "refresh_devices", obs_module_text("RefreshDeviceList"),
		[](obs_properties_t *, obs_property_t *, void *data) {
			//static_cast<srccpy *>(data)->Refresh();
			return false;
		},
		bs);
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);
	obs_property_t *prop = obs_properties_add_list(props, "device_list", obs_module_text("LocalFile"),
						       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	sc_vec_adb_devices devices;
	bool ok = sc_adb_list_devices(bs->server.m_intr, 0, devices,true);
	//obs_property_set_modified_callback(prop, /*DeviceSelectionChanged*/);
	if (devices.size() > 0 && ok) {
		for (const auto &device : devices)
			AddDevice(prop, device);
	}


	
	/*obs_properties_add_path(props, "local_file", obs_module_text("LocalFile"), OBS_PATH_FILE, "*.*", path->array);
	obs_properties_add_text(props, "url", obs_module_text("URL"), OBS_TEXT_DEFAULT);

	obs_properties_add_int(props, "width", obs_module_text("Width"), 1, 8192, 1);
	obs_properties_add_int(props, "height", obs_module_text("Height"), 1, 8192, 1);

	obs_properties_add_bool(props, "reroute_audio", obs_module_text("RerouteAudio"));

	obs_property_t *fps_set = obs_properties_add_bool(props, "fps_custom", obs_module_text("CustomFrameRate"));
	obs_property_set_modified_callback(fps_set, is_fps_custom);

#ifndef ENABLE_BROWSER_SHARED_TEXTURE
	obs_property_set_enabled(fps_set, false);
#endif

	obs_properties_add_int(props, "fps", obs_module_text("FPS"), 1, 60, 1);

	obs_property_t *p = obs_properties_add_text(props, "css", obs_module_text("CSS"), OBS_TEXT_MULTILINE);
	obs_property_text_set_monospace(p, true);
	obs_properties_add_bool(props, "shutdown", obs_module_text("ShutdownSourceNotVisible"));
	obs_properties_add_bool(props, "restart_when_active", obs_module_text("RefreshBrowserActive"));

	obs_property_t *controlLevel = obs_properties_add_list(props, "webpage_control_level",
							       obs_module_text("WebpageControlLevel"),
							       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(controlLevel, obs_module_text("WebpageControlLevel.Level.None"),
				  (int)ControlLevel::None);
	obs_property_list_add_int(controlLevel, obs_module_text("WebpageControlLevel.Level.ReadObs"),
				  (int)ControlLevel::ReadObs);
	obs_property_list_add_int(controlLevel, obs_module_text("WebpageControlLevel.Level.ReadUser"),
				  (int)ControlLevel::ReadUser);
	obs_property_list_add_int(controlLevel, obs_module_text("WebpageControlLevel.Level.Basic"),
				  (int)ControlLevel::Basic);
	obs_property_list_add_int(controlLevel, obs_module_text("WebpageControlLevel.Level.Advanced"),
				  (int)ControlLevel::Advanced);
	obs_property_list_add_int(controlLevel, obs_module_text("WebpageControlLevel.Level.All"),
				  (int)ControlLevel::All);*/
	return props;
}

void register_srccpy()
{
	struct obs_source_info info{};

	info.id = "srccpy_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION |
			    OBS_SOURCE_DO_NOT_DUPLICATE | OBS_SOURCE_SRGB;
	info.get_properties = browser_source_get_properties;
	info.get_defaults = srccpy_source_get_defaults;
	info.icon_type = OBS_ICON_TYPE_BROWSER;

	info.get_name = [](void *) {
		return obs_module_text("BrowserSource");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		return new srccpy(settings, source);
	};
	info.destroy = [](void *data) {
		//static_cast<BrowserSource *>(data)->Destroy();
	};
	info.update = [](void *data, obs_data_t *settings) {
		//static_cast<BrowserSource *>(data)->Update(settings);
	};
	info.get_width = [](void *data) {
		//return (uint32_t)static_cast<BrowserSource *>(data)->width;
		return (uint32_t)1080;
	};
	info.get_height = [](void *data) {
		//return (uint32_t)static_cast<BrowserSource *>(data)->height;
		return (uint32_t)1920;
	};
	info.video_tick = [](void *data, float) {
		//static_cast<BrowserSource *>(data)->Tick();
	};
	info.video_render = [](void *data, gs_effect_t *) {
		//static_cast<BrowserSource *>(data)->Render();
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
