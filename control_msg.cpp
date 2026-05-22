#include "control_msg.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "util/binary.h"
#include "util/common.h"
#include "util/sc_log.h"

#ifdef LOGW
#undef LOGW
#endif
#define LOGW(...) scrcpy_log(LOG_WARNING, __VA_ARGS__)

#ifdef LOGV
#undef LOGV
#endif
#define LOGV(...) scrcpy_log(LOG_DEBUG, __VA_ARGS__)
#define CLAMP(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))

/**
 * Map an enum value to a string based on an array, without crashing on an
 * out-of-bounds index.
 */
#define ENUM_TO_LABEL(labels, value) \
    ((size_t) (value) < ARRAY_LEN(labels) ? labels[value] : "???")

#define KEYEVENT_ACTION_LABEL(value) \
    ENUM_TO_LABEL(android_keyevent_action_labels, value)

#define MOTIONEVENT_ACTION_LABEL(value) \
    ENUM_TO_LABEL(android_motionevent_action_labels, value)

static const char *const android_keyevent_action_labels[] = {
    "down",
    "up",
    "multi",
};

static const char *const android_motionevent_action_labels[] = {
    "down",
    "up",
    "move",
    "cancel",
    "outside",
    "pointer-down",
    "pointer-up",
    "hover-move",
    "scroll",
    "hover-enter",
    "hover-exit",
    "btn-press",
    "btn-release",
};

static const char *const copy_key_labels[] = {
    "none",
    "copy",
    "cut",
};

static inline const char *
get_well_known_pointer_id_name(uint64_t pointer_id) {
    switch (pointer_id) {
        case SC_POINTER_ID_MOUSE:
            return "mouse";
        case SC_POINTER_ID_GENERIC_FINGER:
            return "finger";
        case SC_POINTER_ID_VIRTUAL_FINGER:
            return "vfinger";
        default:
            return NULL;
    }
}

static void
write_position(uint8_t *buf, const struct sc_position *position) {
    sc_write32be(&buf[0], position->point.x);
    sc_write32be(&buf[4], position->point.y);
    sc_write16be(&buf[8], position->screen_size.width);
    sc_write16be(&buf[10], position->screen_size.height);
}

static size_t
sc_str_utf8_truncation_index(const char *utf8, size_t max_len) {
    if (!utf8) {
        return 0;
    }
    size_t len = strlen(utf8);
    if (len <= max_len) {
        return len;
    }
    size_t i = max_len;
    while (i > 0 && (utf8[i] & 0xC0) == 0x80) {
        i--;
    }
    return i;
}

// Write truncated string, and return the size
static size_t
write_string_payload(uint8_t *payload, const char *utf8, size_t max_len) {
    if (!utf8) {
        return 0;
    }
    size_t len = sc_str_utf8_truncation_index(utf8, max_len);
    memcpy(payload, utf8, len);
    return len;
}

// Write length (4 bytes) + string (non null-terminated)
static size_t
write_string(uint8_t *buf, const char *utf8, size_t max_len) {
    size_t len = write_string_payload(buf + 4, utf8, max_len);
    sc_write32be(buf, (uint32_t)len);
    return 4 + len;
}

// Write length (1 byte) + string (non null-terminated)
static size_t
write_string_tiny(uint8_t *buf, const char *utf8, size_t max_len) {
    assert(max_len <= 0xFF);
    size_t len = write_string_payload(buf + 1, utf8, max_len);
    buf[0] = (uint8_t)len;
    return 1 + len;
}

