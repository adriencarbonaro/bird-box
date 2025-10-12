#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "math.h"

#define HI_BYTE(x) ((x) >> 8) & 0xff
#define LO_BYTE(x) (x) & 0xff

#define TYPEDEF(x) typedef uint##x##_t uint##x
TYPEDEF(8);
TYPEDEF(16);
TYPEDEF(32);

static const char *TAG = "birdbox";

#define I2S_BCLK   25
#define I2S_LRCLK  22
#define I2S_DOUT   26

#define SAMPLE_RATE     44100

#define CHUNK_SIZE 4096
#define CHUNK_TO_READ 1

#define WAV_HEADER_SIZE 78
#define RIFF_HEADER_OFFSET 12

static i2s_chan_handle_t tx_chan;

FILE *f;
static uint8 buffer[CHUNK_SIZE] = { 0 };

uint32 current = WAV_HEADER_SIZE;
uint8 end = false;

int find_data_offset(FILE *fp) {
    char chunk_id[5] = {0};
    uint32_t chunk_size;

    fseek(fp, RIFF_HEADER_OFFSET, SEEK_SET);

    while (fread(chunk_id, 1, 4, fp) == 4) {
        fread(&chunk_size, 4, 1, fp);

        if (strncmp(chunk_id, "data", 4) == 0) {
            return ftell(fp); // Found "data" chunk, return offset
        }

        // Skip this chunk's data
        fseek(fp, chunk_size, SEEK_CUR);
    }

    return -1; // data chunk not found
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

static void i2s_write(const uint8* data, const uint16 data_len, size_t* written)
{
    if (i2s_channel_write(tx_chan, data, data_len, written, 1000))
    {
        ESP_LOGI(TAG, "Write Task: i2s write failed");
    }
}

static void play(void)
{
    size_t written = 0;
    if (fseek(f, current, SEEK_SET))
    {
        ESP_LOGE(TAG, "Failed to seek position %u in file", current);
    }
    size_t block_read = fread(buffer, CHUNK_SIZE, CHUNK_TO_READ, f);
    if (block_read != CHUNK_TO_READ)
    {
        if (feof(f))
        {
            ESP_LOGI(TAG, "End of file (block read: %u)", block_read);
            end = true;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to read file");
        }
    }

    current += CHUNK_SIZE;

    i2s_write(buffer, CHUNK_SIZE, &written);

    memset(buffer, 0, CHUNK_SIZE);
}

static void reset(void)
{
    current = WAV_HEADER_SIZE;
    end = false;
}

static void play_task(void *arg)
{
    i2s_init(SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);

    f = fopen("/littlefs/sound_44100_1ch.wav", "rb");

    while (1)
    {
        if (end)
        {
            reset();
        }
        play();
    }

    ESP_LOGI(TAG, "end is true, end playing");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Birdbox with trigger input starting...");

    /* Print chip information */
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "This is %s chip with %d CPU cores, WiFi%s%s, ",
        CONFIG_IDF_TARGET,
        chip_info.cores,
        (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
        (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

    ESP_LOGI(TAG, "silicon revision %d, ", chip_info.revision);

    ESP_LOGI(TAG, "Free heap: %lu", esp_get_free_heap_size());

    ESP_LOGI(TAG, "Now we are starting the LittleFs Demo ...");


    /* Setup flash */
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGE(TAG, "Failed to find LittleFS partition");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get LittleFS partition information (%s)", esp_err_to_name(ret));
        esp_littlefs_format(conf.partition_label);
    }
    else
    {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    // Start playback task
    i2s_init(SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    xTaskCreate(play_task, "play_task", 4096, NULL, 5, NULL);
}
