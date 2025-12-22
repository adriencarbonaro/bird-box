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
#include "version.h"
#include "wifi.h"

typedef struct {
    mqtt_event_listener_cb_t callback;
} mqtt_event_listener_t;

static const char *TAG = "mqtt";
static esp_mqtt_client_handle_t mqtt_client = NULL;
static mqtt_event_listener_t* event_listeners[5] = {0};

static mqtt_task_data_t mqtt_task;

static inline bool is_configured_topic(esp_mqtt_event_handle_t event)
{
    for (uint16 i = 0; i < MAX_NB_TOPICS; i++)
    {
        if (strncmp(event->topic, mqtt_task.topics[i], event->topic_len) == 0)
        {
            return 1;
        }
    }
    return 0;
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
            mqtt_task.is_connected = true;

            for (uint16 i = 0; i < MAX_NB_TOPICS; i++)
            {
                if (mqtt_task.topics[i] == NULL)
                    continue;

                ESP_LOGI(TAG, "Subscribing to topic %s", mqtt_task.topics[i]);
                esp_mqtt_client_subscribe(event->client,
                                          mqtt_task.topics[i],
                                          0);
            }

            send_version();

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
            if (!is_configured_topic(event))
                return;

            ESP_LOGI(TAG, "Received message on topic %.*s: %.*s",
                     event->topic_len, event->topic,
                     event->data_len, event->data);

            for (uint16 i = 0; i < ARRAY_DIM(event_listeners); i++)
            {
                mqtt_event_listener_t* event_listener = event_listeners[i];
                if (event_listener != NULL)
                {
                    event_listener->callback(event->topic,
                                             event->topic_len,
                                             event->data,
                                             event->data_len);
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

static void publish(const char* topic, const char* msg)
{
    if (mqtt_client == NULL)
        return;

    esp_mqtt_client_publish(mqtt_client, topic, msg, 0, 0, 0);
}

static void send_version(void)
{
    publish(CONFIG_BIRDBOX_MQTT_TOPIC_VERSION, VERSION);
}

void mqtt_subscribe_topic(const char* topic)
{
    for (uint16 i = 0; i < MAX_NB_TOPICS; i++)
    {
        if (mqtt_task.topics[i] == NULL)
        {
            mqtt_task.topics[i] = topic;
            return;
        }
    }
}

void mqtt_subscribe_listener(mqtt_event_listener_cb_t callback)
{
    for (uint16 i = 0; i < ARRAY_DIM(event_listeners); i++)
    {
        if (event_listeners[i] == NULL)
        {
            ESP_LOGI(TAG, "Adding mqtt msg client callback");

            event_listeners[i] = malloc(sizeof(mqtt_event_listener_t));
            if (event_listeners[i] == NULL)
            {
                ESP_LOGE(TAG, "Error allocating memory");
                return;
            }

            event_listeners[i]->callback = callback;

            return;
        }
    }
}

void send_status(const char* status_str)
{
    if (mqtt_client == NULL)
        return;

    esp_mqtt_client_publish(mqtt_client, CONFIG_BIRDBOX_MQTT_TOPIC_STATE, status_str, 0, 0, 0);
}

void mqtt_init()
{
    memset(&mqtt_task, 0, sizeof(mqtt_task_data_t));
}

void mqtt_start(EventGroupHandle_t wifi_event_group)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_BIRDBOX_MQTT_URI,
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 5000,
    };

    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client,
                                   ESP_EVENT_ANY_ID,
                                   mqtt_event_handler,
                                   mqtt_client);

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
