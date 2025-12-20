#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/projdefs.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include <string.h>

#include "config.h"
#include "mqtt.h"
#include "utils.h"
#include "utils/types.h"
#include "wifi.h"

#define WAV_FMT_HEADER_OFFSET           (20)
#define WAV_FMT_HEADER_SIZE             (16)
#define WAV_FMT_HEADER_SKIP_SIZE        (WAV_FMT_HEADER_OFFSET + WAV_FMT_HEADER_SIZE)

typedef enum {
    CMD_PLAY,
    CMD_PAUSE,
    CMD_VOLUME,
} cmd_t;

typedef struct {
    cmd_t type;
    float volume;
} task_cmd_t;

const char* play_status[] = {
    [CMD_PLAY] = "play",
    [CMD_PAUSE] = "pause",
};

static const char* topics[] =
{
    CONFIG_BIRDBOX_MQTT_TOPIC_SET,
    CONFIG_BIRDBOX_MQTT_TOPIC_VOLUME,
    CONFIG_BIRDBOX_MQTT_TOPIC_STATE,
};

void play_task(void *arg);

static QueueHandle_t play_cmd_queue = NULL;
static i2s_chan_handle_t tx_chan;
static EventGroupHandle_t s_wifi_event_group;
static esp_http_client_handle_t client = NULL;

static TaskHandle_t stream_task_handle = NULL;
static volatile bool stop_playback = false;
static volatile float gain = AMPLIFY_GAIN;

static const char *TAG = "birdbox";

static uint8 buffer[I2S_BUFFER_SIZE];

void amplify_buffer(int16_t *data, size_t len, float gain)
{
    for (size_t i = 0; i < len; i++)
    {
        int32_t sample = (int32_t)(data[i] * gain);
        if (sample > INT16_MAX) sample = INT16_MAX;
        else if (sample < INT16_MIN) sample = INT16_MIN;
        data[i] = (int16_t)sample;
    }
}

static void i2s_write(const uint8* data, const uint16 data_len, size_t* written)
{
    if (i2s_channel_write(tx_chan, data, data_len, written, 1000))
    {
        ESP_LOGI(TAG, "Write Task: i2s write failed");
    }
}

static void i2s_clean(void)
{
    memset(buffer, 0, I2S_BUFFER_SIZE);
    i2s_write(buffer, I2S_BUFFER_SIZE, NULL);
}

void stop_wav_playback(void)
{
    if (!client)
        return;

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    i2s_clean();
}

static void i2s_init(void)
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws   = I2S_LRCLK,
            .dout = I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void stream_task(void *arg)
{
    esp_http_client_config_t config = {
        .url = WAV_SERVER_URL,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Streaming from %s", config.url);

    esp_http_client_fetch_headers(client);

    int data_read = esp_http_client_read(client, (char*)buffer, WAV_FMT_HEADER_SKIP_SIZE);
    if (data_read != WAV_FMT_HEADER_SKIP_SIZE)
        goto end;

    char chunk_id[5];
    uint32_t chunk_size = 0;
    while (strcmp(chunk_id, "data") != 0)
    {
        if (chunk_size != 0)
        {
            data_read = esp_http_client_read(client, (char*)buffer, chunk_size);
            if (data_read != chunk_size)
                goto end;
        }
        data_read = esp_http_client_read(client, (char*)buffer, 8);
        if (data_read != 8)
            goto end;

        memcpy(chunk_id, buffer, 4);
        chunk_id[4] = '\0';
        memcpy(&chunk_size, buffer + 4, 4);
    }

    while ((data_read = esp_http_client_read(client, (char*)buffer, I2S_BUFFER_SIZE)) > 0)
    {
        if (stop_playback)
        {
            ESP_LOGI(TAG, "Playback stopped");
            break;
        }

        int16_t* buf16 = (int16_t*)buffer;
        amplify_buffer(buf16, I2S_BUFFER_SIZE / 2, gain);
        i2s_write(buffer, data_read, NULL);
    }

end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    i2s_clean();
    send_status("pause");
    ESP_LOGI(TAG, "Stream finished");

    stream_task_handle = NULL;
    vTaskDelete(NULL);
}

void play_task(void *arg)
{
    task_cmd_t cmd;

    while (xQueueReceive(play_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE)
    {
        switch (cmd.type)
        {
            case CMD_PLAY:
                if (stream_task_handle == NULL) // only start if not already running
                {
                    stop_playback = false;
                    xTaskCreate(stream_task, "stream_task", 8192, NULL, 5, &stream_task_handle);
                }
                send_status("play");
                break;

            case CMD_PAUSE:
                if (stream_task_handle != NULL)
                {
                    stop_playback = true;   // signal the stream task to stop
                }
                send_status("pause");
                break;

            case CMD_VOLUME:

                gain = cmd.volume;
                ESP_LOGI(TAG, "Volume changed: %.2f", gain);
                break;

            default:
                break;
        }
    }
}

void on_msg(const char* topic,
            int topic_len,
            const char* msg,
            int msg_len)
{
    task_cmd_t event;
    memset(&event, 0, sizeof(event));

    if (strncmp(topic, CONFIG_BIRDBOX_MQTT_TOPIC_SET, topic_len) == 0)
    {
        if (strncmp(msg, "play", msg_len) == 0)
        {
            event.type = CMD_PLAY;
        }
        else if (strncmp(msg, "pause", msg_len) == 0)
        {
            event.type = CMD_PAUSE;
        }
        else
        {
            return;
        }
    }
    else if (strncmp(topic, CONFIG_BIRDBOX_MQTT_TOPIC_VOLUME, topic_len) == 0)
    {
        event.type = CMD_VOLUME;

        // copy payload into null-terminated buffer
        char buf[16]; // enough for "255.00" etc
        int copy_len = MIN(msg_len, sizeof(buf) - 1);
        memcpy(buf, msg, copy_len);
        buf[copy_len] = '\0';

        event.volume = atof(buf);
        ESP_LOGI(TAG, "Parsed volume: %.2f", event.volume);
    }
    else
    {
        return;
    }

    // Send the populated object to the queue
    xQueueSend(play_cmd_queue, &event, 0);
}

void app_main(void)
{
    i2s_init();

    s_wifi_event_group = xEventGroupCreate();
    wifi_init(s_wifi_event_group);

    mqtt_init();
    for (uint16 i = 0; i < ARRAY_DIM(topics); i++)
    {
        mqtt_subscribe_topic(topics[i]);
    }
    mqtt_subscribe_listener(on_msg);
    mqtt_start(s_wifi_event_group);

    play_cmd_queue = xQueueCreate(8, sizeof(task_cmd_t));
    xTaskCreate(play_task, "play_task", 4096, NULL, 5, NULL);
}
