#include "supervisor.h"

#include "audio.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "types.h"
#include "mp3.h"
#include "mqtt.h"
#include "stream.h"

/* Global objects *************************************************************/
static QueueHandle_t supervisor_cmd_queue = NULL;

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

static void supervisor_task(void *arg)
{
    task_cmd_t cmd;

    while (xQueueReceive(supervisor_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE)
    {
        switch (cmd.type)
        {
            case CMD_PLAY:
            {
                mp3_decode_start();
                stream_start();
                audio_start();
                update_state(STATE_PLAYING);
                break;
            }

            case CMD_PAUSE:
            {
                stream_stop();
                audio_stop();
                mp3_decode_stop();
                update_state(STATE_IDLE);
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
}

/* Functions ******************************************************************/
void supervisor_event(task_cmd_t* event)
{
    assert(supervisor_cmd_queue);
    xQueueSend(supervisor_cmd_queue, event, 0);
}

void supervisor_init(void)
{
    supervisor_cmd_queue = xQueueCreate(8, sizeof(task_cmd_t));
    xTaskCreate(supervisor_task, "supervisor_task", 4096, NULL, tskIDLE_PRIORITY, NULL);
}
