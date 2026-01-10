#include "stream.h"
#include "string.h"

#include "config.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "play.h"
#include "mqtt.h"
#include "types.h"

/* Defines ********************************************************************/
#define WAV_FMT_HEADER_OFFSET           (20)
#define WAV_FMT_HEADER_SIZE             (16)
#define WAV_FMT_HEADER_SKIP_SIZE        (WAV_FMT_HEADER_OFFSET + \
                                         WAV_FMT_HEADER_SIZE)

/* Global objects *************************************************************/
static TaskHandle_t stream_task_handle = NULL;
static i2s_chan_handle_t tx_chan = NULL;

static const char* TAG = "stream_task";
static uint8 buffer[I2S_BUFFER_SIZE];
static volatile float gain = AMPLIFY_GAIN;
static volatile bool stop_playback = false;

/* Static functions ***********************************************************/
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

static void i2s_write(const uint8* data, const uint16 data_len, size_t* written)
{
    if (i2s_channel_write(tx_chan, data, data_len, written, 1000))
    {
        ESP_LOGI(TAG, "Write Task: i2s write failed");
    }
}

static void i2s_clean(void)
{
    memset(buffer, 0, I2S_BUFFER_SIZE);
    i2s_write(buffer, I2S_BUFFER_SIZE, NULL);
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

static void stream_task(void *arg)
{
    esp_http_client_config_t config = {
        .url = WAV_SERVER_URL,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Streaming from %s", config.url);

    esp_http_client_fetch_headers(client);

    int data_read = esp_http_client_read(client,
                                         (char*)buffer,
                                         WAV_FMT_HEADER_SKIP_SIZE);
    if (data_read != WAV_FMT_HEADER_SKIP_SIZE)
        goto end;

    char chunk_id[5];
    uint32_t chunk_size = 0;
    while (strcmp(chunk_id, "data") != 0)
    {
        if (chunk_size != 0)
        {
            data_read = esp_http_client_read(client, (char*)buffer, chunk_size);
            if (data_read != chunk_size)
                goto end;
        }
        data_read = esp_http_client_read(client, (char*)buffer, 8);
        if (data_read != 8)
            goto end;

        memcpy(chunk_id, buffer, 4);
        chunk_id[4] = '\0';
        memcpy(&chunk_size, buffer + 4, 4);
    }

    while ((data_read = esp_http_client_read(client, (char*)buffer, I2S_BUFFER_SIZE)) > 0)
    {
        if (stop_playback)
        {
            ESP_LOGI(TAG, "Playback stopped");
            break;
        }

        int16_t* buf16 = (int16_t*)buffer;
        amplify_buffer(buf16, data_read / sizeof(int16_t), gain);
        i2s_write(buffer, data_read, NULL);
    }

end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    i2s_clean();

    /* @todo: Should notify play task rather than sending mqtt message direclty*/
    send_state("idle");

    ESP_LOGI(TAG, "Stream finished");

    stream_task_handle = NULL;
    vTaskDelete(NULL);
}

void stream_start(void)
{
    stop_playback = false;
    xTaskCreate(stream_task, "stream_task", 8192, NULL, 5,
                &stream_task_handle);
}

void stream_stop(void)
{
    stop_playback = true;
}

void stream_gain(float new_gain)
{
    gain = new_gain;
}

void stream_init(void)
{
    i2s_init();
}
