#ifndef TOPPERS_FMP3_ARDUINO_BRIDGE_H
#define TOPPERS_FMP3_ARDUINO_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t toppers_arduino_setup_calls;
extern volatile uint32_t toppers_arduino_loop_calls;
extern volatile int32_t toppers_arduino_last_delay_result;

void toppers_arduino_task(intptr_t exinf);

/*
 * Override this weak hook when Arduino core initialization becomes available.
 * The bridge deliberately does not link initArduino() or core.a because they pull
 * the ESP-IDF startup and FreeRTOS boundary back into the image.
 */
void toppers_arduino_runtime_init(void);

#ifdef __cplusplus
}
#endif

#endif  // TOPPERS_FMP3_ARDUINO_BRIDGE_H
