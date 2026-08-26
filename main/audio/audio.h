#ifndef AUDIO_H
#define AUDIO_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ===== Audio format ===== */
#define BYTES_PER_SAMPLE      2
#define CHANNELS              1

#define AUDIO_BYTES_PER_SEC   (SAMPLE_RATE * BYTES_PER_SAMPLE * CHANNELS)

/* ===== Buffering ===== */
#define PREBUFFER_MS          300
#define PREBUFFER_SIZE        (AUDIO_BYTES_PER_SEC * PREBUFFER_MS / 1000)

/* The ring has to be bigger than the prebuffer target, or the target can never
 * be reached: the producer blocks as the ring approaches full. */
#define PCM_RING_SIZE         (PREBUFFER_SIZE * 2)
#define MP3_RING_SIZE         (32 * 1024)

/* Prebuffer loop. A byte target on its own deadlocks on a stream shorter than
 * the target, so also stop waiting once the buffer has stopped growing, and
 * never wait longer than the hard cap. */
#define PREBUFFER_POLL_MS     10
#define PREBUFFER_STALL_MS    400
#define PREBUFFER_MAX_WAIT_MS 5000

/* ===== Audio timing ===== */
#define I2S_WRITE_CHUNK       1024

/* One I2S write expressed in milliseconds of audio (~11 ms at 44.1 kHz mono). */
#define I2S_CHUNK_MS          ((I2S_WRITE_CHUNK * 1000) / AUDIO_BYTES_PER_SEC)

/* Only treat a dry PCM ring as end-of-stream after this much continuous
 * starvation. A shorter gap is an underrun - a Wi-Fi retransmit or a slow
 * decode burst - and must not cut playback short. */
#define AUDIO_EOF_SILENCE_MS   300
#define AUDIO_EOF_EMPTY_ROUNDS ((AUDIO_EOF_SILENCE_MS / I2S_CHUNK_MS) + 1)


void audio_init(EventGroupHandle_t event_group);
void audio_task(void *arg);
void audio_stop(void);
void audio_start(void);

void set_volume(float volume);

#endif /* AUDIO_H */
