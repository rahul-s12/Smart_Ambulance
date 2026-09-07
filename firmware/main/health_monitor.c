#include "health_monitor.h"
#include "oled_display.h"

#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA_GPIO GPIO_NUM_6
#define I2C_SCL_GPIO GPIO_NUM_7
#define I2C_FREQUENCY_HZ 100000
#define MAX30102_ADDRESS 0x57
#define DS18B20_GPIO GPIO_NUM_4
#define ECG_ADC_UNIT ADC_UNIT_1
#define ECG_ADC_CHANNEL ADC_CHANNEL_0 /* GPIO 0 on ESP32-C6 */
#define ECG_SAMPLE_INTERVAL_MS 10
#define OLED_UPDATE_INTERVAL_MS 500
#define FINGER_THRESHOLD 20000U
#define REPORT_INTERVAL_MS 1000
#define TEMP_REQUEST_INTERVAL_MS 1500
#define TEMP_CONVERSION_MS 800
#define SENSOR_RETRY_INTERVAL_MS 2000
#define HR_FILTER_SIZE 4
#define BPM_BUFFER_SIZE 5
#define SPO2_WINDOW_SIZE 25

#define REG_FIFO_WR_PTR 0x04
#define REG_FIFO_DATA 0x07
#define REG_FIFO_CONFIG 0x08
#define REG_MODE_CONFIG 0x09
#define REG_SPO2_CONFIG 0x0A
#define REG_LED1_PA 0x0C
#define REG_LED2_PA 0x0D

static const char *TAG = "HEALTH";
static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_max30102;
static bool s_i2c_initialized;
static adc_oneshot_unit_handle_t s_ecg_adc;
static int64_t s_last_ecg_sample_ms;
static int64_t s_last_oled_update_ms;
static health_vitals_t s_vitals;
static bool s_temperature_pending;
static int64_t s_temperature_requested_ms;
static int64_t s_last_temperature_request_ms;
static int64_t s_last_retry_ms;
static int64_t s_last_report_ms;

static uint32_t s_ir_filter[HR_FILTER_SIZE];
static size_t s_ir_filter_index, s_ir_filter_count;
static float s_baseline, s_pulse_1, s_pulse_2;
static uint32_t s_last_peak_ms;
static float s_bpm_buffer[BPM_BUFFER_SIZE];
static size_t s_bpm_index, s_bpm_count;
static uint32_t s_red_samples[SPO2_WINDOW_SIZE], s_ir_samples[SPO2_WINDOW_SIZE];
static size_t s_spo2_index, s_spo2_count;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static esp_err_t max_write(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_master_transmit(s_max30102, data, sizeof(data), 100);
}

static esp_err_t max_read(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(s_max30102, &reg, 1, data, length, 100);
}

static void clear_pulse_state(void)
{
    s_ir_filter_index = s_ir_filter_count = 0;
    s_baseline = s_pulse_1 = s_pulse_2 = 0.0f;
    s_last_peak_ms = 0;
    s_bpm_index = s_bpm_count = 0;
    s_spo2_index = s_spo2_count = 0;
    s_vitals.heart_rate_bpm = 0.0f;
    s_vitals.spo2_percent = 0.0f;
}

static bool init_i2c(void)
{
    if (s_i2c_initialized)
        return true;

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C bus setup failed: %s", esp_err_to_name(err));
        return false;
    }
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MAX30102_ADDRESS,
        .scl_speed_hz = I2C_FREQUENCY_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &device_config, &s_max30102);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "MAX30102 device setup failed: %s", esp_err_to_name(err));
        return false;
    }
    s_i2c_initialized = true;
    return true;
}

