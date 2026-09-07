#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "health_monitor.h"

esp_err_t wifi_connect(void);
void mqtt_start(void);
void mqtt_publish_vitals(void);

void app_main(void)
{
    ESP_LOGI("MAIN", "Starting native ESP-IDF Smart Ambulance firmware");
    health_monitor_init();
    ESP_ERROR_CHECK(wifi_connect());
    mqtt_start();

    int64_t last_publish_ms = 0;
    while (true)
    {
        health_monitor_update();

        int64_t current_ms = esp_timer_get_time() / 1000;
        if (current_ms - last_publish_ms >= 1000)
        {
            mqtt_publish_vitals();
            last_publish_ms = current_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}