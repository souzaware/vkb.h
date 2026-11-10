/*
 * vkb.h - framework-independent keyboard input for C99.
 *
 * The host owns the window and the event loop. It feeds native keyboard
 * events into this library. The library normalizes them into a small event
 * stream with a physical key, a layout-aware logical key, modifiers, and
 * committed UTF-8 text.
 *
 * Supported hosts:
 *   - Windows: feed WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN, WM_SYSKEYUP,
 *     WM_CHAR, WM_SYSCHAR, WM_UNICHAR, WM_INPUTLANGCHANGE, WM_KILLFOCUS.
 *   - macOS: feed NSEvent keyDown, keyUp, and flagsChanged. Forward committed
 *     text from insertText: through vkb_feed_text, or set
 *     macos_emit_text_from_key_events when only simple text is needed.
 *   - Wayland: feed wl_keyboard.keymap, modifiers, key, repeat_info, enter,
 *     leave. Text is resolved with xkbcommon, including dead keys and compose.
 *
 * Not included: IME preedit strings, candidate windows, and candidate lists.
 * A host with its own IME layer can feed committed text with vkb_feed_text.
 *
 * Define VKB_IMPLEMENTATION in exactly one translation unit before including
 * this file.
 *
 * C99. No allocations per event. One allocation per context, plus xkbcommon
 * allocations when a Wayland keymap changes.
 */

#ifndef VKB_H
#define VKB_H

/*
 * clock_gettime is POSIX, but this library is C99. Set the feature macro
 * before any system header only in the translation unit that compiles the
 * implementation, so other translation units are untouched.
 */
#if defined(VKB_IMPLEMENTATION) && \
	(defined(__linux__) || defined(__APPLE__)) && \
	!defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VKB_VERSION_MAJOR 0
#define VKB_VERSION_MINOR 1
#define VKB_VERSION_PATCH 0

/* Event queue. The host drains it once per frame. */
#define VKB_EVENT_QUEUE_CAPACITY 256

/* A single native key or text commit. Control characters are key events, not
 * text events, so this only needs to hold printable text and compose results. */
#define VKB_TEXT_CAPACITY 128

#define VKB_LAYOUT_NAME_CAPACITY 64
#define VKB_LAYOUT_CAPACITY 16
#define VKB_PHYSICAL_KEY_CAPACITY 256
#define VKB_ERROR_MESSAGE_CAPACITY 160

/* Wayland key repeat is client-side. These bounds keep one late tick from
 * flooding the queue after the host was suspended. */
#define VKB_REPEATS_PER_TICK_CAPACITY 32
#define VKB_MILLISECONDS_PER_SECOND 1000
#define VKB_NANOSECONDS_PER_MILLISECOND 1000000u
#define VKB_DEFAULT_REPEAT_DELAY_MILLISECONDS 500
#define VKB_DEFAULT_REPEAT_INTERVAL_MILLISECONDS 33

#define VKB_UNICODE_MAXIMUM 0x10FFFFu
#define VKB_UNICODE_HIGH_SURROGATE_FIRST 0xD800u
#define VKB_UNICODE_HIGH_SURROGATE_LAST 0xDBFFu
#define VKB_UNICODE_LOW_SURROGATE_FIRST 0xDC00u
#define VKB_UNICODE_LOW_SURROGATE_LAST 0xDFFFu
#define VKB_UTF16_SURROGATE_BASE 0x10000u
#define VKB_UTF16_SURROGATE_SHIFT 10u
#define VKB_UTF8_CONTINUATION_PREFIX 0x80u
#define VKB_UTF8_CONTINUATION_MASK 0xC0u

#if defined(_MSC_VER)
#define VKB_INLINE static __inline
#else
#define VKB_INLINE static inline
#endif

typedef enum vkb_event_type {
	VKB_EVENT_KEY_DOWN = 0,
	VKB_EVENT_KEY_UP,
	VKB_EVENT_TEXT,
	VKB_EVENT_LAYOUT_CHANGED
} vkb_event_type;

typedef enum vkb_key_state {
	VKB_KEY_STATE_RELEASED = 0,
	VKB_KEY_STATE_PRESSED = 1
} vkb_key_state;

typedef enum vkb_wayland_keymap_format {
	VKB_WAYLAND_KEYMAP_FORMAT_XKB_V1 = 1
} vkb_wayland_keymap_format;

typedef uint32_t vkb_modifiers;

enum vkb_modifier_flag {
	VKB_MODIFIER_SHIFT = 1u << 0,
	VKB_MODIFIER_CONTROL = 1u << 1,
	VKB_MODIFIER_ALT = 1u << 2,
	VKB_MODIFIER_SUPER = 1u << 3,
	VKB_MODIFIER_LEFT_SHIFT = 1u << 4,
	VKB_MODIFIER_RIGHT_SHIFT = 1u << 5,
	VKB_MODIFIER_LEFT_CONTROL = 1u << 6,
	VKB_MODIFIER_RIGHT_CONTROL = 1u << 7,
	VKB_MODIFIER_LEFT_ALT = 1u << 8,
	VKB_MODIFIER_RIGHT_ALT = 1u << 9,
	VKB_MODIFIER_LEFT_SUPER = 1u << 10,
	VKB_MODIFIER_RIGHT_SUPER = 1u << 11,
	VKB_MODIFIER_CAPS_LOCK = 1u << 12,
	VKB_MODIFIER_NUM_LOCK = 1u << 13,
	VKB_MODIFIER_ALT_GR = 1u << 14
};

VKB_INLINE bool vkb_modifiers_has_shift(vkb_modifiers modifiers) {
	return (modifiers & VKB_MODIFIER_SHIFT) != 0;
}

VKB_INLINE bool vkb_modifiers_has_control(vkb_modifiers modifiers) {
	return (modifiers & VKB_MODIFIER_CONTROL) != 0;
}

VKB_INLINE bool vkb_modifiers_has_alt(vkb_modifiers modifiers) {
	return (modifiers & VKB_MODIFIER_ALT) != 0;
}

VKB_INLINE bool vkb_modifiers_has_super(vkb_modifiers modifiers) {
	return (modifiers & VKB_MODIFIER_SUPER) != 0;
}

VKB_INLINE bool vkb_modifiers_has_alt_gr(vkb_modifiers modifiers) {
	return (modifiers & VKB_MODIFIER_ALT_GR) != 0;
}

/*
 * Physical keys use the USB HID Usage Tables, Keyboard/Keypad page (0x07).
 * The numeric value is the HID usage identifier. That gives Windows, macOS,
 * and Linux one shared physical vocabulary without inventing another table.
 */
typedef enum vkb_physical_key {
	VKB_PHYSICAL_KEY_UNKNOWN = 0x00,

	VKB_PHYSICAL_KEY_A = 0x04,
	VKB_PHYSICAL_KEY_B = 0x05,
	VKB_PHYSICAL_KEY_C = 0x06,
	VKB_PHYSICAL_KEY_D = 0x07,
	VKB_PHYSICAL_KEY_E = 0x08,
	VKB_PHYSICAL_KEY_F = 0x09,
	VKB_PHYSICAL_KEY_G = 0x0A,
	VKB_PHYSICAL_KEY_H = 0x0B,
	VKB_PHYSICAL_KEY_I = 0x0C,
	VKB_PHYSICAL_KEY_J = 0x0D,
	VKB_PHYSICAL_KEY_K = 0x0E,
	VKB_PHYSICAL_KEY_L = 0x0F,
	VKB_PHYSICAL_KEY_M = 0x10,
	VKB_PHYSICAL_KEY_N = 0x11,
	VKB_PHYSICAL_KEY_O = 0x12,
	VKB_PHYSICAL_KEY_P = 0x13,
	VKB_PHYSICAL_KEY_Q = 0x14,
	VKB_PHYSICAL_KEY_R = 0x15,
	VKB_PHYSICAL_KEY_S = 0x16,
	VKB_PHYSICAL_KEY_T = 0x17,
	VKB_PHYSICAL_KEY_U = 0x18,
	VKB_PHYSICAL_KEY_V = 0x19,
	VKB_PHYSICAL_KEY_W = 0x1A,
	VKB_PHYSICAL_KEY_X = 0x1B,
	VKB_PHYSICAL_KEY_Y = 0x1C,
	VKB_PHYSICAL_KEY_Z = 0x1D,

	VKB_PHYSICAL_KEY_DIGIT_1 = 0x1E,
	VKB_PHYSICAL_KEY_DIGIT_2 = 0x1F,
	VKB_PHYSICAL_KEY_DIGIT_3 = 0x20,
	VKB_PHYSICAL_KEY_DIGIT_4 = 0x21,
	VKB_PHYSICAL_KEY_DIGIT_5 = 0x22,
	VKB_PHYSICAL_KEY_DIGIT_6 = 0x23,
	VKB_PHYSICAL_KEY_DIGIT_7 = 0x24,
	VKB_PHYSICAL_KEY_DIGIT_8 = 0x25,
	VKB_PHYSICAL_KEY_DIGIT_9 = 0x26,
	VKB_PHYSICAL_KEY_DIGIT_0 = 0x27,

	VKB_PHYSICAL_KEY_RETURN = 0x28,
	VKB_PHYSICAL_KEY_ESCAPE = 0x29,
	VKB_PHYSICAL_KEY_BACKSPACE = 0x2A,
	VKB_PHYSICAL_KEY_TAB = 0x2B,
	VKB_PHYSICAL_KEY_SPACE = 0x2C,
	VKB_PHYSICAL_KEY_MINUS = 0x2D,
	VKB_PHYSICAL_KEY_EQUAL = 0x2E,
	VKB_PHYSICAL_KEY_LEFT_BRACKET = 0x2F,
	VKB_PHYSICAL_KEY_RIGHT_BRACKET = 0x30,
	VKB_PHYSICAL_KEY_BACKSLASH = 0x31,
	VKB_PHYSICAL_KEY_NON_US_HASH = 0x32,
	VKB_PHYSICAL_KEY_SEMICOLON = 0x33,
	VKB_PHYSICAL_KEY_APOSTROPHE = 0x34,
	VKB_PHYSICAL_KEY_GRAVE_ACCENT = 0x35,
	VKB_PHYSICAL_KEY_COMMA = 0x36,
	VKB_PHYSICAL_KEY_PERIOD = 0x37,
	VKB_PHYSICAL_KEY_SLASH = 0x38,
	VKB_PHYSICAL_KEY_CAPS_LOCK = 0x39,

	VKB_PHYSICAL_KEY_F1 = 0x3A,
	VKB_PHYSICAL_KEY_F2 = 0x3B,
	VKB_PHYSICAL_KEY_F3 = 0x3C,
	VKB_PHYSICAL_KEY_F4 = 0x3D,
	VKB_PHYSICAL_KEY_F5 = 0x3E,
	VKB_PHYSICAL_KEY_F6 = 0x3F,
	VKB_PHYSICAL_KEY_F7 = 0x40,
	VKB_PHYSICAL_KEY_F8 = 0x41,
	VKB_PHYSICAL_KEY_F9 = 0x42,
	VKB_PHYSICAL_KEY_F10 = 0x43,
	VKB_PHYSICAL_KEY_F11 = 0x44,
	VKB_PHYSICAL_KEY_F12 = 0x45,

	VKB_PHYSICAL_KEY_PRINT_SCREEN = 0x46,
	VKB_PHYSICAL_KEY_SCROLL_LOCK = 0x47,
	VKB_PHYSICAL_KEY_PAUSE = 0x48,
	VKB_PHYSICAL_KEY_INSERT = 0x49,
	VKB_PHYSICAL_KEY_HOME = 0x4A,
	VKB_PHYSICAL_KEY_PAGE_UP = 0x4B,
	VKB_PHYSICAL_KEY_DELETE_FORWARD = 0x4C,
	VKB_PHYSICAL_KEY_END = 0x4D,
	VKB_PHYSICAL_KEY_PAGE_DOWN = 0x4E,
	VKB_PHYSICAL_KEY_RIGHT_ARROW = 0x4F,
	VKB_PHYSICAL_KEY_LEFT_ARROW = 0x50,
	VKB_PHYSICAL_KEY_DOWN_ARROW = 0x51,
	VKB_PHYSICAL_KEY_UP_ARROW = 0x52,

	VKB_PHYSICAL_KEY_NUM_LOCK = 0x53,
	VKB_PHYSICAL_KEY_KEYPAD_DIVIDE = 0x54,
	VKB_PHYSICAL_KEY_KEYPAD_MULTIPLY = 0x55,
	VKB_PHYSICAL_KEY_KEYPAD_SUBTRACT = 0x56,
	VKB_PHYSICAL_KEY_KEYPAD_ADD = 0x57,
	VKB_PHYSICAL_KEY_KEYPAD_ENTER = 0x58,
	VKB_PHYSICAL_KEY_KEYPAD_1 = 0x59,
	VKB_PHYSICAL_KEY_KEYPAD_2 = 0x5A,
	VKB_PHYSICAL_KEY_KEYPAD_3 = 0x5B,
	VKB_PHYSICAL_KEY_KEYPAD_4 = 0x5C,
	VKB_PHYSICAL_KEY_KEYPAD_5 = 0x5D,
	VKB_PHYSICAL_KEY_KEYPAD_6 = 0x5E,
	VKB_PHYSICAL_KEY_KEYPAD_7 = 0x5F,
	VKB_PHYSICAL_KEY_KEYPAD_8 = 0x60,
	VKB_PHYSICAL_KEY_KEYPAD_9 = 0x61,
	VKB_PHYSICAL_KEY_KEYPAD_0 = 0x62,
	VKB_PHYSICAL_KEY_KEYPAD_PERIOD = 0x63,
	VKB_PHYSICAL_KEY_NON_US_BACKSLASH = 0x64,
	VKB_PHYSICAL_KEY_APPLICATION = 0x65,
	VKB_PHYSICAL_KEY_POWER = 0x66,
	VKB_PHYSICAL_KEY_KEYPAD_EQUAL = 0x67,

	VKB_PHYSICAL_KEY_F13 = 0x68,
	VKB_PHYSICAL_KEY_F14 = 0x69,
	VKB_PHYSICAL_KEY_F15 = 0x6A,
	VKB_PHYSICAL_KEY_F16 = 0x6B,
	VKB_PHYSICAL_KEY_F17 = 0x6C,
	VKB_PHYSICAL_KEY_F18 = 0x6D,
	VKB_PHYSICAL_KEY_F19 = 0x6E,
	VKB_PHYSICAL_KEY_F20 = 0x6F,
	VKB_PHYSICAL_KEY_F21 = 0x70,
	VKB_PHYSICAL_KEY_F22 = 0x71,
	VKB_PHYSICAL_KEY_F23 = 0x72,
	VKB_PHYSICAL_KEY_F24 = 0x73,

	VKB_PHYSICAL_KEY_EXECUTE = 0x74,
	VKB_PHYSICAL_KEY_HELP = 0x75,
	VKB_PHYSICAL_KEY_MENU = 0x76,
	VKB_PHYSICAL_KEY_SELECT = 0x77,
	VKB_PHYSICAL_KEY_STOP = 0x78,
	VKB_PHYSICAL_KEY_AGAIN = 0x79,
	VKB_PHYSICAL_KEY_UNDO = 0x7A,
	VKB_PHYSICAL_KEY_CUT = 0x7B,
	VKB_PHYSICAL_KEY_COPY = 0x7C,
	VKB_PHYSICAL_KEY_PASTE = 0x7D,
	VKB_PHYSICAL_KEY_FIND = 0x7E,
	VKB_PHYSICAL_KEY_MUTE = 0x7F,
	VKB_PHYSICAL_KEY_VOLUME_UP = 0x80,
	VKB_PHYSICAL_KEY_VOLUME_DOWN = 0x81,
	VKB_PHYSICAL_KEY_LOCKING_CAPS_LOCK = 0x82,
	VKB_PHYSICAL_KEY_LOCKING_NUM_LOCK = 0x83,
	VKB_PHYSICAL_KEY_LOCKING_SCROLL_LOCK = 0x84,
	VKB_PHYSICAL_KEY_KEYPAD_COMMA = 0x85,
	VKB_PHYSICAL_KEY_KEYPAD_EQUAL_SIGN = 0x86,

	VKB_PHYSICAL_KEY_INTERNATIONAL_1 = 0x87,
	VKB_PHYSICAL_KEY_INTERNATIONAL_2 = 0x88,
	VKB_PHYSICAL_KEY_INTERNATIONAL_3 = 0x89,
	VKB_PHYSICAL_KEY_INTERNATIONAL_4 = 0x8A,
	VKB_PHYSICAL_KEY_INTERNATIONAL_5 = 0x8B,
	VKB_PHYSICAL_KEY_INTERNATIONAL_6 = 0x8C,
	VKB_PHYSICAL_KEY_INTERNATIONAL_7 = 0x8D,
	VKB_PHYSICAL_KEY_INTERNATIONAL_8 = 0x8E,
	VKB_PHYSICAL_KEY_INTERNATIONAL_9 = 0x8F,

	VKB_PHYSICAL_KEY_LANGUAGE_1 = 0x90,
	VKB_PHYSICAL_KEY_LANGUAGE_2 = 0x91,
	VKB_PHYSICAL_KEY_LANGUAGE_3 = 0x92,
	VKB_PHYSICAL_KEY_LANGUAGE_4 = 0x93,
	VKB_PHYSICAL_KEY_LANGUAGE_5 = 0x94,
	VKB_PHYSICAL_KEY_LANGUAGE_6 = 0x95,
	VKB_PHYSICAL_KEY_LANGUAGE_7 = 0x96,
	VKB_PHYSICAL_KEY_LANGUAGE_8 = 0x97,
	VKB_PHYSICAL_KEY_LANGUAGE_9 = 0x98,

	VKB_PHYSICAL_KEY_ALTERNATE_ERASE = 0x99,
	VKB_PHYSICAL_KEY_SYSTEM_REQUEST = 0x9A,
	VKB_PHYSICAL_KEY_CANCEL = 0x9B,
	VKB_PHYSICAL_KEY_CLEAR = 0x9C,
	VKB_PHYSICAL_KEY_PRIOR = 0x9D,
	VKB_PHYSICAL_KEY_RETURN_2 = 0x9E,
	VKB_PHYSICAL_KEY_SEPARATOR = 0x9F,
	VKB_PHYSICAL_KEY_OUT = 0xA0,
	VKB_PHYSICAL_KEY_OPER = 0xA1,
	VKB_PHYSICAL_KEY_CLEAR_AGAIN = 0xA2,
	VKB_PHYSICAL_KEY_CR_SEL = 0xA3,
	VKB_PHYSICAL_KEY_EX_SEL = 0xA4,

	VKB_PHYSICAL_KEY_LEFT_CONTROL = 0xE0,
	VKB_PHYSICAL_KEY_LEFT_SHIFT = 0xE1,
	VKB_PHYSICAL_KEY_LEFT_ALT = 0xE2,
	VKB_PHYSICAL_KEY_LEFT_GUI = 0xE3,
	VKB_PHYSICAL_KEY_RIGHT_CONTROL = 0xE4,
	VKB_PHYSICAL_KEY_RIGHT_SHIFT = 0xE5,
	VKB_PHYSICAL_KEY_RIGHT_ALT = 0xE6,
	VKB_PHYSICAL_KEY_RIGHT_GUI = 0xE7
} vkb_physical_key;

