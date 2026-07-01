# zmk-rgb-effects

Modular RGB effects for ZMK keyboards. Provides per-key per-layer RGB indicators and a framework for adding custom effects without forking ZMK.

Requires: [ZMK fork](https://github.com/afiqzudinhadi/zmk) branch `rgb-modular-v1.1` (modular effect registration API).

## Setup

Add to your `west.yml`:

```yaml
manifest:
  remotes:
    - name: afiqzudinhadi
      url-base: https://github.com/afiqzudinhadi
  projects:
    - name: zmk
      remote: afiqzudinhadi
      revision: rgb-modular-v1.1
      import: app/west.yml
    - name: zmk-rgb-effects
      remote: afiqzudinhadi
      revision: v1.1.1
```

Enable in your `.conf`:

```
CONFIG_ZMK_RGB_UNDERGLOW=y
CONFIG_ZMK_RGB_EFFECT_LAYER=y
```

## Per-key layer effect

Define per-key colors in your keymap/overlay using devicetree:

```dts
#include <dt-bindings/zmk/rgb_colors.h>

/ {
    underglow-layer {
        compatible = "zmk,underglow-layer";
        pixel-lookup = <
            5  4  3  2  1  0
            6  7  8  9  10 11
            17 16 15 14 13 12
        >;

        layer0 {
            layer-id = <0>;
            bindings = <
                &ugc RGB_RED  &ugc RGB_RED  &ugc RGB_OFF ...
            >;
        };

        layer1 {
            layer-id = <1>;
            fade-delay = <15>;
            bindings = <
                &ugc RGB_BLUE &ugc RGB_BLUE &ugc RGB_OFF ...
            >;
        };
    };
};
```

### Properties

- `pixel-lookup` — maps LED chain index to key position
- `layer-id` — which keymap layer this color map applies to
- `fade-delay` — seconds before reverting to animated effect (-1 = never)
- `bindings` — color per key using behaviors below

### Behaviors

| Behavior | Description | Params |
|----------|-------------|--------|
| `&ugc` | Static color | `param1` = RGB hex color |
| `&ubi` | Battery indicator | `param1` = low color, `param2` = ok color, `threshold` DT prop |
| `&ugi` | HID indicator (caps/num/scroll lock) | `param1` = off color, `param2` = on color, `indicator` DT prop |

## Adding custom effects

Create a `.c` file with `ZMK_RGB_EFFECT_DEFINE`:

```c
#include <zmk/rgb_effect.h>

static void my_render(struct zmk_rgb_effect_ctx *ctx) {
    for (int i = 0; i < ctx->num_pixels; i++) {
        struct zmk_led_hsb hsb = ctx->base_color;
        hsb.h = (*ctx->animation_step + i * 30) % 360;
        ctx->pixels[i] = zmk_rgb_hsb_to_rgb(zmk_rgb_hsb_scale_min_max(hsb));
    }
    *ctx->animation_step += ctx->animation_speed;
}

ZMK_RGB_EFFECT_DEFINE(my_effect, "My Effect",
    my_render, 0, NULL, NULL, NULL, NULL);
```

Add to `CMakeLists.txt`:

```cmake
target_sources(app PRIVATE src/effects/my_effect.c)
```

The effect auto-registers via linker section. Accessible by cycling with `RGB_EFF`.

### As a separate module repo

You can also create effects in their own repo without modifying this module:

```
my-zmk-rgb-rainbow/
├── zephyr/
│   └── module.yml
├── CMakeLists.txt
├── Kconfig
└── src/
    └── rainbow.c
```

`zephyr/module.yml`:
```yaml
build:
  cmake: .
  kconfig: Kconfig
```

`Kconfig`:
```
config ZMK_RGB_EFFECT_RAINBOW
    bool "Rainbow RGB effect"
    depends on ZMK_RGB_UNDERGLOW
    default y
```

`CMakeLists.txt`:
```cmake
if(CONFIG_ZMK_RGB_EFFECT_RAINBOW)
  target_sources(app PRIVATE src/rainbow.c)
endif()
```

`src/rainbow.c` uses `ZMK_RGB_EFFECT_DEFINE` exactly as above.

Add to your `west.yml`:
```yaml
- name: my-zmk-rgb-rainbow
  remote: your-github-remote
  revision: main
```

Requires the ZMK fork with the modular effect API. No dependency on this module — the linker section collects effect registrations across all modules.

- `rgb-modular-v1.0` — basic effect API (`ZMK_RGB_EFFECT_DEFINE` with 6 params: render, flags, on_select, on_deselect). Sufficient for simple animated or static effects.
- `rgb-modular-v1.1` — extended API (adds `on_idle`, `is_active` callbacks, `ZMK_RGB_EFFECT_PERSISTENT` flag, `zmk_rgb_request_refresh_wakeup()`, `zmk_rgb_set_tick_delay()`, `zmk_rgb_is_on()`). Needed for effects that persist across RGB toggle/idle or use fade-delay.

Start with `rgb-modular-v1.0` if you want to explore the API from scratch.

### Effect flags

| Flag | Description |
|------|-------------|
| `0` | Animated — core runs render at 25ms tick |
| `ZMK_RGB_EFFECT_STATIC` | No periodic tick — render called on demand via `zmk_rgb_request_refresh()` |
| `ZMK_RGB_EFFECT_PERSISTENT` | Survives RGB toggle and idle sleep |

### Effect callbacks

| Callback | When called |
|----------|-------------|
| `render` | Each tick (animated) or on refresh (static) |
| `on_select` | Effect becomes active |
| `on_deselect` | User cycles to different effect |
| `on_idle(bool awake)` | Keyboard sleeps/wakes (persistent effects only) |
| `is_active()` | Queried for ext power gating — return `true` to keep LEDs powered |

### Render context

| Field | Type | Description |
|-------|------|-------------|
| `pixels` | `struct led_rgb *` | LED buffer to write |
| `num_pixels` | `uint16_t` | Strip length |
| `base_color` | `struct zmk_led_hsb` | User-configured HSB color |
| `animation_step` | `uint16_t *` | Read/write animation counter |
| `animation_speed` | `uint8_t` | User-configured speed (1-5) |

### Utility functions

- `zmk_rgb_hsb_to_rgb(hsb)` — HSB to RGB conversion
- `zmk_rgb_hsb_scale_min_max(hsb)` — scale brightness to configured min/max
- `zmk_rgb_hsb_scale_zero_max(hsb)` — scale brightness from 0 to max
- `zmk_rgb_request_refresh()` — trigger re-render for static effects
- `zmk_rgb_request_refresh_wakeup(bool)` — refresh with conditional wake from sleep
- `zmk_rgb_set_tick_delay(int seconds)` — delayed tick restart (for fade effects)
- `zmk_rgb_is_on()` — query RGB on/off state

## Versioning

`vX.Y.Z` where:
- **X** — major version (breaking API changes)
- **Y** — ZMK core dependency version (matches `rgb-modular-vX.Y`)
- **Z** — custom effects (0 = defaults only, 1+ = custom effects included)
