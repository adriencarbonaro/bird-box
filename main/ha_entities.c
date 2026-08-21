#include "ha_entities.h"

#include <stdint.h>

#include "ha.h"

#define ARRAY_DIM(arr) (sizeof(arr) / sizeof((arr)[0]))

extern void on_command_play(const char* id, const char* payload);
extern void on_command_pause(const char* id, const char* payload);
extern void on_volume(const char* id, const char* payload);

static const ha_entity_t s_entities[] = {
    {
        .id = "state",
        .name = "Playing state",
        .platform = HA_SENSOR,
        .state_topic = "state",
        .icon = "mdi:play-pause",
    },
    {
        .id = "volume",
        .name = "Playing volume",
        .platform = HA_INPUT_NUMBER,
        .state_topic = "volume/state",
        .command_topic = "volume/set",
        .on_command = on_volume,
        .icon = "mdi:volume-high",
    },
    {
        .id = "play",
        .name = "Play",
        .platform = HA_BUTTON,
        .on_command = on_command_play,
        .command_topic = "set",
        .icon = "mdi:play-circle",
    },
    {
        .id = "pause",
        .name = "Pause",
        .platform = HA_BUTTON,
        .on_command = on_command_pause,
        .command_topic = "set",
        .icon = "mdi:pause-circle",
    },
};

const ha_entity_t* get_entities(uint16_t* nb_entities)
{
    *nb_entities = ARRAY_DIM(s_entities);
    return s_entities;
}
