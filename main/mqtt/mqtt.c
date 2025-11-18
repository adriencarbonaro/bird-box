#include "mqtt.h"

#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/projdefs.h"
#include "freertos/portmacro.h"
#include "utils/types.h"
#include "utils.h"
#include "wifi.h"

typedef struct {
    const char* msg;
    mqtt_event_listener_cb_t callback;
} mqtt_event_listener_t;

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_event_listener_t* event_listeners[5] = {0};

static inline bool is_birdbox_topic(esp_mqtt_event_handle_t event)
{
    return strncmp(event->topic,
                   CONFIG_BIRDBOX_MQTT_TOPIC,
                   event->topic_len) == 0;
}

static inline bool is_birdbox_message(esp_mqtt_event_handle_t event,
                                      const char* msg)
{
    return strncmp(event->data, msg, event->data_len) == 0;
}

static void mqtt_event_handler(void* event_handler_arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to server");
            ESP_LOGI(TAG, "Subscribing to topic %s", CONFIG_BIRDBOX_MQTT_TOPIC);
            esp_mqtt_client_subscribe(event->client,
                                      CONFIG_BIRDBOX_MQTT_TOPIC,
                                      0);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected from server");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Subscription success (msg_id=%d)", event->msg_id);
            break;

        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "Unsubscription success (msg_id=%d)", event->msg_id);
            break;

        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "Published, msg_id=%d", event->msg_id);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "Received message on topic %.*s: %.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            if (is_birdbox_topic(event))
            {
                for (uint16 i = 0; i < ARRAY_DIM(event_listeners); i++)
                {
                    mqtt_event_listener_t* event_listener = event_listeners[i];
                    if (event_listener != NULL)
                    {
                        if (is_birdbox_message(event, event_listener->msg))
                        {
                            event_listener->callback();
                        }
                    }
                }
            }
            break;

        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(TAG, "Before connect...");
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "Error");
            break;

        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

void mqtt_subscribe_event(const char* msg, mqtt_event_listener_cb_t callback)
{
    for (uint16 i = 0; i < ARRAY_DIM(event_listeners); i++)
    {
        if (event_listeners[i] == NULL)
        {
            ESP_LOGI(TAG, "Adding client callback for msg %s", msg);

            event_listeners[i] = malloc(sizeof(mqtt_event_listener_t));
            if (event_listeners[i] == NULL)
            {
                ESP_LOGE(TAG, "Error allocating memory");
                return;
            }

            event_listeners[i]->msg = msg;
            event_listeners[i]->callback = callback;

            return;
        }
    }
}

void mqtt_init(EventGroupHandle_t wifi_event_group)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_BIRDBOX_MQTT_URI,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 5000,
    };

    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);

    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Starting mqtt client");
        esp_mqtt_client_start(mqtt_client);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Wifi connection failed");
    } else {
        ESP_LOGE(TAG, "Wifi unexpected event");
    }

}
