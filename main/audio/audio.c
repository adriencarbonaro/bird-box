#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/i2s.h"
#include <string.h>

/* ===== Audio format ===== */
#define SAMPLE_RATE        44100
#define BYTES_PER_SAMPLE   2       // 16-bit
#define CHANNELS           1

#define AUDIO_BYTES_PER_SEC \
    (SAMPLE_RATE * BYTES_PER_SAMPLE * CHANNELS)

/* ===== Buffering ===== */
#define PREBUFFER_MS        500
#define PREBUFFER_BYTES     (AUDIO_BYTES_PER_SEC * PREBUFFER_MS / 1000)

#define RINGBUF_SIZE_BYTES  (PREBUFFER_BYTES * 3)

/* ===== Audio timing ===== */
#define AUDIO_PERIOD_MS     10
#define I2S_WRITE_CHUNK \
    (AUDIO_BYTES_PER_SEC * AUDIO_PERIOD_MS / 1000)

/* ===== Helpers ===== */
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

extern RingbufHandle_t audio_rb;

void audio_task(void *arg)
{
    size_t bytes_written;
    uint8_t out[I2S_WRITE_CHUNK];

    /* -------- Prebuffer -------- */
    while (xRingbufferGetCurFreeSize(audio_rb) >
           (RINGBUF_SIZE_BYTES - PREBUFFER_BYTES)) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    i2s_start(I2S_NUM_0);

    TickType_t last_wake = xTaskGetTickCount();

    /* -------- Playback loop -------- */
    while (1) {
        /* How much audio is currently buffered */
        size_t fill =
            RINGBUF_SIZE_BYTES - xRingbufferGetCurFreeSize(audio_rb);

        size_t to_read = MIN(fill, I2S_WRITE_CHUNK);
        size_t copied = 0;

        if (to_read > 0) {
            size_t item_size = 0;

            uint8_t *data = (uint8_t *) xRingbufferReceiveUpTo(
                audio_rb,
                &item_size,
                0,          // non-blocking
                to_read
            );

            if (data && item_size > 0) {
                memcpy(out, data, item_size);
                copied = item_size;
                vRingbufferReturnItem(audio_rb, data);
            }
        }

        /* Pad with silence if needed */
        if (copied < I2S_WRITE_CHUNK) {
            memset(out + copied, 0, I2S_WRITE_CHUNK - copied);
        }

        /* Always feed I2S a fixed-size buffer */
        i2s_write(
            I2S_NUM_0,
            out,
            I2S_WRITE_CHUNK,
            &bytes_written,
            portMAX_DELAY
        );

        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(AUDIO_PERIOD_MS));
    }
}

