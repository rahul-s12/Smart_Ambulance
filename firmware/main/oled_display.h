#pragma once

#include <stdbool.h>

#include "driver/i2c_master.h"
#include "health_monitor.h"

bool oled_display_init(i2c_master_bus_handle_t i2c_bus);
bool oled_display_is_ready(void);
void oled_display_show_vitals(const health_vitals_t *vitals);