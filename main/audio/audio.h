#ifndef AUDIO_H
#define AUDIO_H

/* ===== Audio format ===== */
#define BYTES_PER_SAMPLE      2
#define CHANNELS              1

#define AUDIO_BYTES_PER_SEC   (SAMPLE_RATE * BYTES_PER_SAMPLE * CHANNELS)

/* ===== Buffering ===== */
#define PREBUFFER_MS          500
#define PREBUFFER_BYTES       (AUDIO_BYTES_PER_SEC * PREBUFFER_MS / 1000)

#define RINGBUF_SIZE_BYTES    (PREBUFFER_BYTES * 2)

/* ===== Audio timing ===== */
#define AUDIO_PERIOD_MS       10
#define I2S_WRITE_CHUNK       (AUDIO_BYTES_PER_SEC * AUDIO_PERIOD_MS / 1000)

void audio_init(void);
void audio_task(void *arg);

#endif /* AUDIO_H */
