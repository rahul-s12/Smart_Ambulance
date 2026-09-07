#include <stdbool.h>
#include <stdio.h>

#include "certificates.h"
#include "configuration.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "health_monitor.h"
#include "mqtt_client.h"

static esp_mqtt_client_handle_t s_client;
static bool s_connected;

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id, void *data)
{
    (void)args;
    (void)base;
    (void)data;
    if (id == MQTT_EVENT_CONNECTED)
    {
        s_connected = true;
        ESP_LOGI("MQTT", "Connected to MQTT broker");
    }
    else if (id == MQTT_EVENT_DISCONNECTED)
    {
        s_connected = false;
        ESP_LOGW("MQTT", "Disconnected; ESP-MQTT will retry");
    }
    else if (id == MQTT_EVENT_ERROR)
    {
        ESP_LOGE("MQTT", "MQTT/TLS connection error");
    }
}

void mqtt_start(void)
{
    esp_mqtt_client_config_t config = {0};
    config.broker.address.hostname = MQTT_BROKER;
    config.broker.address.port = MQTT_PORT;
    config.broker.address.transport = MQTT_TRANSPORT_OVER_SSL;
    config.broker.verification.certificate = ROOT_CA;
    config.credentials.username = MQTT_USERNAME;
    config.credentials.authentication.password = MQTT_PASSWORD;
    config.credentials.client_id = CLIENT_ID;

    s_client = esp_mqtt_client_init(&config);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
}

void mqtt_publish_vitals(void)
{
    if (!s_connected)
    {
        ESP_LOGW("MQTT", "Skipping publish: MQTT is not connected");
        return;
    }

    health_vitals_t vitals = health_monitor_get_vitals();
    char payload[160];
    int length = snprintf(payload, sizeof(payload),
                          "{\"patientId\":\"P001\",\"ambulanceId\":\"AMB001\",\"heartRate\":%.1f,\"spo2\":%.1f,\"temperature\":%.2f}",
                          vitals.heart_rate_bpm, vitals.spo2_percent, vitals.temperature_c);

    if (length < 0 || length >= sizeof(payload))
    {
        ESP_LOGE("MQTT", "Unable to build JSON payload");
        return;
    }

    int message_id = esp_mqtt_client_publish(s_client, TOPIC_VITALS, payload, length, 0, 0);
    ESP_LOGI("MQTT", "Published message id=%d: %s", message_id, payload);
}
