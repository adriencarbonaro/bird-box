#ifndef MQTT_H_
#define MQTT_H_

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

#define MAX_NB_TOPICS 4

typedef struct {
    const char* topics[MAX_NB_TOPICS];
    bool is_connected;
} mqtt_task_data_t;

typedef void (*mqtt_event_listener_cb_t)(const char* topic,
                                         int topic_len,
                                         const char* msg,
                                         int msg_len);

void mqtt_subscribe_topic(const char* topic);
void mqtt_subscribe_listener(mqtt_event_listener_cb_t callback);

void mqtt_init(void);
void mqtt_start(EventGroupHandle_t wifi_event_group);

void send_status(const char* status_str);

#endif /* MQTT_H_ */
