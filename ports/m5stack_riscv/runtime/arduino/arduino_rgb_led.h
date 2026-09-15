/*
 *  rgbLedWrite (ESP32-C6): one WS2812 pixel over RMT TX channel 0
 *
 *  ESP32-C6 (ports/m5stack_riscv) only. The M5Stack core's rgbLedWrite()
 *  (esp32-hal-rgb-led.c) sits on esp32-hal-rmt.c = ESP-IDF's
 *  esp_driver_rmt + FreeRTOS, none of which this port links. This is a
 *  self-contained replacement with the core's signature
 *  (esp32-hal-rgb-led.h:30) and the core's default colour order (GRB,
 *  RGB_BUILTIN_LED_COLOR_ORDER = LED_COLOR_ORDER_GRB), driven through the
 *  header-only hal/rmt_ll.h and direct RMTMEM writes, no interrupt line
 *  (completion is polled), so arduino_interrupt.cfg is unchanged.
 *
 *  The M5NanoC6's RGB LED is data = G20, and its power gate is G19 (M5
 *  docs; unverified on hardware as of stage 6 task 1). This function only
 *  drives the data pin it is given; the sketch enables the power pin.
 *
 *  Task context only, one pixel per call (no chaining). pinMode(pin,
 *  OUTPUT) is applied to the data pin on the first call (and whenever the
 *  pin changes), then the pad's output is re-routed from simple GPIO to
 *  RMT_SIG_OUT0; a later digitalWrite() on that pin has no effect until
 *  pinMode() is called on it again.
 */

#ifndef TOPPERS_ARDUINO_RGB_LED_H
#define TOPPERS_ARDUINO_RGB_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  Same signature as the M5Stack core (esp32-hal-rgb-led.h:30). */
void rgbLedWrite(uint8_t pin, uint8_t red_val, uint8_t green_val, uint8_t blue_val);

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_ARDUINO_RGB_LED_H */
