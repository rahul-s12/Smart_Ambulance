#include <stdio.h>

#include "configuration.h"
#include "esp_check.h"  //error-handling macro
#include "esp_event.h"
#include "esp_log.h"    
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void *args, esp_event_base_t base, int32_t id, void *data)
{
    (void)args;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *disconnect = (wifi_event_sta_disconnected_t *)data;
        ESP_LOGW("WIFI", "Disconnected (reason=%d); retrying", disconnect->reason);
        esp_wifi_connect();
    }
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI("WIFI", "Connected; IP=" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t initialise_nvs(void)
{
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), "WIFI", "NVS erase failed");
        result = nvs_flash_init();
    }
    return result;
}

esp_err_t wifi_connect(void)
{
    ESP_RETURN_ON_ERROR(initialise_nvs(), "WIFI", "NVS initialization failed");
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL)
        return ESP_ERR_NO_MEM;

    ESP_RETURN_ON_ERROR(esp_netif_init(), "WIFI", "Network initialization failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), "WIFI", "Event loop initialization failed");
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), "WIFI", "Wi-Fi initialization failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL), "WIFI", "Wi-Fi handler registration failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL), "WIFI", "IP handler registration failed");

    wifi_config_t config = {0};
    snprintf((char *)config.sta.ssid, sizeof(config.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)config.sta.password, sizeof(config.sta.password), "%s", WIFI_PASSWORD);
    config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), "WIFI", "Unable to set station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &config), "WIFI", "Unable to set configuration");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), "WIFI", "Unable to start Wi-Fi");

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    return ESP_OK;
}
