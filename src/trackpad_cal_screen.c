#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/display/widgets/output_status.h>
#include <zmk/display/widgets/peripheral_status.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/trackpad_cal_state_changed.h>
#include <zephyr/sys/slist.h>
#include <lvgl.h>

#include <trackpad_cal.h>

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
static struct zmk_widget_battery_status battery_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
static struct zmk_widget_output_status output_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
static struct zmk_widget_peripheral_status peripheral_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
static struct zmk_widget_layer_status layer_status_widget;
#endif

static sys_slist_t cal_widgets = SYS_SLIST_STATIC_INIT(&cal_widgets);

struct cal_widget {
    sys_snode_t node;
    lv_obj_t *obj;
};

static void set_cal_text(lv_obj_t *label, int16_t a) {
    int16_t signed_a = a;
    if (signed_a > 180) {
        signed_a -= 360;
    }
    /* 32px OLED: number + 8-way tick instead of a tiny unreadble compass. */
    const char *dir = ">";
    int16_t q = ((a + 22) % 360) / 45;
    switch (q) {
    case 0:
        dir = ">";
        break;
    case 1:
        dir = "/";
        break;
    case 2:
        dir = "^";
        break;
    case 3:
        dir = "\\";
        break;
    case 4:
        dir = "<";
        break;
    case 5:
        dir = "/";
        break;
    case 6:
        dir = "v";
        break;
    default:
        dir = "\\";
        break;
    }
    char text[16];
    snprintf(text, sizeof(text), "(%s)%+d", dir, signed_a);
    lv_label_set_text(label, text);
}

static void cal_update_cb(int16_t a) {
    struct cal_widget *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&cal_widgets, widget, node) { set_cal_text(widget->obj, a); }
}

static int16_t cal_get_state(const zmk_event_t *eh) {
    const struct zmk_trackpad_cal_state_changed *ev = as_zmk_trackpad_cal_state_changed(eh);
    if (ev != NULL) {
        return ev->angle;
    }
    return trackpad_cal_get_angle();
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_trackpad_cal, int16_t, cal_update_cb, cal_get_state)
ZMK_SUBSCRIPTION(widget_trackpad_cal, zmk_trackpad_cal_state_changed);

static struct cal_widget cal_status_widget;

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
    zmk_widget_battery_status_init(&battery_status_widget, screen);
    lv_obj_align(zmk_widget_battery_status_obj(&battery_status_widget), LV_ALIGN_TOP_RIGHT, 0, 0);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT, 0, 0);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
    zmk_widget_peripheral_status_init(&peripheral_status_widget, screen);
    lv_obj_align(zmk_widget_peripheral_status_obj(&peripheral_status_widget), LV_ALIGN_TOP_LEFT, 0,
                 0);
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_set_style_text_font(zmk_widget_layer_status_obj(&layer_status_widget),
                               lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_LEFT, 0, 0);
#endif

    cal_status_widget.obj = lv_label_create(screen);
    lv_obj_set_style_text_font(cal_status_widget.obj, lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(cal_status_widget.obj, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    sys_slist_append(&cal_widgets, &cal_status_widget.node);
    widget_trackpad_cal_init();

    return screen;
}
