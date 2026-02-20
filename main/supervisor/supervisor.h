#ifndef SUPERVISOR_H_
#define SUPERVISOR_H_

#include "esp_bit_defs.h"

/* Structs - Enums ************************************************************/
typedef enum {
    STOP_REASON_STREAM_OPEN_FAILED,
    STOP_REASON_AUDIO_ENDED,
} stop_reason_t;

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
void supervisor_init(void);
void supervisor_event(task_cmd_t* event);

#endif /* SUPERVISOR_H_ */
