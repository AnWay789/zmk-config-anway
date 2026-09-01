#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>
#include <drivers/behavior.h>
#include <drivers/input_processor.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>

#include <trackpad_cal.h>
#include <zmk/events/trackpad_cal_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(zmk_trackpad_cal_state_changed);

#define ANGLE_SCALE 1000
#define SPEED_SCALE 100
#define SPEED_MIN 25
#define SPEED_MAX 400
#define DEFAULT_ANGLE CONFIG_TRACKPAD_CAL_DEFAULT_ANGLE
#define DEFAULT_SPEED CONFIG_TRACKPAD_CAL_DEFAULT_SPEED
#define SPD_CMD_BASE 10000

static int16_t angle = DEFAULT_ANGLE;
static int16_t speed = DEFAULT_SPEED;
static int16_t sin_val;
static int16_t cos_val;

static const int16_t sin_table[] = {0,   87,  174, 259, 342, 423, 500, 574, 643, 707,
                                    766, 819, 866, 906, 940, 966, 985, 996, 1000};
static const int16_t cos_table[] = {1000, 996, 985, 966, 940, 906, 866, 819, 766, 707,
                                    643,  574, 500, 423, 342, 259, 174, 87,  0};

static void lookup_sin_cos(int a, int16_t *s, int16_t *c) {
    a %= 360;
    if (a < 0) {
        a += 360;
    }
    int index = (a % 90) / 5;
    int quadrant = a / 90;
    int16_t sb = sin_table[index];
    int16_t cb = cos_table[index];
    switch (quadrant) {
    case 0:
        *s = sb;
        *c = cb;
        break;
    case 1:
        *s = cb;
        *c = -sb;
        break;
    case 2:
        *s = -sb;
        *c = -cb;
        break;
    default:
        *s = -cb;
        *c = sb;
        break;
    }
}

static void apply_trig(void) { lookup_sin_cos(angle, &sin_val, &cos_val); }

static void raise_changed(void) {
    raise_zmk_trackpad_cal_state_changed(
        (struct zmk_trackpad_cal_state_changed){.angle = angle, .speed = speed});
}

static void save_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    settings_save_one("tp/ang", &angle, sizeof(angle));
    settings_save_one("tp/spd", &speed, sizeof(speed));
}

static K_WORK_DELAYABLE_DEFINE(save_work, save_work_cb);

static void persist(void) { k_work_reschedule(&save_work, K_MSEC(800)); }

int16_t trackpad_cal_get_angle(void) { return angle; }

int16_t trackpad_cal_get_speed(void) { return speed; }

void trackpad_cal_sincos(int16_t *s, int16_t *c) {
    *s = sin_val;
    *c = cos_val;
}

void trackpad_cal_set(int16_t a) {
    a %= 360;
    if (a < 0) {
        a += 360;
    }
    angle = a;
    apply_trig();
    raise_changed();
    persist();
}

void trackpad_cal_add(int16_t delta) { trackpad_cal_set(angle + delta); }

void trackpad_cal_speed_add(int16_t delta) {
    int16_t s = speed + delta;
    if (s < SPEED_MIN) {
        s = SPEED_MIN;
    } else if (s > SPEED_MAX) {
        s = SPEED_MAX;
    }
    speed = s;
    raise_changed();
    persist();
}

void trackpad_cal_reset(void) {
    speed = DEFAULT_SPEED;
    trackpad_cal_set(DEFAULT_ANGLE);
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int cal_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    const char *next;
    if (settings_name_steq(name, "ang", &next) && !next) {
        if (len != sizeof(angle)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, &angle, sizeof(angle));
        if (rc >= 0) {
            apply_trig();
            raise_changed();
        }
        return MIN(rc, 0);
    }
    if (settings_name_steq(name, "spd", &next) && !next) {
        if (len != sizeof(speed)) {
            return -EINVAL;
        }
        int rc = read_cb(cb_arg, &speed, sizeof(speed));
        if (rc >= 0) {
            if (speed < SPEED_MIN) {
                speed = SPEED_MIN;
            } else if (speed > SPEED_MAX) {
                speed = SPEED_MAX;
            }
            raise_changed();
        }
        return MIN(rc, 0);
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(tp, "tp", NULL, cal_settings_set, NULL, NULL);
#endif

static int cal_init(void) {
    apply_trig();
    return 0;
}

SYS_INIT(cal_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#define DT_DRV_COMPAT zmk_input_processor_trackpad_rotate

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct tpr_data {
    int32_t x;
    int32_t y;
    int32_t rem_x;
    int32_t rem_y;
};

static int32_t scale_axis(int32_t value, int32_t *remainder) {
    int32_t mul = value * speed + *remainder;
    int32_t out = mul / SPEED_SCALE;
    *remainder = mul - out * SPEED_SCALE;
    return out;
}

static int tpr_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                            uint32_t param2, struct zmk_input_processor_state *state) {
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);
    struct tpr_data *data = dev->data;

    if (event->type != INPUT_EV_REL) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (event->code == INPUT_REL_X) {
        int32_t x = event->value;
        int32_t y = data->y;
        data->x = event->value;
        /* rotate-90 + extra tilt currently mirrors screen X; flip it back. */
        event->value = scale_axis(-((x * cos_val - y * sin_val) / ANGLE_SCALE), &data->rem_x);
    } else if (event->code == INPUT_REL_Y) {
        int32_t x = data->x;
        int32_t y = event->value;
        data->y = event->value;
        event->value = scale_axis((x * sin_val + y * cos_val) / ANGLE_SCALE, &data->rem_y);
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api tpr_api = {.handle_event = tpr_handle_event};

#define TPR_INST(n)                                                                                \
    static struct tpr_data tpr_data_##n;                                                           \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &tpr_data_##n, NULL, POST_KERNEL,                         \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tpr_api);

DT_INST_FOREACH_STATUS_OKAY(TPR_INST)

#endif

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT zmk_behavior_trackpad_rotate

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    int32_t v = (int32_t)binding->param1;
    if (v == 0) {
        trackpad_cal_reset();
    } else if (v >= 9000 && v <= 11000) {
        trackpad_cal_speed_add((int16_t)(v - SPD_CMD_BASE));
    } else {
        trackpad_cal_add((int16_t)v);
    }
    return 0;
}

static const struct behavior_driver_api tp_rot_api = {
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
    .binding_pressed = on_pressed,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &tp_rot_api);

#endif
