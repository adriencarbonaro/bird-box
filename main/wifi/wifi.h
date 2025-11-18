#ifndef WIFI_H_
#define WIFI_H_

#include "esp_bit_defs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

void wifi_init(EventGroupHandle_t event_group);

#endif /* WIFI_H_ */

