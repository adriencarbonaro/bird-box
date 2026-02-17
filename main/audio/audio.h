#ifndef AUDIO_H
#define AUDIO_H

/* ===== Audio format ===== */
#define BYTES_PER_SAMPLE      2
#define CHANNELS              1

#define AUDIO_BYTES_PER_SEC   (SAMPLE_RATE * BYTES_PER_SAMPLE * CHANNELS)

/* ===== Buffering ===== */
#define PREBUFFER_MS          500
#define PCM_RINGBUF_SIZEBYTES (AUDIO_BYTES_PER_SEC * PREBUFFER_MS / 1000)

#define MP3_RB_SIZE           (32 * 1024)

/* ===== Audio timing ===== */
#define I2S_WRITE_CHUNK       1024


void audio_init(void);
void audio_task(void *arg);

void set_volume(float volume);

#endif /* AUDIO_H */
