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
 *  docs; verified by eye in stage 6: G19 HIGH lights, G19 untouched does
 *  not, colour order red/green/blue is right). This function only
 *  drives the data pin it is given; the sketch enables the power pin.
 *
 *  Task context only, one pixel per call (no chaining). Every call applies
 *  pinMode(pin, OUTPUT) to the data pin and then re-routes the pad's
 *  output from simple GPIO to RMT_SIG_OUT0 (not cached, so a pinMode() on
 *  the pin between two writes cannot leave the LED silently un-routed); a
 *  digitalWrite() on that pin has no effect until pinMode() is called on
 *  it again. The pin rule is pinMode's (ard_gpio_pin_ok): GPIO 12/13 and
 *  pin >= GPIO_NUM_MAX are refused before anything is touched.
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
