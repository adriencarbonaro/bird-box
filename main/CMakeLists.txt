set(srcs
    main.c
    audio/audio.c
    audio/audio.h
    stream/stream.c
    stream/stream.h
    supervisor/supervisor.c
    supervisor/supervisor.h
    mp3/mp3.c
    mp3/mp3.h
    mqtt/mqtt.c
    mqtt/mqtt.h
    utils/types.h
    utils/utils.h
    wifi/wifi.c
    wifi/wifi.h
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
                 "audio"
                 "mp3"
                 "mqtt"
                 "stream"
                 "supervisor"
                 "utils"
                 "wifi"
    REQUIRES esp_driver_i2s
             esp_driver_gpio
             esp_http_client
             esp_ringbuf
             esp_wifi
             nvs_flash
             mqtt
)
