#include "options.h"

#include <stddef.h>
#include <cstring>

// 使用 memset 零初始化
static scrcpy_options create_default_options()
{
	scrcpy_options opts{};
	// 设置默认值
	// 基础枚举默认值
	opts.log_level = SC_LOG_LEVEL_INFO;
	opts.video_codec = SC_CODEC_H264;
	opts.audio_codec = SC_CODEC_OPUS;
	opts.video_source = SC_VIDEO_SOURCE_DISPLAY;
	opts.audio_source = SC_AUDIO_SOURCE_AUTO;
	opts.record_format = SC_RECORD_FORMAT_AUTO;

	opts.keyboard_input_mode = SC_KEYBOARD_INPUT_MODE_AUTO;
	opts.mouse_input_mode = SC_MOUSE_INPUT_MODE_AUTO;
	opts.gamepad_input_mode = SC_GAMEPAD_INPUT_MODE_DISABLED;

	opts.camera_facing = SC_CAMERA_FACING_ANY;

	// 功能开关
	opts.control = true;
	opts.video_playback = true;
	opts.audio_playback = true;
	opts.mipmaps = true;
	opts.forward_key_repeat = true;
	opts.clipboard_autosync = true;
	opts.downsize_on_error = true;
	opts.cleanup = true;

	opts.power_on = true;
	opts.video = true;
	opts.audio = true;
	opts.window = true;
	opts.mouse_hover = true;

	opts.vd_destroy_content = true;
	opts.vd_system_decorations = true;

	return opts;
}

const struct scrcpy_options scrcpy_options_default = create_default_options();

enum sc_orientation sc_orientation_apply(enum sc_orientation src, enum sc_orientation transform)
{
	assert(!(src & ~7));
	assert(!(transform & ~7));

	unsigned transform_hflip = transform & 4;
	unsigned transform_rotation = transform & 3;
	unsigned src_hflip = src & 4;
	unsigned src_rotation = src & 3;
	unsigned src_swap = src & 1;
	if (src_swap && transform_hflip) {
		// If the src is rotated by 90 or 270 degrees, applying a flipped
		// transformation requires an additional 180 degrees rotation to
		// compensate for the inversion of the order of multiplication:
		//
		//     hflip1 �� rotate1 �� hflip2 �� rotate2
		//     `--------------'   `--------------'
		//           src             transform
		//
		// In the final result, we want all the hflips then all the rotations,
		// so we must move hflip2 to the left:
		//
		//     hflip1 �� hflip2 �� rotate1' �� rotate2
		//
		// with rotate1' = | rotate1           if src is 0�� or 180��
		//                 | rotate1 + 180��    if src is 90�� or 270��

		src_rotation += 2;
	}

	unsigned result_hflip = src_hflip ^ transform_hflip;
	unsigned result_rotation = (transform_rotation + src_rotation) % 4;
	enum sc_orientation result = static_cast<sc_orientation>(result_hflip | result_rotation);
	return result;
}
