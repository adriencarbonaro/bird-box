#ifndef STREAM_H_
#define STREAM_H_

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Structs - Enums ************************************************************/

/* Prototypes *****************************************************************/
void stream_init(EventGroupHandle_t stop_event_group);

#endif /* STREAM_H_ */