static bool init_max30102(void)
{
    if (!init_i2c())
        return false;
    esp_err_t err = max_write(REG_MODE_CONFIG, 0x40); /* reset */
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "MAX30102 not found: %s", esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Matches health_monitoring.ino: average=4, red+IR, 100 Hz, 411 us, 4096 nA. */
    err = max_write(REG_FIFO_WR_PTR, 0x00);
    if (err == ESP_OK)
        err = max_write(REG_FIFO_WR_PTR + 1, 0x00);
    if (err == ESP_OK)
        err = max_write(REG_FIFO_WR_PTR + 2, 0x00);
    if (err == ESP_OK)
        err = max_write(REG_FIFO_CONFIG, 0x5F);
    if (err == ESP_OK)
        err = max_write(REG_MODE_CONFIG, 0x03);
    if (err == ESP_OK)
        err = max_write(REG_SPO2_CONFIG, 0x27);
    if (err == ESP_OK)
        err = max_write(REG_LED1_PA, 0x1F);
    if (err == ESP_OK)
        err = max_write(REG_LED2_PA, 0x1F);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "MAX30102 configuration failed: %s", esp_err_to_name(err));
        return false;
    }
    s_vitals.max30102_ready = true;
    ESP_LOGI(TAG, "MAX30102 ready (SDA GPIO%d, SCL GPIO%d)", I2C_SDA_GPIO, I2C_SCL_GPIO);
    return true;
}

static void ow_write_bit(bool value)
{
    gpio_set_level(DS18B20_GPIO, 0);
    if (value)
    {
        esp_rom_delay_us(6);
        gpio_set_level(DS18B20_GPIO, 1);
        esp_rom_delay_us(64);
    }
    else
    {
        esp_rom_delay_us(60);
        gpio_set_level(DS18B20_GPIO, 1);
        esp_rom_delay_us(10);
    }
}

static bool ow_read_bit(void)
{
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(6);
    gpio_set_level(DS18B20_GPIO, 1);
    esp_rom_delay_us(9);
    bool value = gpio_get_level(DS18B20_GPIO);
    esp_rom_delay_us(55);
    return value;
}

static void ow_write_byte(uint8_t value)
{
    for (int bit = 0; bit < 8; ++bit)
        ow_write_bit((value >> bit) & 1U);
}

static uint8_t ow_read_byte(void)
{
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit)
        if (ow_read_bit())
            value |= 1U << bit;
    return value;
}

static bool ow_reset(void)
{
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(480);
    gpio_set_level(DS18B20_GPIO, 1);
    esp_rom_delay_us(70);
    bool present = !gpio_get_level(DS18B20_GPIO);
    esp_rom_delay_us(410);
    return present;
}

static uint8_t ow_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0;
    for (size_t index = 0; index < length; ++index)
    {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1U) ? ((crc >> 1) ^ 0x8CU) : (crc >> 1);
    }
    return crc;
}

static void request_temperature(void)
{
    if (!ow_reset())
    {
        s_vitals.ds18b20_ready = false;
        return;
    }
    ow_write_byte(0xCC); /* Skip ROM: one DS18B20 on the bus. */
    ow_write_byte(0x44); /* Start temperature conversion. */
    s_vitals.ds18b20_ready = true;
    s_temperature_pending = true;
    s_temperature_requested_ms = now_ms();
}

static void read_temperature(void)
{
    uint8_t scratchpad[9];
    if (!ow_reset())
    {
        s_vitals.ds18b20_ready = false;
        return;
    }
    ow_write_byte(0xCC);
    ow_write_byte(0xBE); /* Read scratchpad. */
    for (size_t index = 0; index < sizeof(scratchpad); ++index)
        scratchpad[index] = ow_read_byte();
    if (ow_crc8(scratchpad, sizeof(scratchpad)) != 0)
    {
        ESP_LOGW(TAG, "DS18B20 CRC check failed");
        return;
    }
    int16_t raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
    float temperature = raw / 16.0f;
    if (temperature >= -55.0f && temperature <= 125.0f)
    {
        s_vitals.temperature_c = temperature;
        s_vitals.ds18b20_ready = true;
    }
}

