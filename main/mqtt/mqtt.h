#ifndef MQTT_H_
#define MQTT_H_

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"

#define MAX_NB_TOPICS 4

typedef void (*mqtt_event_listener_cb_t)(const char* topic,
                                         int topic_len,
                                         const char* msg,
                                         int msg_len);

void mqtt_subscribe_topic(const char* topic);
void mqtt_subscribe_listener(mqtt_event_listener_cb_t callback);

void mqtt_init(void);
void mqtt_start(EventGroupHandle_t wifi_event_group);

void send_state(const char* state_str);

#endif /* MQTT_H_ */
