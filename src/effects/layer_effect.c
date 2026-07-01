/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/rgb_underglow.h>
#include <zmk/rgb_effect.h>
#include <zmk/rgb_underglow_layer.h>
#include <zmk/matrix.h>
#include <zmk/keymap.h>
#include <zmk/behavior.h>

#include <zmk/event_manager.h>
#include <zmk/events/underglow_color_changed.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zmk/events/split_peripheral_layer_changed.h>
#endif

#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/bluetooth/peripheral_layers.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define DT_DRV_COMPAT zmk_underglow_layer
#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static bool layer_effect_active = false;

static struct led_rgb hex_to_rgb_scaled(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    return (struct led_rgb){
        r : (brightness * r) / 0xff,
        g : (brightness * g) / 0xff,
        b : (brightness * b) / 0xff
    };
}

static int apply_rgbmap(struct led_rgb *pixels, uint16_t num_pixels,
                        const struct zmk_behavior_binding *bindings,
                        size_t rgbmap_len, uint8_t layer, uint8_t brightness) {
    int rc = 0;
    for (int i = 0; i < num_pixels; i++) {
        uint8_t midx = rgb_pixel_lookup(i);
        if (midx >= ZMK_KEYMAP_LEN) {
            LOG_DBG("out of range");
        } else {
            const struct device *dev = zmk_behavior_get_binding(bindings[midx].behavior_dev);

            if (dev == NULL) {
                continue;
            }

            const struct behavior_driver_api *api = (const struct behavior_driver_api *)dev->api;

            if (api->binding_pressed == NULL) {
                continue;
            }
            struct zmk_behavior_binding_event event = {
                .position = midx, .layer = layer, .timestamp = k_uptime_get()};

            int color = api->binding_pressed(
                (struct zmk_behavior_binding *)&bindings[midx], event);

            if (color > 0) {
                pixels[i] = hex_to_rgb_scaled(
                    (color & 0xFF0000) >> 16, (color & 0xFF00) >> 8, color & 0xFF, brightness);
                rc = 1;
            } else {
                pixels[i] = (struct led_rgb){r : 0, g : 0, b : 0};
            }
        }
    }
    return rc;
}

static void layer_effect_render(struct zmk_rgb_effect_ctx *ctx) {
    if (!layer_effect_active) {
        return;
    }

    uint8_t layer = rgb_underglow_top_layer();
    const struct zmk_behavior_binding *rgbmap = rgb_underglow_get_bindings(layer);
    if (rgbmap != NULL) {
        if (apply_rgbmap(ctx->pixels, ctx->num_pixels, rgbmap, ZMK_KEYMAP_LEN,
                         layer, ctx->base_color.b)) {
            int fade_delay = zmk_rgbmap_fade_delay(layer);
            if (fade_delay >= 0) {
                zmk_rgb_set_tick_delay(fade_delay);
            }
        }
    } else {
        if (zmk_rgb_is_on()) {
            zmk_rgb_underglow_transient_off();
        }
    }
}

static void layer_effect_on_select(void) {
    layer_effect_active = true;
}

static void layer_effect_on_deselect(void) {
    layer_effect_active = false;
}

static void layer_effect_on_idle(bool awake) {
    if (awake) {
        if (layer_effect_active) {
            zmk_rgb_request_refresh_wakeup(true);
        }
    } else {
        zmk_rgb_underglow_transient_off();
    }
}

static bool layer_effect_is_active(void) {
    return layer_effect_active;
}

ZMK_RGB_EFFECT_DEFINE(effect_layer, "Layer Indicators",
                      layer_effect_render,
                      ZMK_RGB_EFFECT_STATIC | ZMK_RGB_EFFECT_PERSISTENT,
                      layer_effect_on_select, layer_effect_on_deselect,
                      layer_effect_on_idle, layer_effect_is_active);

/* Event listeners */

static int layer_effect_event_listener(const zmk_event_t *eh) {
    if (!layer_effect_active) {
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    if (as_zmk_split_peripheral_layer_changed(eh)) {
        const struct zmk_split_peripheral_layer_changed *ev =
            as_zmk_split_peripheral_layer_changed(eh);
        LOG_DBG("peripheral layer changed: %08x", ev->layers);
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        set_peripheral_layers_state(ev->layers);
#endif
        zmk_rgb_request_refresh_wakeup(true);
        return 0;
    }
#endif

    if (as_zmk_underglow_color_changed(eh)) {
        const struct zmk_underglow_color_changed *ev = as_zmk_underglow_color_changed(eh);
        uint8_t layer = rgb_underglow_top_layer();
        if ((ev->layers & BIT(layer)) == BIT(layer)) {
            zmk_rgb_request_refresh_wakeup(ev->wakeup);
        }
        return 0;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_effect, layer_effect_event_listener);

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
ZMK_SUBSCRIPTION(layer_effect, zmk_split_peripheral_layer_changed);
#endif
ZMK_SUBSCRIPTION(layer_effect, zmk_underglow_color_changed);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
