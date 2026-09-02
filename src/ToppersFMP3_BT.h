#ifndef TOPPERS_FMP3_BT_H
#define TOPPERS_FMP3_BT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Bluetooth Classic SPP for sketches, on the M5Stack Basic (ESP32).
 *
 * Not a Stream. This port does not link the M5Stack core's core.a, so Print
 * and Stream do not exist here; the methods are named after Stream's so a
 * sketch reads the way an Arduino sketch does, but BT cannot be handed to
 * anything expecting a Stream. Same shape as ToppersFMP3_WiFi.
 *
 * ESP32-S3 has no Bluetooth Classic at all, so this exists on the
 * M5Stack Core (ESP32) board of this package and nowhere else.
 *
 * SECURITY: begin() makes the device discoverable and accepts any pairing
 * request - Secure Simple Pairing confirmations are auto-accepted and legacy
 * pairing answers with PIN 1234. Any device in range can pair and connect.
 * Do not put anything on this link you would not broadcast.
 */

#ifdef __cplusplus
extern "C" {
#endif

bool     toppersBtBegin(const char *device_name);
void     toppersBtEnd(void);
bool     toppersBtConnected(void);
int      toppersBtAvailable(void);
int      toppersBtRead(void);
size_t   toppersBtReadBytes(uint8_t *buf, size_t len);
size_t   toppersBtWrite(const uint8_t *buf, size_t len);
uint32_t toppersBtDropped(void);

/*
 * One line to the kernel log. A sketch has no Serial here, so this is how an
 * example says anything; same helper the Wi-Fi examples use.
 */
void     toppersBtLog(const char *message);

#ifdef __cplusplus
}

class ToppersFMP3BTClass {
public:
    /*
     * Bring the stack up and start the SPP server under device_name, which is
     * what a phone shows in its pairing list. Returns false if a stage did not
     * come up; the reason goes to the kernel log.
     */
    bool begin(const char *deviceName = "M5Stack-SPP");

    /* Tear it back down. Safe to call when begin() failed. */
    void end();

    /* True while a remote device holds a connection. */
    bool connected();

    /* Bytes waiting to be read. */
    int available();

    /* One byte, or -1 when nothing is waiting. */
    int read();

    /* Up to len bytes; returns how many were taken. */
    size_t readBytes(uint8_t *buf, size_t len);

    /*
     * Send bytes. Returns how many were accepted: 0 when nothing is connected,
     * and 0 when the peer has congested the link rather than queueing without
     * bound.
     */
    size_t write(const uint8_t *buf, size_t len);
    size_t write(uint8_t byte);

    /*
     * Bytes the receive buffer had to drop because the sketch did not read
     * them in time. Monotonic. A sketch that cares whether it is keeping up
     * can watch this move instead of guessing.
     */
    uint32_t droppedBytes();
};

extern ToppersFMP3BTClass BT;

#endif  // __cplusplus

#endif  // TOPPERS_FMP3_BT_H
