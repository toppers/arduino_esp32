/*
 *  rgbLedWrite: one WS2812 / SK6812 pixel over RMT TX channel 0
 *
 *  One prototype, two implementations in ports/m5stack_xtensa, one per chip
 *  (the CMakeLists links exactly one, into wifi-connect only):
 *    arduino_rgb_led.c      ESP32-S3  (M5AtomS3 Lite: WS2812B-2020 on G35)
 *    arduino_rgb_led_lx6.c  ESP32/LX6 (M5AtomLite: SK6812 3535 on G27)
 *  Counterpart of the ESP32-C6 file of the same name
 *  (ports/m5stack_riscv/runtime/arduino/arduino_rgb_led.c); each .c file
 *  lists what differs from the others. The rest of this comment is written
 *  from the S3 file's point of view; the LX6 file's own header says where
 *  it departs from it (the timeout path, the timings, the pin rule).
 *
 *  The M5Stack core's rgbLedWrite() (esp32-hal-rgb-led.c) sits on
 *  esp32-hal-rmt.c = ESP-IDF's esp_driver_rmt + FreeRTOS, none of which this
 *  port links. This is a self-contained replacement with the core's
 *  signature (esp32-hal-rgb-led.h:30) and the core's default colour order
 *  (GRB, RGB_BUILTIN_LED_COLOR_ORDER = LED_COLOR_ORDER_GRB), driven through
 *  the header-only hal/rmt_ll.h and direct RMTMEM writes, with no interrupt
 *  line (completion is polled), so arduino_interrupt.cfg is unchanged.
 *
 *  The M5AtomS3 Lite's RGB LED (WS2812B-2020) is data = G35 and has no
 *  power gate (M5Unified's _pin_table_other0: board_M5AtomS3Lite ->
 *  GPIO_NUM_35; the M5NanoC6, by contrast, needs G19 high). This function
 *  only drives the data pin it is given.
 *
 *  Task context only, one pixel per call (no chaining). Every call applies
 *  pinMode(pin, OUTPUT) to the data pin and then re-routes the pad's output
 *  from simple GPIO to RMT_SIG_OUT0 (not cached, so a pinMode() on the pin
 *  between two writes cannot leave the LED silently un-routed); a
 *  digitalWrite() on that pin has no effect until pinMode() is called on it
 *  again. The pin rule is pinMode's (ard_gpio_pin_ok): the USB Serial/JTAG
 *  pads, the MSPI pads, the pins outside SOC_GPIO_VALID_GPIO_MASK and
 *  pin >= GPIO_NUM_MAX are refused before anything is touched. G35 is one of
 *  the octal-PSRAM pads (33-37), which this port deliberately allows: a
 *  board without octal PSRAM - the AtomS3 Lite has none at all - uses them
 *  as ordinary GPIOs.
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
