#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "audio.h"
#include "config.h"
#include "mqtt.h"
#include "mp3.h"

extern RingbufHandle_t mp3_rb;
#define HTTP_READ_CHUNK 1024

static const char* TAG = "stream";
static volatile bool stop_playback = false;
static EventGroupHandle_t stream_event_group = NULL;

static void stream_task(void *arg)
{
    uint8_t buffer[HTTP_READ_CHUNK];

    esp_http_client_config_t config = {
        .url = MP3_SERVER_URL,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return;
    }

    while (1)
    {

        xEventGroupWaitBits(stream_event_group,
            1,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

        if (esp_http_client_open(client, 0) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to open HTTP connection");
            esp_http_client_cleanup(client);
            return;
        }

        ESP_LOGI(TAG, "Streaming MP3 from %s", config.url);

        esp_http_client_fetch_headers(client);

        while (1)
        {
            if (xEventGroupGetBits(stream_event_group) & 2)
            {
                ESP_LOGI(TAG, "Playback stopped");
                break;
            }

            int bytes_read = esp_http_client_read(client, (char*)buffer, HTTP_READ_CHUNK);

            if (bytes_read < 0)
            {
                ESP_LOGE(TAG, "HTTP read error");
                break;
            }
            else if (bytes_read == 0)
            {
                ESP_LOGI(TAG, "Stream ended");
                break;
            }

            xRingbufferSend(mp3_rb, buffer, bytes_read, portMAX_DELAY);
        }

        /* @todo: Should notify play task rather than sending mqtt message direclty*/
        send_state("idle");
        ESP_LOGI(TAG, "Closing HTTP client");
        esp_http_client_close(client);
        mp3_decode_stop();
        audio_stop();
    }
}

void stream_stop(void)
{
    xEventGroupSetBits(stream_event_group, 2);
}

void stream_start(void)
{
    xEventGroupClearBits(stream_event_group, 2);
    xEventGroupSetBits(stream_event_group, 1);
}

void stream_init(void)
{
    stream_event_group = xEventGroupCreate();
    xTaskCreatePinnedToCore(stream_task, "stream_task", 8192, NULL, 3, NULL, 1);
}
