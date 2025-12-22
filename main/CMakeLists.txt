set(srcs
    main.c
    play/play.c
    play/play.h
    stream/stream.c
    stream/stream.h
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
                 "mqtt"
                 "play"
                 "stream"
                 "utils"
                 "wifi"
)
