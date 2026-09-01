#pragma once

#include <stdint.h>

int16_t trackpad_cal_get_angle(void);
int16_t trackpad_cal_get_speed(void);
void trackpad_cal_add(int16_t delta);
void trackpad_cal_speed_add(int16_t delta);
void trackpad_cal_reset(void);
void trackpad_cal_sincos(int16_t *sin_val, int16_t *cos_val);