extern "C" size_t
sc_control_msg_serialize(const struct sc_control_msg *msg, uint8_t *buf) {
    buf[0] = msg->type;
    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_KEYCODE:
            buf[1] = msg->inject_keycode.action;
            sc_write32be(&buf[2], msg->inject_keycode.keycode);
            sc_write32be(&buf[6], msg->inject_keycode.repeat);
            sc_write32be(&buf[10], msg->inject_keycode.metastate);
            return 14;
        case SC_CONTROL_MSG_TYPE_INJECT_TEXT: {
            size_t len = write_string(&buf[1], msg->inject_text.text,
                                      SC_CONTROL_MSG_INJECT_TEXT_MAX_LENGTH);
            return 1 + len;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
            buf[1] = msg->inject_touch_event.action;
            sc_write64be(&buf[2], msg->inject_touch_event.pointer_id);
            write_position(&buf[10], &msg->inject_touch_event.position);
            uint16_t pressure =
                sc_float_to_u16fp(msg->inject_touch_event.pressure);
            sc_write16be(&buf[22], pressure);
            sc_write32be(&buf[24], msg->inject_touch_event.action_button);
            sc_write32be(&buf[28], msg->inject_touch_event.buttons);
            return 32;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT: {
            write_position(&buf[1], &msg->inject_scroll_event.position);
            // Accept values in the range [-16, 16].
            // Normalize to [-1, 1] in order to use sc_float_to_i16fp().
            float hscroll_norm = msg->inject_scroll_event.hscroll / 16;
            hscroll_norm = CLAMP(hscroll_norm, -1, 1);
            float vscroll_norm = msg->inject_scroll_event.vscroll / 16;
            vscroll_norm = CLAMP(vscroll_norm, -1, 1);
            int16_t hscroll = sc_float_to_i16fp(hscroll_norm);
            int16_t vscroll = sc_float_to_i16fp(vscroll_norm);
            sc_write16be(&buf[13], (uint16_t) hscroll);
            sc_write16be(&buf[15], (uint16_t) vscroll);
            sc_write32be(&buf[17], msg->inject_scroll_event.buttons);
            return 21;
        }
        case SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
            buf[1] = msg->back_or_screen_on.action;
            return 2;
        case SC_CONTROL_MSG_TYPE_GET_CLIPBOARD:
            buf[1] = msg->get_clipboard.copy_key;
            return 2;
        case SC_CONTROL_MSG_TYPE_SET_CLIPBOARD: {
            sc_write64be(&buf[1], msg->set_clipboard.sequence);
            buf[9] = !!msg->set_clipboard.paste;
            size_t len = write_string(&buf[10], msg->set_clipboard.text,
                                      SC_CONTROL_MSG_CLIPBOARD_TEXT_MAX_LENGTH);
            return 10 + len;
        }
        case SC_CONTROL_MSG_TYPE_SET_DISPLAY_POWER:
            buf[1] = msg->set_display_power.on;
            return 2;
        case SC_CONTROL_MSG_TYPE_START_APP: {
            size_t len = write_string_tiny(&buf[1], msg->start_app.name, 255);
            return 1 + len;
        }
        case SC_CONTROL_MSG_TYPE_CAMERA_SET_TORCH:
            buf[1] = msg->camera_set_torch.on ? 1 : 0;
            return 2;
        case SC_CONTROL_MSG_TYPE_RESIZE_DISPLAY:
            sc_write16be(&buf[1], msg->resize_display.width);
            sc_write16be(&buf[3], msg->resize_display.height);
            return 5;
        case SC_CONTROL_MSG_TYPE_SWITCH_VIDEO_SOURCE:
            buf[1] = msg->switch_video_source.source;
            if (msg->switch_video_source.source == 0) {
                sc_write32be(&buf[2], msg->switch_video_source.display_id);
                return 6;
            } else {
                size_t len = write_string_tiny(&buf[2], msg->switch_video_source.camera_id, 255);
                return 2 + len;
            }
        case SC_CONTROL_MSG_TYPE_GET_DEVICE_INFO:
            return 1;
        case SC_CONTROL_MSG_TYPE_PAUSE_RESUME_STREAM:
            buf[1] = msg->pause_resume.stream_type;
            buf[2] = msg->pause_resume.pause;
            return 3;
        case SC_CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
        case SC_CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
        case SC_CONTROL_MSG_TYPE_COLLAPSE_PANELS:
        case SC_CONTROL_MSG_TYPE_ROTATE_DEVICE:
        case SC_CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS:
        case SC_CONTROL_MSG_TYPE_RESET_VIDEO:
        case SC_CONTROL_MSG_TYPE_CAMERA_ZOOM_IN:
        case SC_CONTROL_MSG_TYPE_CAMERA_ZOOM_OUT:
            // no additional data
            return 1;
        default:
            LOGW("Unknown message type: %u", (unsigned) msg->type);
            return 0;
    }
}