/*
 * Logical keys are layout-aware. A printable key is its Unicode codepoint in
 * the active layout. Non-printing keys live above the Unicode range.
 */
typedef uint32_t vkb_key;

#define VKB_KEY_UNKNOWN 0u
#define VKB_KEY_BACKSPACE 0x08u
#define VKB_KEY_TAB 0x09u
#define VKB_KEY_RETURN 0x0Du
#define VKB_KEY_ESCAPE 0x1Bu
#define VKB_KEY_DELETE 0x7Fu
#define VKB_KEY_SPACE 0x20u
#define VKB_KEY_REPLACEMENT_CHARACTER 0xFFFDu

#define VKB_KEY_LEFT_ARROW 0x110000u
#define VKB_KEY_RIGHT_ARROW 0x110001u
#define VKB_KEY_UP_ARROW 0x110002u
#define VKB_KEY_DOWN_ARROW 0x110003u
#define VKB_KEY_HOME 0x110004u
#define VKB_KEY_END 0x110005u
#define VKB_KEY_PAGE_UP 0x110006u
#define VKB_KEY_PAGE_DOWN 0x110007u
#define VKB_KEY_INSERT 0x110008u
#define VKB_KEY_DELETE_FORWARD 0x110009u

#define VKB_KEY_F1 0x110010u
#define VKB_KEY_F2 0x110011u
#define VKB_KEY_F3 0x110012u
#define VKB_KEY_F4 0x110013u
#define VKB_KEY_F5 0x110014u
#define VKB_KEY_F6 0x110015u
#define VKB_KEY_F7 0x110016u
#define VKB_KEY_F8 0x110017u
#define VKB_KEY_F9 0x110018u
#define VKB_KEY_F10 0x110019u
#define VKB_KEY_F11 0x11001Au
#define VKB_KEY_F12 0x11001Bu
#define VKB_KEY_F13 0x11001Cu
#define VKB_KEY_F14 0x11001Du
#define VKB_KEY_F15 0x11001Eu
#define VKB_KEY_F16 0x11001Fu
#define VKB_KEY_F17 0x110020u
#define VKB_KEY_F18 0x110021u
#define VKB_KEY_F19 0x110022u
#define VKB_KEY_F20 0x110023u
#define VKB_KEY_F21 0x110024u
#define VKB_KEY_F22 0x110025u
#define VKB_KEY_F23 0x110026u
#define VKB_KEY_F24 0x110027u

#define VKB_KEY_LEFT_SHIFT 0x110030u
#define VKB_KEY_RIGHT_SHIFT 0x110031u
#define VKB_KEY_LEFT_CONTROL 0x110032u
#define VKB_KEY_RIGHT_CONTROL 0x110033u
#define VKB_KEY_LEFT_ALT 0x110034u
#define VKB_KEY_RIGHT_ALT 0x110035u
#define VKB_KEY_LEFT_SUPER 0x110036u
#define VKB_KEY_RIGHT_SUPER 0x110037u
#define VKB_KEY_CAPS_LOCK 0x110038u
#define VKB_KEY_NUM_LOCK 0x110039u
#define VKB_KEY_SCROLL_LOCK 0x11003Au
#define VKB_KEY_PRINT_SCREEN 0x11003Bu
#define VKB_KEY_PAUSE 0x11003Cu
#define VKB_KEY_MENU 0x11003Du
#define VKB_KEY_APPLICATION 0x11003Eu

#define VKB_KEY_KEYPAD_0 0x110040u
#define VKB_KEY_KEYPAD_1 0x110041u
#define VKB_KEY_KEYPAD_2 0x110042u
#define VKB_KEY_KEYPAD_3 0x110043u
#define VKB_KEY_KEYPAD_4 0x110044u
#define VKB_KEY_KEYPAD_5 0x110045u
#define VKB_KEY_KEYPAD_6 0x110046u
#define VKB_KEY_KEYPAD_7 0x110047u
#define VKB_KEY_KEYPAD_8 0x110048u
#define VKB_KEY_KEYPAD_9 0x110049u
#define VKB_KEY_KEYPAD_DIVIDE 0x11004Au
#define VKB_KEY_KEYPAD_MULTIPLY 0x11004Bu
#define VKB_KEY_KEYPAD_SUBTRACT 0x11004Cu
#define VKB_KEY_KEYPAD_ADD 0x11004Du
#define VKB_KEY_KEYPAD_ENTER 0x11004Eu
#define VKB_KEY_KEYPAD_PERIOD 0x11004Fu
#define VKB_KEY_KEYPAD_EQUAL 0x110050u
#define VKB_KEY_KEYPAD_COMMA 0x110051u

#define VKB_KEY_MUTE 0x110060u
#define VKB_KEY_VOLUME_UP 0x110061u
#define VKB_KEY_VOLUME_DOWN 0x110062u

#define VKB_KEY_DEAD_GRAVE 0x110100u
#define VKB_KEY_DEAD_ACUTE 0x110101u
#define VKB_KEY_DEAD_CIRCUMFLEX 0x110102u
#define VKB_KEY_DEAD_TILDE 0x110103u
#define VKB_KEY_DEAD_DIAERESIS 0x110104u
#define VKB_KEY_DEAD_CEDILLA 0x110105u
#define VKB_KEY_DEAD_CARON 0x110106u
#define VKB_KEY_DEAD_BREVE 0x110107u
#define VKB_KEY_DEAD_OGONEK 0x110108u
#define VKB_KEY_DEAD_DOUBLE_ACUTE 0x110109u
#define VKB_KEY_DEAD_ABOVE_DOT 0x11010Au
#define VKB_KEY_DEAD_ABOVE_RING 0x11010Bu
#define VKB_KEY_DEAD_STROKE 0x11010Cu
#define VKB_KEY_DEAD_BELOW_DOT 0x11010Du

VKB_INLINE bool vkb_key_is_printable(vkb_key key) {
	if (key < 0x20u || (key >= 0x7Fu && key <= 0x9Fu)) {
		return false;
	}
	if (key >= VKB_UNICODE_HIGH_SURROGATE_FIRST &&
		key <= VKB_UNICODE_LOW_SURROGATE_LAST) {
		return false;
	}
	return key <= VKB_UNICODE_MAXIMUM;
}

typedef struct vkb_event {
	vkb_event_type type;
	vkb_physical_key physical_key;
	vkb_key logical_key;
	vkb_modifiers modifiers;
	bool is_repeat;
	char text[VKB_TEXT_CAPACITY];
	size_t text_length;
	char layout_name[VKB_LAYOUT_NAME_CAPACITY];
	uint64_t timestamp_milliseconds;
} vkb_event;

typedef struct vkb_config {
	bool text_input_enabled;
	/* Wayland only: Windows and macOS repeat via their native event stream. */
	bool key_repeat_enabled;
	/*
	 * macOS committed text normally arrives through insertText:. Keep this
	 * false and forward that text to vkb_feed_text. Enabling it reads
	 * NSEvent.characters as a shortcut for simple layouts, but a dead-key
	 * layout can emit the spacing accent instead of the composed character,
	 * so it is off by default.
	 */
	bool macos_emit_text_from_key_events;
	uint32_t repeat_delay_milliseconds;
	uint32_t repeat_interval_milliseconds;
	/* Windows: HWND for keyboard layout queries. Other platforms: unused. */
	void *native_window;
	/* Windows: thread id for GetKeyboardLayout. Zero means the current thread. */
	uint64_t native_thread_id;
} vkb_config;

typedef struct vkb_context vkb_context;

vkb_config vkb_config_default(void);

vkb_context *vkb_context_create(const vkb_config *configuration);
void vkb_context_destroy(vkb_context *context);

/* Error for the last create call in this thread. */
const char *vkb_last_error(void);
/* Error for the last failed operation on this context. */
const char *vkb_context_error(const vkb_context *context);

bool vkb_next_event(vkb_context *context, vkb_event *event);
void vkb_clear_events(vkb_context *context);
size_t vkb_dropped_event_count(const vkb_context *context);

bool vkb_physical_key_is_down(const vkb_context *context, vkb_physical_key physical_key);
bool vkb_logical_key_is_down(const vkb_context *context, vkb_key logical_key);
vkb_modifiers vkb_get_modifiers(const vkb_context *context);

void vkb_set_text_input_enabled(vkb_context *context, bool enabled);
bool vkb_get_text_input_enabled(const vkb_context *context);

/*
 * Host-provided committed text. Use this for IME commits, macOS insertText:,
 * or any text path the host already owns. Returns false only for invalid
 * arguments or text larger than VKB_TEXT_CAPACITY.
 */
bool vkb_feed_text(vkb_context *context, const char *utf8_text, size_t text_length);

/* Clear held keys, modifiers, compose state, and repeat state. Emits key-up
 * events for keys that were down. */
void vkb_focus_lost(vkb_context *context);

/* Wayland client-side key repeat. Call once per frame with a monotonic time. */
void vkb_tick(vkb_context *context, uint64_t current_time_milliseconds);

int vkb_layout_count(const vkb_context *context);
const char *vkb_layout_name(const vkb_context *context, int layout_index);
int vkb_layout_active_index(const vkb_context *context);
const char *vkb_layout_active_name(const vkb_context *context);

#if defined(_WIN32)
bool vkb_win32_process_message(
	vkb_context *context,
	void *window_handle,
	uint32_t message,
	uintptr_t word_parameter,
	intptr_t long_parameter);
#endif

#if defined(__APPLE__)
bool vkb_macos_process_event(vkb_context *context, void *native_event);
#endif

#if defined(__linux__)
/*
 * Wayland wl_keyboard feed. The file descriptor is consumed and closed by the
 * library. The keymap must arrive before modifiers or key events.
 */
bool vkb_wayland_set_keymap(
	vkb_context *context,
	int file_descriptor,
	uint32_t size,
	uint32_t format);
bool vkb_wayland_set_modifiers(
	vkb_context *context,
	uint32_t depressed,
	uint32_t latched,
	uint32_t locked,
	uint32_t group);
bool vkb_wayland_process_key(
	vkb_context *context,
	uint32_t linux_input_event_code,
	uint32_t state,
	uint32_t time_milliseconds);
void vkb_wayland_set_repeat_info(
	vkb_context *context,
	int32_t rate,
	int32_t delay_milliseconds);
void vkb_wayland_enter(
	vkb_context *context,
	const uint32_t *linux_input_event_codes,
	size_t count);
void vkb_wayland_leave(vkb_context *context);
#endif

#ifdef VKB_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#include <wctype.h>
#elif defined(__linux__)
#include <errno.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#endif

#if defined(_MSC_VER)
#define VKB_THREAD_LOCAL __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define VKB_THREAD_LOCAL _Thread_local
#else
#define VKB_THREAD_LOCAL __thread
#endif

static VKB_THREAD_LOCAL char vkb_global_error_message[VKB_ERROR_MESSAGE_CAPACITY];

static void vkb_assertion_failure(const char *expression, const char *file, int line) {
	fprintf(stderr, "vkb assertion failed: %s (%s:%d)\n", expression, file, line);
	abort();
}

#define VKB_ASSERT(expression) \
	((expression) ? (void)0 : vkb_assertion_failure(#expression, __FILE__, __LINE__))

struct vkb_context {
	vkb_config configuration;

	vkb_event event_queue[VKB_EVENT_QUEUE_CAPACITY];
	size_t event_queue_head;
	size_t event_queue_tail;
	size_t dropped_event_count;

	uint8_t physical_key_down[VKB_PHYSICAL_KEY_CAPACITY / 8];
	vkb_key logical_key_for_physical_key[VKB_PHYSICAL_KEY_CAPACITY];

	vkb_modifiers modifiers;
	bool text_input_enabled;

	char error_message[VKB_ERROR_MESSAGE_CAPACITY];

	int layout_count;
	int active_layout_index;
	char layout_names[VKB_LAYOUT_CAPACITY][VKB_LAYOUT_NAME_CAPACITY];

#if defined(_WIN32)
	HKL keyboard_layout;
	HKL keyboard_layouts[VKB_LAYOUT_CAPACITY];
	DWORD thread_id;
	uint32_t pending_high_surrogate;
#elif defined(__APPLE__)
	bool macos_caps_lock_active;
#elif defined(__linux__)
	struct xkb_context *xkb_context;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;
	struct xkb_compose_table *xkb_compose_table;
	struct xkb_compose_state *xkb_compose_state;
	xkb_mod_index_t xkb_shift_modifier;
	xkb_mod_index_t xkb_control_modifier;
	xkb_mod_index_t xkb_alt_modifier;
	xkb_mod_index_t xkb_super_modifier;
	xkb_mod_index_t xkb_caps_lock_modifier;
	xkb_mod_index_t xkb_num_lock_modifier;
	xkb_mod_index_t xkb_altgr_modifier;
	bool repeat_enabled;
	uint32_t repeating_keycode;
	uint64_t next_repeat_time_milliseconds;
	uint32_t repeat_delay_milliseconds;
	uint32_t repeat_interval_milliseconds;
#endif
};

/*
 * Used only for display labels (layout names, error strings). Input data never
 * goes through this path and is never truncated.
 */
static void vkb_copy_string(char *destination, size_t destination_capacity, const char *source) {
	size_t source_length;
	if (destination_capacity == 0) {
		return;
	}
	if (source == NULL) {
		source = "";
	}
	source_length = strlen(source);
	if (source_length >= destination_capacity) {
		source_length = destination_capacity - 1;
	}
	memcpy(destination, source, source_length);
	destination[source_length] = '\0';
}

static void vkb_set_error(vkb_context *context, const char *message) {
	if (context == NULL) {
		vkb_copy_string(vkb_global_error_message, sizeof(vkb_global_error_message), message);
		return;
	}
	vkb_copy_string(context->error_message, sizeof(context->error_message), message);
}

static uint64_t vkb_current_time_milliseconds(void) {
#if defined(_WIN32)
	return (uint64_t)GetTickCount64();
#else
	struct timespec time_value;
	if (clock_gettime(CLOCK_MONOTONIC, &time_value) != 0) {
		return 0;
	}
	return (uint64_t)time_value.tv_sec * VKB_MILLISECONDS_PER_SECOND +
		(uint64_t)time_value.tv_nsec / VKB_NANOSECONDS_PER_MILLISECOND;
#endif
}

#if defined(_WIN32)
static size_t vkb_utf8_encode(uint32_t codepoint, char *buffer, size_t buffer_capacity) {
	if (buffer_capacity < 4) {
		return 0;
	}
	if (codepoint > VKB_UNICODE_MAXIMUM ||
		(codepoint >= VKB_UNICODE_HIGH_SURROGATE_FIRST &&
		 codepoint <= VKB_UNICODE_LOW_SURROGATE_LAST)) {
		codepoint = VKB_KEY_REPLACEMENT_CHARACTER;
	}
	if (codepoint <= 0x7Fu) {
		buffer[0] = (char)codepoint;
		return 1;
	}
	if (codepoint <= 0x7FFu) {
		buffer[0] = (char)(0xC0u | (codepoint >> 6));
		buffer[1] = (char)(0x80u | (codepoint & 0x3Fu));
		return 2;
	}
	if (codepoint <= 0xFFFFu) {
		buffer[0] = (char)(0xE0u | (codepoint >> 12));
		buffer[1] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
		buffer[2] = (char)(0x80u | (codepoint & 0x3Fu));
		return 3;
	}
	buffer[0] = (char)(0xF0u | (codepoint >> 18));
	buffer[1] = (char)(0x80u | ((codepoint >> 12) & 0x3Fu));
	buffer[2] = (char)(0x80u | ((codepoint >> 6) & 0x3Fu));
	buffer[3] = (char)(0x80u | (codepoint & 0x3Fu));
	return 4;
}
#endif

#if defined(__linux__) || defined(__APPLE__)
static bool vkb_utf8_decode_first(
	const char *text,
	size_t text_length,
	uint32_t *codepoint,
	size_t *bytes_consumed) {
	unsigned char first;
	uint32_t value;
	size_t count;
	size_t index;

	if (text == NULL || text_length == 0) {
		return false;
	}
	first = (unsigned char)text[0];
	if (first <= 0x7Fu) {
		*codepoint = first;
		*bytes_consumed = 1;
		return true;
	}
	if ((first & 0xE0u) == 0xC0u) {
		value = first & 0x1Fu;
		count = 2;
	} else if ((first & 0xF0u) == 0xE0u) {
		value = first & 0x0Fu;
		count = 3;
	} else if ((first & 0xF8u) == 0xF0u) {
		value = first & 0x07u;
		count = 4;
	} else {
		return false;
	}
	if (text_length < count) {
		return false;
	}
	for (index = 1; index < count; index += 1) {
		unsigned char continuation = (unsigned char)text[index];
		if ((continuation & VKB_UTF8_CONTINUATION_MASK) != VKB_UTF8_CONTINUATION_PREFIX) {
			return false;
		}
		value = (value << 6) | (uint32_t)(continuation & 0x3Fu);
	}
	if ((count == 2 && value < 0x80u) ||
		(count == 3 && value < 0x800u) ||
		(count == 4 && value < VKB_UTF16_SURROGATE_BASE) ||
		value > VKB_UNICODE_MAXIMUM ||
		(value >= VKB_UNICODE_HIGH_SURROGATE_FIRST &&
		 value <= VKB_UNICODE_LOW_SURROGATE_LAST)) {
		return false;
	}
	*codepoint = value;
	*bytes_consumed = count;
	return true;
}
#endif

static bool vkb_physical_key_is_down_internal(
	const vkb_context *context,
	vkb_physical_key physical_key) {
	unsigned int key_value = (unsigned int)physical_key;
	if (key_value >= VKB_PHYSICAL_KEY_CAPACITY) {
		return false;
	}
	return (context->physical_key_down[key_value / 8] & (uint8_t)(1u << (key_value % 8))) != 0;
}

static void vkb_physical_key_set_down(
	vkb_context *context,
	vkb_physical_key physical_key,
	bool is_down) {
	unsigned int key_value = (unsigned int)physical_key;
	uint8_t mask;
	if (key_value >= VKB_PHYSICAL_KEY_CAPACITY) {
		return;
	}
	mask = (uint8_t)(1u << (key_value % 8));
	if (is_down) {
		context->physical_key_down[key_value / 8] |= mask;
	} else {
		context->physical_key_down[key_value / 8] &= (uint8_t)~mask;
	}
}

static void vkb_store_logical_key(
	vkb_context *context,
	vkb_physical_key physical_key,
	vkb_key logical_key) {
	unsigned int key_value = (unsigned int)physical_key;
	if (key_value >= VKB_PHYSICAL_KEY_CAPACITY) {
		return;
	}
	context->logical_key_for_physical_key[key_value] = logical_key;
}

static vkb_key vkb_take_logical_key(
	vkb_context *context,
	vkb_physical_key physical_key,
	vkb_key fallback) {
	unsigned int key_value = (unsigned int)physical_key;
	vkb_key logical_key;
	if (key_value >= VKB_PHYSICAL_KEY_CAPACITY) {
		return fallback;
	}
	logical_key = context->logical_key_for_physical_key[key_value];
	context->logical_key_for_physical_key[key_value] = VKB_KEY_UNKNOWN;
	return logical_key != VKB_KEY_UNKNOWN ? logical_key : fallback;
}

static bool vkb_push_event(vkb_context *context, const vkb_event *event) {
	size_t next_head;
	VKB_ASSERT(context != NULL);
	VKB_ASSERT(event != NULL);
	VKB_ASSERT(event->text_length <= VKB_TEXT_CAPACITY);

	next_head = (context->event_queue_head + 1) % VKB_EVENT_QUEUE_CAPACITY;
	if (next_head == context->event_queue_tail) {
		context->dropped_event_count += 1;
		vkb_set_error(context, "event queue full");
		return false;
	}
	context->event_queue[context->event_queue_head] = *event;
	context->event_queue_head = next_head;
	return true;
}

static void vkb_emit_key_event(
	vkb_context *context,
	vkb_event_type type,
	vkb_physical_key physical_key,
	vkb_key logical_key,
	bool is_repeat,
	uint64_t timestamp_milliseconds) {
	vkb_event event;
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.physical_key = physical_key;
	event.logical_key = logical_key;
	event.modifiers = context->modifiers;
	event.is_repeat = is_repeat;
	event.timestamp_milliseconds = timestamp_milliseconds;
	vkb_push_event(context, &event);
}

static bool vkb_emit_text(
	vkb_context *context,
	const char *utf8_text,
	size_t text_length,
	uint64_t timestamp_milliseconds) {
	vkb_event event;
	if (text_length == 0) {
		return true;
	}
	VKB_ASSERT(utf8_text != NULL);
	if (text_length > VKB_TEXT_CAPACITY) {
		vkb_set_error(context, "text exceeds VKB_TEXT_CAPACITY");
		return false;
	}
	if (!context->text_input_enabled) {
		return true;
	}
	memset(&event, 0, sizeof(event));
	event.type = VKB_EVENT_TEXT;
	event.modifiers = context->modifiers;
	event.text_length = text_length;
	memcpy(event.text, utf8_text, text_length);
	event.timestamp_milliseconds = timestamp_milliseconds;
	return vkb_push_event(context, &event);
}

#if defined(_WIN32)
static bool vkb_emit_text_codepoint(
	vkb_context *context,
	uint32_t codepoint,
	uint64_t timestamp_milliseconds) {
	char buffer[4];
	size_t length;
	if (!vkb_key_is_printable(codepoint)) {
		return true;
	}
	length = vkb_utf8_encode(codepoint, buffer, sizeof(buffer));
	return vkb_emit_text(context, buffer, length, timestamp_milliseconds);
}
#endif

#if defined(_WIN32) || defined(__linux__)
static void vkb_emit_layout_event(vkb_context *context, uint64_t timestamp_milliseconds) {
	vkb_event event;
	memset(&event, 0, sizeof(event));
	event.type = VKB_EVENT_LAYOUT_CHANGED;
	event.modifiers = context->modifiers;
	event.timestamp_milliseconds = timestamp_milliseconds;
	if (context->active_layout_index >= 0 &&
		context->active_layout_index < context->layout_count) {
		vkb_copy_string(
			event.layout_name,
			sizeof(event.layout_name),
			context->layout_names[context->active_layout_index]);
	}
	vkb_push_event(context, &event);
}
#endif

static void vkb_reset_logical_keys(vkb_context *context) {
	size_t index;
	for (index = 0; index < VKB_PHYSICAL_KEY_CAPACITY; index += 1) {
		context->logical_key_for_physical_key[index] = VKB_KEY_UNKNOWN;
	}
}

#if defined(__linux__) || defined(__APPLE__)
static void vkb_recompute_side_modifiers(vkb_context *context) {
	context->modifiers &= (vkb_modifiers)~(
		VKB_MODIFIER_LEFT_SHIFT |
		VKB_MODIFIER_RIGHT_SHIFT |
		VKB_MODIFIER_LEFT_CONTROL |
		VKB_MODIFIER_RIGHT_CONTROL |
		VKB_MODIFIER_LEFT_ALT |
		VKB_MODIFIER_RIGHT_ALT |
		VKB_MODIFIER_LEFT_SUPER |
		VKB_MODIFIER_RIGHT_SUPER);
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_LEFT_SHIFT)) {
		context->modifiers |= VKB_MODIFIER_LEFT_SHIFT | VKB_MODIFIER_SHIFT;
	}
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_RIGHT_SHIFT)) {
		context->modifiers |= VKB_MODIFIER_RIGHT_SHIFT | VKB_MODIFIER_SHIFT;
	}
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_LEFT_CONTROL)) {
		context->modifiers |= VKB_MODIFIER_LEFT_CONTROL | VKB_MODIFIER_CONTROL;
	}
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_RIGHT_CONTROL)) {
		context->modifiers |= VKB_MODIFIER_RIGHT_CONTROL | VKB_MODIFIER_CONTROL;
	}
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_LEFT_ALT)) {
		context->modifiers |= VKB_MODIFIER_LEFT_ALT | VKB_MODIFIER_ALT;
	}
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_RIGHT_ALT)) {
		context->modifiers |= VKB_MODIFIER_RIGHT_ALT | VKB_MODIFIER_ALT;
	}
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_LEFT_GUI)) {
		context->modifiers |= VKB_MODIFIER_LEFT_SUPER | VKB_MODIFIER_SUPER;
	}
	if (vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_RIGHT_GUI)) {
		context->modifiers |= VKB_MODIFIER_RIGHT_SUPER | VKB_MODIFIER_SUPER;
	}
}
#endif

