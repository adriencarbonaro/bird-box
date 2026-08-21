set(srcs
    main.c
    ha_entities.c
    audio/audio.c
    audio/audio.h
    stream/stream.c
    stream/stream.h
    supervisor/supervisor.c
    supervisor/supervisor.h
    mp3/mp3.c
    mp3/mp3.h
    utils/types.h
    utils/utils.h
)

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
                 "audio"
                 "mp3"
                 "stream"
                 "supervisor"
                 "utils"
    REQUIRES esp_driver_i2s
             esp_driver_gpio
             esp_http_client
             esp_ringbuf
             ha-mqtt
             wifi
)

# Resolve version.h, generated into the build dir by the Makefile `version`
# target (scripts/build_version.py).
target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_BINARY_DIR}/"
)
