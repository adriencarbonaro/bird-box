#ifndef CONFIG_H_
#define CONFIG_H_

#include "sdkconfig.h"
#include "driver/i2s_std.h"

#define SAMPLE_RATE                     (44100)

#define I2S_BCLK                        (25)
#define I2S_LRCLK                       (22)
#define I2S_DOUT                        (26)

#define AMPLIFY_GAIN                    CONFIG_BIRDBOX_AMPLIFY_GAIN
#define MP3_SERVER_URL                  CONFIG_BIRDBOX_MP3_SERVER_URL

#endif /* CONFIG_H_ */