static void vkb_clear_held_state(vkb_context *context) {
	size_t physical_key_value;
	for (physical_key_value = 0; physical_key_value < VKB_PHYSICAL_KEY_CAPACITY; physical_key_value += 1) {
		if (vkb_physical_key_is_down_internal(context, (vkb_physical_key)physical_key_value)) {
			vkb_key logical_key = vkb_take_logical_key(
				context,
				(vkb_physical_key)physical_key_value,
				VKB_KEY_UNKNOWN);
			vkb_emit_key_event(
				context,
				VKB_EVENT_KEY_UP,
				(vkb_physical_key)physical_key_value,
				logical_key,
				false,
				vkb_current_time_milliseconds());
			vkb_physical_key_set_down(context, (vkb_physical_key)physical_key_value, false);
		}
	}
	vkb_reset_logical_keys(context);
	context->modifiers = 0;
#if defined(_WIN32)
	context->pending_high_surrogate = 0;
#elif defined(__linux__)
	if (context->xkb_compose_state != NULL) {
		xkb_compose_state_reset(context->xkb_compose_state);
	}
	if (context->xkb_state != NULL) {
		xkb_layout_index_t group = xkb_state_serialize_layout(
			context->xkb_state,
			XKB_STATE_LAYOUT_EFFECTIVE);
		xkb_state_update_mask(context->xkb_state, 0, 0, 0, 0, 0, group);
	}
	context->repeating_keycode = 0;
	context->next_repeat_time_milliseconds = 0;
#endif
}

vkb_config vkb_config_default(void) {
	vkb_config configuration;
	memset(&configuration, 0, sizeof(configuration));
	configuration.text_input_enabled = true;
	configuration.key_repeat_enabled = true;
	configuration.macos_emit_text_from_key_events = false;
	configuration.repeat_delay_milliseconds = VKB_DEFAULT_REPEAT_DELAY_MILLISECONDS;
	configuration.repeat_interval_milliseconds = VKB_DEFAULT_REPEAT_INTERVAL_MILLISECONDS;
	configuration.native_window = NULL;
	configuration.native_thread_id = 0;
	return configuration;
}

const char *vkb_last_error(void) {
	return vkb_global_error_message;
}

const char *vkb_context_error(const vkb_context *context) {
	if (context == NULL) {
		return vkb_global_error_message;
	}
	return context->error_message;
}

bool vkb_next_event(vkb_context *context, vkb_event *event) {
	VKB_ASSERT(context != NULL);
	VKB_ASSERT(event != NULL);
	if (context->event_queue_tail == context->event_queue_head) {
		return false;
	}
	*event = context->event_queue[context->event_queue_tail];
	context->event_queue_tail = (context->event_queue_tail + 1) % VKB_EVENT_QUEUE_CAPACITY;
	return true;
}

void vkb_clear_events(vkb_context *context) {
	VKB_ASSERT(context != NULL);
	context->event_queue_tail = context->event_queue_head;
}

size_t vkb_dropped_event_count(const vkb_context *context) {
	VKB_ASSERT(context != NULL);
	return context->dropped_event_count;
}

bool vkb_physical_key_is_down(const vkb_context *context, vkb_physical_key physical_key) {
	VKB_ASSERT(context != NULL);
	return vkb_physical_key_is_down_internal(context, physical_key);
}

bool vkb_logical_key_is_down(const vkb_context *context, vkb_key logical_key) {
	size_t physical_key_value;
	VKB_ASSERT(context != NULL);
	if (logical_key == VKB_KEY_UNKNOWN) {
		return false;
	}
	for (physical_key_value = 0; physical_key_value < VKB_PHYSICAL_KEY_CAPACITY; physical_key_value += 1) {
		if (vkb_physical_key_is_down_internal(context, (vkb_physical_key)physical_key_value) &&
			context->logical_key_for_physical_key[physical_key_value] == logical_key) {
			return true;
		}
	}
	return false;
}

vkb_modifiers vkb_get_modifiers(const vkb_context *context) {
	VKB_ASSERT(context != NULL);
	return context->modifiers;
}

void vkb_set_text_input_enabled(vkb_context *context, bool enabled) {
	VKB_ASSERT(context != NULL);
	context->text_input_enabled = enabled;
#if defined(_WIN32)
	context->pending_high_surrogate = 0;
#elif defined(__linux__)
	if (context->xkb_compose_state != NULL) {
		xkb_compose_state_reset(context->xkb_compose_state);
	}
#endif
}

bool vkb_get_text_input_enabled(const vkb_context *context) {
	VKB_ASSERT(context != NULL);
	return context->text_input_enabled;
}

bool vkb_feed_text(vkb_context *context, const char *utf8_text, size_t text_length) {
	VKB_ASSERT(context != NULL);
	if (utf8_text == NULL && text_length != 0) {
		vkb_set_error(context, "text pointer is null");
		return false;
	}
	return vkb_emit_text(context, utf8_text, text_length, vkb_current_time_milliseconds());
}

void vkb_focus_lost(vkb_context *context) {
	VKB_ASSERT(context != NULL);
	vkb_clear_held_state(context);
}

int vkb_layout_count(const vkb_context *context) {
	VKB_ASSERT(context != NULL);
	return context->layout_count;
}

const char *vkb_layout_name(const vkb_context *context, int layout_index) {
	VKB_ASSERT(context != NULL);
	if (layout_index < 0 || layout_index >= context->layout_count) {
		return "";
	}
	return context->layout_names[layout_index];
}

int vkb_layout_active_index(const vkb_context *context) {
	VKB_ASSERT(context != NULL);
	return context->active_layout_index;
}

const char *vkb_layout_active_name(const vkb_context *context) {
	VKB_ASSERT(context != NULL);
	if (context->active_layout_index < 0 ||
		context->active_layout_index >= context->layout_count) {
		return "";
	}
	return context->layout_names[context->active_layout_index];
}

#if defined(__linux__)

static bool vkb_read_file_descriptor(int file_descriptor, uint32_t size, char **text) {
	char *buffer;
	size_t total = 0;

	*text = NULL;
	if (size == 0) {
		close(file_descriptor);
		return false;
	}
	buffer = (char *)malloc((size_t)size + 1);
	if (buffer == NULL) {
		close(file_descriptor);
		return false;
	}
	while (total < size) {
		ssize_t count = read(file_descriptor, buffer + total, (size_t)size - total);
		if (count < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (count == 0) {
			break;
		}
		total += (size_t)count;
	}
	close(file_descriptor);
	if (total != size) {
		free(buffer);
		return false;
	}
	buffer[size] = '\0';
	*text = buffer;
	return true;
}

static xkb_mod_index_t vkb_wayland_modifier_index(struct xkb_keymap *keymap, const char *name) {
	return xkb_keymap_mod_get_index(keymap, name);
}

static bool vkb_wayland_modifier_is_active(
	const vkb_context *context,
	xkb_mod_index_t modifier_index) {
	if (modifier_index == XKB_MOD_INVALID || context->xkb_state == NULL) {
		return false;
	}
	return xkb_state_mod_index_is_active(
		context->xkb_state,
		modifier_index,
		XKB_STATE_MODS_EFFECTIVE) != 0;
}

static void vkb_wayland_update_layout_names(vkb_context *context) {
	xkb_layout_index_t layout_count;
	xkb_layout_index_t layout_index;

	context->layout_count = 0;
	if (context->xkb_keymap == NULL) {
		return;
	}
	layout_count = xkb_keymap_num_layouts(context->xkb_keymap);
	if (layout_count > VKB_LAYOUT_CAPACITY) {
		layout_count = VKB_LAYOUT_CAPACITY;
	}
	context->layout_count = (int)layout_count;
	for (layout_index = 0; layout_index < layout_count; layout_index += 1) {
		const char *name = xkb_keymap_layout_get_name(context->xkb_keymap, layout_index);
		if (name == NULL || name[0] == '\0') {
			snprintf(
				context->layout_names[layout_index],
				sizeof(context->layout_names[layout_index]),
				"Layout %u",
				(unsigned int)layout_index + 1u);
		} else {
			vkb_copy_string(
				context->layout_names[layout_index],
				sizeof(context->layout_names[layout_index]),
				name);
		}
	}
	if (context->layout_count == 0) {
		context->active_layout_index = -1;
	} else if (context->active_layout_index < 0) {
		context->active_layout_index = 0;
	}
}

static void vkb_wayland_update_modifiers(vkb_context *context) {
	vkb_modifiers modifiers = 0;

	if (vkb_wayland_modifier_is_active(context, context->xkb_shift_modifier)) {
		modifiers |= VKB_MODIFIER_SHIFT;
	}
	if (vkb_wayland_modifier_is_active(context, context->xkb_control_modifier)) {
		modifiers |= VKB_MODIFIER_CONTROL;
	}
	if (vkb_wayland_modifier_is_active(context, context->xkb_alt_modifier)) {
		modifiers |= VKB_MODIFIER_ALT;
	}
	if (vkb_wayland_modifier_is_active(context, context->xkb_super_modifier)) {
		modifiers |= VKB_MODIFIER_SUPER;
	}
	if (vkb_wayland_modifier_is_active(context, context->xkb_caps_lock_modifier)) {
		modifiers |= VKB_MODIFIER_CAPS_LOCK;
	}
	if (vkb_wayland_modifier_is_active(context, context->xkb_num_lock_modifier)) {
		modifiers |= VKB_MODIFIER_NUM_LOCK;
	}
	context->modifiers = modifiers;
	vkb_recompute_side_modifiers(context);
	if (vkb_wayland_modifier_is_active(context, context->xkb_altgr_modifier) ||
		(vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_RIGHT_ALT) &&
		 vkb_physical_key_is_down_internal(context, VKB_PHYSICAL_KEY_LEFT_CONTROL))) {
		context->modifiers |= VKB_MODIFIER_ALT_GR;
	}
}

