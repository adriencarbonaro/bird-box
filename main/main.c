#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_chip_info.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <string.h>

#define I2S_NUM                         (0)
#define SAMPLE_RATE                     (44100)

#define I2S_BUFFER_SIZE                 (4096)
#define I2S_BCLK                        (25)
#define I2S_LRCLK                       (22)
#define I2S_DOUT                        (26)

#define WAV_FMT_HEADER_OFFSET           (20)
#define WAV_FMT_HEADER_SIZE             (16)
#define WAV_FMT_HEADER_SKIP_SIZE        (WAV_FMT_HEADER_OFFSET + WAV_FMT_HEADER_SIZE)

#define HI_BYTE(x) ((x) >> 8) & 0xff
#define LO_BYTE(x) (x) & 0xff

#define TYPEDEF(x) typedef uint##x##_t uint##x
TYPEDEF(8);
TYPEDEF(16);
TYPEDEF(32);

void play(void *arg);

static const char *TAG = "birdbox";

static i2s_chan_handle_t tx_chan;

static uint8 buffer[I2S_BUFFER_SIZE];

void amplify_buffer(int16_t *data, size_t len, float gain)
{
    for (size_t i = 0; i < len; i++)
    {
        int32_t sample = (int32_t)(data[i] * gain);
        if (sample > INT16_MAX) sample = INT16_MAX;
        else if (sample < INT16_MIN) sample = INT16_MIN;
        data[i] = (int16_t)sample;
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xTaskCreate(play, "play_task", 4096, NULL, 5, NULL);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGW(TAG, "Disconnected, reason: %d", d->reason);
        esp_wifi_connect();
    }
}

static void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_BIRDBOX_WIFI_SSID,
            .password = CONFIG_BIRDBOX_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Wi-Fi config: ssid=%s password:%s",
             wifi_config.sta.ssid, wifi_config.sta.password);

    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi initialized and connecting...");
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

int32_t parse_wav_header(const uint8_t *buffer, size_t buf_size)
{
    size_t offset = 0;

    // Check RIFF header
    if ((buf_size < 12) ||
        (memcmp(buffer, "RIFF", 4) != 0) ||
        (memcmp(buffer + 8, "WAVE", 4) != 0))
    {
        return -1; // Not a valid WAV
    }

    offset = 12;

    // Loop over chunks until we find "data"
    while (offset + 8 <= buf_size)
    {
        char chunk_id[5];
        uint32_t chunk_size;
        memcpy(chunk_id, buffer + offset, 4);
        chunk_id[4] = '\0';
        memcpy(&chunk_size, buffer + offset + 4, 4); // Little endian

        offset += 8;

        if (strcmp(chunk_id, "data") == 0)
            return offset; // audio data starts here

        // Skip this chunk’s data
        offset += chunk_size;

        // Chunk size is padded to even number of bytes
        if (chunk_size % 2 == 1)
            offset++;
    }

    return -1; // No data chunk found
}

void stream_wav_over_http(const char *url)
{
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (esp_http_client_open(client, 0) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return;
    }

    ESP_LOGI(TAG, "Streaming from %s", url);

    esp_http_client_fetch_headers(client);
    int64_t content_length = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "Server response content-length: %lld", content_length);
    if (content_length == 0)
        goto end;

    vTaskDelay(pdMS_TO_TICKS(100));

    int data_read = esp_http_client_read(client, (char*)buffer, WAV_FMT_HEADER_SKIP_SIZE);
    if (data_read != WAV_FMT_HEADER_SKIP_SIZE)
        goto end;

    char chunk_id[5];
    uint32_t chunk_size = 0;
    while (strcmp(chunk_id, "data") != 0)
    {
        if (chunk_size != 0)
        {
            // flush chunk
            data_read = esp_http_client_read(client, (char*)buffer, chunk_size);
            if (data_read != chunk_size)
                goto end;
        }
        data_read = esp_http_client_read(client, (char*)buffer, 8);
        if (data_read != 8)
            goto end;

        memcpy(chunk_id, buffer, 4);
        chunk_id[4] = '\0';
        memcpy(&chunk_size, buffer + 4, 4); // Little endian
    }

    while ((data_read = esp_http_client_read(client, (char*)buffer, I2S_BUFFER_SIZE)) > 0)
    {
        int16_t* buf16 = (int16_t*)buffer;
        amplify_buffer(buf16, (I2S_BUFFER_SIZE / 2), CONFIG_BIRDBOX_AMPLIFY_GAIN * 1.0f);
        i2s_write(buffer, data_read, NULL);
    }

end:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    i2s_clean();
    ESP_LOGI(TAG, "Stream finished");
}

static void i2s_init(uint32_t sample_rate, i2s_data_bit_width_t bits, uint16_t channels)
{
    i2s_chan_config_t tx_chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&tx_chan_cfg, &tx_chan, NULL));

    i2s_std_config_t tx_std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, channels),
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

void play(void *arg)
{
    ESP_LOGI(TAG, "Starting playback (url=%s)...", CONFIG_BIRDBOX_WAV_SERVER_URL);
    while (1)
    {
        stream_wav_over_http(CONFIG_BIRDBOX_WAV_SERVER_URL);
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    i2s_init(SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
}
