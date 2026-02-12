#include "audio.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/i2s_std.h"
#include <string.h>

/* ===== Helpers ===== */
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

static const char* TAG = "audio";

RingbufHandle_t audio_rb = NULL;
static i2s_chan_handle_t tx_chan = NULL;

static void i2s_write(const uint8_t* data, const uint16_t data_len, size_t* written)
{
    if (i2s_channel_write(tx_chan, data, data_len, written, 1000))
    {
        ESP_LOGI(TAG, "Write Task: i2s write failed");
    }
}

static void i2s_init(void)
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws   = I2S_LRCLK,
            .dout = I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_chan, &tx_std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_chan));
}

void audio_task(void *arg)
{
    size_t bytes_written;
    uint8_t out[I2S_WRITE_CHUNK];

    /* -------- Prebuffer -------- */
    size_t max_ring_buffer_size = xRingbufferGetMaxItemSize(audio_rb);
    while (xRingbufferGetCurFreeSize(audio_rb) >
           (max_ring_buffer_size - PREBUFFER_BYTES))
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    TickType_t last_wake = xTaskGetTickCount();

    /* -------- Playback loop -------- */
    while (1) {
        /* How much audio is currently buffered */
        size_t fill =
            max_ring_buffer_size - xRingbufferGetCurFreeSize(audio_rb);

        size_t to_read = MIN(fill, I2S_WRITE_CHUNK);
        ESP_LOGI(TAG, "free=%u fill=%u, to_read=%u", xRingbufferGetCurFreeSize(audio_rb), fill, to_read);
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
            ESP_LOGI(TAG, "filling with silence");
            memset(out + copied, 0, I2S_WRITE_CHUNK - copied);
        }

        /* Always feed I2S a fixed-size buffer */
        i2s_write(out, I2S_WRITE_CHUNK, &bytes_written);

        vTaskDelayUntil(&last_wake,
                        pdMS_TO_TICKS(AUDIO_PERIOD_MS));
    }
}

void audio_init(void)
{
    i2s_init();

    audio_rb = xRingbufferCreate(RINGBUF_SIZE_BYTES, RINGBUF_TYPE_BYTEBUF);
    assert(audio_rb != NULL);

    xTaskCreatePinnedToCore(audio_task,
                            "audio_task",
                            4096,
                            NULL,
                            3,
                            NULL,
                            0);
}