static void vkb_wayland_update_layout(vkb_context *context, uint64_t timestamp_milliseconds) {
	xkb_layout_index_t group;
	int layout_index;
	if (context->xkb_state == NULL || context->layout_count == 0) {
		return;
	}
	group = xkb_state_serialize_layout(context->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
	layout_index = (int)group;
	if (layout_index < 0 || layout_index >= context->layout_count) {
		layout_index = 0;
	}
	if (layout_index != context->active_layout_index) {
		context->active_layout_index = layout_index;
		vkb_emit_layout_event(
			context,
			timestamp_milliseconds != 0 ? timestamp_milliseconds : vkb_current_time_milliseconds());
	}
}

static bool vkb_wayland_text_is_allowed(const vkb_context *context) {
	if (!context->text_input_enabled) {
		return false;
	}
	if ((context->modifiers & VKB_MODIFIER_SUPER) != 0) {
		return false;
	}
	if ((context->modifiers & VKB_MODIFIER_CONTROL) != 0 &&
		(context->modifiers & VKB_MODIFIER_ALT_GR) == 0) {
		return false;
	}
	if ((context->modifiers & VKB_MODIFIER_ALT) != 0 &&
		(context->modifiers & VKB_MODIFIER_ALT_GR) == 0) {
		return false;
	}
	return true;
}

static vkb_key vkb_wayland_keysym_to_key(xkb_keysym_t keysym) {
	uint32_t codepoint;

	if (keysym >= XKB_KEY_KP_0 && keysym <= XKB_KEY_KP_9) {
		return VKB_KEY_KEYPAD_0 + (vkb_key)(keysym - XKB_KEY_KP_0);
	}
	switch (keysym) {
	case XKB_KEY_KP_Divide: return VKB_KEY_KEYPAD_DIVIDE;
	case XKB_KEY_KP_Multiply: return VKB_KEY_KEYPAD_MULTIPLY;
	case XKB_KEY_KP_Subtract: return VKB_KEY_KEYPAD_SUBTRACT;
	case XKB_KEY_KP_Add: return VKB_KEY_KEYPAD_ADD;
	case XKB_KEY_KP_Enter: return VKB_KEY_KEYPAD_ENTER;
	case XKB_KEY_KP_Decimal: return VKB_KEY_KEYPAD_PERIOD;
	case XKB_KEY_KP_Separator: return VKB_KEY_KEYPAD_COMMA;
	case XKB_KEY_KP_Equal: return VKB_KEY_KEYPAD_EQUAL;
	default: break;
	}
	codepoint = xkb_keysym_to_utf32(keysym);
	if (codepoint != 0 && vkb_key_is_printable(codepoint)) {
		return codepoint;
	}
	switch (keysym) {
	case XKB_KEY_BackSpace:
		return VKB_KEY_BACKSPACE;
	case XKB_KEY_Tab:
		return VKB_KEY_TAB;
	case XKB_KEY_Return:
		return VKB_KEY_RETURN;
	case XKB_KEY_Escape:
		return VKB_KEY_ESCAPE;
	case XKB_KEY_Delete:
		return VKB_KEY_DELETE_FORWARD;
	case XKB_KEY_Left:
		return VKB_KEY_LEFT_ARROW;
	case XKB_KEY_Right:
		return VKB_KEY_RIGHT_ARROW;
	case XKB_KEY_Up:
		return VKB_KEY_UP_ARROW;
	case XKB_KEY_Down:
		return VKB_KEY_DOWN_ARROW;
	case XKB_KEY_Home:
		return VKB_KEY_HOME;
	case XKB_KEY_End:
		return VKB_KEY_END;
	case XKB_KEY_Prior:
		return VKB_KEY_PAGE_UP;
	case XKB_KEY_Next:
		return VKB_KEY_PAGE_DOWN;
	case XKB_KEY_Insert:
		return VKB_KEY_INSERT;
	case XKB_KEY_F1: return VKB_KEY_F1;
	case XKB_KEY_F2: return VKB_KEY_F2;
	case XKB_KEY_F3: return VKB_KEY_F3;
	case XKB_KEY_F4: return VKB_KEY_F4;
	case XKB_KEY_F5: return VKB_KEY_F5;
	case XKB_KEY_F6: return VKB_KEY_F6;
	case XKB_KEY_F7: return VKB_KEY_F7;
	case XKB_KEY_F8: return VKB_KEY_F8;
	case XKB_KEY_F9: return VKB_KEY_F9;
	case XKB_KEY_F10: return VKB_KEY_F10;
	case XKB_KEY_F11: return VKB_KEY_F11;
	case XKB_KEY_F12: return VKB_KEY_F12;
	case XKB_KEY_F13: return VKB_KEY_F13;
	case XKB_KEY_F14: return VKB_KEY_F14;
	case XKB_KEY_F15: return VKB_KEY_F15;
	case XKB_KEY_F16: return VKB_KEY_F16;
	case XKB_KEY_F17: return VKB_KEY_F17;
	case XKB_KEY_F18: return VKB_KEY_F18;
	case XKB_KEY_F19: return VKB_KEY_F19;
	case XKB_KEY_F20: return VKB_KEY_F20;
	case XKB_KEY_F21: return VKB_KEY_F21;
	case XKB_KEY_F22: return VKB_KEY_F22;
	case XKB_KEY_F23: return VKB_KEY_F23;
	case XKB_KEY_F24: return VKB_KEY_F24;
	case XKB_KEY_Shift_L: return VKB_KEY_LEFT_SHIFT;
	case XKB_KEY_Shift_R: return VKB_KEY_RIGHT_SHIFT;
	case XKB_KEY_Control_L: return VKB_KEY_LEFT_CONTROL;
	case XKB_KEY_Control_R: return VKB_KEY_RIGHT_CONTROL;
	case XKB_KEY_Alt_L: return VKB_KEY_LEFT_ALT;
	case XKB_KEY_Alt_R: return VKB_KEY_RIGHT_ALT;
	case XKB_KEY_Super_L: return VKB_KEY_LEFT_SUPER;
	case XKB_KEY_Super_R: return VKB_KEY_RIGHT_SUPER;
	case XKB_KEY_Meta_L: return VKB_KEY_LEFT_SUPER;
	case XKB_KEY_Meta_R: return VKB_KEY_RIGHT_SUPER;
	case XKB_KEY_Caps_Lock: return VKB_KEY_CAPS_LOCK;
	case XKB_KEY_Num_Lock: return VKB_KEY_NUM_LOCK;
	case XKB_KEY_Scroll_Lock: return VKB_KEY_SCROLL_LOCK;
	case XKB_KEY_Print: return VKB_KEY_PRINT_SCREEN;
	case XKB_KEY_Pause: return VKB_KEY_PAUSE;
	case XKB_KEY_Menu: return VKB_KEY_MENU;
	case XKB_KEY_XF86AudioMute: return VKB_KEY_MUTE;
	case XKB_KEY_XF86AudioRaiseVolume: return VKB_KEY_VOLUME_UP;
	case XKB_KEY_XF86AudioLowerVolume: return VKB_KEY_VOLUME_DOWN;
	case XKB_KEY_dead_grave: return VKB_KEY_DEAD_GRAVE;
	case XKB_KEY_dead_acute: return VKB_KEY_DEAD_ACUTE;
	case XKB_KEY_dead_circumflex: return VKB_KEY_DEAD_CIRCUMFLEX;
	case XKB_KEY_dead_tilde: return VKB_KEY_DEAD_TILDE;
	case XKB_KEY_dead_diaeresis: return VKB_KEY_DEAD_DIAERESIS;
	case XKB_KEY_dead_cedilla: return VKB_KEY_DEAD_CEDILLA;
	case XKB_KEY_dead_caron: return VKB_KEY_DEAD_CARON;
	case XKB_KEY_dead_breve: return VKB_KEY_DEAD_BREVE;
	case XKB_KEY_dead_ogonek: return VKB_KEY_DEAD_OGONEK;
	case XKB_KEY_dead_doubleacute: return VKB_KEY_DEAD_DOUBLE_ACUTE;
	case XKB_KEY_dead_abovedot: return VKB_KEY_DEAD_ABOVE_DOT;
	case XKB_KEY_dead_abovering: return VKB_KEY_DEAD_ABOVE_RING;
	case XKB_KEY_dead_stroke: return VKB_KEY_DEAD_STROKE;
	case XKB_KEY_dead_belowdot: return VKB_KEY_DEAD_BELOW_DOT;
	default:
		return VKB_KEY_UNKNOWN;
	}
}

static size_t vkb_wayland_translate_text(
	vkb_context *context,
	xkb_keycode_t keycode,
	char *buffer,
	size_t buffer_capacity) {
	xkb_keysym_t keysym;
	int length;

	if (!vkb_wayland_text_is_allowed(context)) {
		if (context->xkb_compose_state != NULL &&
			xkb_compose_state_get_status(context->xkb_compose_state) ==
				XKB_COMPOSE_COMPOSING) {
			xkb_compose_state_reset(context->xkb_compose_state);
		}
		return 0;
	}
	keysym = xkb_state_key_get_one_sym(context->xkb_state, keycode);
	if (context->xkb_compose_state != NULL) {
		enum xkb_compose_feed_result feed_result =
			xkb_compose_state_feed(context->xkb_compose_state, keysym);
		enum xkb_compose_status status =
			xkb_compose_state_get_status(context->xkb_compose_state);
		if (feed_result == XKB_COMPOSE_FEED_ACCEPTED) {
			if (status == XKB_COMPOSE_COMPOSING) {
				return 0;
			}
			if (status == XKB_COMPOSE_COMPOSED) {
				length = xkb_compose_state_get_utf8(
					context->xkb_compose_state,
					buffer,
					buffer_capacity);
				xkb_compose_state_reset(context->xkb_compose_state);
				if (length <= 0) {
					return 0;
				}
				return (size_t)length;
			}
			if (status == XKB_COMPOSE_CANCELLED) {
				xkb_compose_state_reset(context->xkb_compose_state);
				return 0;
			}
		}
		if (status == XKB_COMPOSE_COMPOSING) {
			return 0;
		}
	}
	length = xkb_state_key_get_utf8(context->xkb_state, keycode, buffer, buffer_capacity);
	if (length <= 0) {
		return 0;
	}
	{
		uint32_t codepoint;
		size_t bytes_consumed;
		if (!vkb_utf8_decode_first(buffer, (size_t)length, &codepoint, &bytes_consumed)) {
			return 0;
		}
		if (!vkb_key_is_printable(codepoint)) {
			return 0;
		}
	}
	return (size_t)length;
}

static vkb_key vkb_wayland_base_logical_key(
	vkb_context *context,
	xkb_keycode_t keycode,
	xkb_layout_index_t group) {
	const xkb_keysym_t *symbols = NULL;
	int symbol_count;

	if (context->layout_count > 0 && group >= (xkb_layout_index_t)context->layout_count) {
		group = 0;
	}
	symbol_count = xkb_keymap_key_get_syms_by_level(
		context->xkb_keymap,
		keycode,
		group,
		0,
		&symbols);
	if (symbol_count <= 0 || symbols == NULL) {
		return VKB_KEY_UNKNOWN;
	}
	return vkb_wayland_keysym_to_key(symbols[0]);
}

static bool vkb_wayland_physical_key_is_modifier(vkb_physical_key physical_key) {
	switch (physical_key) {
	case VKB_PHYSICAL_KEY_LEFT_SHIFT:
	case VKB_PHYSICAL_KEY_RIGHT_SHIFT:
	case VKB_PHYSICAL_KEY_LEFT_CONTROL:
	case VKB_PHYSICAL_KEY_RIGHT_CONTROL:
	case VKB_PHYSICAL_KEY_LEFT_ALT:
	case VKB_PHYSICAL_KEY_RIGHT_ALT:
	case VKB_PHYSICAL_KEY_LEFT_GUI:
	case VKB_PHYSICAL_KEY_RIGHT_GUI:
	case VKB_PHYSICAL_KEY_CAPS_LOCK:
	case VKB_PHYSICAL_KEY_NUM_LOCK:
		return true;
	default:
		return false;
	}
}

/*
 * Numeric cases are the Linux input-event-codes values. They are protocol
 * constants, not tuning numbers; naming every one here would duplicate the
 * kernel header without adding information.
 */
static vkb_physical_key vkb_wayland_linux_input_code_to_physical_key(
	uint32_t linux_input_event_code) {
	switch (linux_input_event_code) {
	case 1: return VKB_PHYSICAL_KEY_ESCAPE;
	case 2: return VKB_PHYSICAL_KEY_DIGIT_1;
	case 3: return VKB_PHYSICAL_KEY_DIGIT_2;
	case 4: return VKB_PHYSICAL_KEY_DIGIT_3;
	case 5: return VKB_PHYSICAL_KEY_DIGIT_4;
	case 6: return VKB_PHYSICAL_KEY_DIGIT_5;
	case 7: return VKB_PHYSICAL_KEY_DIGIT_6;
	case 8: return VKB_PHYSICAL_KEY_DIGIT_7;
	case 9: return VKB_PHYSICAL_KEY_DIGIT_8;
	case 10: return VKB_PHYSICAL_KEY_DIGIT_9;
	case 11: return VKB_PHYSICAL_KEY_DIGIT_0;
	case 12: return VKB_PHYSICAL_KEY_MINUS;
	case 13: return VKB_PHYSICAL_KEY_EQUAL;
	case 14: return VKB_PHYSICAL_KEY_BACKSPACE;
	case 15: return VKB_PHYSICAL_KEY_TAB;
	case 16: return VKB_PHYSICAL_KEY_Q;
	case 17: return VKB_PHYSICAL_KEY_W;
	case 18: return VKB_PHYSICAL_KEY_E;
	case 19: return VKB_PHYSICAL_KEY_R;
	case 20: return VKB_PHYSICAL_KEY_T;
	case 21: return VKB_PHYSICAL_KEY_Y;
	case 22: return VKB_PHYSICAL_KEY_U;
	case 23: return VKB_PHYSICAL_KEY_I;
	case 24: return VKB_PHYSICAL_KEY_O;
	case 25: return VKB_PHYSICAL_KEY_P;
	case 26: return VKB_PHYSICAL_KEY_LEFT_BRACKET;
	case 27: return VKB_PHYSICAL_KEY_RIGHT_BRACKET;
	case 28: return VKB_PHYSICAL_KEY_RETURN;
	case 29: return VKB_PHYSICAL_KEY_LEFT_CONTROL;
	case 30: return VKB_PHYSICAL_KEY_A;
	case 31: return VKB_PHYSICAL_KEY_S;
	case 32: return VKB_PHYSICAL_KEY_D;
	case 33: return VKB_PHYSICAL_KEY_F;
	case 34: return VKB_PHYSICAL_KEY_G;
	case 35: return VKB_PHYSICAL_KEY_H;
	case 36: return VKB_PHYSICAL_KEY_J;
	case 37: return VKB_PHYSICAL_KEY_K;
	case 38: return VKB_PHYSICAL_KEY_L;
	case 39: return VKB_PHYSICAL_KEY_SEMICOLON;
	case 40: return VKB_PHYSICAL_KEY_APOSTROPHE;
	case 41: return VKB_PHYSICAL_KEY_GRAVE_ACCENT;
	case 42: return VKB_PHYSICAL_KEY_LEFT_SHIFT;
	case 43: return VKB_PHYSICAL_KEY_BACKSLASH;
	case 44: return VKB_PHYSICAL_KEY_Z;
	case 45: return VKB_PHYSICAL_KEY_X;
	case 46: return VKB_PHYSICAL_KEY_C;
	case 47: return VKB_PHYSICAL_KEY_V;
	case 48: return VKB_PHYSICAL_KEY_B;
	case 49: return VKB_PHYSICAL_KEY_N;
	case 50: return VKB_PHYSICAL_KEY_M;
	case 51: return VKB_PHYSICAL_KEY_COMMA;
	case 52: return VKB_PHYSICAL_KEY_PERIOD;
	case 53: return VKB_PHYSICAL_KEY_SLASH;
	case 54: return VKB_PHYSICAL_KEY_RIGHT_SHIFT;
	case 55: return VKB_PHYSICAL_KEY_KEYPAD_MULTIPLY;
	case 56: return VKB_PHYSICAL_KEY_LEFT_ALT;
	case 57: return VKB_PHYSICAL_KEY_SPACE;
	case 58: return VKB_PHYSICAL_KEY_CAPS_LOCK;
	case 59: return VKB_PHYSICAL_KEY_F1;
	case 60: return VKB_PHYSICAL_KEY_F2;
	case 61: return VKB_PHYSICAL_KEY_F3;
	case 62: return VKB_PHYSICAL_KEY_F4;
	case 63: return VKB_PHYSICAL_KEY_F5;
	case 64: return VKB_PHYSICAL_KEY_F6;
	case 65: return VKB_PHYSICAL_KEY_F7;
	case 66: return VKB_PHYSICAL_KEY_F8;
	case 67: return VKB_PHYSICAL_KEY_F9;
	case 68: return VKB_PHYSICAL_KEY_F10;
	case 69: return VKB_PHYSICAL_KEY_NUM_LOCK;
	case 70: return VKB_PHYSICAL_KEY_SCROLL_LOCK;
	case 71: return VKB_PHYSICAL_KEY_KEYPAD_7;
	case 72: return VKB_PHYSICAL_KEY_KEYPAD_8;
	case 73: return VKB_PHYSICAL_KEY_KEYPAD_9;
	case 74: return VKB_PHYSICAL_KEY_KEYPAD_SUBTRACT;
	case 75: return VKB_PHYSICAL_KEY_KEYPAD_4;
	case 76: return VKB_PHYSICAL_KEY_KEYPAD_5;
	case 77: return VKB_PHYSICAL_KEY_KEYPAD_6;
	case 78: return VKB_PHYSICAL_KEY_KEYPAD_ADD;
	case 79: return VKB_PHYSICAL_KEY_KEYPAD_1;
	case 80: return VKB_PHYSICAL_KEY_KEYPAD_2;
	case 81: return VKB_PHYSICAL_KEY_KEYPAD_3;
	case 82: return VKB_PHYSICAL_KEY_KEYPAD_0;
	case 83: return VKB_PHYSICAL_KEY_KEYPAD_PERIOD;
	case 86: return VKB_PHYSICAL_KEY_NON_US_BACKSLASH;
	case 87: return VKB_PHYSICAL_KEY_F11;
	case 88: return VKB_PHYSICAL_KEY_F12;
	case 89: return VKB_PHYSICAL_KEY_INTERNATIONAL_1;
	case 90: return VKB_PHYSICAL_KEY_LANGUAGE_1;
	case 91: return VKB_PHYSICAL_KEY_LANGUAGE_2;
	case 92: return VKB_PHYSICAL_KEY_LANGUAGE_3;
	case 93: return VKB_PHYSICAL_KEY_LANGUAGE_4;
	case 94: return VKB_PHYSICAL_KEY_LANGUAGE_5;
	case 95: return VKB_PHYSICAL_KEY_SEPARATOR;
	case 96: return VKB_PHYSICAL_KEY_KEYPAD_ENTER;
	case 97: return VKB_PHYSICAL_KEY_RIGHT_CONTROL;
	case 98: return VKB_PHYSICAL_KEY_KEYPAD_DIVIDE;
	case 99: return VKB_PHYSICAL_KEY_PRINT_SCREEN;
	case 100: return VKB_PHYSICAL_KEY_RIGHT_ALT;
	case 102: return VKB_PHYSICAL_KEY_HOME;
	case 103: return VKB_PHYSICAL_KEY_UP_ARROW;
	case 104: return VKB_PHYSICAL_KEY_PAGE_UP;
	case 105: return VKB_PHYSICAL_KEY_LEFT_ARROW;
	case 106: return VKB_PHYSICAL_KEY_RIGHT_ARROW;
	case 107: return VKB_PHYSICAL_KEY_END;
	case 108: return VKB_PHYSICAL_KEY_DOWN_ARROW;
	case 109: return VKB_PHYSICAL_KEY_PAGE_DOWN;
	case 110: return VKB_PHYSICAL_KEY_INSERT;
	case 111: return VKB_PHYSICAL_KEY_DELETE_FORWARD;
	case 113: return VKB_PHYSICAL_KEY_MUTE;
	case 114: return VKB_PHYSICAL_KEY_VOLUME_DOWN;
	case 115: return VKB_PHYSICAL_KEY_VOLUME_UP;
	case 116: return VKB_PHYSICAL_KEY_POWER;
	case 117: return VKB_PHYSICAL_KEY_KEYPAD_EQUAL;
	case 119: return VKB_PHYSICAL_KEY_PAUSE;
	case 121: return VKB_PHYSICAL_KEY_KEYPAD_COMMA;
	case 122: return VKB_PHYSICAL_KEY_LANGUAGE_1;
	case 123: return VKB_PHYSICAL_KEY_LANGUAGE_2;
	case 124: return VKB_PHYSICAL_KEY_INTERNATIONAL_3;
	case 125: return VKB_PHYSICAL_KEY_LEFT_GUI;
	case 126: return VKB_PHYSICAL_KEY_RIGHT_GUI;
	case 127: return VKB_PHYSICAL_KEY_APPLICATION;
	case 128: return VKB_PHYSICAL_KEY_STOP;
	case 129: return VKB_PHYSICAL_KEY_AGAIN;
	case 130: return VKB_PHYSICAL_KEY_MENU;
	case 131: return VKB_PHYSICAL_KEY_UNDO;
	case 132: return VKB_PHYSICAL_KEY_CUT;
	case 133: return VKB_PHYSICAL_KEY_COPY;
	case 134: return VKB_PHYSICAL_KEY_PASTE;
	case 135: return VKB_PHYSICAL_KEY_FIND;
	case 136: return VKB_PHYSICAL_KEY_MUTE;
	case 137: return VKB_PHYSICAL_KEY_VOLUME_UP;
	case 138: return VKB_PHYSICAL_KEY_VOLUME_DOWN;
	case 183: return VKB_PHYSICAL_KEY_F13;
	case 184: return VKB_PHYSICAL_KEY_F14;
	case 185: return VKB_PHYSICAL_KEY_F15;
	case 186: return VKB_PHYSICAL_KEY_F16;
	case 187: return VKB_PHYSICAL_KEY_F17;
	case 188: return VKB_PHYSICAL_KEY_F18;
	case 189: return VKB_PHYSICAL_KEY_F19;
	case 190: return VKB_PHYSICAL_KEY_F20;
	case 191: return VKB_PHYSICAL_KEY_F21;
	case 192: return VKB_PHYSICAL_KEY_F22;
	case 193: return VKB_PHYSICAL_KEY_F23;
	case 194: return VKB_PHYSICAL_KEY_F24;
	default:
		return VKB_PHYSICAL_KEY_UNKNOWN;
	}
}

static void vkb_wayland_emit_key_translation(
	vkb_context *context,
	uint32_t linux_input_event_code,
	bool is_repeat,
	uint64_t timestamp_milliseconds) {
	xkb_keycode_t keycode = (xkb_keycode_t)(linux_input_event_code + 8u);
	xkb_layout_index_t group;
	vkb_physical_key physical_key;
	vkb_key logical_key;
	char text_buffer[VKB_TEXT_CAPACITY];
	size_t text_length;

	physical_key = vkb_wayland_linux_input_code_to_physical_key(linux_input_event_code);
	group = xkb_state_serialize_layout(context->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
	logical_key = vkb_wayland_base_logical_key(context, keycode, group);
	if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
		vkb_store_logical_key(context, physical_key, logical_key);
	}
	vkb_emit_key_event(
		context,
		VKB_EVENT_KEY_DOWN,
		physical_key,
		logical_key,
		is_repeat,
		timestamp_milliseconds);
	text_length = vkb_wayland_translate_text(context, keycode, text_buffer, sizeof(text_buffer));
	if (text_length != 0) {
		vkb_emit_text(context, text_buffer, text_length, timestamp_milliseconds);
	}
}

bool vkb_wayland_set_keymap(
	vkb_context *context,
	int file_descriptor,
	uint32_t size,
	uint32_t format) {
	char *keymap_text = NULL;
	struct xkb_keymap *new_keymap;
	struct xkb_state *new_state;
	struct xkb_compose_state *new_compose_state = NULL;

	VKB_ASSERT(context != NULL);
	if (format != VKB_WAYLAND_KEYMAP_FORMAT_XKB_V1) {
		close(file_descriptor);
		vkb_set_error(context, "unsupported Wayland keymap format");
		return false;
	}
	if (!vkb_read_file_descriptor(file_descriptor, size, &keymap_text)) {
		vkb_set_error(context, "could not read Wayland keymap");
		return false;
	}
	new_keymap = xkb_keymap_new_from_string(
		context->xkb_context,
		keymap_text,
		XKB_KEYMAP_FORMAT_TEXT_V1,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	free(keymap_text);
	if (new_keymap == NULL) {
		vkb_set_error(context, "could not compile Wayland keymap");
		return false;
	}
	new_state = xkb_state_new(new_keymap);
	if (new_state == NULL) {
		xkb_keymap_unref(new_keymap);
		vkb_set_error(context, "could not create xkb state");
		return false;
	}
	if (context->xkb_compose_table != NULL) {
		new_compose_state = xkb_compose_state_new(
			context->xkb_compose_table,
			XKB_COMPOSE_STATE_NO_FLAGS);
	}
	if (context->xkb_state != NULL) {
		xkb_state_unref(context->xkb_state);
	}
	if (context->xkb_keymap != NULL) {
		xkb_keymap_unref(context->xkb_keymap);
	}
	if (context->xkb_compose_state != NULL) {
		xkb_compose_state_unref(context->xkb_compose_state);
	}
	context->xkb_keymap = new_keymap;
	context->xkb_state = new_state;
	context->xkb_compose_state = new_compose_state;
	context->xkb_shift_modifier = vkb_wayland_modifier_index(new_keymap, XKB_MOD_NAME_SHIFT);
	context->xkb_control_modifier = vkb_wayland_modifier_index(new_keymap, XKB_MOD_NAME_CTRL);
	context->xkb_alt_modifier = vkb_wayland_modifier_index(new_keymap, XKB_MOD_NAME_ALT);
	context->xkb_super_modifier = vkb_wayland_modifier_index(new_keymap, XKB_MOD_NAME_LOGO);
	context->xkb_caps_lock_modifier = vkb_wayland_modifier_index(new_keymap, XKB_MOD_NAME_CAPS);
	context->xkb_num_lock_modifier = vkb_wayland_modifier_index(new_keymap, XKB_MOD_NAME_NUM);
	context->xkb_altgr_modifier = vkb_wayland_modifier_index(new_keymap, "Mod5");
	if (context->xkb_altgr_modifier == XKB_MOD_INVALID) {
		context->xkb_altgr_modifier = vkb_wayland_modifier_index(new_keymap, "LevelThree");
	}
	context->active_layout_index = 0;
	vkb_wayland_update_layout_names(context);
	vkb_wayland_update_modifiers(context);
	vkb_emit_layout_event(context, vkb_current_time_milliseconds());
	return true;
}

bool vkb_wayland_set_modifiers(
	vkb_context *context,
	uint32_t depressed,
	uint32_t latched,
	uint32_t locked,
	uint32_t group) {
	VKB_ASSERT(context != NULL);
	if (context->xkb_state == NULL) {
		vkb_set_error(context, "modifiers arrived before the Wayland keymap");
		return false;
	}
	if (context->layout_count > 0 && group >= (uint32_t)context->layout_count) {
		group = 0;
	}
	xkb_state_update_mask(
		context->xkb_state,
		depressed,
		latched,
		locked,
		0,
		0,
		group);
	vkb_wayland_update_modifiers(context);
	vkb_wayland_update_layout(context, vkb_current_time_milliseconds());
	return true;
}

bool vkb_wayland_process_key(
	vkb_context *context,
	uint32_t linux_input_event_code,
	uint32_t state,
	uint32_t time_milliseconds) {
	xkb_keycode_t keycode;
	vkb_physical_key physical_key;
	uint64_t timestamp_milliseconds;
	bool is_down;
	bool was_down;

	VKB_ASSERT(context != NULL);
	if (context->xkb_state == NULL || context->xkb_keymap == NULL) {
		vkb_set_error(context, "key arrived before the Wayland keymap");
		return false;
	}
	if (state != VKB_KEY_STATE_RELEASED && state != VKB_KEY_STATE_PRESSED) {
		vkb_set_error(context, "invalid Wayland key state");
		return false;
	}
	keycode = (xkb_keycode_t)(linux_input_event_code + 8u);
	physical_key = vkb_wayland_linux_input_code_to_physical_key(linux_input_event_code);
	is_down = state == VKB_KEY_STATE_PRESSED;
	timestamp_milliseconds = time_milliseconds != 0
		? (uint64_t)time_milliseconds
		: vkb_current_time_milliseconds();

	if (is_down) {
		was_down = physical_key != VKB_PHYSICAL_KEY_UNKNOWN &&
			vkb_physical_key_is_down_internal(context, physical_key);
		if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
			vkb_physical_key_set_down(context, physical_key, true);
		}
		vkb_wayland_update_modifiers(context);
		vkb_wayland_emit_key_translation(
			context,
			linux_input_event_code,
			was_down,
			timestamp_milliseconds);
		if (context->repeat_enabled &&
			context->repeat_interval_milliseconds != 0 &&
			physical_key != VKB_PHYSICAL_KEY_UNKNOWN &&
			!vkb_wayland_physical_key_is_modifier(physical_key)) {
			context->repeating_keycode = linux_input_event_code;
			context->next_repeat_time_milliseconds =
				timestamp_milliseconds + (was_down
					? context->repeat_interval_milliseconds
					: context->repeat_delay_milliseconds);
		} else {
			context->repeating_keycode = 0;
			context->next_repeat_time_milliseconds = 0;
		}
		return true;
	}

	if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN &&
		!vkb_physical_key_is_down_internal(context, physical_key)) {
		return true;
	}
	if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
		vkb_physical_key_set_down(context, physical_key, false);
	}
	vkb_wayland_update_modifiers(context);
	{
		xkb_layout_index_t group =
			xkb_state_serialize_layout(context->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
		vkb_key logical_key = vkb_wayland_base_logical_key(context, keycode, group);
		if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
			logical_key = vkb_take_logical_key(context, physical_key, logical_key);
		}
		vkb_emit_key_event(
			context,
			VKB_EVENT_KEY_UP,
			physical_key,
			logical_key,
			false,
			timestamp_milliseconds);
	}
	if (context->repeating_keycode == linux_input_event_code) {
		context->repeating_keycode = 0;
		context->next_repeat_time_milliseconds = 0;
	}
	return true;
}

