#include "mp3.h"

#include "audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "mp3dec.h"

#define MP3_INPUT_CHUNK_SIZE  (1024)
#define PCM_OUTPUT_SAMPLES    (1152)
#define LEFTOVER_SIZE         (512)
#define DATA_BUFFER_SIZE      (MP3_INPUT_CHUNK_SIZE + LEFTOVER_SIZE)

extern RingbufHandle_t mp3_rb;
extern RingbufHandle_t pcm_rb;

static EventGroupHandle_t mp3_event_group = NULL;

static HMP3Decoder decoder = NULL;

static const char* TAG = "mp3_decode_task";

static void cleanup(void)
{
    ESP_LOGI(TAG, "cleanup");
    if (decoder != NULL)
        MP3FreeDecoder(decoder);

    flush_ringbuffer(mp3_rb);
}

void mp3_decode_start(void)
{
    xEventGroupSetBits(mp3_event_group, 1);
}

void mp3_decode_stop(void)
{
    xEventGroupSetBits(mp3_event_group, 2);
}

void mp3_decode_task(void *arg)
{
    while (1)
    {
        /* Wait for start command */
        xEventGroupWaitBits(mp3_event_group, 1, pdTRUE, pdFALSE, portMAX_DELAY);

        /* Clear stop bit */
        xEventGroupClearBits(mp3_event_group, 2);

        decoder = MP3InitDecoder();
        if (!decoder)
        {
            ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
            vTaskDelete(NULL);
            return;
        }

        /* Create PCM out buffer */
        int16_t pcm_out[PCM_OUTPUT_SAMPLES];
        memset(pcm_out, 0, PCM_OUTPUT_SAMPLES * sizeof(int16_t));

        /* Create leftover buffer */
        uint8_t leftover[LEFTOVER_SIZE];
        memset(leftover, 0, LEFTOVER_SIZE);
        size_t leftover_len = 0;

        while(1)
        {
            size_t bytes_available;

            if (xEventGroupGetBits(mp3_event_group) & 2)
                break;

            /* Wait for data in ringbuffer */
            uint8_t *new_data = (uint8_t *)xRingbufferReceiveUpTo(mp3_rb,
                                    &bytes_available,
                                    pdMS_TO_TICKS(100),
                                    MP3_INPUT_CHUNK_SIZE);

            if (!new_data)
                continue;

            if (!bytes_available)
                break;

            uint8_t data[DATA_BUFFER_SIZE];
            memset(data, 0, DATA_BUFFER_SIZE);

            memcpy(data, leftover, leftover_len);
            memcpy(data + leftover_len, new_data, bytes_available);

            vRingbufferReturnItem(mp3_rb, new_data);

            uint8_t *read_ptr = data;
            uint8_t *backup = NULL;
            size_t backup_len = 0;

            int bytes_left = bytes_available + leftover_len;

            int err = 0;
            leftover_len = 0;

            while (bytes_left > 0)
            {
                int offset = MP3FindSyncWord(read_ptr, bytes_left);
                if (offset < 0)
                    break;

                read_ptr += offset;
                bytes_left -= offset;

                /* If decoding fails, keep backup to prepend next buffer */
                backup = read_ptr;
                backup_len = bytes_left;

                err = MP3Decode(decoder, &read_ptr, &bytes_left, pcm_out, 0);

                if (err != 0)
                    break;

                MP3FrameInfo frameInfo;
                MP3GetLastFrameInfo(decoder, &frameInfo);
                int pcm_bytes = frameInfo.outputSamps * sizeof(int16_t);

                if (xRingbufferSend(pcm_rb, pcm_out, pcm_bytes, pdMS_TO_TICKS(100)) != pdTRUE) {
                    if (xEventGroupGetBits(mp3_event_group) & 2) break;
                }
            }

            if (err != 0)
            {
                leftover_len = backup_len;
                memset(leftover, 0, LEFTOVER_SIZE);
                memcpy(leftover, backup, leftover_len);
            }
        }

        cleanup();
    }
}

void mp3_decode_init(void)
{
    mp3_event_group = xEventGroupCreate();

    xTaskCreatePinnedToCore(mp3_decode_task,
                            "mp3_decode",
                            8192,
                            NULL,
                            2,
                            NULL,
                            1);
}

