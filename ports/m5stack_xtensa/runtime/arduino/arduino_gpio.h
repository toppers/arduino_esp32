/*
 *  pinMode / digitalWrite / digitalRead (ESP32-S3 / ESP32)
 *
 *  Xtensa (ports/m5stack_xtensa: ESP32-S3 and ESP32) counterpart of the
 *  ESP32-C6 file of the same name (ports/m5stack_riscv, stage 6). This
 *  port links neither the M5Stack core's esp32-hal-gpio.c.o (the link
 *  driver takes the sketch objects and the stage, never build/core) nor
 *  ESP-IDF's driver/gpio, so a sketch that wrote pinMode() failed at the
 *  linker. arduino_gpio.c provides the three functions with the core's
 *  exact signatures (esp32-hal-gpio.h) on top of the header-only
 *  hal/gpio_ll.h (the ESP32 needs its own IO_MUX offset table, see the .c).
 *
 *  Division of labour with arduino_interrupt.c (ruling R3, stage 6):
 *  attachInterrupt() does NOT call pinMode(). The sketch configures the pad
 *  (pinMode) first and attaches the callback second; attachInterrupt only
 *  programs the interrupt type / enable bits of the pin register, so the
 *  order pinMode -> attachInterrupt leaves both settings in place, and a
 *  later pinMode() on the same pin does not touch int_type / int_ena.
 *
 *  The mode and level constants below carry the VALUES of the M5Stack core
 *  3.3.8 esp32-hal-gpio.h (INPUT 0x01, OUTPUT 0x03, ...). A sketch includes
 *  Arduino.h and sees the core's definitions; arduino_gpio.c includes this
 *  header and sees these. Both are guarded with #ifndef so that a TU that
 *  has both agrees on one value, and the _Static_assert in arduino_gpio.c
 *  pins the pairing (OUTPUT contains the INPUT bit, as in the core, which
 *  is why an OUTPUT pin is readable).
 */

#ifndef TOPPERS_ARDUINO_GPIO_H
#define TOPPERS_ARDUINO_GPIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*  Levels (esp32-hal-gpio.h:42-43). */
#ifndef LOW
#define LOW		0x0
#endif
#ifndef HIGH
#define HIGH	0x1
#endif

/*  Pin modes (esp32-hal-gpio.h:46-55). Bit layout of the core: bit0 =
 *  input buffer, bit1 = output driver, bit2 = pull-up, bit3 = pull-down,
 *  bit4 = open drain. Only INPUT, INPUT_PULLUP, INPUT_PULLDOWN and OUTPUT
 *  are accepted by this port's pinMode(); the rest are defined so that a
 *  sketch's request can be recognised and refused by value. */
#ifndef INPUT
#define INPUT				0x01
#endif
#ifndef OUTPUT
#define OUTPUT				0x03
#endif
#ifndef PULLUP
#define PULLUP				0x04
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP		0x05
#endif
#ifndef PULLDOWN
#define PULLDOWN			0x08
#endif
#ifndef INPUT_PULLDOWN
#define INPUT_PULLDOWN		0x09
#endif
#ifndef OPEN_DRAIN
#define OPEN_DRAIN			0x10
#endif
#ifndef OUTPUT_OPEN_DRAIN
#define OUTPUT_OPEN_DRAIN	0x13
#endif

/*  Same signatures as the M5Stack core (esp32-hal-gpio.h:74-76). */
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int  digitalRead(uint8_t pin);

/*  The pin rule of this port: false for the console pads (ESP32-S3: USB
 *  Serial/JTAG GPIO 19/20; ESP32: UART0 GPIO 1/3), the SPI flash pads
 *  (ESP32-S3: 26-32; ESP32: 6-11), pads that are not GPIOs on the ESP32
 *  (20, 24, 28-31) and pin >= GPIO_NUM_MAX. */
bool ard_gpio_pin_ok(uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_ARDUINO_GPIO_H */