void vkb_wayland_set_repeat_info(
	vkb_context *context,
	int32_t rate,
	int32_t delay_milliseconds) {
	VKB_ASSERT(context != NULL);
	context->repeat_delay_milliseconds = delay_milliseconds > 0
		? (uint32_t)delay_milliseconds
		: 0;
	if (rate <= 0 || !context->configuration.key_repeat_enabled) {
		context->repeat_enabled = false;
		context->repeat_interval_milliseconds = 0;
		context->repeating_keycode = 0;
		return;
	}
	context->repeat_interval_milliseconds =
		(uint32_t)(VKB_MILLISECONDS_PER_SECOND / rate);
	if (context->repeat_interval_milliseconds == 0) {
		context->repeat_interval_milliseconds = 1;
	}
	context->repeat_enabled = true;
}

void vkb_wayland_enter(
	vkb_context *context,
	const uint32_t *linux_input_event_codes,
	size_t count) {
	size_t index;
	VKB_ASSERT(context != NULL);
	memset(context->physical_key_down, 0, sizeof(context->physical_key_down));
	vkb_reset_logical_keys(context);
	context->repeating_keycode = 0;
	context->next_repeat_time_milliseconds = 0;
	if (context->xkb_compose_state != NULL) {
		xkb_compose_state_reset(context->xkb_compose_state);
	}
	if (linux_input_event_codes == NULL) {
		if (context->xkb_state != NULL) {
			vkb_wayland_update_modifiers(context);
		}
		return;
	}
	for (index = 0; index < count; index += 1) {
		uint32_t linux_input_event_code = linux_input_event_codes[index];
		vkb_physical_key physical_key =
			vkb_wayland_linux_input_code_to_physical_key(linux_input_event_code);
		if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
			xkb_keycode_t keycode = (xkb_keycode_t)(linux_input_event_code + 8u);
			xkb_layout_index_t group;
			vkb_physical_key_set_down(context, physical_key, true);
			group = context->xkb_state != NULL
				? xkb_state_serialize_layout(context->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE)
				: 0;
			if (context->xkb_keymap != NULL && context->xkb_state != NULL) {
				vkb_store_logical_key(
					context,
					physical_key,
					vkb_wayland_base_logical_key(context, keycode, group));
			}
		}
	}
	if (context->xkb_state != NULL) {
		vkb_wayland_update_modifiers(context);
	}
}

void vkb_wayland_leave(vkb_context *context) {
	VKB_ASSERT(context != NULL);
	vkb_focus_lost(context);
}

#endif /* __linux__ */

#if defined(_WIN32)

#define VKB_WINDOWS_KEY_DOWN_MASK 0x8000
#define VKB_WINDOWS_KEY_TOGGLE_MASK 0x0001
#define VKB_WINDOWS_DEAD_KEY_MASK 0x80000000u
#define VKB_WINDOWS_CHARACTER_MASK 0xFFFFu
#define VKB_WINDOWS_SCAN_CODE_MASK 0xFFu
#define VKB_WINDOWS_EXTENDED_SCAN_CODE_MASK 0xE000u
#define VKB_WINDOWS_EXTENDED_FLAG_MASK ((uintptr_t)1u << 24)
#define VKB_WINDOWS_REPEAT_FLAG_MASK ((uintptr_t)1u << 30)

static vkb_key vkb_windows_special_logical_key(
	UINT virtual_key,
	uint32_t scan_code,
	bool extended) {
	switch (virtual_key) {
	case VK_BACK: return VKB_KEY_BACKSPACE;
	case VK_TAB: return VKB_KEY_TAB;
	case VK_RETURN: return extended ? VKB_KEY_KEYPAD_ENTER : VKB_KEY_RETURN;
	case VK_ESCAPE: return VKB_KEY_ESCAPE;
	case VK_DELETE: return VKB_KEY_DELETE_FORWARD;
	case VK_LEFT: return VKB_KEY_LEFT_ARROW;
	case VK_RIGHT: return VKB_KEY_RIGHT_ARROW;
	case VK_UP: return VKB_KEY_UP_ARROW;
	case VK_DOWN: return VKB_KEY_DOWN_ARROW;
	case VK_HOME: return VKB_KEY_HOME;
	case VK_END: return VKB_KEY_END;
	case VK_PRIOR: return VKB_KEY_PAGE_UP;
	case VK_NEXT: return VKB_KEY_PAGE_DOWN;
	case VK_INSERT: return VKB_KEY_INSERT;
	case VK_F1: return VKB_KEY_F1;
	case VK_F2: return VKB_KEY_F2;
	case VK_F3: return VKB_KEY_F3;
	case VK_F4: return VKB_KEY_F4;
	case VK_F5: return VKB_KEY_F5;
	case VK_F6: return VKB_KEY_F6;
	case VK_F7: return VKB_KEY_F7;
	case VK_F8: return VKB_KEY_F8;
	case VK_F9: return VKB_KEY_F9;
	case VK_F10: return VKB_KEY_F10;
	case VK_F11: return VKB_KEY_F11;
	case VK_F12: return VKB_KEY_F12;
	case VK_F13: return VKB_KEY_F13;
	case VK_F14: return VKB_KEY_F14;
	case VK_F15: return VKB_KEY_F15;
	case VK_F16: return VKB_KEY_F16;
	case VK_F17: return VKB_KEY_F17;
	case VK_F18: return VKB_KEY_F18;
	case VK_F19: return VKB_KEY_F19;
	case VK_F20: return VKB_KEY_F20;
	case VK_F21: return VKB_KEY_F21;
	case VK_F22: return VKB_KEY_F22;
	case VK_F23: return VKB_KEY_F23;
	case VK_F24: return VKB_KEY_F24;
	case VK_LSHIFT:
	case VK_SHIFT:
		return scan_code == 0x36 ? VKB_KEY_RIGHT_SHIFT : VKB_KEY_LEFT_SHIFT;
	case VK_RSHIFT:
		return VKB_KEY_RIGHT_SHIFT;
	case VK_LCONTROL:
		return VKB_KEY_LEFT_CONTROL;
	case VK_RCONTROL:
		return VKB_KEY_RIGHT_CONTROL;
	case VK_CONTROL:
		return extended ? VKB_KEY_RIGHT_CONTROL : VKB_KEY_LEFT_CONTROL;
	case VK_LMENU:
		return VKB_KEY_LEFT_ALT;
	case VK_RMENU:
		return VKB_KEY_RIGHT_ALT;
	case VK_MENU:
		return extended ? VKB_KEY_RIGHT_ALT : VKB_KEY_LEFT_ALT;
	case VK_LWIN:
		return VKB_KEY_LEFT_SUPER;
	case VK_RWIN:
		return VKB_KEY_RIGHT_SUPER;
	case VK_CAPITAL:
		return VKB_KEY_CAPS_LOCK;
	case VK_NUMLOCK:
		return VKB_KEY_NUM_LOCK;
	case VK_SCROLL:
		return VKB_KEY_SCROLL_LOCK;
	case VK_SNAPSHOT:
		return VKB_KEY_PRINT_SCREEN;
	case VK_PAUSE:
		return VKB_KEY_PAUSE;
	case VK_APPS:
		return VKB_KEY_APPLICATION;
	case VK_MULTIPLY:
		return VKB_KEY_KEYPAD_MULTIPLY;
	case VK_DIVIDE:
		return VKB_KEY_KEYPAD_DIVIDE;
	case VK_SUBTRACT:
		return VKB_KEY_KEYPAD_SUBTRACT;
	case VK_ADD:
		return VKB_KEY_KEYPAD_ADD;
	case VK_DECIMAL:
		return VKB_KEY_KEYPAD_PERIOD;
	case VK_SEPARATOR:
		return VKB_KEY_KEYPAD_COMMA;
	case VK_NUMPAD0:
		return VKB_KEY_KEYPAD_0;
	case VK_NUMPAD1:
		return VKB_KEY_KEYPAD_1;
	case VK_NUMPAD2:
		return VKB_KEY_KEYPAD_2;
	case VK_NUMPAD3:
		return VKB_KEY_KEYPAD_3;
	case VK_NUMPAD4:
		return VKB_KEY_KEYPAD_4;
	case VK_NUMPAD5:
		return VKB_KEY_KEYPAD_5;
	case VK_NUMPAD6:
		return VKB_KEY_KEYPAD_6;
	case VK_NUMPAD7:
		return VKB_KEY_KEYPAD_7;
	case VK_NUMPAD8:
		return VKB_KEY_KEYPAD_8;
	case VK_NUMPAD9:
		return VKB_KEY_KEYPAD_9;
	case VK_VOLUME_MUTE:
		return VKB_KEY_MUTE;
	case VK_VOLUME_UP:
		return VKB_KEY_VOLUME_UP;
	case VK_VOLUME_DOWN:
		return VKB_KEY_VOLUME_DOWN;
	default:
		return VKB_KEY_UNKNOWN;
	}
}

