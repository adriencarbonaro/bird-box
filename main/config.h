#ifndef CONFIG_H_
#define CONFIG_H_

#define I2S_NUM                         (0)
#define SAMPLE_RATE                     (44100)

#define I2S_BUFFER_SIZE                 (4096)
#define I2S_BCLK                        (25)
#define I2S_LRCLK                       (22)
#define I2S_DOUT                        (26)

#define AMPLIFY_GAIN                    CONFIG_BIRDBOX_AMPLIFY_GAIN
#define WAV_SERVER_URL                  CONFIG_BIRDBOX_WAV_SERVER_URL

#define MQTT_URI                        CONFIG_BIRDBOX_MQTT_URI

#define MQTT_TOPIC_VERSION              CONFIG_BIRDBOX_MQTT_TOPIC_VERSION
#define MQTT_TOPIC_SET                  CONFIG_BIRDBOX_MQTT_TOPIC_SET
#define MQTT_TOPIC_VOLUME               CONFIG_BIRDBOX_MQTT_TOPIC_VOLUME
#define MQTT_TOPIC_STATE                CONFIG_BIRDBOX_MQTT_TOPIC_STATE

#define MQTT_MSG_PLAY                   CONFIG_BIRDBOX_MQTT_MSG_PLAY
#define MQTT_MSG_PAUSE                  CONFIG_BIRDBOX_MQTT_MSG_PAUSE

#define WIFI_SSID                       CONFIG_BIRDBOX_WIFI_SSID
#define WIFI_PASSWORD                   CONFIG_BIRDBOX_WIFI_PASSWORD

#endif /* CONFIG_H_ */