extern "C" void
sc_control_msg_log(const struct sc_control_msg *msg) {
#define LOG_CMSG(fmt, ...) LOGV("input: " fmt, ## __VA_ARGS__)
    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_KEYCODE:
            LOG_CMSG("key %-4s code=%d repeat=%" PRIu32 " meta=%06lx",
                     KEYEVENT_ACTION_LABEL(msg->inject_keycode.action),
                     (int) msg->inject_keycode.keycode,
                     msg->inject_keycode.repeat,
                     (long) msg->inject_keycode.metastate);
            break;
        case SC_CONTROL_MSG_TYPE_INJECT_TEXT:
            LOG_CMSG("text \"%s\"", msg->inject_text.text);
            break;
        case SC_CONTROL_MSG_TYPE_INJECT_TOUCH_EVENT: {
            int action = msg->inject_touch_event.action
                       & AMOTION_EVENT_ACTION_MASK;
            uint64_t id = msg->inject_touch_event.pointer_id;
            const char *pointer_name = get_well_known_pointer_id_name(id);
            if (pointer_name) {
                // string pointer id
                LOG_CMSG("touch [id=%s] %-4s position=%" PRIi32 ",%" PRIi32
                             " pressure=%f action_button=%06lx buttons=%06lx",
                          pointer_name,
                          MOTIONEVENT_ACTION_LABEL(action),
                          msg->inject_touch_event.position.point.x,
                          msg->inject_touch_event.position.point.y,
                          msg->inject_touch_event.pressure,
                          (long) msg->inject_touch_event.action_button,
                          (long) msg->inject_touch_event.buttons);
            } else {
                // numeric pointer id
                LOG_CMSG("touch [id=%" PRIu64 "] %-4s position=%" PRIi32 ",%"
                             PRIi32 " pressure=%f action_button=%06lx"
                             " buttons=%06lx",
                          id,
                          MOTIONEVENT_ACTION_LABEL(action),
                          msg->inject_touch_event.position.point.x,
                          msg->inject_touch_event.position.point.y,
                          msg->inject_touch_event.pressure,
                          (long) msg->inject_touch_event.action_button,
                          (long) msg->inject_touch_event.buttons);
            }
            break;
        }
        case SC_CONTROL_MSG_TYPE_INJECT_SCROLL_EVENT:
            LOG_CMSG("scroll position=%" PRIi32 ",%" PRIi32 " hscroll=%f"
                         " vscroll=%f buttons=%06lx",
                     msg->inject_scroll_event.position.point.x,
                     msg->inject_scroll_event.position.point.y,
                     msg->inject_scroll_event.hscroll,
                     msg->inject_scroll_event.vscroll,
                     (long) msg->inject_scroll_event.buttons);
            break;
        case SC_CONTROL_MSG_TYPE_BACK_OR_SCREEN_ON:
            LOG_CMSG("back-or-screen-on %s",
                     KEYEVENT_ACTION_LABEL(msg->back_or_screen_on.action));
            break;
        case SC_CONTROL_MSG_TYPE_GET_CLIPBOARD:
            LOG_CMSG("get clipboard copy_key=%s",
                     copy_key_labels[msg->get_clipboard.copy_key]);
            break;
        case SC_CONTROL_MSG_TYPE_SET_CLIPBOARD:
            LOG_CMSG("clipboard %" PRIu64 " %s \"%s\"",
                     msg->set_clipboard.sequence,
                     msg->set_clipboard.paste ? "paste" : "nopaste",
                     msg->set_clipboard.text);
            break;
        case SC_CONTROL_MSG_TYPE_SET_DISPLAY_POWER:
            LOG_CMSG("display power %s",
                     msg->set_display_power.on ? "on" : "off");
            break;
        case SC_CONTROL_MSG_TYPE_RESIZE_DISPLAY:
            LOG_CMSG("resize display %" PRIu16 "x%" PRIu16,
                     msg->resize_display.width, msg->resize_display.height);
            break;
        case SC_CONTROL_MSG_TYPE_SWITCH_VIDEO_SOURCE:
            if (msg->switch_video_source.source == 0) {
                LOG_CMSG("switch video source: display %" PRIu32,
                         msg->switch_video_source.display_id);
            } else {
                LOG_CMSG("switch video source: camera \"%s\"",
                         msg->switch_video_source.camera_id ? msg->switch_video_source.camera_id : "");
            }
            break;
        case SC_CONTROL_MSG_TYPE_GET_DEVICE_INFO:
            LOG_CMSG("get device info");
            break;
        case SC_CONTROL_MSG_TYPE_PAUSE_RESUME_STREAM:
            LOG_CMSG("pause resume stream: type=%u pause=%s",
                     msg->pause_resume.stream_type,
                     msg->pause_resume.pause ? "true" : "false");
            break;
        case SC_CONTROL_MSG_TYPE_EXPAND_NOTIFICATION_PANEL:
            LOG_CMSG("expand notification panel");
            break;
        case SC_CONTROL_MSG_TYPE_EXPAND_SETTINGS_PANEL:
            LOG_CMSG("expand settings panel");
            break;
        case SC_CONTROL_MSG_TYPE_COLLAPSE_PANELS:
            LOG_CMSG("collapse panels");
            break;
        case SC_CONTROL_MSG_TYPE_ROTATE_DEVICE:
            LOG_CMSG("rotate device");
            break;
        case SC_CONTROL_MSG_TYPE_OPEN_HARD_KEYBOARD_SETTINGS:
            LOG_CMSG("open hard keyboard settings");
            break;
        case SC_CONTROL_MSG_TYPE_START_APP:
            LOG_CMSG("start app \"%s\"", msg->start_app.name);
            break;
        case SC_CONTROL_MSG_TYPE_RESET_VIDEO:
            LOG_CMSG("reset video");
            break;
        case SC_CONTROL_MSG_TYPE_CAMERA_SET_TORCH:
            LOG_CMSG("camera set torch %s",
                     msg->camera_set_torch.on ? "on" : "off");
            break;
        case SC_CONTROL_MSG_TYPE_CAMERA_ZOOM_IN:
            LOG_CMSG("camera zoom in");
            break;
        case SC_CONTROL_MSG_TYPE_CAMERA_ZOOM_OUT:
            LOG_CMSG("camera zoom out");
            break;
        default:
            LOG_CMSG("unknown type: %u", (unsigned) msg->type);
            break;
    }
}

extern "C" bool
sc_control_msg_is_droppable(const struct sc_control_msg *msg) {
    (void)msg;
    return true;
}

extern "C" void
sc_control_msg_destroy(struct sc_control_msg *msg) {
    switch (msg->type) {
        case SC_CONTROL_MSG_TYPE_INJECT_TEXT:
            free(msg->inject_text.text);
            break;
        case SC_CONTROL_MSG_TYPE_SET_CLIPBOARD:
            free(msg->set_clipboard.text);
            break;
        case SC_CONTROL_MSG_TYPE_START_APP:
            free(msg->start_app.name);
            break;
        case SC_CONTROL_MSG_TYPE_SWITCH_VIDEO_SOURCE:
            if (msg->switch_video_source.source != 0) {
                free(msg->switch_video_source.camera_id);
            }
            break;
        case SC_CONTROL_MSG_TYPE_GET_DEVICE_INFO:
            break;
        default:
            // do nothing
            break;
    }
}
