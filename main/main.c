#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/projdefs.h"
#include "freertos/portmacro.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include <string.h>

#include "audio.h"
#include "config.h"
#include "mp3.h"
#include "mqtt.h"
#include "stream.h"
#include "supervisor.h"
#include "utils.h"
#include "utils/types.h"
#include "wifi.h"

RingbufHandle_t mp3_rb = NULL;
RingbufHandle_t pcm_rb = NULL;

/* Prototypes *****************************************************************/

static void on_set    (const char* msg, uint16 msg_len, task_cmd_t* event);
static void on_volume (const char* msg, uint16 msg_len, task_cmd_t* event);

/* Global pointers ************************************************************/

static const char *TAG = "main";

/* Structs - Enums ************************************************************/

typedef struct {
    const char* topic;
    void (*handler)(const char* msg, uint16 msg_len, task_cmd_t* event);
} mqtt_config_t;

/* Global objects *************************************************************/

static const mqtt_config_t mqtt_topic_config[] = {
    { MQTT_TOPIC_SET,    on_set },
    { MQTT_TOPIC_VOLUME, on_volume },
};

/* Static functions ***********************************************************/

static int is_topic(const char* config_topic,
                    const char* topic,
                    uint16 topic_len)
{
    return strncmp(config_topic, topic, topic_len) == 0;
}

static void on_msg(const char* topic,
                   int topic_len,
                   const char* msg,
                   int msg_len)
{
    task_cmd_t event;
    memset(&event, 0, sizeof(event));

    for (uint16 i = 0; i < ARRAY_DIM(mqtt_topic_config); i++)
    {
        mqtt_config_t mqtt_topic_config_item = mqtt_topic_config[i];
        if (is_topic(mqtt_topic_config_item.topic, topic, topic_len))
        {
            mqtt_topic_config_item.handler(msg, msg_len, &event);
            break;
        }
    }

    supervisor_event(&event);
}

static void configure_subscriptions(void)
{
    for (uint16 i = 0; i < ARRAY_DIM(mqtt_topic_config); i++)
    {
        mqtt_subscribe_topic(mqtt_topic_config[i].topic);
    }
    mqtt_subscribe_listener(on_msg);
}

static void on_set(const char* msg, uint16 msg_len, task_cmd_t* event)
{
    memset(event, 0, sizeof(event));

    if (strncmp(msg, MQTT_MSG_PLAY, msg_len) == 0)
    {
        event->type = CMD_PLAY;
    }
    else if (strncmp(msg, MQTT_MSG_PAUSE, msg_len) == 0)
    {
        event->type = CMD_PAUSE;
    }
    else
        return;
}

static void on_volume(const char* msg, uint16 msg_len, task_cmd_t* event)
{
    memset(event, 0, sizeof(event));

    event->type = CMD_VOLUME;

    // copy payload into null-terminated buffer
    char buf[16];
    int copy_len = MIN(msg_len, sizeof(buf) - 1);
    memcpy(buf, msg, copy_len);
    buf[copy_len] = '\0';

    event->volume = atof(buf);
    ESP_LOGI(TAG, "Parsed volume: %.2f", event->volume);
}

void app_main(void)
{
    mp3_rb = xRingbufferCreate(MP3_RB_SIZE, RINGBUF_TYPE_BYTEBUF);
    pcm_rb = xRingbufferCreate(PCM_RINGBUF_SIZEBYTES, RINGBUF_TYPE_BYTEBUF);
    assert(mp3_rb);
    assert(pcm_rb);

    /* Wifi driver */
    EventGroupHandle_t s_wifi_event_group = xEventGroupCreate();
    wifi_init(s_wifi_event_group);

    /* MQTT driver */
    mqtt_init();
    configure_subscriptions();
    mqtt_start(s_wifi_event_group);

    /* Audio tasks */
    stream_init();
    audio_init();
    mp3_decode_init();
    supervisor_init();
}
