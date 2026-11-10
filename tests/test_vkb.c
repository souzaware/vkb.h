/*
 * Linux/Wayland probe for vkb.h.
 *
 * No compositor is needed: the probe builds real xkbcommon keymaps and feeds
 * them through the same file-descriptor path wl_keyboard uses.
 */

#define VKB_IMPLEMENTATION
#include "../vkb.h"

#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

static int failures;

static void check(bool condition, const char *message) {
	if (!condition) {
		fprintf(stderr, "[vkb-test] FAIL: %s\n", message);
		failures += 1;
	}
}

static void check_text(
	const char *expected,
	const vkb_event *event,
	const char *message) {
	if (event->type != VKB_EVENT_TEXT) {
		fprintf(
			stderr,
			"[vkb-test] FAIL: %s (event type %d, expected text)\n",
			message,
			(int)event->type);
		failures += 1;
		return;
	}
	if (event->text_length != strlen(expected) ||
		memcmp(event->text, expected, event->text_length) != 0) {
		fprintf(
			stderr,
			"[vkb-test] FAIL: %s (got '%.*s', expected '%s')\n",
			message,
			(int)event->text_length,
			event->text,
			expected);
		failures += 1;
	}
}

static vkb_event next_event(vkb_context *context, const char *message) {
	vkb_event event;
	if (!vkb_next_event(context, &event)) {
		fprintf(stderr, "[vkb-test] FAIL: %s (no event)\n", message);
		failures += 1;
		memset(&event, 0, sizeof(event));
	}
	return event;
}

static void drain_events(vkb_context *context) {
	vkb_event event;
	while (vkb_next_event(context, &event)) {
	}
}

static struct xkb_keymap *create_keymap(
	const char *layout,
	const char *variant) {
	struct xkb_context *xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_rule_names names;
	struct xkb_keymap *keymap;

	if (xkb_context == NULL) {
		fprintf(stderr, "[vkb-test] FAIL: could not create xkb context\n");
		exit(1);
	}
	memset(&names, 0, sizeof(names));
	names.layout = layout;
	names.variant = variant;
	keymap = xkb_keymap_new_from_names(
		xkb_context,
		&names,
		XKB_KEYMAP_COMPILE_NO_FLAGS);
	xkb_context_unref(xkb_context);
	return keymap;
}

static void write_all(int file_descriptor, const char *text, size_t length) {
	size_t offset = 0;
	while (offset < length) {
		ssize_t written = write(file_descriptor, text + offset, length - offset);
		if (written <= 0) {
			fprintf(stderr, "[vkb-test] FAIL: keymap write failed\n");
			exit(1);
		}
		offset += (size_t)written;
	}
}

static void feed_keymap(
	vkb_context *context,
	const char *layout,
	const char *variant) {
	struct xkb_keymap *keymap = create_keymap(layout, variant);
	char *text;
	size_t length;
	int descriptors[2];

	if (keymap == NULL) {
		fprintf(
			stderr,
			"[vkb-test] FAIL: could not create keymap %s(%s)\n",
			layout,
			variant != NULL ? variant : "");
		exit(1);
	}
	text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
	if (text == NULL) {
		fprintf(stderr, "[vkb-test] FAIL: could not serialize keymap\n");
		exit(1);
	}
	length = strlen(text) + 1;
	if (pipe(descriptors) != 0) {
		fprintf(stderr, "[vkb-test] FAIL: pipe failed\n");
		exit(1);
	}
	write_all(descriptors[1], text, length);
	close(descriptors[1]);
	check(
		vkb_wayland_set_keymap(
			context,
			descriptors[0],
			(uint32_t)length,
			VKB_WAYLAND_KEYMAP_FORMAT_XKB_V1),
		"wayland keymap accepted");
	free(text);
	xkb_keymap_unref(keymap);
	check(vkb_layout_count(context) > 0, "keymap exposes a layout");
	drain_events(context);
}

static uint32_t modifier_mask(
	const char *layout,
	const char *variant,
	const char *modifier_name) {
	struct xkb_keymap *keymap = create_keymap(layout, variant);
	xkb_mod_index_t index;
	uint32_t mask;

	if (keymap == NULL) {
		fprintf(stderr, "[vkb-test] FAIL: modifier keymap missing\n");
		exit(1);
	}
	index = xkb_keymap_mod_get_index(keymap, modifier_name);
	if (index == XKB_MOD_INVALID || index >= 32) {
		fprintf(
			stderr,
			"[vkb-test] FAIL: modifier %s is missing\n",
			modifier_name);
		exit(1);
	}
	mask = 1u << (uint32_t)index;
	xkb_keymap_unref(keymap);
	return mask;
}

