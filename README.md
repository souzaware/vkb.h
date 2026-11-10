# vkb.h

Framework-independent keyboard input for C99.

The host owns the window and the event loop. It feeds native keyboard events
into this library. The library returns a small event stream with:

- a physical key (USB HID usage),
- a layout-aware logical key (Unicode for printable keys, named constants for
  non-printing keys),
- modifier state,
- committed UTF-8 text.

The header is the whole library. Define `VKB_IMPLEMENTATION` in exactly one
translation unit.

```c
#define VKB_IMPLEMENTATION
#include "vkb.h"
```

## Platforms

| Host | Feed | Text source |
|---|---|---|
| Windows | `WM_KEYDOWN`, `WM_KEYUP`, `WM_SYSKEYDOWN`, `WM_SYSKEYUP`, `WM_CHAR`, `WM_SYSCHAR`, `WM_UNICHAR`, `WM_INPUTLANGCHANGE`, `WM_KILLFOCUS` | `WM_CHAR` / `WM_UNICHAR` |
| macOS | `NSEvent` `keyDown`, `keyUp`, `flagsChanged` | host `insertText:` through `vkb_feed_text` |
| Wayland | `wl_keyboard` `keymap`, `modifiers`, `key`, `repeat_info`, `enter`, `leave` | xkbcommon, including dead keys and compose |

X11 is deliberately not supported.

## Why host-fed events

A windowing framework already owns native event dispatch. Installing a second
event hook under GLFW, SDL, or a raw native window causes double-handled keys,
lost focus transitions, and competing text input paths. Feeding the events the
host already receives keeps one owner and makes the library usable with any
framework.

## API

Create one context at startup:

```c
vkb_config configuration = vkb_config_default();
configuration.text_input_enabled = true;
vkb_context *context = vkb_context_create(&configuration);
if (context == NULL) {
    fprintf(stderr, "%s\n", vkb_last_error());
}
```

Drain once per frame:

```c
vkb_event event;
while (vkb_next_event(context, &event)) {
    switch (event.type) {
    case VKB_EVENT_KEY_DOWN:
        /* event.physical_key, event.logical_key, event.modifiers, event.is_repeat */
        break;
    case VKB_EVENT_KEY_UP:
        break;
    case VKB_EVENT_TEXT:
        /* event.text, event.text_length */
        break;
    case VKB_EVENT_LAYOUT_CHANGED:
        /* event.layout_name */
        break;
    }
}
```

Shortcut rule:

- `physical_key` is layout-independent. Use it for WASD, physical keybinds,
  and games.
- `logical_key` is layout-aware. Use it for text-oriented shortcuts where the
  active layout should decide the key.
- Both are present on every key event. The application chooses the policy.
- A dead key may have a `VKB_KEY_DEAD_*` logical key where the platform exposes
  it. Committed text is authoritative; do not infer composition from the key
  event alone.

A framework that already resolved a key can feed it directly instead of using a
native backend:

```c
vkb_feed_key(
    context,
    VKB_PHYSICAL_KEY_A,
    'a',
    VKB_MODIFIER_SHIFT,
    true,   /* is_down */
    false,  /* is_repeat */
    0);     /* 0 means current monotonic time */
```

Committed text remains a separate `vkb_feed_text` call. `vkb_feed_key` derives a
repeat when the physical key is already down, even if the host did not set the
repeat flag.

Key state and layout queries:

```c
vkb_physical_key_is_down(context, VKB_PHYSICAL_KEY_A);
vkb_logical_key_is_down(context, 'a');
vkb_get_modifiers(context);
vkb_layout_count(context);
vkb_layout_name(context, index);
vkb_layout_active_name(context);
```

## Wayland

Install a normal `wl_keyboard` listener. The keymap file descriptor is consumed
and closed by the library.

```c
static void keyboard_keymap(
    void *data, struct wl_keyboard *keyboard,
    uint32_t format, int32_t file_descriptor, uint32_t size) {
    vkb_context *context = data;
    vkb_wayland_set_keymap(context, file_descriptor, size, format);
}

static void keyboard_modifiers(
    void *data, struct wl_keyboard *keyboard,
    uint32_t serial, uint32_t depressed, uint32_t latched,
    uint32_t locked, uint32_t group) {
    vkb_context *context = data;
    vkb_wayland_set_modifiers(context, depressed, latched, locked, group);
}

static void keyboard_key(
    void *data, struct wl_keyboard *keyboard,
    uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    vkb_context *context = data;
    vkb_wayland_process_key(context, key, state, time);
}

static void keyboard_repeat_info(
    void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    vkb_context *context = data;
    vkb_wayland_set_repeat_info(context, rate, delay);
}

static void keyboard_enter(
    void *data, struct wl_keyboard *keyboard,
    uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    vkb_context *context = data;
    vkb_wayland_enter(context, keys->data, keys->size / sizeof(uint32_t));
}

static void keyboard_leave(
    void *data, struct wl_keyboard *keyboard,
    uint32_t serial, struct wl_surface *surface) {
    vkb_context *context = data;
    vkb_wayland_leave(context);
}
```

