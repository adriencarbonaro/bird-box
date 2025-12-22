#ifndef PLAY_H_
#define PLAY_H_

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
void play_init(void);
void play_event(task_cmd_t* event);

#endif /* PLAY_H_ */
