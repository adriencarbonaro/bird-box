#ifndef MP3_DECODE_H_
#define MP3_DECODE_H_

typedef enum
{
    MP3_CMD_START,
    MP3_CMD_STOP
} mp3_cmd_t;

void mp3_decode_start(void);
void mp3_decode_stop(void);
void mp3_decode_init(void);

#endif /* MP3_DECODE_H_ */
