#include "mp3.h"

#include "audio.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "mp3dec.h"

#define MP3_INPUT_CHUNK_SIZE  (1024)

/* Helix can emit MAX_NGRAN granules of 576 samples on MAX_NCHAN channels in a
 * single frame. A false sync word yields a header that claims stereo even on a
 * mono stream, so this is sized for the decoder's worst case, not for the
 * format we expect to receive. */
#define PCM_OUTPUT_SAMPLES    (MAX_NCHAN * MAX_NGRAN * 576)

/* Holds any legal MP3 frame (1441 bytes max, at 320 kbps / 32 kHz) so an
 * incomplete frame can always be carried over to the next read. */
#define LEFTOVER_SIZE         (1600)
#define DATA_BUFFER_SIZE      (MP3_INPUT_CHUNK_SIZE + LEFTOVER_SIZE)

extern RingbufHandle_t mp3_rb;
extern RingbufHandle_t pcm_rb;

TaskHandle_t mp3_task_handle = NULL;
extern TaskHandle_t supervisor_task_handle;

static HMP3Decoder decoder = NULL;

static const char* TAG = "mp3_task";

static void stop(void)
{
    if (decoder != NULL) MP3FreeDecoder(decoder);
    decoder = NULL;

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

void mp3_decode_task(void *arg)
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

        decoder = MP3InitDecoder();
        if (!decoder)
        {
            ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
            vTaskDelete(NULL);
            return;
        }

        /* These live in .bss rather than on the stack: this task is a
         * singleton, they are the largest buffers here, and keeping them off
         * the outermost stack frame means a decoder overrun can no longer
         * reach the heap block sitting behind the task stack. */
        static int16_t pcm_out[PCM_OUTPUT_SAMPLES];
        static uint8_t leftover[LEFTOVER_SIZE];
        static uint8_t data[DATA_BUFFER_SIZE];

        memset(pcm_out, 0, sizeof(pcm_out));
        memset(leftover, 0, sizeof(leftover));
        size_t leftover_len = 0;

        while(1)
        {
            size_t bytes_available;

            if (check_supervisor_stop()) break;

            /* Wait for data in ringbuffer */
            uint8_t *new_data = (uint8_t *)xRingbufferReceiveUpTo(mp3_rb,
                                    &bytes_available,
                                    pdMS_TO_TICKS(100),
                                    MP3_INPUT_CHUNK_SIZE);

            if (!new_data)
            {
                ESP_LOGW(TAG, "No data");
                continue;
            }

            if (!bytes_available)
            {
                ESP_LOGW(TAG, "No bytes avail");
                break;
            }

            memset(data, 0, DATA_BUFFER_SIZE);

            memcpy(data, leftover, leftover_len);
            memcpy(data + leftover_len, new_data, bytes_available);

            vRingbufferReturnItem(mp3_rb, new_data);

            uint8_t *read_ptr = data;

            /* Bytes we could not consume this round, carried to the next read */
            uint8_t *carry = NULL;
            size_t carry_len = 0;

            int bytes_left = bytes_available + leftover_len;

            leftover_len = 0;

            while (bytes_left > 0)
            {
                int offset = MP3FindSyncWord(read_ptr, bytes_left);
                if (offset < 0)
                {
                    /* No sync word in the tail. Carry it rather than dropping
                     * it: a sync can straddle the read boundary, and losing
                     * those bytes desynchronises the stream and forces a
                     * resync on random data. */
                    carry = read_ptr;
                    carry_len = bytes_left;
                    break;
                }

                read_ptr += offset;
                bytes_left -= offset;

                uint8_t *frame_start = read_ptr;
                size_t frame_bytes_left = bytes_left;

                int err = MP3Decode(decoder, &read_ptr, &bytes_left, pcm_out, 0);

                if (err == ERR_MP3_INVALID_FRAMEHEADER)
                {
                    /* False sync. MP3Decode rejects the header before it
                     * touches read_ptr/bytes_left, so step over the sync byte
                     * and keep scanning this buffer. */
                    read_ptr = frame_start + 1;
                    bytes_left = (int)frame_bytes_left - 1;
                    continue;
                }

                if (err != 0)
                {
                    /* Incomplete frame: carry it to the next read. */
                    carry = frame_start;
                    carry_len = frame_bytes_left;
                    break;
                }

                MP3FrameInfo frameInfo;
                MP3GetLastFrameInfo(decoder, &frameInfo);

                /* A frame decoded off a false sync can report a format we
                 * never asked for. Drop it instead of feeding the pipeline
                 * garbage at the wrong channel count or rate. */
                if (frameInfo.nChans != CHANNELS || frameInfo.samprate != SAMPLE_RATE)
                {
                    ESP_LOGW(TAG, "Dropping frame: %d ch, %d Hz",
                             frameInfo.nChans, frameInfo.samprate);
                    continue;
                }

                size_t pcm_bytes = (size_t)frameInfo.outputSamps * sizeof(int16_t);
                if (pcm_bytes > sizeof(pcm_out))
                    pcm_bytes = sizeof(pcm_out);

                xRingbufferSend(pcm_rb, pcm_out, pcm_bytes, pdMS_TO_TICKS(100));
            }

            if (carry != NULL && carry_len > 0)
            {
                if (carry_len > LEFTOVER_SIZE)
                    carry_len = LEFTOVER_SIZE;

                memset(leftover, 0, LEFTOVER_SIZE);
                memcpy(leftover, carry, carry_len);
                leftover_len = carry_len;
            }

            if (check_supervisor_stop()) break;
        }
    }
}

void mp3_decode_init(void)
{
    xTaskCreatePinnedToCore(mp3_decode_task,
                            "mp3_decode",
                            16384,
                            NULL,
                            2,
                            &mp3_task_handle,
                            1);
}

