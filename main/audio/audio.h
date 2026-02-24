#ifndef AUDIO_H
#define AUDIO_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ===== Audio format ===== */
#define BYTES_PER_SAMPLE      2
#define CHANNELS              1

#define AUDIO_BYTES_PER_SEC   (SAMPLE_RATE * BYTES_PER_SAMPLE * CHANNELS)

/* ===== Buffering ===== */
#define PREBUFFER_MS          500
#define PREBUFFER_SIZE        (AUDIO_BYTES_PER_SEC * PREBUFFER_MS / 1000)

#define PCM_RING_SIZE         (PREBUFFER_SIZE)
#define MP3_RING_SIZE         (32 * 1024)

/* ===== Audio timing ===== */
#define I2S_WRITE_CHUNK       1024


void audio_init(EventGroupHandle_t event_group);
void audio_task(void *arg);
void audio_stop(void);
void audio_start(void);

void set_volume(float volume);

#endif /* AUDIO_H */