static vkb_key vkb_windows_logical_key(
	UINT virtual_key,
	HKL keyboard_layout,
	uint32_t scan_code,
	bool extended) {
	vkb_key special_key = vkb_windows_special_logical_key(
		virtual_key,
		scan_code,
		extended);
	UINT translated;
	uint32_t codepoint;
	bool is_dead;

	if (special_key != VKB_KEY_UNKNOWN) {
		return special_key;
	}
	translated = MapVirtualKeyExW(virtual_key, MAPVK_VK_TO_CHAR, keyboard_layout);
	if (translated == 0) {
		return VKB_KEY_UNKNOWN;
	}
	is_dead = (translated & VKB_WINDOWS_DEAD_KEY_MASK) != 0;
	codepoint = translated & VKB_WINDOWS_CHARACTER_MASK;
	if (is_dead) {
		switch (codepoint) {
		case '`': return VKB_KEY_DEAD_GRAVE;
		case '\'': return VKB_KEY_DEAD_ACUTE;
		case '^': return VKB_KEY_DEAD_CIRCUMFLEX;
		case '~': return VKB_KEY_DEAD_TILDE;
		case '"': return VKB_KEY_DEAD_DIAERESIS;
		case ',': return VKB_KEY_DEAD_CEDILLA;
		default: break;
		}
	}
	{
		wchar_t characters[2];
		characters[0] = (wchar_t)codepoint;
		characters[1] = L'\0';
		CharLowerW(characters);
		codepoint = (uint32_t)characters[0];
	}
	return codepoint;
}

/*
 * Numeric cases are the Windows Set 1 scan codes and their 0xE0 extended
 * forms. They are hardware protocol constants, not tuning numbers.
 */
static vkb_physical_key vkb_windows_scan_code_to_physical_key(
	uint32_t scan_code,
	bool extended) {
	if (extended) {
		switch (scan_code) {
		case 0x1C: return VKB_PHYSICAL_KEY_KEYPAD_ENTER;
		case 0x1D: return VKB_PHYSICAL_KEY_RIGHT_CONTROL;
		case 0x35: return VKB_PHYSICAL_KEY_KEYPAD_DIVIDE;
		case 0x37: return VKB_PHYSICAL_KEY_PRINT_SCREEN;
		case 0x38: return VKB_PHYSICAL_KEY_RIGHT_ALT;
		case 0x46: return VKB_PHYSICAL_KEY_PAUSE;
		case 0x47: return VKB_PHYSICAL_KEY_HOME;
		case 0x48: return VKB_PHYSICAL_KEY_UP_ARROW;
		case 0x49: return VKB_PHYSICAL_KEY_PAGE_UP;
		case 0x4B: return VKB_PHYSICAL_KEY_LEFT_ARROW;
		case 0x4D: return VKB_PHYSICAL_KEY_RIGHT_ARROW;
		case 0x4F: return VKB_PHYSICAL_KEY_END;
		case 0x50: return VKB_PHYSICAL_KEY_DOWN_ARROW;
		case 0x51: return VKB_PHYSICAL_KEY_PAGE_DOWN;
		case 0x52: return VKB_PHYSICAL_KEY_INSERT;
		case 0x53: return VKB_PHYSICAL_KEY_DELETE_FORWARD;
		case 0x5B: return VKB_PHYSICAL_KEY_LEFT_GUI;
		case 0x5C: return VKB_PHYSICAL_KEY_RIGHT_GUI;
		case 0x5D: return VKB_PHYSICAL_KEY_APPLICATION;
		case 0x5E: return VKB_PHYSICAL_KEY_POWER;
		default: return VKB_PHYSICAL_KEY_UNKNOWN;
		}
	}
	switch (scan_code) {
	case 0x01: return VKB_PHYSICAL_KEY_ESCAPE;
	case 0x02: return VKB_PHYSICAL_KEY_DIGIT_1;
	case 0x03: return VKB_PHYSICAL_KEY_DIGIT_2;
	case 0x04: return VKB_PHYSICAL_KEY_DIGIT_3;
	case 0x05: return VKB_PHYSICAL_KEY_DIGIT_4;
	case 0x06: return VKB_PHYSICAL_KEY_DIGIT_5;
	case 0x07: return VKB_PHYSICAL_KEY_DIGIT_6;
	case 0x08: return VKB_PHYSICAL_KEY_DIGIT_7;
	case 0x09: return VKB_PHYSICAL_KEY_DIGIT_8;
	case 0x0A: return VKB_PHYSICAL_KEY_DIGIT_9;
	case 0x0B: return VKB_PHYSICAL_KEY_DIGIT_0;
	case 0x0C: return VKB_PHYSICAL_KEY_MINUS;
	case 0x0D: return VKB_PHYSICAL_KEY_EQUAL;
	case 0x0E: return VKB_PHYSICAL_KEY_BACKSPACE;
	case 0x0F: return VKB_PHYSICAL_KEY_TAB;
	case 0x10: return VKB_PHYSICAL_KEY_Q;
	case 0x11: return VKB_PHYSICAL_KEY_W;
	case 0x12: return VKB_PHYSICAL_KEY_E;
	case 0x13: return VKB_PHYSICAL_KEY_R;
	case 0x14: return VKB_PHYSICAL_KEY_T;
	case 0x15: return VKB_PHYSICAL_KEY_Y;
	case 0x16: return VKB_PHYSICAL_KEY_U;
	case 0x17: return VKB_PHYSICAL_KEY_I;
	case 0x18: return VKB_PHYSICAL_KEY_O;
	case 0x19: return VKB_PHYSICAL_KEY_P;
	case 0x1A: return VKB_PHYSICAL_KEY_LEFT_BRACKET;
	case 0x1B: return VKB_PHYSICAL_KEY_RIGHT_BRACKET;
	case 0x1C: return VKB_PHYSICAL_KEY_RETURN;
	case 0x1D: return VKB_PHYSICAL_KEY_LEFT_CONTROL;
	case 0x1E: return VKB_PHYSICAL_KEY_A;
	case 0x1F: return VKB_PHYSICAL_KEY_S;
	case 0x20: return VKB_PHYSICAL_KEY_D;
	case 0x21: return VKB_PHYSICAL_KEY_F;
	case 0x22: return VKB_PHYSICAL_KEY_G;
	case 0x23: return VKB_PHYSICAL_KEY_H;
	case 0x24: return VKB_PHYSICAL_KEY_J;
	case 0x25: return VKB_PHYSICAL_KEY_K;
	case 0x26: return VKB_PHYSICAL_KEY_L;
	case 0x27: return VKB_PHYSICAL_KEY_SEMICOLON;
	case 0x28: return VKB_PHYSICAL_KEY_APOSTROPHE;
	case 0x29: return VKB_PHYSICAL_KEY_GRAVE_ACCENT;
	case 0x2A: return VKB_PHYSICAL_KEY_LEFT_SHIFT;
	case 0x2B: return VKB_PHYSICAL_KEY_BACKSLASH;
	case 0x2C: return VKB_PHYSICAL_KEY_Z;
	case 0x2D: return VKB_PHYSICAL_KEY_X;
	case 0x2E: return VKB_PHYSICAL_KEY_C;
	case 0x2F: return VKB_PHYSICAL_KEY_V;
	case 0x30: return VKB_PHYSICAL_KEY_B;
	case 0x31: return VKB_PHYSICAL_KEY_N;
	case 0x32: return VKB_PHYSICAL_KEY_M;
	case 0x33: return VKB_PHYSICAL_KEY_COMMA;
	case 0x34: return VKB_PHYSICAL_KEY_PERIOD;
	case 0x35: return VKB_PHYSICAL_KEY_SLASH;
	case 0x36: return VKB_PHYSICAL_KEY_RIGHT_SHIFT;
	case 0x37: return VKB_PHYSICAL_KEY_KEYPAD_MULTIPLY;
	case 0x38: return VKB_PHYSICAL_KEY_LEFT_ALT;
	case 0x39: return VKB_PHYSICAL_KEY_SPACE;
	case 0x3A: return VKB_PHYSICAL_KEY_CAPS_LOCK;
	case 0x3B: return VKB_PHYSICAL_KEY_F1;
	case 0x3C: return VKB_PHYSICAL_KEY_F2;
	case 0x3D: return VKB_PHYSICAL_KEY_F3;
	case 0x3E: return VKB_PHYSICAL_KEY_F4;
	case 0x3F: return VKB_PHYSICAL_KEY_F5;
	case 0x40: return VKB_PHYSICAL_KEY_F6;
	case 0x41: return VKB_PHYSICAL_KEY_F7;
	case 0x42: return VKB_PHYSICAL_KEY_F8;
	case 0x43: return VKB_PHYSICAL_KEY_F9;
	case 0x44: return VKB_PHYSICAL_KEY_F10;
	case 0x45: return VKB_PHYSICAL_KEY_NUM_LOCK;
	case 0x46: return VKB_PHYSICAL_KEY_SCROLL_LOCK;
	case 0x47: return VKB_PHYSICAL_KEY_KEYPAD_7;
	case 0x48: return VKB_PHYSICAL_KEY_KEYPAD_8;
	case 0x49: return VKB_PHYSICAL_KEY_KEYPAD_9;
	case 0x4A: return VKB_PHYSICAL_KEY_KEYPAD_SUBTRACT;
	case 0x4B: return VKB_PHYSICAL_KEY_KEYPAD_4;
	case 0x4C: return VKB_PHYSICAL_KEY_KEYPAD_5;
	case 0x4D: return VKB_PHYSICAL_KEY_KEYPAD_6;
	case 0x4E: return VKB_PHYSICAL_KEY_KEYPAD_ADD;
	case 0x4F: return VKB_PHYSICAL_KEY_KEYPAD_1;
	case 0x50: return VKB_PHYSICAL_KEY_KEYPAD_2;
	case 0x51: return VKB_PHYSICAL_KEY_KEYPAD_3;
	case 0x52: return VKB_PHYSICAL_KEY_KEYPAD_0;
	case 0x53: return VKB_PHYSICAL_KEY_KEYPAD_PERIOD;
	case 0x56: return VKB_PHYSICAL_KEY_NON_US_BACKSLASH;
	case 0x57: return VKB_PHYSICAL_KEY_F11;
	case 0x58: return VKB_PHYSICAL_KEY_F12;
	case 0x64: return VKB_PHYSICAL_KEY_F13;
	case 0x65: return VKB_PHYSICAL_KEY_F14;
	case 0x66: return VKB_PHYSICAL_KEY_F15;
	case 0x67: return VKB_PHYSICAL_KEY_F16;
	case 0x68: return VKB_PHYSICAL_KEY_F17;
	case 0x69: return VKB_PHYSICAL_KEY_F18;
	case 0x6A: return VKB_PHYSICAL_KEY_F19;
	case 0x6B: return VKB_PHYSICAL_KEY_F20;
	case 0x6C: return VKB_PHYSICAL_KEY_F21;
	case 0x6D: return VKB_PHYSICAL_KEY_F22;
	case 0x6E: return VKB_PHYSICAL_KEY_F23;
	case 0x6F: return VKB_PHYSICAL_KEY_F24;
	default: return VKB_PHYSICAL_KEY_UNKNOWN;
	}
}

static bool vkb_windows_key_is_down(int virtual_key) {
	return (GetKeyState(virtual_key) & VKB_WINDOWS_KEY_DOWN_MASK) != 0;
}

static void vkb_windows_refresh_modifiers(
	vkb_context *context,
	UINT virtual_key,
	uint32_t scan_code,
	bool extended,
	bool is_down) {
	bool left_shift = vkb_windows_key_is_down(VK_LSHIFT);
	bool right_shift = vkb_windows_key_is_down(VK_RSHIFT);
	bool left_control = vkb_windows_key_is_down(VK_LCONTROL);
	bool right_control = vkb_windows_key_is_down(VK_RCONTROL);
	bool left_alt = vkb_windows_key_is_down(VK_LMENU);
	bool right_alt = vkb_windows_key_is_down(VK_RMENU);
	bool left_super = vkb_windows_key_is_down(VK_LWIN);
	bool right_super = vkb_windows_key_is_down(VK_RWIN);
	vkb_modifiers modifiers = 0;

	if (virtual_key == VK_SHIFT) {
		if (scan_code == 0x36) {
			right_shift = is_down;
		} else {
			left_shift = is_down;
		}
	} else if (virtual_key == VK_RSHIFT) {
		right_shift = is_down;
	} else if (virtual_key == VK_LSHIFT) {
		left_shift = is_down;
	}
	if (virtual_key == VK_CONTROL) {
		if (extended) {
			right_control = is_down;
		} else {
			left_control = is_down;
		}
	} else if (virtual_key == VK_RCONTROL) {
		right_control = is_down;
	} else if (virtual_key == VK_LCONTROL) {
		left_control = is_down;
	}
	if (virtual_key == VK_MENU) {
		if (extended) {
			right_alt = is_down;
		} else {
			left_alt = is_down;
		}
	} else if (virtual_key == VK_RMENU) {
		right_alt = is_down;
	} else if (virtual_key == VK_LMENU) {
		left_alt = is_down;
	}

	if (left_shift || right_shift) {
		modifiers |= VKB_MODIFIER_SHIFT;
	}
	if (left_control || right_control) {
		modifiers |= VKB_MODIFIER_CONTROL;
	}
	if (left_alt || right_alt) {
		modifiers |= VKB_MODIFIER_ALT;
	}
	if (left_super || right_super) {
		modifiers |= VKB_MODIFIER_SUPER;
	}
	if (left_shift) modifiers |= VKB_MODIFIER_LEFT_SHIFT;
	if (right_shift) modifiers |= VKB_MODIFIER_RIGHT_SHIFT;
	if (left_control) modifiers |= VKB_MODIFIER_LEFT_CONTROL;
	if (right_control) modifiers |= VKB_MODIFIER_RIGHT_CONTROL;
	if (left_alt) modifiers |= VKB_MODIFIER_LEFT_ALT;
	if (right_alt) modifiers |= VKB_MODIFIER_RIGHT_ALT;
	if (left_super) modifiers |= VKB_MODIFIER_LEFT_SUPER;
	if (right_super) modifiers |= VKB_MODIFIER_RIGHT_SUPER;
	if ((GetKeyState(VK_CAPITAL) & VKB_WINDOWS_KEY_TOGGLE_MASK) != 0) {
		modifiers |= VKB_MODIFIER_CAPS_LOCK;
	}
	if ((GetKeyState(VK_NUMLOCK) & VKB_WINDOWS_KEY_TOGGLE_MASK) != 0) {
		modifiers |= VKB_MODIFIER_NUM_LOCK;
	}
	if (right_alt && left_control) {
		modifiers |= VKB_MODIFIER_ALT_GR;
	}
	context->modifiers = modifiers;
}

static bool vkb_windows_text_is_allowed(const vkb_context *context) {
	if (!context->text_input_enabled) {
		return false;
	}
	if ((context->modifiers & VKB_MODIFIER_SUPER) != 0) {
		return false;
	}
	if ((context->modifiers & VKB_MODIFIER_CONTROL) != 0 &&
		(context->modifiers & VKB_MODIFIER_ALT_GR) == 0) {
		return false;
	}
	if ((context->modifiers & VKB_MODIFIER_ALT) != 0 &&
		(context->modifiers & VKB_MODIFIER_ALT_GR) == 0) {
		return false;
	}
	return true;
}

static void vkb_windows_layout_name(HKL keyboard_layout, char *name, size_t name_capacity) {
	wchar_t wide_name[128];
	LCID locale_identifier;
	int wide_length;
	int utf8_length;

	locale_identifier = MAKELCID(LOWORD((DWORD_PTR)keyboard_layout), SORT_DEFAULT);
	wide_length = GetLocaleInfoW(
		locale_identifier,
		LOCALE_SLOCALIZEDDISPLAYNAME,
		wide_name,
		(int)(sizeof(wide_name) / sizeof(wide_name[0])));
	if (wide_length > 0) {
		utf8_length = WideCharToMultiByte(
			CP_UTF8,
			0,
			wide_name,
			-1,
			name,
			(int)name_capacity,
			NULL,
			NULL);
		if (utf8_length > 0) {
			return;
		}
	}
	snprintf(
		name,
		name_capacity,
		"Keyboard layout %04X",
		(unsigned int)LOWORD((DWORD_PTR)keyboard_layout));
}

static bool vkb_windows_refresh_layout(vkb_context *context, void *window_handle) {
	HKL keyboard_layout;
	DWORD thread_id;
	int layout_count;
	int layout_index;
	bool layout_changed;

	thread_id = (DWORD)context->configuration.native_thread_id;
	if (thread_id == 0 && window_handle != NULL) {
		thread_id = GetWindowThreadProcessId((HWND)window_handle, NULL);
	}
	if (thread_id == 0) {
		thread_id = GetCurrentThreadId();
	}
	context->thread_id = thread_id;
	keyboard_layout = GetKeyboardLayout(thread_id);
	if (keyboard_layout == NULL) {
		vkb_set_error(context, "GetKeyboardLayout failed");
		return false;
	}
	layout_changed = keyboard_layout != context->keyboard_layout ||
		context->layout_count == 0;
	context->keyboard_layout = keyboard_layout;
	if (!layout_changed) {
		return true;
	}
	layout_count = GetKeyboardLayoutList(
		VKB_LAYOUT_CAPACITY,
		context->keyboard_layouts);
	if (layout_count <= 0) {
		context->keyboard_layouts[0] = keyboard_layout;
		layout_count = 1;
	}
	if (layout_count > VKB_LAYOUT_CAPACITY) {
		layout_count = VKB_LAYOUT_CAPACITY;
	}
	context->layout_count = layout_count;
	context->active_layout_index = -1;
	for (layout_index = 0; layout_index < layout_count; layout_index += 1) {
		vkb_windows_layout_name(
			context->keyboard_layouts[layout_index],
			context->layout_names[layout_index],
			sizeof(context->layout_names[layout_index]));
		if (context->keyboard_layouts[layout_index] == keyboard_layout) {
			context->active_layout_index = layout_index;
		}
	}
	if (context->active_layout_index < 0) {
		context->keyboard_layouts[0] = keyboard_layout;
		vkb_windows_layout_name(
			keyboard_layout,
			context->layout_names[0],
			sizeof(context->layout_names[0]));
		context->active_layout_index = 0;
	}
	vkb_emit_layout_event(context, vkb_current_time_milliseconds());
	return true;
}

