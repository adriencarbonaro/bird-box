#include "audio.h"
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "driver/i2s_std.h"
#include <string.h>
#include "supervisor.h"

static const char* TAG = "audio";

static i2s_chan_handle_t tx_chan = NULL;
static volatile float volume = AMPLIFY_GAIN;
static const uint8_t silence[I2S_WRITE_CHUNK] = {0};
static uint8_t out[I2S_WRITE_CHUNK] = {0};

static EventGroupHandle_t task_stop_event_group = NULL;
TaskHandle_t audio_task_handle = NULL;
extern TaskHandle_t supervisor_task_handle;

extern RingbufHandle_t pcm_rb;

static void amplify_buffer(int16_t *data, size_t len, float gain)
{
    for (size_t i = 0; i < len; i++)
    {
        int32_t sample = (int32_t)(data[i] * gain);
        if (sample > INT16_MAX) sample = INT16_MAX;
        else if (sample < INT16_MIN) sample = INT16_MIN;
        data[i] = (int16_t)sample;
    }
}

static void i2s_write(const uint8_t* data, const uint16_t data_len, size_t* written)
{
    esp_err_t status = ESP_OK;
    if ((status = i2s_channel_write(tx_chan, data, data_len, written, portMAX_DELAY)) != ESP_OK)
    {
        ESP_LOGI(TAG, "Write Task: i2s write failed with status: %u", status);
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

void set_volume(float new_volume)
{
    volume = new_volume;
}

static void stop(void)
{
    // First push enough silence while still enabled
    for (int i = 0; i < 6; i++)
    {
        size_t written;
        i2s_channel_write(tx_chan, silence, 480, &written, portMAX_DELAY);
    }

    // Wait long enough for DMA to drain
    vTaskDelay(pdMS_TO_TICKS(5));

    // Now disable
    i2s_channel_disable(tx_chan);

    i2s_channel_enable(tx_chan);

    xTaskNotify(supervisor_task_handle, 1, eSetValueWithOverwrite);
}

static bool check_supervisor_stop(void)
{
    uint32_t cmd = 0;
    if (xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, 0) == pdTRUE)
    {
        if (cmd == 2)
        {
            ESP_LOGI(TAG, "audio task stopped by supervisor");
            stop();
            return true;
        }
    }
    return false;
}

void audio_task(void *arg)
{
    while (1)
    {
        /* Wait for start notification */
        ESP_LOGI(TAG, "Waiting for start command");
        uint32_t cmd;
        xTaskNotifyWait(0, 0xFFFFFFFF, &cmd, portMAX_DELAY);
        if (cmd == 2)
        {
            ESP_LOGI(TAG, "task (waiting for start) stopped by supervisor");
            stop();
            continue;
        }
        else if (cmd != 1) continue;

        size_t bytes_written;

        /* Prebuffer.
         *
         * Waiting on a byte target alone deadlocks whenever the stream holds
         * less PCM than the target: nothing downstream can report the end of
         * playback because that only happens once we reach the write loop
         * below. So also stop waiting when the buffer stops growing, which
         * covers both a short stream and a dead producer. */
        size_t   used = 0;
        size_t   last_used = 0;
        uint32_t stalled_ms = 0;
        uint32_t waited_ms = 0;
        bool     stopped = false;

        while (1)
        {
            if (check_supervisor_stop()) { stopped = true; break; }

            size_t free = xRingbufferGetCurFreeSize(pcm_rb);
            used = (free < PCM_RING_SIZE) ? (PCM_RING_SIZE - free) : 0;

            if (used >= PREBUFFER_SIZE)
            {
                ESP_LOGI(TAG, "Prebuffered %u bytes (target reached)", used);
                break;
            }

            if (used > last_used)
            {
                last_used = used;
                stalled_ms = 0;
            }
            else
            {
                stalled_ms += PREBUFFER_POLL_MS;
            }

            /* Stream was shorter than the target: play what we have. */
            if (used > 0 && stalled_ms >= PREBUFFER_STALL_MS)
            {
                ESP_LOGI(TAG, "Prebuffered %u bytes (stream shorter than target)", used);
                break;
            }

            if (waited_ms >= PREBUFFER_MAX_WAIT_MS)
            {
                ESP_LOGW(TAG, "Prebuffer timed out (%u bytes)", used);
                break;
            }

            vTaskDelay(pdMS_TO_TICKS(PREBUFFER_POLL_MS));
            waited_ms += PREBUFFER_POLL_MS;
        }

        if (stopped) continue;

        if (used == 0)
        {
            ESP_LOGW(TAG, "No audio data, aborting playback");
            xEventGroupSetBits(task_stop_event_group,
                               BIT(STOP_REASON_AUDIO_ENDED));
            continue;
        }

        size_t empty_rounds = 0;
        bool   eof_signalled = false;

        while (1)
        {
            size_t copied = 0;

            while (copied < I2S_WRITE_CHUNK)
            {
                size_t bytes_available = 0;

                uint8_t* data = (uint8_t*)xRingbufferReceiveUpTo(
                    pcm_rb,
                    &bytes_available,
                    pdMS_TO_TICKS(5),
                    I2S_WRITE_CHUNK - copied
                );

                if (!data)
                {
                    ESP_LOGD(TAG, "Underrun (copied=%u)", copied);
                    break;
                }

                if (bytes_available == 0)
                {
                    ESP_LOGD(TAG, "Empty item");
                    vRingbufferReturnItem(pcm_rb, data);
                    break;
                }

                /* Fill temporary buffer */
                memcpy(out + copied, data, bytes_available);
                copied += bytes_available;

                vRingbufferReturnItem(pcm_rb, data);
            }

            /* A single dry poll is a 5 ms underrun, not the end of the
             * stream. Only give up once the ring has stayed completely empty
             * for AUDIO_EOF_SILENCE_MS. */
            if (copied == 0)
            {
                if (!eof_signalled && ++empty_rounds >= AUDIO_EOF_EMPTY_ROUNDS)
                {
                    ESP_LOGI(TAG, "PCM ring dry for %u ms, ending playback",
                             (unsigned)AUDIO_EOF_SILENCE_MS);
                    xEventGroupSetBits(task_stop_event_group,
                                       BIT(STOP_REASON_AUDIO_ENDED));
                    eof_signalled = true;
                }
            }
            else
            {
                empty_rounds = 0;
                eof_signalled = false;
            }

            /* Fill remaining space with silence */
            if (copied < I2S_WRITE_CHUNK)
                memset(out + copied, 0, I2S_WRITE_CHUNK - copied);

            /* Apply volume */
            int16_t* sample_buffer = (int16_t*)out;
            amplify_buffer(sample_buffer, I2S_WRITE_CHUNK / sizeof(int16_t), volume);

            i2s_write(out, I2S_WRITE_CHUNK, &bytes_written);

            if (check_supervisor_stop()) break;
        }
    }
}

void audio_init(EventGroupHandle_t stop_event_group)
{
    task_stop_event_group = stop_event_group;

    i2s_init();

    xTaskCreatePinnedToCore(audio_task,
                            "audio_task",
                            16384,
                            NULL,
                            4,
                            &audio_task_handle,
                            0);
}
