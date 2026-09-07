#pragma once

#include <stdbool.h>

typedef struct
{
    float heart_rate_bpm;
    float spo2_percent;
    float temperature_c;
    int ecg_raw;
    bool finger_present;
    bool max30102_ready;
    bool ds18b20_ready;
    bool ecg_ready;
} health_vitals_t;

typedef health_vitals_t vitals;

/* Starts MAX30102, DS18B20, ECG ADC, and the OLED. Sensor failures are retried. */
bool health_monitor_init(void);

/* Call frequently from the application loop to service sensors and display updates. */
void health_monitor_update(void);

health_vitals_t health_monitor_get_vitals(void);