static void process_sample(uint32_t red, uint32_t ir)
{
    if (ir < FINGER_THRESHOLD)
    {
        if (s_vitals.finger_present)
            ESP_LOGI(TAG, "No finger detected");
        s_vitals.finger_present = false;
        clear_pulse_state();
        return;
    }
    if (!s_vitals.finger_present)
    {
        ESP_LOGI(TAG, "Finger detected");
        clear_pulse_state();
    }
    s_vitals.finger_present = true;

    s_ir_filter[s_ir_filter_index] = ir;
    s_ir_filter_index = (s_ir_filter_index + 1) % HR_FILTER_SIZE;
    if (s_ir_filter_count < HR_FILTER_SIZE)
        ++s_ir_filter_count;
    uint64_t filter_sum = 0;
    for (size_t index = 0; index < s_ir_filter_count; ++index)
        filter_sum += s_ir_filter[index];
    float filtered_ir = (float)filter_sum / s_ir_filter_count;
    if (s_baseline == 0.0f)
        s_baseline = filtered_ir;
    s_baseline = s_baseline * 0.99f + filtered_ir * 0.01f;
    float pulse = filtered_ir - s_baseline;

    uint32_t time_ms = (uint32_t)now_ms();
    if (s_pulse_2 < s_pulse_1 && s_pulse_1 > pulse && s_pulse_1 > 80.0f)
    {
        uint32_t interval_ms = time_ms - s_last_peak_ms;
        if (s_last_peak_ms != 0 && interval_ms >= 400 && interval_ms <= 1500)
        {
            s_bpm_buffer[s_bpm_index] = 60000.0f / interval_ms;
            s_bpm_index = (s_bpm_index + 1) % BPM_BUFFER_SIZE;
            if (s_bpm_count < BPM_BUFFER_SIZE)
                ++s_bpm_count;
        }
        s_last_peak_ms = time_ms;
    }
    s_pulse_2 = s_pulse_1;
    s_pulse_1 = pulse;
    if (s_bpm_count)
    {
        float bpm_sum = 0.0f;
        for (size_t index = 0; index < s_bpm_count; ++index)
            bpm_sum += s_bpm_buffer[index];
        s_vitals.heart_rate_bpm = bpm_sum / s_bpm_count;
    }

    s_red_samples[s_spo2_index] = red;
    s_ir_samples[s_spo2_index] = ir;
    s_spo2_index = (s_spo2_index + 1) % SPO2_WINDOW_SIZE;
    if (s_spo2_count < SPO2_WINDOW_SIZE)
        ++s_spo2_count;
    if (s_spo2_count == SPO2_WINDOW_SIZE)
    {
        uint32_t red_min = UINT32_MAX, red_max = 0, ir_min = UINT32_MAX, ir_max = 0;
        uint64_t red_sum = 0, ir_sum = 0;
        for (size_t index = 0; index < SPO2_WINDOW_SIZE; ++index)
        {
            uint32_t r = s_red_samples[index], i = s_ir_samples[index];
            red_sum += r;
            ir_sum += i;
            if (r < red_min)
                red_min = r;
            if (r > red_max)
                red_max = r;
            if (i < ir_min)
                ir_min = i;
            if (i > ir_max)
                ir_max = i;
        }
        float red_mean = (float)red_sum / SPO2_WINDOW_SIZE;
        float ir_mean = (float)ir_sum / SPO2_WINDOW_SIZE;
        if (red_mean > 0.0f && ir_mean > 0.0f && ir_max > ir_min)
        {
            /* Demonstration estimate carried over from the working monitor; it is not clinical-grade. */
            float ratio = ((float)(red_max - red_min) / red_mean) / ((float)(ir_max - ir_min) / ir_mean);
            float estimate = 110.0f - 25.0f * ratio;
            if (estimate < 0.0f)
                estimate = 0.0f;
            if (estimate > 100.0f)
                estimate = 100.0f;
            s_vitals.spo2_percent = s_vitals.spo2_percent == 0.0f ? estimate : s_vitals.spo2_percent * 0.7f + estimate * 0.3f;
        }
    }
}

