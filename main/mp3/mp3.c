#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include "mp3dec.h"

#define MP3_INPUT_CHUNK_SIZE  (1024)
#define PCM_OUTPUT_SAMPLES    (1152)
#define LEFTOVER_SIZE         (512)
#define DATA_BUFFER_SIZE      (MP3_INPUT_CHUNK_SIZE + LEFTOVER_SIZE)

extern RingbufHandle_t mp3_rb;
extern RingbufHandle_t pcm_rb;

static const char* TAG = "mp3_decode_task";

void mp3_decode_task(void *arg)
{
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder)
    {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        vTaskDelete(NULL);
        return;
    }

    static int16_t pcm_out[PCM_OUTPUT_SAMPLES];
    static uint8_t leftover[LEFTOVER_SIZE];
    static size_t leftover_len = 0;

    while (1)
    {
        size_t bytes_available;

        uint8_t *new_data = (uint8_t *)xRingbufferReceiveUpTo(mp3_rb,
                                &bytes_available,
                                portMAX_DELAY,
                                MP3_INPUT_CHUNK_SIZE);
        if (!new_data || !bytes_available)
            break;

        uint8_t data[DATA_BUFFER_SIZE];
        memset(data, 0, DATA_BUFFER_SIZE);

        memcpy(data, leftover, leftover_len);
        memcpy(data + leftover_len, new_data, bytes_available);

        uint8_t *read_ptr = data;
        uint8_t *backup = NULL;
        size_t backup_len = 0;

        int bytes_left = bytes_available + leftover_len;

        int err = 0;

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

            xRingbufferSend(pcm_rb, pcm_out, pcm_bytes, portMAX_DELAY);
        }

        if (err != 0)
        {
            memset(leftover, 0, LEFTOVER_SIZE);
            memcpy(leftover, backup, backup_len);
        }

        leftover_len = backup_len;

        vRingbufferReturnItem(mp3_rb, new_data);
    }
}

void mp3_decode_init(void)
{
    xTaskCreatePinnedToCore(mp3_decode_task,
                            "mp3_decode",
                            8192,
                            NULL,
                            5,
                            NULL,
                            1);
}