static void press_key(
	vkb_context *context,
	uint32_t linux_input_event_code,
	uint32_t timestamp_milliseconds) {
	check(
		vkb_wayland_process_key(
			context,
			linux_input_event_code,
			VKB_KEY_STATE_PRESSED,
			timestamp_milliseconds),
		"key press accepted");
}

static void release_key(
	vkb_context *context,
	uint32_t linux_input_event_code,
	uint32_t timestamp_milliseconds) {
	check(
		vkb_wayland_process_key(
			context,
			linux_input_event_code,
			VKB_KEY_STATE_RELEASED,
			timestamp_milliseconds),
		"key release accepted");
}

static void test_us_layout(vkb_context *context) {
	vkb_event event;
	uint32_t shift;

	feed_keymap(context, "us", NULL);
	shift = modifier_mask("us", NULL, XKB_MOD_NAME_SHIFT);

	press_key(context, KEY_A, 100);
	event = next_event(context, "US A key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "US A is a key down");
	check(event.physical_key == VKB_PHYSICAL_KEY_A, "US A physical key");
	check(event.logical_key == 'a', "US A logical key");
	event = next_event(context, "US A text");
	check_text("a", &event, "US A text");
	check(
		vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A),
		"US A is held");
	release_key(context, KEY_A, 110);
	event = next_event(context, "US A key up");
	check(event.type == VKB_EVENT_KEY_UP, "US A is a key up");
	check(
		!vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A),
		"US A is released");

	check(
		vkb_wayland_set_modifiers(context, shift, 0, 0, 0),
		"US shift modifiers accepted");
	press_key(context, KEY_1, 200);
	event = next_event(context, "US shift+1 key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "US shift+1 is a key down");
	event = next_event(context, "US shift+1 text");
	check_text("!", &event, "US shift+1 text");
	release_key(context, KEY_1, 210);
	event = next_event(context, "US shift+1 key up");
	check(event.type == VKB_EVENT_KEY_UP, "US shift+1 is a key up");
	check(
		vkb_wayland_set_modifiers(context, 0, 0, 0, 0),
		"US modifiers cleared");
}

