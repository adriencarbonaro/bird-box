set(srcs
    main.c
    mqtt/mqtt.c
    mqtt/mqtt.h
    utils/types.h
    utils/utils.h
    wifi/wifi.c
    wifi/wifi.h
)

# Generate version.c from git
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    set(VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/version.c)
    message("Creating ${VERSION_FILE}")
    add_custom_command(
      OUTPUT ${VERSION_FILE}
      COMMAND ${CMAKE_COMMAND} -E echo "const char *git_version = \"$(git describe --tags --always --dirty 2>/dev/null || echo unknown)\";" > ${VERSION_FILE}
      VERBATIM)
    list(APPEND srcs ${VERSION_FILE})
    message("Created ${VERSION_FILE}")
else()
    message("Cannot create ${VERSION_FILE}")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
                 "mqtt"
                 "utils"
                 "wifi"
)
