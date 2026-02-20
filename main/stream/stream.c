#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "config.h"
#include "mqtt.h"
#include "supervisor.h"

extern RingbufHandle_t mp3_rb;
#define HTTP_READ_CHUNK 1024

static const char* TAG = "stream";
static volatile bool stop_playback = false;
static uint8_t buffer[HTTP_READ_CHUNK];

TaskHandle_t stream_task_handle = NULL;
extern TaskHandle_t supervisor_task_handle;

EventGroupHandle_t task_stop_event_group = NULL;

static void stop(esp_http_client_handle_t client)
{
    esp_http_client_close(client);

    xTaskNotify(supervisor_task_handle, 1, eSetValueWithOverwrite);
}

static bool check_supervisor_stop(esp_http_client_handle_t client)
{
    uint32_t cmd = 0;
    if (xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, 0) == pdTRUE)
    {
        if (cmd == 2)
        {
            ESP_LOGI(TAG, "task stopped by supervisor");
            stop(client);
            return true;
        }
    }
    return false;
}

static void stream_task(void *arg)
{
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
        /* Wait for start notification */
        ESP_LOGI(TAG, "Waiting for start command");
        uint32_t cmd;
        xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, portMAX_DELAY);
        if (cmd == 2)
        {
            ESP_LOGI(TAG, "task (waiting for start) stopped by supervisor");
            stop(client);
            continue;
        }
        else if (cmd != 1) continue;
        ESP_LOGI(TAG, "task starts");

        if (esp_http_client_open(client, 0) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to open HTTP connection");
            esp_http_client_close(client);
            xEventGroupSetBits(task_stop_event_group,
                               BIT(STOP_REASON_STREAM_OPEN_FAILED));
            continue;
        }

        ESP_LOGI(TAG, "Streaming MP3 from %s", config.url);

        esp_http_client_fetch_headers(client);

        while (1)
        {
            if (check_supervisor_stop(client)) break;

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
    }
}

void stream_init(EventGroupHandle_t stop_event_group)
{
    task_stop_event_group = stop_event_group;

    xTaskCreatePinnedToCore(stream_task,
        "stream_task",
        16384,
        NULL,
        3,
        &stream_task_handle,
        1);
}
