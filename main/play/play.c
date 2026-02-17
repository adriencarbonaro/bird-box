#include "play.h"

#include "audio.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "types.h"
#include "mqtt.h"
#include "stream.h"

/* Global objects *************************************************************/
static QueueHandle_t play_cmd_queue = NULL;

static const char* TAG = "play_task";

static const char* state_str_list[] = {
    [STATE_IDLE] = "idle",
    [STATE_PLAYING] = "playing",
};

/* Static functions ***********************************************************/
static void update_state(state_t state)
{
    send_state(state_str_list[state]);
}

static void play_task(void *arg)
{
    task_cmd_t cmd;

    while (xQueueReceive(play_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE)
    {
        switch (cmd.type)
        {
            case CMD_PLAY:
            {
                stream_start();
                update_state(STATE_PLAYING);
                break;
            }

            case CMD_PAUSE:
            {
                stream_stop();
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
void play_event(task_cmd_t* event)
{
    assert(play_cmd_queue);
    xQueueSend(play_cmd_queue, event, 0);
}

void play_init(void)
{
    play_cmd_queue = xQueueCreate(8, sizeof(task_cmd_t));
    xTaskCreate(play_task, "play_task", 4096, NULL, tskIDLE_PRIORITY, NULL);
}
