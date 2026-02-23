#ifndef SUPERVISOR_H_
#define SUPERVISOR_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Structs - Enums ************************************************************/
typedef enum {
    CMD_PLAY,
    CMD_PAUSE,
    CMD_VOLUME,
} cmd_t;

typedef enum {
    STATE_IDLE,
    STATE_PLAYING,
} state_t;

typedef struct {
    cmd_t type;
    float volume;
} task_cmd_t;

/* Prototypes *****************************************************************/
void supervisor_init(EventGroupHandle_t stream_event_group);
void supervisor_event(task_cmd_t* event);

#endif /* SUPERVISOR_H_ */
