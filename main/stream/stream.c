#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "config.h"
#include "mqtt.h"

extern RingbufHandle_t mp3_rb;
#define HTTP_READ_CHUNK 1024

static const char* TAG = "stream";
static volatile bool stop_playback = false;

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
        vTaskDelete(NULL);
        return;
    }

    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Streaming MP3 from %s", config.url);

    esp_http_client_fetch_headers(client);

    while (1)
    {
        if (stop_playback)
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
    esp_http_client_cleanup(client);

    vTaskDelete(NULL);
}

void stream_stop(void)
{
    stop_playback = true;
}

void stream_start(void)
{
    stop_playback = false;
    xTaskCreatePinnedToCore(stream_task, "stream_task", 8192, NULL, 2, NULL, 1);
}
