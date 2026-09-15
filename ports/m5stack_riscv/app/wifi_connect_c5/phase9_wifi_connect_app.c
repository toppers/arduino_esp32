/*
 * Stable FMP3 application identity for the Arduino Wi-Fi connect profile.
 *
 * ESP32-C5 (ports/m5stack_riscv, C5 plan stage 3) copy of the C6 file
 * ../wifi_connect/phase9_wifi_connect_app.c (itself a copy of the Xtensa
 * ports/m5stack_xtensa/app/wifi_connect/phase9_wifi_connect_app.c). The
 * directory exists for the cfg beside it (arduino_interrupt_c5.cfg is
 * included there instead of arduino_interrupt.cfg); the executable task
 * body is compiled by the Arduino builder and linked as an external object
 * (toppers_arduino_task, src/bridge/ArduinoSketchBridge.cpp).
 */
const char toppers_phase9_wifi_connect_application[] =
    "Arduino WiFi connect bridge";