static bool vkb_windows_process_character(
	vkb_context *context,
	uint32_t code_unit,
	uint64_t timestamp_milliseconds) {
	if (!vkb_windows_text_is_allowed(context)) {
		context->pending_high_surrogate = 0;
		return true;
	}
	if (code_unit >= VKB_UNICODE_HIGH_SURROGATE_FIRST &&
		code_unit <= VKB_UNICODE_HIGH_SURROGATE_LAST) {
		if (context->pending_high_surrogate != 0) {
			vkb_emit_text_codepoint(
				context,
				VKB_KEY_REPLACEMENT_CHARACTER,
				timestamp_milliseconds);
		}
		context->pending_high_surrogate = code_unit;
		return true;
	}
	if (code_unit >= VKB_UNICODE_LOW_SURROGATE_FIRST &&
		code_unit <= VKB_UNICODE_LOW_SURROGATE_LAST) {
		if (context->pending_high_surrogate != 0) {
			uint32_t codepoint = VKB_UTF16_SURROGATE_BASE +
				((context->pending_high_surrogate - VKB_UNICODE_HIGH_SURROGATE_FIRST) <<
					VKB_UTF16_SURROGATE_SHIFT) +
				(code_unit - VKB_UNICODE_LOW_SURROGATE_FIRST);
			context->pending_high_surrogate = 0;
			return vkb_emit_text_codepoint(context, codepoint, timestamp_milliseconds);
		}
		return true;
	}
	if (context->pending_high_surrogate != 0) {
		vkb_emit_text_codepoint(
			context,
			VKB_KEY_REPLACEMENT_CHARACTER,
			timestamp_milliseconds);
		context->pending_high_surrogate = 0;
	}
	return vkb_emit_text_codepoint(context, code_unit, timestamp_milliseconds);
}

static bool vkb_windows_process_key(
	vkb_context *context,
	void *window_handle,
	UINT virtual_key,
	LPARAM long_parameter,
	bool is_down) {
	uint32_t scan_code;
	bool extended;
	bool is_repeat;
	vkb_physical_key physical_key;
	vkb_key logical_key;
	uint64_t timestamp_milliseconds;
	uintptr_t key_data = (uintptr_t)long_parameter;

	if (!vkb_windows_refresh_layout(context, window_handle)) {
		return false;
	}
	scan_code = (uint32_t)((key_data >> 16) & VKB_WINDOWS_SCAN_CODE_MASK);
	extended = (key_data & VKB_WINDOWS_EXTENDED_FLAG_MASK) != 0;
	if (scan_code == 0) {
		UINT translated = MapVirtualKeyExW(
			virtual_key,
			MAPVK_VK_TO_VSC_EX,
			context->keyboard_layout);
		scan_code = translated & VKB_WINDOWS_SCAN_CODE_MASK;
		extended = (translated & VKB_WINDOWS_EXTENDED_SCAN_CODE_MASK) != 0;
	}
	physical_key = vkb_windows_scan_code_to_physical_key(scan_code, extended);
	if (virtual_key == VK_PAUSE) {
		physical_key = VKB_PHYSICAL_KEY_PAUSE;
	} else if (virtual_key == VK_SNAPSHOT) {
		physical_key = VKB_PHYSICAL_KEY_PRINT_SCREEN;
	}
	logical_key = vkb_windows_logical_key(
		virtual_key,
		context->keyboard_layout,
		scan_code,
		extended);
	is_repeat = (key_data & VKB_WINDOWS_REPEAT_FLAG_MASK) != 0;
	timestamp_milliseconds = vkb_current_time_milliseconds();

	if (is_down) {
		bool was_down = physical_key != VKB_PHYSICAL_KEY_UNKNOWN &&
			vkb_physical_key_is_down_internal(context, physical_key);
		if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
			vkb_physical_key_set_down(context, physical_key, true);
		}
		vkb_windows_refresh_modifiers(
			context,
			virtual_key,
			scan_code,
			extended,
			true);
		if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
			vkb_store_logical_key(context, physical_key, logical_key);
		}
		vkb_emit_key_event(
			context,
			VKB_EVENT_KEY_DOWN,
			physical_key,
			logical_key,
			is_repeat || was_down,
			timestamp_milliseconds);
		return true;
	}

	if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN &&
		!vkb_physical_key_is_down_internal(context, physical_key)) {
		return true;
	}
	if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
		vkb_physical_key_set_down(context, physical_key, false);
	}
	vkb_windows_refresh_modifiers(
		context,
		virtual_key,
		scan_code,
		extended,
		false);
	if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
		logical_key = vkb_take_logical_key(context, physical_key, logical_key);
	}
	vkb_emit_key_event(
		context,
		VKB_EVENT_KEY_UP,
		physical_key,
		logical_key,
		false,
		timestamp_milliseconds);
	return true;
}

bool vkb_win32_process_message(
	vkb_context *context,
	void *window_handle,
	uint32_t message,
	uintptr_t word_parameter,
	intptr_t long_parameter) {
	VKB_ASSERT(context != NULL);
	switch (message) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if (word_parameter == VK_PROCESSKEY || word_parameter == VK_PACKET) {
			return true;
		}
		return vkb_windows_process_key(
			context,
			window_handle,
			(UINT)word_parameter,
			(LPARAM)long_parameter,
			true);
	case WM_KEYUP:
	case WM_SYSKEYUP:
		if (word_parameter == VK_PROCESSKEY) {
			return true;
		}
		return vkb_windows_process_key(
			context,
			window_handle,
			(UINT)word_parameter,
			(LPARAM)long_parameter,
			false);
	case WM_CHAR:
	case WM_SYSCHAR:
		vkb_windows_refresh_modifiers(context, 0, 0, false, false);
		vkb_windows_process_character(
			context,
			(uint32_t)word_parameter,
			vkb_current_time_milliseconds());
		return true;
	case WM_UNICHAR:
		if (word_parameter == (uintptr_t)UNICODE_NOCHAR) {
			return false;
		}
		vkb_windows_refresh_modifiers(context, 0, 0, false, false);
		if (vkb_windows_text_is_allowed(context)) {
			vkb_emit_text_codepoint(
				context,
				(uint32_t)word_parameter,
				vkb_current_time_milliseconds());
		}
		return true;
	case WM_INPUTLANGCHANGE:
		return vkb_windows_refresh_layout(context, window_handle);
	case WM_KILLFOCUS:
		vkb_focus_lost(context);
		return true;
	case WM_ACTIVATE:
		if (LOWORD((DWORD)word_parameter) == WA_INACTIVE) {
			vkb_focus_lost(context);
		}
		return true;
	default:
		return false;
	}
}

#endif /* _WIN32 */

#if defined(__APPLE__)

#define VKB_MACOS_EVENT_KEY_DOWN 10
#define VKB_MACOS_EVENT_KEY_UP 11
#define VKB_MACOS_EVENT_FLAGS_CHANGED 12

#define VKB_MACOS_MODIFIER_CAPS_LOCK (1u << 16)
#define VKB_MACOS_MODIFIER_SHIFT (1u << 17)
#define VKB_MACOS_MODIFIER_CONTROL (1u << 18)
#define VKB_MACOS_MODIFIER_OPTION (1u << 19)
#define VKB_MACOS_MODIFIER_COMMAND (1u << 20)
#define VKB_MACOS_MODIFIER_DEVICE_INDEPENDENT_MASK 0xFFFF0000u

typedef uintptr_t (*vkb_macos_unsigned_integer_message)(void *object, SEL selector);
typedef signed char (*vkb_macos_boolean_message)(void *object, SEL selector);
typedef double (*vkb_macos_double_message)(void *object, SEL selector);
typedef void *(*vkb_macos_object_message)(void *object, SEL selector);
typedef const char *(*vkb_macos_utf8_string_message)(void *object, SEL selector);

/*
 * objc_msgSend must be cast to the exact method signature. This is the
 * documented Objective-C runtime pattern, not an incompatible-call mistake.
 */
#if defined(__clang__) && defined(__has_warning)
#if __has_warning("-Wcast-function-type-mismatch")
#define VKB_MACOS_CAST_WARNING_ENABLED 1
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#if __has_warning("-Wcast-function-type-strict")
#pragma clang diagnostic ignored "-Wcast-function-type-strict"
#endif
#endif
#endif

static uintptr_t vkb_macos_send_unsigned_integer(void *object, const char *selector_name) {
	SEL selector = sel_registerName(selector_name);
	return ((vkb_macos_unsigned_integer_message)objc_msgSend)(object, selector);
}

static signed char vkb_macos_send_boolean(void *object, const char *selector_name) {
	SEL selector = sel_registerName(selector_name);
	return ((vkb_macos_boolean_message)objc_msgSend)(object, selector);
}

static double vkb_macos_send_double(void *object, const char *selector_name) {
	SEL selector = sel_registerName(selector_name);
	return ((vkb_macos_double_message)objc_msgSend)(object, selector);
}

static const char *vkb_macos_send_utf8_string(void *object, const char *selector_name) {
	SEL selector = sel_registerName(selector_name);
	SEL utf8_selector = sel_registerName("UTF8String");
	void *string = ((vkb_macos_object_message)objc_msgSend)(object, selector);
	if (string == NULL) {
		return NULL;
	}
	return ((vkb_macos_utf8_string_message)objc_msgSend)(string, utf8_selector);
}

#if defined(VKB_MACOS_CAST_WARNING_ENABLED)
#pragma clang diagnostic pop
#undef VKB_MACOS_CAST_WARNING_ENABLED
#endif

static void vkb_macos_refresh_modifiers(vkb_context *context, uintptr_t modifier_flags) {
	vkb_modifiers modifiers = 0;
	if ((modifier_flags & VKB_MACOS_MODIFIER_SHIFT) != 0) {
		modifiers |= VKB_MODIFIER_SHIFT;
	}
	if ((modifier_flags & VKB_MACOS_MODIFIER_CONTROL) != 0) {
		modifiers |= VKB_MODIFIER_CONTROL;
	}
	if ((modifier_flags & VKB_MACOS_MODIFIER_OPTION) != 0) {
		modifiers |= VKB_MODIFIER_ALT;
	}
	if ((modifier_flags & VKB_MACOS_MODIFIER_COMMAND) != 0) {
		modifiers |= VKB_MODIFIER_SUPER;
	}
	if ((modifier_flags & VKB_MACOS_MODIFIER_CAPS_LOCK) != 0) {
		modifiers |= VKB_MODIFIER_CAPS_LOCK;
	}
	context->modifiers = modifiers;
	vkb_recompute_side_modifiers(context);
}

/*
 * Numeric cases are the macOS virtual key codes from Carbon HIToolbox.
 * They are hardware protocol constants, not tuning numbers.
 */
static vkb_physical_key vkb_macos_keycode_to_physical_key(uint32_t keycode) {
	switch (keycode) {
	case 0x00: return VKB_PHYSICAL_KEY_A;
	case 0x01: return VKB_PHYSICAL_KEY_S;
	case 0x02: return VKB_PHYSICAL_KEY_D;
	case 0x03: return VKB_PHYSICAL_KEY_F;
	case 0x04: return VKB_PHYSICAL_KEY_H;
	case 0x05: return VKB_PHYSICAL_KEY_G;
	case 0x06: return VKB_PHYSICAL_KEY_Z;
	case 0x07: return VKB_PHYSICAL_KEY_X;
	case 0x08: return VKB_PHYSICAL_KEY_C;
	case 0x09: return VKB_PHYSICAL_KEY_V;
	case 0x0A: return VKB_PHYSICAL_KEY_NON_US_HASH;
	case 0x0B: return VKB_PHYSICAL_KEY_B;
	case 0x0C: return VKB_PHYSICAL_KEY_Q;
	case 0x0D: return VKB_PHYSICAL_KEY_W;
	case 0x0E: return VKB_PHYSICAL_KEY_E;
	case 0x0F: return VKB_PHYSICAL_KEY_R;
	case 0x10: return VKB_PHYSICAL_KEY_Y;
	case 0x11: return VKB_PHYSICAL_KEY_T;
	case 0x12: return VKB_PHYSICAL_KEY_DIGIT_1;
	case 0x13: return VKB_PHYSICAL_KEY_DIGIT_2;
	case 0x14: return VKB_PHYSICAL_KEY_DIGIT_3;
	case 0x15: return VKB_PHYSICAL_KEY_DIGIT_4;
	case 0x16: return VKB_PHYSICAL_KEY_DIGIT_6;
	case 0x17: return VKB_PHYSICAL_KEY_DIGIT_5;
	case 0x18: return VKB_PHYSICAL_KEY_EQUAL;
	case 0x19: return VKB_PHYSICAL_KEY_DIGIT_9;
	case 0x1A: return VKB_PHYSICAL_KEY_DIGIT_7;
	case 0x1B: return VKB_PHYSICAL_KEY_MINUS;
	case 0x1C: return VKB_PHYSICAL_KEY_DIGIT_8;
	case 0x1D: return VKB_PHYSICAL_KEY_DIGIT_0;
	case 0x1E: return VKB_PHYSICAL_KEY_RIGHT_BRACKET;
	case 0x1F: return VKB_PHYSICAL_KEY_O;
	case 0x20: return VKB_PHYSICAL_KEY_U;
	case 0x21: return VKB_PHYSICAL_KEY_LEFT_BRACKET;
	case 0x22: return VKB_PHYSICAL_KEY_I;
	case 0x23: return VKB_PHYSICAL_KEY_P;
	case 0x24: return VKB_PHYSICAL_KEY_RETURN;
	case 0x25: return VKB_PHYSICAL_KEY_L;
	case 0x26: return VKB_PHYSICAL_KEY_J;
	case 0x27: return VKB_PHYSICAL_KEY_APOSTROPHE;
	case 0x28: return VKB_PHYSICAL_KEY_K;
	case 0x29: return VKB_PHYSICAL_KEY_SEMICOLON;
	case 0x2A: return VKB_PHYSICAL_KEY_BACKSLASH;
	case 0x2B: return VKB_PHYSICAL_KEY_COMMA;
	case 0x2C: return VKB_PHYSICAL_KEY_SLASH;
	case 0x2D: return VKB_PHYSICAL_KEY_N;
	case 0x2E: return VKB_PHYSICAL_KEY_M;
	case 0x2F: return VKB_PHYSICAL_KEY_PERIOD;
	case 0x30: return VKB_PHYSICAL_KEY_TAB;
	case 0x31: return VKB_PHYSICAL_KEY_SPACE;
	case 0x32: return VKB_PHYSICAL_KEY_GRAVE_ACCENT;
	case 0x33: return VKB_PHYSICAL_KEY_BACKSPACE;
	case 0x34: return VKB_PHYSICAL_KEY_KEYPAD_ENTER;
	case 0x35: return VKB_PHYSICAL_KEY_ESCAPE;
	case 0x36: return VKB_PHYSICAL_KEY_RIGHT_GUI;
	case 0x37: return VKB_PHYSICAL_KEY_LEFT_GUI;
	case 0x38: return VKB_PHYSICAL_KEY_LEFT_SHIFT;
	case 0x39: return VKB_PHYSICAL_KEY_CAPS_LOCK;
	case 0x3A: return VKB_PHYSICAL_KEY_LEFT_ALT;
	case 0x3B: return VKB_PHYSICAL_KEY_LEFT_CONTROL;
	case 0x3C: return VKB_PHYSICAL_KEY_RIGHT_SHIFT;
	case 0x3D: return VKB_PHYSICAL_KEY_RIGHT_ALT;
	case 0x3E: return VKB_PHYSICAL_KEY_RIGHT_CONTROL;
	case 0x3F: return VKB_PHYSICAL_KEY_UNKNOWN;
	case 0x40: return VKB_PHYSICAL_KEY_F17;
	case 0x41: return VKB_PHYSICAL_KEY_KEYPAD_PERIOD;
	case 0x42: return VKB_PHYSICAL_KEY_KEYPAD_MULTIPLY;
	case 0x43: return VKB_PHYSICAL_KEY_KEYPAD_ADD;
	case 0x44: return VKB_PHYSICAL_KEY_CLEAR;
	case 0x45: return VKB_PHYSICAL_KEY_KEYPAD_DIVIDE;
	case 0x46: return VKB_PHYSICAL_KEY_KEYPAD_ENTER;
	case 0x47: return VKB_PHYSICAL_KEY_KEYPAD_SUBTRACT;
	case 0x48: return VKB_PHYSICAL_KEY_F18;
	case 0x49: return VKB_PHYSICAL_KEY_F19;
	case 0x4A: return VKB_PHYSICAL_KEY_KEYPAD_EQUAL;
	case 0x4B: return VKB_PHYSICAL_KEY_KEYPAD_0;
	case 0x4C: return VKB_PHYSICAL_KEY_KEYPAD_1;
	case 0x4D: return VKB_PHYSICAL_KEY_KEYPAD_2;
	case 0x4E: return VKB_PHYSICAL_KEY_KEYPAD_3;
	case 0x4F: return VKB_PHYSICAL_KEY_KEYPAD_4;
	case 0x50: return VKB_PHYSICAL_KEY_KEYPAD_5;
	case 0x51: return VKB_PHYSICAL_KEY_KEYPAD_6;
	case 0x52: return VKB_PHYSICAL_KEY_KEYPAD_7;
	case 0x53: return VKB_PHYSICAL_KEY_F20;
	case 0x54: return VKB_PHYSICAL_KEY_KEYPAD_8;
	case 0x55: return VKB_PHYSICAL_KEY_KEYPAD_9;
	case 0x56: return VKB_PHYSICAL_KEY_F5;
	case 0x57: return VKB_PHYSICAL_KEY_F6;
	case 0x58: return VKB_PHYSICAL_KEY_F7;
	case 0x59: return VKB_PHYSICAL_KEY_F3;
	case 0x5A: return VKB_PHYSICAL_KEY_F8;
	case 0x5B: return VKB_PHYSICAL_KEY_F9;
	case 0x5C: return VKB_PHYSICAL_KEY_F11;
	case 0x5D: return VKB_PHYSICAL_KEY_F13;
	case 0x5E: return VKB_PHYSICAL_KEY_F16;
	case 0x5F: return VKB_PHYSICAL_KEY_F14;
	case 0x60: return VKB_PHYSICAL_KEY_F10;
	case 0x61: return VKB_PHYSICAL_KEY_F12;
	case 0x62: return VKB_PHYSICAL_KEY_F15;
	case 0x63: return VKB_PHYSICAL_KEY_HELP;
	case 0x64: return VKB_PHYSICAL_KEY_HOME;
	case 0x65: return VKB_PHYSICAL_KEY_PAGE_UP;
	case 0x66: return VKB_PHYSICAL_KEY_DELETE_FORWARD;
	case 0x67: return VKB_PHYSICAL_KEY_F4;
	case 0x68: return VKB_PHYSICAL_KEY_END;
	case 0x69: return VKB_PHYSICAL_KEY_F2;
	case 0x6A: return VKB_PHYSICAL_KEY_PAGE_DOWN;
	case 0x6B: return VKB_PHYSICAL_KEY_F1;
	case 0x6C: return VKB_PHYSICAL_KEY_LEFT_ARROW;
	case 0x6D: return VKB_PHYSICAL_KEY_RIGHT_ARROW;
	case 0x6E: return VKB_PHYSICAL_KEY_DOWN_ARROW;
	case 0x6F: return VKB_PHYSICAL_KEY_UP_ARROW;
	default: return VKB_PHYSICAL_KEY_UNKNOWN;
	}
}