static bool init_ecg_adc(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ECG_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_config, &s_ecg_adc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ECG ADC initialization failed: %s", esp_err_to_name(err));
        return false;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    err = adc_oneshot_config_channel(s_ecg_adc, ECG_ADC_CHANNEL, &channel_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ECG ADC channel configuration failed: %s", esp_err_to_name(err));
        return false;
    }
    s_vitals.ecg_ready = true;
    ESP_LOGI(TAG, "ECG ADC ready on GPIO0 (ADC1 channel 0)");
    return true;
}

static void update_ecg(void)
{
    int64_t current_ms = now_ms();
    if (!s_vitals.ecg_ready || current_ms - s_last_ecg_sample_ms < ECG_SAMPLE_INTERVAL_MS) return;
    s_last_ecg_sample_ms = current_ms;

    int raw_value = 0;
    if (adc_oneshot_read(s_ecg_adc, ECG_ADC_CHANNEL, &raw_value) == ESP_OK) {
        s_vitals.ecg_raw = raw_value;
        /* Use a serial-plotter extension; each line is one ECG sample at 100 Hz. */
        printf("ECG:%d\n", raw_value);
    } else {
        s_vitals.ecg_ready = false;
        ESP_LOGW(TAG, "ECG ADC read failed");
    }
}
static void update_max30102(void)
{
    uint8_t pointers[3];
    if (max_read(REG_FIFO_WR_PTR, pointers, sizeof(pointers)) != ESP_OK)
    {
        s_vitals.max30102_ready = false;
        return;
    }
    uint8_t available = (pointers[0] - pointers[2]) & 0x1F;
    if (available > 8)
        available = 8; /* Keep this loop responsive even after a delayed iteration. */
    while (available--)
    {
        uint8_t data[6];
        if (max_read(REG_FIFO_DATA, data, sizeof(data)) != ESP_OK)
        {
            s_vitals.max30102_ready = false;
            return;
        }
        uint32_t red = ((uint32_t)(data[0] & 0x03) << 16) | ((uint32_t)data[1] << 8) | data[2];
        uint32_t ir = ((uint32_t)(data[3] & 0x03) << 16) | ((uint32_t)data[4] << 8) | data[5];
        process_sample(red, ir);
    }
}

bool health_monitor_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << DS18B20_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    gpio_set_level(DS18B20_GPIO, 1);
    bool max_ready = init_max30102();
    init_ecg_adc();
    if (s_i2c_initialized) oled_display_init(s_i2c_bus);
    request_temperature();
    return max_ready;
}

void health_monitor_update(void)
{
    int64_t current_ms = now_ms();
    if (!s_vitals.max30102_ready && current_ms - s_last_retry_ms >= SENSOR_RETRY_INTERVAL_MS)
    {
        s_last_retry_ms = current_ms;
        init_max30102();
    }
    if (s_vitals.max30102_ready)
        update_max30102();

    update_ecg();

    if (!s_temperature_pending && current_ms - s_last_temperature_request_ms >= TEMP_REQUEST_INTERVAL_MS)
    {
        s_last_temperature_request_ms = current_ms;
        request_temperature();
    }
    if (s_temperature_pending && current_ms - s_temperature_requested_ms >= TEMP_CONVERSION_MS)
    {
        s_temperature_pending = false;
        read_temperature();
    }
    if (!oled_display_is_ready() && s_i2c_initialized) oled_display_init(s_i2c_bus);
    if (oled_display_is_ready() && current_ms - s_last_oled_update_ms >= OLED_UPDATE_INTERVAL_MS) {
        s_last_oled_update_ms = current_ms;
        oled_display_show_vitals(&s_vitals);
    }

    if (current_ms - s_last_report_ms >= REPORT_INTERVAL_MS)
    {
        s_last_report_ms = current_ms;
        ESP_LOGI(TAG, "Vitals: HR=%.1f bpm, SpO2=%.1f %%, Temp=%.2f C%s",
                 s_vitals.heart_rate_bpm, s_vitals.spo2_percent, s_vitals.temperature_c,
                 s_vitals.finger_present ? "" : " (place finger on MAX30102)");
    }
}

health_vitals_t health_monitor_get_vitals(void)
{
    return s_vitals;
}