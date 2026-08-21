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
#include "ha.h"
#include "ha_entities.h"
#include "mp3.h"
#include "mqtt.h"
#include "sdkconfig.h"
#include "stream.h"
#include "supervisor.h"
#include "utils.h"
#include "utils/types.h"
#include "version.h"
#include "wifi.h"

/* Global pointers ************************************************************/

static const char *TAG = "main";

/* Structs - Enums ************************************************************/

/* Global objects *************************************************************/
static const ha_identity_t identity = {
    .device_id = CONFIG_DEVICE_ID,
    .device_name = CONFIG_DEVICE_NAME,
    .manufacturer = CONFIG_MANUFACTURER,
    .model = CONFIG_MODEL,
    .version_str = DESCRIBE,
};

/* Static functions ***********************************************************/

#define ON_COMMAND_HANDLER(name, cmd) \
    void on_command_##name(const char* id, const char* payload) \
    { \
        task_cmd_t event; \
        memset(&event, 0, sizeof(event)); \
        event.type = CMD_##cmd; \
        supervisor_event(&event); \
    }

ON_COMMAND_HANDLER(play, PLAY)
ON_COMMAND_HANDLER(pause, PAUSE)

void on_volume(const char* id, const char* payload)
{
    task_cmd_t event;
    memset(&event, 0, sizeof(event));

    event.type = CMD_VOLUME;

    // copy payload into null-terminated buffer
    char buf[16];
    int copy_len = MIN(strlen(payload), sizeof(buf) - 1);
    memcpy(buf, payload, copy_len);
    buf[copy_len] = '\0';

    event.volume = atof(buf);
    ESP_LOGI(TAG, "Parsed volume: %.2f", event.volume);

    supervisor_event(&event);
}

void app_main(void)
{
    /* Init Home Assistant layer */
    uint16_t nb_entities = 0;
    const ha_entity_t* entities = get_entities(&nb_entities);
    ha_init(&identity, entities, nb_entities);

    wifi_init(mqtt_start, NULL);

    supervisor_init();
}