Call `vkb_tick(context, monotonic_milliseconds)` once per frame. Wayland key
repeat is client-side, so this is required for held keys to repeat. Use the
same monotonic millisecond time base as the `wl_keyboard.key` time argument.
Feed only real press/release transitions; if a framework synthesizes repeat
events, ignore those and let `vkb_tick` own repeat.

Text is produced by the library on Wayland. Do not also feed the framework's
character callback, or text will be duplicated. Dead keys and compose use
xkbcommon's compose table for the process locale (`LC_ALL`, `LC_CTYPE`, or
`LANG`), with `en_US.UTF-8` as a fallback.

## Windows

Call the message feeder from the window procedure:

```c
LRESULT CALLBACK window_procedure(
    HWND window, UINT message, WPARAM word_parameter, LPARAM long_parameter) {
    vkb_win32_process_message(
        context, window, message, word_parameter, long_parameter);
    return DefWindowProcW(window, message, word_parameter, long_parameter);
}
```

Set `configuration.native_window = window` before creating the context or
before the first key message. The library queries `GetKeyboardLayout` and
refreshes on `WM_INPUTLANGCHANGE`, so Alt+Shift / Win+Space layout changes are
picked up without restarting.

Text comes from `WM_CHAR` and `WM_UNICHAR`. Ctrl, Alt, and Super combinations
are not turned into text; AltGr is recognized as right Alt plus left Control.
On Windows, AltGr also sets the generic Control and Alt bits because that is
what the OS reports. Check `VKB_MODIFIER_ALT_GR` before treating Control or Alt
as a shortcut modifier.

## macOS

Forward key events from the view:

```objc
- (void)keyDown:(NSEvent *)event {
    vkb_macos_process_event(context, event);
    [self interpretKeyEvents:@[event]]; /* keep the host text system */
}

- (void)keyUp:(NSEvent *)event {
    vkb_macos_process_event(context, event);
}

- (void)flagsChanged:(NSEvent *)event {
    vkb_macos_process_event(context, event);
}

- (void)insertText:(id)string {
    const char *utf8 = [string UTF8String];
    vkb_feed_text(context, utf8, strlen(utf8));
}
```

`vkb_macos_process_event` handles physical and logical key identity, modifiers,
and key up/down. It does not replace `NSTextInputClient`; committed text must
arrive through `vkb_feed_text`. That is the path that makes dead keys, Option
sequences, and input methods produce correct text.

`configuration.macos_emit_text_from_key_events` is off by default. Enabling it
reads `NSEvent.characters` directly as a shortcut for simple layouts. It is not
correct for dead-key layouts, because the spacing accent can be committed
before the composition finishes.

macOS layout enumeration is not implemented; `vkb_layout_count` returns 0.
Text is still layout-correct because the host text system resolves it per
event.

## Text input

`vkb_set_text_input_enabled(false)` stops text events while preserving key
events. Use it when a text field loses focus, so shortcuts do not insert text.

Text events carry printable text. Enter, Tab, Backspace, Delete, and arrows are
key events, not text events, so the host can route them consistently in text
fields and in shortcut handling.

`vkb_feed_text` accepts committed UTF-8 text from a host IME or text system.
The text must fit in `VKB_TEXT_CAPACITY` (128 bytes). It returns false on
invalid arguments or oversized commits.

IME preedit strings, candidate windows, and candidate lists are not included.
A host with its own IME layer can feed committed results and ignore the rest.

## Modifiers

`vkb_modifiers` uses these bits:

```
VKB_MODIFIER_SHIFT, CONTROL, ALT, SUPER       generic
VKB_MODIFIER_LEFT_SHIFT / RIGHT_SHIFT          side-specific
VKB_MODIFIER_LEFT_CONTROL / RIGHT_CONTROL
VKB_MODIFIER_LEFT_ALT / RIGHT_ALT
VKB_MODIFIER_LEFT_SUPER / RIGHT_SUPER
VKB_MODIFIER_CAPS_LOCK, NUM_LOCK, ALT_GR
```

Helpers: `vkb_modifiers_has_shift`, `vkb_modifiers_has_control`,
`vkb_modifiers_has_alt`, `vkb_modifiers_has_super`, `vkb_modifiers_has_alt_gr`.

Side-specific bits are best-effort when the platform reports generic flags
only.

## Build

Linux:

```sh
cc -std=c99 -c your_translation_unit.c $(pkg-config --cflags xkbcommon)
cc ... -lxkbcommon
```

Windows: link `user32.lib` and `kernel32.lib` (the default MSVC desktop link
already includes them in most projects).

macOS: link `-lobjc`. A Cocoa application already links it.

## Test

The Linux probe builds real keymaps and feeds them through the Wayland path. It
does not need a compositor.

```sh
./tests/build_and_run.sh
```

It covers US, German, Russian, AltGr, dead-key compose, runtime layout group
switching, host text, repeat, and focus loss.

## Limits

- No X11.
- No IME preedit/candidate UI.
- macOS layout enumeration is not implemented.
- Wayland key repeat is client-side; Windows and macOS use the OS repeat.
- One context per application is normal. The context is not thread-safe; feed
  and drain on the thread that owns the window.

## References

The API shape follows the problem, not another library. SDL3 and raylib were
consulted for platform behavior and physical-key vocabulary; no code is copied.
The physical key values are the public USB HID Usage Tables, Keyboard/Keypad
page (0x07).
