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
} cmd_t;

void play_task(void *arg);

static TaskHandle_t play_task_handle = NULL;
static i2s_chan_handle_t tx_chan;
static EventGroupHandle_t s_wifi_event_group;

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

}

void stream_wav_over_http(const char *url)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return;
    }

    ESP_LOGI(TAG, "Streaming from %s", url);

    esp_http_client_fetch_headers(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "Server response content-length: %lld", content_length);
    if (content_length == 0)
        goto end;

    vTaskDelay(pdMS_TO_TICKS(100));

    int data_read = esp_http_client_read(client, (char*)buffer, WAV_FMT_HEADER_SKIP_SIZE);
    if (data_read != WAV_FMT_HEADER_SKIP_SIZE)
        goto end;

    char chunk_id[5];
    uint32_t chunk_size = 0;
    while (strcmp(chunk_id, "data") != 0)
    {
        if (chunk_size != 0)
        {
            // flush chunk
            data_read = esp_http_client_read(client, (char*)buffer, chunk_size);
            if (data_read != chunk_size)
                goto end;
        }
        data_read = esp_http_client_read(client, (char*)buffer, 8);
        if (data_read != 8)
            goto end;

        memcpy(chunk_id, buffer, 4);
        chunk_id[4] = '\0';
        memcpy(&chunk_size, buffer + 4, 4); // Little endian
    }

    while ((data_read = esp_http_client_read(client, (char*)buffer, I2S_BUFFER_SIZE)) > 0)
    {
        int16_t* buf16 = (int16_t*)buffer;
        amplify_buffer(buf16, (I2S_BUFFER_SIZE / 2), AMPLIFY_GAIN * 1.0f);
        i2s_write(buffer, data_read, NULL);
    }

end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    i2s_clean();
    ESP_LOGI(TAG, "Stream finished");
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

void play_task(void *arg)
{
    while (1)
    {
        // Wait indefinitely for either START or STOP
        uint32_t cmd = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (cmd == CMD_PLAY)
        {
            ESP_LOGI(TAG, "Starting playback (url=%s)...", WAV_SERVER_URL);
            stream_wav_over_http(WAV_SERVER_URL);
        }
        else if (cmd == CMD_PAUSE)
        {
            stop_wav_playback();
        }
    }
}

void on_play(void)
{
    xTaskNotify(play_task_handle, CMD_PLAY, eSetValueWithOverwrite);
}

void on_pause(void)
{
    xTaskNotify(play_task_handle, CMD_PAUSE, eSetValueWithOverwrite);
}

void app_main(void)
{
    i2s_init();

    s_wifi_event_group = xEventGroupCreate();
    wifi_init(s_wifi_event_group);

    xTaskCreate(play_task, "play_task", 4096, NULL, 5, &play_task_handle);

    mqtt_init(s_wifi_event_group);
    mqtt_subscribe_event(MQTT_MSG_PLAY, on_play);
    mqtt_subscribe_event(MQTT_MSG_PAUSE, on_pause);
}
