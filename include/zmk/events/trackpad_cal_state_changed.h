#pragma once

#include <zmk/event_manager.h>
#include <stdint.h>

struct zmk_trackpad_cal_state_changed {
    int16_t angle;
};

ZMK_EVENT_DECLARE(zmk_trackpad_cal_state_changed);