static vkb_key vkb_macos_special_logical_key(uint32_t keycode) {
	switch (keycode) {
	case 0x24: return VKB_KEY_RETURN;
	case 0x30: return VKB_KEY_TAB;
	case 0x33: return VKB_KEY_BACKSPACE;
	case 0x35: return VKB_KEY_ESCAPE;
	case 0x34:
	case 0x46: return VKB_KEY_KEYPAD_ENTER;
	case 0x37: return VKB_KEY_LEFT_SUPER;
	case 0x36: return VKB_KEY_RIGHT_SUPER;
	case 0x38: return VKB_KEY_LEFT_SHIFT;
	case 0x3C: return VKB_KEY_RIGHT_SHIFT;
	case 0x39: return VKB_KEY_CAPS_LOCK;
	case 0x3A: return VKB_KEY_LEFT_ALT;
	case 0x3D: return VKB_KEY_RIGHT_ALT;
	case 0x3B: return VKB_KEY_LEFT_CONTROL;
	case 0x3E: return VKB_KEY_RIGHT_CONTROL;
	case 0x6C: return VKB_KEY_LEFT_ARROW;
	case 0x6D: return VKB_KEY_RIGHT_ARROW;
	case 0x6E: return VKB_KEY_DOWN_ARROW;
	case 0x6F: return VKB_KEY_UP_ARROW;
	case 0x64: return VKB_KEY_HOME;
	case 0x68: return VKB_KEY_END;
	case 0x65: return VKB_KEY_PAGE_UP;
	case 0x6A: return VKB_KEY_PAGE_DOWN;
	case 0x66: return VKB_KEY_DELETE_FORWARD;
	case 0x63: return VKB_KEY_INSERT;
	case 0x6B: return VKB_KEY_F1;
	case 0x69: return VKB_KEY_F2;
	case 0x59: return VKB_KEY_F3;
	case 0x67: return VKB_KEY_F4;
	case 0x56: return VKB_KEY_F5;
	case 0x57: return VKB_KEY_F6;
	case 0x58: return VKB_KEY_F7;
	case 0x5A: return VKB_KEY_F8;
	case 0x5B: return VKB_KEY_F9;
	case 0x60: return VKB_KEY_F10;
	case 0x5C: return VKB_KEY_F11;
	case 0x61: return VKB_KEY_F12;
	case 0x5D: return VKB_KEY_F13;
	case 0x5F: return VKB_KEY_F14;
	case 0x62: return VKB_KEY_F15;
	case 0x5E: return VKB_KEY_F16;
	case 0x40: return VKB_KEY_F17;
	case 0x48: return VKB_KEY_F18;
	case 0x49: return VKB_KEY_F19;
	case 0x53: return VKB_KEY_F20;
	case 0x41: return VKB_KEY_KEYPAD_PERIOD;
	case 0x42: return VKB_KEY_KEYPAD_MULTIPLY;
	case 0x43: return VKB_KEY_KEYPAD_ADD;
	case 0x45: return VKB_KEY_KEYPAD_DIVIDE;
	case 0x47: return VKB_KEY_KEYPAD_SUBTRACT;
	case 0x4A: return VKB_KEY_KEYPAD_EQUAL;
	case 0x4B: return VKB_KEY_KEYPAD_0;
	case 0x4C: return VKB_KEY_KEYPAD_1;
	case 0x4D: return VKB_KEY_KEYPAD_2;
	case 0x4E: return VKB_KEY_KEYPAD_3;
	case 0x4F: return VKB_KEY_KEYPAD_4;
	case 0x50: return VKB_KEY_KEYPAD_5;
	case 0x51: return VKB_KEY_KEYPAD_6;
	case 0x52: return VKB_KEY_KEYPAD_7;
	case 0x54: return VKB_KEY_KEYPAD_8;
	case 0x55: return VKB_KEY_KEYPAD_9;
	default: return VKB_KEY_UNKNOWN;
	}
}

static vkb_key vkb_macos_logical_key(
	vkb_context *context,
	void *native_event,
	uint32_t keycode) {
	const char *text;
	uint32_t codepoint;
	size_t bytes_consumed;
	vkb_key special_key = vkb_macos_special_logical_key(keycode);

	if (special_key != VKB_KEY_UNKNOWN) {
		return special_key;
	}
	text = vkb_macos_send_utf8_string(native_event, "charactersIgnoringModifiers");
	if (text != NULL && text[0] != '\0' &&
		vkb_utf8_decode_first(text, strlen(text), &codepoint, &bytes_consumed)) {
		wint_t lowered = towlower((wint_t)codepoint);
		if (lowered != WEOF) {
			codepoint = (uint32_t)lowered;
		}
		return codepoint;
	}
	(void)context;
	return VKB_KEY_UNKNOWN;
}

static bool vkb_macos_modifier_key(
	uint32_t keycode,
	vkb_physical_key *physical_key,
	uintptr_t *modifier_flag) {
	switch (keycode) {
	case 0x38: *physical_key = VKB_PHYSICAL_KEY_LEFT_SHIFT; *modifier_flag = VKB_MACOS_MODIFIER_SHIFT; return true;
	case 0x3C: *physical_key = VKB_PHYSICAL_KEY_RIGHT_SHIFT; *modifier_flag = VKB_MACOS_MODIFIER_SHIFT; return true;
	case 0x3B: *physical_key = VKB_PHYSICAL_KEY_LEFT_CONTROL; *modifier_flag = VKB_MACOS_MODIFIER_CONTROL; return true;
	case 0x3E: *physical_key = VKB_PHYSICAL_KEY_RIGHT_CONTROL; *modifier_flag = VKB_MACOS_MODIFIER_CONTROL; return true;
	case 0x3A: *physical_key = VKB_PHYSICAL_KEY_LEFT_ALT; *modifier_flag = VKB_MACOS_MODIFIER_OPTION; return true;
	case 0x3D: *physical_key = VKB_PHYSICAL_KEY_RIGHT_ALT; *modifier_flag = VKB_MACOS_MODIFIER_OPTION; return true;
	case 0x37: *physical_key = VKB_PHYSICAL_KEY_LEFT_GUI; *modifier_flag = VKB_MACOS_MODIFIER_COMMAND; return true;
	case 0x36: *physical_key = VKB_PHYSICAL_KEY_RIGHT_GUI; *modifier_flag = VKB_MACOS_MODIFIER_COMMAND; return true;
	case 0x39: *physical_key = VKB_PHYSICAL_KEY_CAPS_LOCK; *modifier_flag = VKB_MACOS_MODIFIER_CAPS_LOCK; return true;
	default: return false;
	}
}

static void vkb_macos_process_flags_changed(
	vkb_context *context,
	void *native_event,
	uintptr_t modifier_flags,
	uint32_t keycode,
	uint64_t timestamp_milliseconds) {
	vkb_physical_key physical_key;
	uintptr_t modifier_flag;
	bool is_down;
	bool was_down;
	(void)native_event;

	if (!vkb_macos_modifier_key(keycode, &physical_key, &modifier_flag)) {
		return;
	}
	was_down = vkb_physical_key_is_down_internal(context, physical_key);
	/*
	 * flagsChanged reports the key that changed, while modifierFlags is the
	 * generic family state. With both shifts held, releasing one keeps the
	 * generic flag set; the key that was already down must go up anyway.
	 */
	is_down = (modifier_flags & modifier_flag) != 0 && !was_down;
	if (physical_key == VKB_PHYSICAL_KEY_CAPS_LOCK) {
		bool caps_lock_active = (modifier_flags & modifier_flag) != 0;
		if (caps_lock_active != context->macos_caps_lock_active) {
			vkb_key logical_key = VKB_KEY_CAPS_LOCK;
			context->macos_caps_lock_active = caps_lock_active;
			vkb_emit_key_event(
				context,
				VKB_EVENT_KEY_DOWN,
				physical_key,
				logical_key,
				false,
				timestamp_milliseconds);
			vkb_emit_key_event(
				context,
				VKB_EVENT_KEY_UP,
				physical_key,
				logical_key,
				false,
				timestamp_milliseconds);
		}
		vkb_macos_refresh_modifiers(context, modifier_flags);
		return;
	}
	if (is_down == was_down) {
		vkb_macos_refresh_modifiers(context, modifier_flags);
		return;
	}
	vkb_physical_key_set_down(context, physical_key, is_down);
	vkb_macos_refresh_modifiers(context, modifier_flags);
	{
		vkb_key logical_key = vkb_macos_special_logical_key(keycode);
		if (is_down) {
			vkb_store_logical_key(context, physical_key, logical_key);
			vkb_emit_key_event(
				context,
				VKB_EVENT_KEY_DOWN,
				physical_key,
				logical_key,
				false,
				timestamp_milliseconds);
		} else {
			logical_key = vkb_take_logical_key(context, physical_key, logical_key);
			vkb_emit_key_event(
				context,
				VKB_EVENT_KEY_UP,
				physical_key,
				logical_key,
				false,
				timestamp_milliseconds);
		}
	}
}

bool vkb_macos_process_event(vkb_context *context, void *native_event) {
	uintptr_t event_type;
	uintptr_t modifier_flags;
	uint32_t keycode;
	uint64_t timestamp_milliseconds;
	double timestamp_seconds;

	VKB_ASSERT(context != NULL);
	if (native_event == NULL) {
		vkb_set_error(context, "native event is null");
		return false;
	}
	event_type = vkb_macos_send_unsigned_integer(native_event, "type");
	modifier_flags = vkb_macos_send_unsigned_integer(native_event, "modifierFlags") &
		VKB_MACOS_MODIFIER_DEVICE_INDEPENDENT_MASK;
	keycode = (uint32_t)vkb_macos_send_unsigned_integer(native_event, "keyCode");
	timestamp_seconds = vkb_macos_send_double(native_event, "timestamp");
	timestamp_milliseconds = timestamp_seconds > 0.0
		? (uint64_t)(timestamp_seconds * (double)VKB_MILLISECONDS_PER_SECOND)
		: vkb_current_time_milliseconds();

	if (event_type == VKB_MACOS_EVENT_FLAGS_CHANGED) {
		vkb_macos_process_flags_changed(
			context,
			native_event,
			modifier_flags,
			keycode,
			timestamp_milliseconds);
		return true;
	}
	if (event_type != VKB_MACOS_EVENT_KEY_DOWN && event_type != VKB_MACOS_EVENT_KEY_UP) {
		return false;
	}

	vkb_macos_refresh_modifiers(context, modifier_flags);
	{
		bool is_down = event_type == VKB_MACOS_EVENT_KEY_DOWN;
		bool is_repeat = is_down && vkb_macos_send_boolean(native_event, "isARepeat") != 0;
		vkb_physical_key physical_key = vkb_macos_keycode_to_physical_key(keycode);
		vkb_key logical_key = vkb_macos_logical_key(context, native_event, keycode);

		if (is_down) {
			bool was_down = physical_key != VKB_PHYSICAL_KEY_UNKNOWN &&
				vkb_physical_key_is_down_internal(context, physical_key);
			if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
				vkb_physical_key_set_down(context, physical_key, true);
				vkb_store_logical_key(context, physical_key, logical_key);
			}
			vkb_emit_key_event(
				context,
				VKB_EVENT_KEY_DOWN,
				physical_key,
				logical_key,
				is_repeat || was_down,
				timestamp_milliseconds);
			if (context->configuration.macos_emit_text_from_key_events &&
				context->text_input_enabled &&
				(context->modifiers & (VKB_MODIFIER_SUPER | VKB_MODIFIER_CONTROL)) == 0) {
				const char *text = vkb_macos_send_utf8_string(native_event, "characters");
				if (text != NULL && text[0] != '\0') {
					uint32_t codepoint;
					size_t bytes_consumed;
					if (vkb_utf8_decode_first(
							text,
							strlen(text),
							&codepoint,
							&bytes_consumed) &&
						vkb_key_is_printable(codepoint)) {
						vkb_emit_text(
							context,
							text,
							strlen(text),
							timestamp_milliseconds);
					}
				}
			}
		} else {
			if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN &&
				!vkb_physical_key_is_down_internal(context, physical_key)) {
				return true;
			}
			if (physical_key != VKB_PHYSICAL_KEY_UNKNOWN) {
				vkb_physical_key_set_down(context, physical_key, false);
				logical_key = vkb_take_logical_key(context, physical_key, logical_key);
			}
			vkb_emit_key_event(
				context,
				VKB_EVENT_KEY_UP,
				physical_key,
				logical_key,
				false,
				timestamp_milliseconds);
		}
	}
	return true;
}

#endif /* __APPLE__ */

static bool vkb_platform_initialize(vkb_context *context) {
#if defined(_WIN32)
	context->thread_id = (DWORD)context->configuration.native_thread_id;
	if (!vkb_windows_refresh_layout(context, context->configuration.native_window)) {
		return false;
	}
	return true;
#elif defined(__APPLE__)
	context->layout_count = 0;
	context->active_layout_index = -1;
	return true;
#elif defined(__linux__)
	const char *locale;
	static const char *fallback_locales[] = {
		"en_US.UTF-8",
		"C.UTF-8"
	};
	size_t fallback_index;

	context->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (context->xkb_context == NULL) {
		vkb_set_error(context, "could not create xkb context");
		return false;
	}
	context->repeat_enabled = context->configuration.key_repeat_enabled;
	context->repeat_delay_milliseconds =
		context->configuration.repeat_delay_milliseconds;
	context->repeat_interval_milliseconds =
		context->configuration.repeat_interval_milliseconds;

	locale = getenv("LC_ALL");
	if (locale == NULL || locale[0] == '\0') {
		locale = getenv("LC_CTYPE");
	}
	if (locale == NULL || locale[0] == '\0') {
		locale = getenv("LANG");
	}
	if (locale == NULL || locale[0] == '\0') {
		locale = "C";
	}
	context->xkb_compose_table = xkb_compose_table_new_from_locale(
		context->xkb_context,
		locale,
		XKB_COMPOSE_COMPILE_NO_FLAGS);
	for (fallback_index = 0;
		 context->xkb_compose_table == NULL &&
		 fallback_index < sizeof(fallback_locales) / sizeof(fallback_locales[0]);
		 fallback_index += 1) {
		context->xkb_compose_table = xkb_compose_table_new_from_locale(
			context->xkb_context,
			fallback_locales[fallback_index],
			XKB_COMPOSE_COMPILE_NO_FLAGS);
	}
	return true;
#else
	vkb_set_error(context, "unsupported platform");
	return false;
#endif
}

vkb_context *vkb_context_create(const vkb_config *configuration) {
	vkb_context *context;
	vkb_config effective_configuration;

	vkb_global_error_message[0] = '\0';
	if (configuration != NULL) {
		effective_configuration = *configuration;
	} else {
		effective_configuration = vkb_config_default();
	}
	if (effective_configuration.repeat_delay_milliseconds == 0) {
		effective_configuration.repeat_delay_milliseconds =
			VKB_DEFAULT_REPEAT_DELAY_MILLISECONDS;
	}
	if (effective_configuration.repeat_interval_milliseconds == 0) {
		effective_configuration.repeat_interval_milliseconds =
			VKB_DEFAULT_REPEAT_INTERVAL_MILLISECONDS;
	}

	context = (vkb_context *)calloc(1, sizeof(vkb_context));
	if (context == NULL) {
		vkb_copy_string(
			vkb_global_error_message,
			sizeof(vkb_global_error_message),
			"out of memory");
		return NULL;
	}
	context->configuration = effective_configuration;
	context->text_input_enabled = effective_configuration.text_input_enabled;
	context->active_layout_index = -1;
	if (!vkb_platform_initialize(context)) {
		vkb_copy_string(
			vkb_global_error_message,
			sizeof(vkb_global_error_message),
			context->error_message);
		vkb_context_destroy(context);
		return NULL;
	}
	return context;
}

void vkb_context_destroy(vkb_context *context) {
	if (context == NULL) {
		return;
	}
#if defined(__linux__)
	if (context->xkb_compose_state != NULL) {
		xkb_compose_state_unref(context->xkb_compose_state);
	}
	if (context->xkb_compose_table != NULL) {
		xkb_compose_table_unref(context->xkb_compose_table);
	}
	if (context->xkb_state != NULL) {
		xkb_state_unref(context->xkb_state);
	}
	if (context->xkb_keymap != NULL) {
		xkb_keymap_unref(context->xkb_keymap);
	}
	if (context->xkb_context != NULL) {
		xkb_context_unref(context->xkb_context);
	}
#endif
	free(context);
}

void vkb_tick(vkb_context *context, uint64_t current_time_milliseconds) {
	VKB_ASSERT(context != NULL);
#if defined(__linux__)
	uint64_t now;
	size_t repeat_count = 0;

	if (!context->repeat_enabled ||
		context->repeating_keycode == 0 ||
		context->repeat_interval_milliseconds == 0 ||
		context->xkb_state == NULL) {
		return;
	}
	now = current_time_milliseconds != 0
		? current_time_milliseconds
		: vkb_current_time_milliseconds();
	if (now < context->next_repeat_time_milliseconds) {
		return;
	}
	while (now >= context->next_repeat_time_milliseconds &&
		repeat_count < VKB_REPEATS_PER_TICK_CAPACITY) {
		vkb_wayland_emit_key_translation(
			context,
			context->repeating_keycode,
			true,
			now);
		context->next_repeat_time_milliseconds +=
			context->repeat_interval_milliseconds;
		repeat_count += 1;
	}
	if (repeat_count == VKB_REPEATS_PER_TICK_CAPACITY &&
		now >= context->next_repeat_time_milliseconds) {
		context->next_repeat_time_milliseconds =
			now + context->repeat_interval_milliseconds;
	}
#else
	(void)current_time_milliseconds;
#endif
}

#undef VKB_ASSERT
#undef VKB_THREAD_LOCAL

#endif /* VKB_IMPLEMENTATION */

#endif /* VKB_H */
