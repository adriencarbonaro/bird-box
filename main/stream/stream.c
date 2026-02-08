#include "stream.h"
#include "audio.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "esp_log.h"
#include "esp_http_client.h"


static void stream_task(void *arg)
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

    int data_read = esp_http_client_read(client,
                                         (char*)buffer,
                                         WAV_FMT_HEADER_SKIP_SIZE);
    if (data_read != WAV_FMT_HEADER_SKIP_SIZE)
        goto end;

    char chunk_id[5] = {0};
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
        memcpy(&chunk_size, buffer + 4, 4);
    }

    while ((data_read = esp_http_client_read(client, (char*)buffer, I2S_WRITE_CHUNK)) > 0)
    {
        if (stop_playback)
        {
            ESP_LOGI(TAG, "Playback stopped");
            break;
        }

        int16_t* buf16 = (int16_t*)buffer;
        amplify_buffer(buf16, data_read / sizeof(int16_t), gain);
        xRingbufferSend(audio_rb, buffer, data_read, portMAX_DELAY);
    }

end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    /* @todo: Should notify play task rather than sending mqtt message direclty*/
    send_state("idle");

    ESP_LOGI(TAG, "Stream finished");

    stream_task_handle = NULL;
    vTaskDelete(NULL);
}