static void test_german_layout(vkb_context *context) {
	vkb_event event;
	uint32_t altgr;

	feed_keymap(context, "de", NULL);
	altgr = modifier_mask("de", NULL, "Mod5");

	press_key(context, KEY_Y, 300);
	event = next_event(context, "German Y key down");
	check(event.physical_key == VKB_PHYSICAL_KEY_Y, "German Y physical key");
	check(event.logical_key == 'z', "German Y logical key");
	event = next_event(context, "German Y text");
	check_text("z", &event, "German Y text");
	release_key(context, KEY_Y, 310);
	event = next_event(context, "German Y key up");
	check(event.type == VKB_EVENT_KEY_UP, "German Y is a key up");

	check(
		vkb_wayland_set_modifiers(context, altgr, 0, 0, 0),
		"German AltGr modifiers accepted");
	press_key(context, KEY_Q, 400);
	event = next_event(context, "German AltGr+Q key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "German AltGr+Q is a key down");
	event = next_event(context, "German AltGr+Q text");
	check_text("@", &event, "German AltGr+Q text");
	release_key(context, KEY_Q, 410);
	event = next_event(context, "German AltGr+Q key up");
	check(event.type == VKB_EVENT_KEY_UP, "German AltGr+Q is a key up");
	check(
		vkb_wayland_set_modifiers(context, 0, 0, 0, 0),
		"German modifiers cleared");
}

static void test_russian_layout(vkb_context *context) {
	vkb_event event;
	uint32_t shift;

	feed_keymap(context, "ru", NULL);
	shift = modifier_mask("ru", NULL, XKB_MOD_NAME_SHIFT);

	press_key(context, KEY_A, 500);
	event = next_event(context, "Russian A key down");
	check(event.physical_key == VKB_PHYSICAL_KEY_A, "Russian A physical key");
	check(event.logical_key == 0x0444u, "Russian A logical key");
	event = next_event(context, "Russian A text");
	check_text("\xD1\x84", &event, "Russian A text");
	release_key(context, KEY_A, 510);
	event = next_event(context, "Russian A key up");
	check(event.type == VKB_EVENT_KEY_UP, "Russian A is a key up");

	check(
		vkb_wayland_set_modifiers(context, shift, 0, 0, 0),
		"Russian shift modifiers accepted");
	press_key(context, KEY_A, 600);
	event = next_event(context, "Russian shift+A key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "Russian shift+A is a key down");
	event = next_event(context, "Russian shift+A text");
	check_text("\xD0\xA4", &event, "Russian shift+A text");
	release_key(context, KEY_A, 610);
	event = next_event(context, "Russian shift+A key up");
	check(event.type == VKB_EVENT_KEY_UP, "Russian shift+A is a key up");
	check(
		vkb_wayland_set_modifiers(context, 0, 0, 0, 0),
		"Russian modifiers cleared");
}

static void test_dead_key_compose(vkb_context *context) {
	vkb_event event;

	feed_keymap(context, "us", "intl");

	press_key(context, KEY_GRAVE, 700);
	event = next_event(context, "dead grave key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "dead grave is a key down");
	check(
		event.logical_key == VKB_KEY_DEAD_GRAVE,
		"dead grave logical key");
	check(
		!vkb_next_event(context, &event),
		"dead grave does not commit text");

	press_key(context, KEY_E, 710);
	event = next_event(context, "dead grave + E key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "composed E is a key down");
	event = next_event(context, "dead grave + E text");
	check_text("\xC3\xA8", &event, "dead grave + E text");
	release_key(context, KEY_E, 720);
	event = next_event(context, "dead grave + E key up");
	check(event.type == VKB_EVENT_KEY_UP, "composed E is a key up");
	release_key(context, KEY_GRAVE, 730);
	event = next_event(context, "dead grave key up");
	check(event.type == VKB_EVENT_KEY_UP, "dead grave key up");
}

static void test_layout_switch(vkb_context *context) {
	vkb_event event;

	feed_keymap(context, "us,ru", NULL);
	check(vkb_layout_count(context) == 2, "two layouts are exposed");
	check(vkb_layout_active_index(context) == 0, "first layout is active");
	check(
		strlen(vkb_layout_name(context, 0)) > 0,
		"first layout has a name");
	check(
		strlen(vkb_layout_name(context, 1)) > 0,
		"second layout has a name");

	press_key(context, KEY_A, 800);
	event = next_event(context, "layout switch first key down");
	check(event.logical_key == 'a', "first layout produces a");
	event = next_event(context, "layout switch first text");
	check_text("a", &event, "first layout text");
	release_key(context, KEY_A, 810);
	event = next_event(context, "layout switch first key up");
	check(event.type == VKB_EVENT_KEY_UP, "first layout key up");

	check(
		vkb_wayland_set_modifiers(context, 0, 0, 0, 1),
		"layout group switch accepted");
	event = next_event(context, "layout changed event");
	check(event.type == VKB_EVENT_LAYOUT_CHANGED, "layout change event");
	check(
		strlen(event.layout_name) > 0,
		"layout change event carries a name");
	check(vkb_layout_active_index(context) == 1, "second layout is active");

	press_key(context, KEY_A, 820);
	event = next_event(context, "second layout key down");
	check(event.logical_key == 0x0444u, "second layout logical key");
	event = next_event(context, "second layout text");
	check_text("\xD1\x84", &event, "second layout text");
	release_key(context, KEY_A, 830);
	event = next_event(context, "second layout key up");
	check(event.type == VKB_EVENT_KEY_UP, "second layout key up");
	check(
		vkb_wayland_set_modifiers(context, 0, 0, 0, 0),
		"layout group reset accepted");
	event = next_event(context, "layout reset event");
	check(event.type == VKB_EVENT_LAYOUT_CHANGED, "layout reset event");
}

static void test_host_text(vkb_context *context) {
	vkb_event event;

	feed_keymap(context, "us", NULL);
	check(
		vkb_feed_text(context, "host text", 9),
		"host text accepted");
	event = next_event(context, "host text event");
	check_text("host text", &event, "host text event");

	vkb_set_text_input_enabled(context, false);
	check(
		vkb_feed_text(context, "ignored", 7),
		"disabled host text is accepted and ignored");
	check(
		!vkb_next_event(context, &event),
		"disabled text input emits nothing");
	press_key(context, KEY_A, 900);
	event = next_event(context, "disabled text key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "disabled text still emits keys");
	check(
		!vkb_next_event(context, &event),
		"disabled text input emits no text");
	release_key(context, KEY_A, 910);
	event = next_event(context, "disabled text key up");
	check(event.type == VKB_EVENT_KEY_UP, "disabled text key up");
	vkb_set_text_input_enabled(context, true);
}

static void test_repeat(vkb_context *context) {
	vkb_event event;

	feed_keymap(context, "us", NULL);
	vkb_wayland_set_repeat_info(context, 10, 100);

	press_key(context, KEY_A, 1000);
	event = next_event(context, "repeat initial key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "repeat initial key down");
	check(!event.is_repeat, "repeat initial key is not a repeat");
	event = next_event(context, "repeat initial text");
	check_text("a", &event, "repeat initial text");

	vkb_tick(context, 1099);
	check(!vkb_next_event(context, &event), "repeat waits for the delay");
	vkb_tick(context, 1100);
	event = next_event(context, "repeat key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "repeat is a key down");
	check(event.is_repeat, "repeat is marked");
	event = next_event(context, "repeat text");
	check_text("a", &event, "repeat text");

	vkb_tick(context, 1210);
	event = next_event(context, "second repeat key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "second repeat is a key down");
	check(event.is_repeat, "second repeat is marked");
	event = next_event(context, "second repeat text");
	check_text("a", &event, "second repeat text");

	release_key(context, KEY_A, 1220);
	event = next_event(context, "repeat key up");
	check(event.type == VKB_EVENT_KEY_UP, "repeat key up");
	vkb_tick(context, 2000);
	check(!vkb_next_event(context, &event), "repeat stops after release");
}

static void test_wayland_focus_state(vkb_context *context) {
	vkb_event event;
	uint32_t keys[1];

	feed_keymap(context, "us", NULL);
	keys[0] = KEY_A;
	vkb_wayland_enter(context, keys, 1);
	check(
		vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A),
		"enter records held keys");
	check(
		!vkb_next_event(context, &event),
		"enter does not emit key down events");
	vkb_wayland_leave(context);
	event = next_event(context, "leave key up");
	check(event.type == VKB_EVENT_KEY_UP, "leave emits key up");
	check(
		!vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A),
		"leave clears held keys");
}

static void test_host_feed_key(vkb_context *context) {
	vkb_event event;

	vkb_feed_key(
		context,
		VKB_PHYSICAL_KEY_A,
		'a',
		VKB_MODIFIER_SHIFT,
		true,
		false,
		1400);
	event = next_event(context, "host key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "host key down type");
	check(event.physical_key == VKB_PHYSICAL_KEY_A, "host key down physical key");
	check(event.logical_key == 'a', "host key down logical key");
	check(event.modifiers == VKB_MODIFIER_SHIFT, "host key down modifiers");
	check(!event.is_repeat, "host key down is not a repeat");
	check(
		vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A),
		"host key down is held");

	vkb_feed_key(
		context,
		VKB_PHYSICAL_KEY_A,
		'a',
		VKB_MODIFIER_SHIFT,
		true,
		false,
		1410);
	event = next_event(context, "host repeat");
	check(event.type == VKB_EVENT_KEY_DOWN, "host repeat type");
	check(event.is_repeat, "host repeat is marked from held state");

	vkb_feed_key(
		context,
		VKB_PHYSICAL_KEY_A,
		'a',
		0,
		false,
		false,
		1420);
	event = next_event(context, "host key up");
	check(event.type == VKB_EVENT_KEY_UP, "host key up type");
	check(event.logical_key == 'a', "host key up keeps the stored logical key");
	check(
		!vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A),
		"host key up releases the key");

	check(vkb_feed_text(context, "hi", 2), "host text accepted");
	event = next_event(context, "host text");
	check_text("hi", &event, "host text bytes");
}

static void test_focus_lost(vkb_context *context) {
	vkb_event event;

	feed_keymap(context, "us", NULL);
	press_key(context, KEY_A, 1300);
	event = next_event(context, "focus loss key down");
	check(event.type == VKB_EVENT_KEY_DOWN, "focus loss key down");
	event = next_event(context, "focus loss text");
	check_text("a", &event, "focus loss text");

	vkb_focus_lost(context);
	event = next_event(context, "focus loss key up");
	check(event.type == VKB_EVENT_KEY_UP, "focus loss emits key up");
	check(
		!vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A),
		"focus loss releases physical key");
	check(vkb_get_modifiers(context) == 0, "focus loss clears modifiers");
}

int main(void) {
	vkb_config configuration;
	vkb_context *context;

	setenv("LC_ALL", "en_US.UTF-8", 1);
	configuration = vkb_config_default();
	context = vkb_context_create(&configuration);
	if (context == NULL) {
		fprintf(stderr, "[vkb-test] FAIL: %s\n", vkb_last_error());
		return 1;
	}

	test_us_layout(context);
	test_german_layout(context);
	test_russian_layout(context);
	test_dead_key_compose(context);
	test_layout_switch(context);
	test_host_text(context);
	test_repeat(context);
	test_wayland_focus_state(context);
	test_focus_lost(context);
	test_host_feed_key(context);

	vkb_context_destroy(context);
	if (failures != 0) {
		fprintf(stderr, "[vkb-test] %d failure(s)\n", failures);
		return 1;
	}
	printf("[vkb-test] success\n");
	return 0;
}
