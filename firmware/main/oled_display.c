#include "oled_display.h"

#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#define SH1106_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)
#define OLED_COLUMN_OFFSET 2

static const char *TAG = "OLED";
static i2c_master_dev_handle_t s_oled;
static bool s_ready;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_PAGES];

static esp_err_t oled_command(uint8_t command)
{
    const uint8_t data[] = {0x00, command};
    return i2c_master_transmit(s_oled, data, sizeof(data), 100);
}

static void set_pixel(uint8_t x, uint8_t y)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) return;
    s_framebuffer[x + (y / 8) * OLED_WIDTH] |= (uint8_t)(1U << (y % 8));
}

static const uint8_t *glyph(char character)
{
    static const uint8_t digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
        {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}
    };
    static const uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t space[5] = {0,0,0,0,0};
    static const uint8_t colon[5] = {0,0x36,0x36,0,0};
    static const uint8_t dot[5] = {0,0x60,0x60,0,0};
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};

    if (character >= '0' && character <= '9') return digits[character - '0'];
    if (character >= 'A' && character <= 'Z') return letters[character - 'A'];
    if (character == ':') return colon;
    if (character == '.') return dot;
    if (character == '-') return dash;
    return space;
}

static void draw_text(uint8_t x, uint8_t y, const char *text)
{
    while (*text && x + 5 < OLED_WIDTH) {
        const uint8_t *bitmap = glyph(*text++);
        for (uint8_t column = 0; column < 5; ++column) {
            for (uint8_t row = 0; row < 7; ++row) {
                if (bitmap[column] & (1U << row)) set_pixel(x + column, y + row);
            }
        }
        x += 6;
    }
}

static void flush_framebuffer(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; ++page) {
        oled_command(0xB0 | page);
        oled_command(0x00 | (OLED_COLUMN_OFFSET & 0x0F));
        oled_command(0x10 | (OLED_COLUMN_OFFSET >> 4));
        uint8_t data[OLED_WIDTH + 1];
        data[0] = 0x40;
        memcpy(&data[1], &s_framebuffer[page * OLED_WIDTH], OLED_WIDTH);
        if (i2c_master_transmit(s_oled, data, sizeof(data), 100) != ESP_OK) {
            s_ready = false;
            ESP_LOGW(TAG, "Display write failed");
            return;
        }
    }
}

bool oled_display_init(i2c_master_bus_handle_t i2c_bus)
{
    if (s_ready) return true;

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SH1106_ADDRESS,
        .scl_speed_hz = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(i2c_bus, &config, &s_oled);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SH1106 not detected at 0x%02X: %s", SH1106_ADDRESS, esp_err_to_name(err));
        return false;
    }

    const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0xAD, 0x8B, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0x80,
        0xD9, 0x22, 0xDB, 0x35, 0xA4, 0xA6, 0xAF
    };
    for (size_t index = 0; index < sizeof(init_commands); ++index) {
        if (oled_command(init_commands[index]) != ESP_OK) {
            ESP_LOGW(TAG, "SH1106 initialization failed");
            return false;
        }
    }
    s_ready = true;
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    draw_text(0, 26, "INITIALIZING");
    flush_framebuffer();
    ESP_LOGI(TAG, "SH1106 OLED ready at 0x%02X", SH1106_ADDRESS);
    return true;
}

bool oled_display_is_ready(void)
{
    return s_ready;
}

void oled_display_show_vitals(const health_vitals_t *vitals)
{
    if (!s_ready || vitals == NULL) return;
    char line[22];
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    draw_text(0, 0, "SMART AMBULANCE");
    if (vitals->heart_rate_bpm > 0.0f) snprintf(line, sizeof(line), "HR: %.0f BPM", vitals->heart_rate_bpm);
    else snprintf(line, sizeof(line), "HR: -- BPM");
    draw_text(0, 16, line);
    if (vitals->spo2_percent > 0.0f) snprintf(line, sizeof(line), "SPO2: %.0f%%", vitals->spo2_percent);
    else snprintf(line, sizeof(line), "SPO2: --%%");
    draw_text(0, 31, line);
    if (vitals->ds18b20_ready) snprintf(line, sizeof(line), "TEMP: %.1f C", vitals->temperature_c);
    else snprintf(line, sizeof(line), "TEMP: -- C");
    draw_text(0, 46, line);
    flush_framebuffer();
}