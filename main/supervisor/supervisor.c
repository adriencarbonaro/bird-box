#include "supervisor.h"

#include "audio.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "types.h"
#include "mp3.h"
#include "mqtt.h"
#include "stream.h"

/* Global objects *************************************************************/
static QueueHandle_t supervisor_cmd_queue = NULL;
TaskHandle_t supervisor_task_handle = NULL;

extern TaskHandle_t stream_task_handle;
extern TaskHandle_t mp3_task_handle;
extern TaskHandle_t audio_task_handle;

static EventGroupHandle_t stream_task_event_group = NULL;

extern RingbufHandle_t mp3_rb;
extern RingbufHandle_t pcm_rb;

static const char* TAG = "supervisor_task";

static const char* state_str_list[] = {
    [STATE_IDLE] = "idle",
    [STATE_PLAYING] = "playing",
};

/* Static functions ***********************************************************/
static void update_state(state_t state)
{
    send_state(state_str_list[state]);
}

void flush_ringbuffer(RingbufHandle_t rb)
{
    if (rb == NULL) return;

    size_t bytes_written;
    void *item;
    while ((item = xRingbufferReceive(rb, &bytes_written, 0)) != NULL)
    {
        vRingbufferReturnItem(rb, item);
    }
}

void stop_pipeline(void)
{
    uint32_t ack;

    /* Stop audio (consumer) */
    ESP_LOGI(TAG, "Stopping audio task");
    xTaskNotify(audio_task_handle, 2, eSetValueWithOverwrite);
    xTaskNotifyWait(0, 0xFFFFFFFF, &ack, portMAX_DELAY);
    ESP_LOGI(TAG, "Done stopping audio task (ack=%u)", ack);

    /* Stop stream (producer) */
    ESP_LOGI(TAG, "Stopping stream task");
    xTaskNotify(stream_task_handle, 2, eSetValueWithOverwrite);
    xTaskNotifyWait(0, 0xFFFFFFFF, &ack, portMAX_DELAY);
    ESP_LOGI(TAG, "Done stopping stream task (ack=%u)", ack);

    /* Stop mp3 decoder */
    ESP_LOGI(TAG, "Stopping mp3 decoder task");
    xTaskNotify(mp3_task_handle, 2, eSetValueWithOverwrite);
    xTaskNotifyWait(0, 0xFFFFFFFF, &ack, portMAX_DELAY);
    ESP_LOGI(TAG, "Done stopping mp3 decoder task (ack=%u)", ack);

    // flush
    flush_ringbuffer(mp3_rb);
    flush_ringbuffer(pcm_rb);
}

static void stop(void)
{
    stop_pipeline();
    update_state(STATE_IDLE);
}

static void supervisor_task(void *arg)
{
    task_cmd_t cmd;

    while (1)
    {
        if (xQueueReceive(supervisor_cmd_queue, &cmd, pdMS_TO_TICKS(10)))
        {
            switch (cmd.type)
            {
                case CMD_PLAY:
                {
                    xTaskNotify(stream_task_handle, 1, eSetValueWithOverwrite);
                    xTaskNotify(mp3_task_handle, 1, eSetValueWithOverwrite);
                    xTaskNotify(audio_task_handle, 1, eSetValueWithOverwrite);
                    // mp3_decode_start();
                    // stream_start();
                    // audio_start();
                    update_state(STATE_PLAYING);
                    break;
                }

                case CMD_PAUSE:
                {
                    stop();
                    break;
                }

                case CMD_VOLUME:
                {
                    set_volume(cmd.volume);
                    ESP_LOGI(TAG, "Volume changed: %.2f", cmd.volume);
                    break;
                }

                default:
                    break;
            }
        }

        if (xEventGroupWaitBits(stream_task_event_group, 1, pdTRUE, pdFALSE, pdMS_TO_TICKS(10)))
        {
            ESP_LOGI(TAG, "Indication stream ended");
            stop();
        }
    }
}

/* Functions ******************************************************************/
void supervisor_event(task_cmd_t* event)
{
    assert(supervisor_cmd_queue);
    xQueueSend(supervisor_cmd_queue, event, 0);
}

void supervisor_init(EventGroupHandle_t stream_event_group)
{
    stream_task_event_group = stream_event_group;

    supervisor_cmd_queue = xQueueCreate(8, sizeof(task_cmd_t));

    xTaskCreate(supervisor_task,
                "supervisor_task",
                8192,
                NULL,
                tskIDLE_PRIORITY,
                &supervisor_task_handle);
}
