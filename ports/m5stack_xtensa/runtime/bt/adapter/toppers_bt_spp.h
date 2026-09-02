/*
 * Bluetooth Classic SPP server for Arduino sketches.
 *
 * See docs/superpowers/specs/2026-09-02-bt-classic-spp-design.md. The sketch
 * facing wrapper is src/ToppersFMP3_BT.{h,cpp}; this is the layer underneath.
 */
#ifndef TOPPERS_BT_SPP_H
#define TOPPERS_BT_SPP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bring the controller, BlueDroid and the SPP server up under device_name.
 * Returns false, with the reason in the kernel log, rather than blocking
 * forever when a stage does not come up.
 */
bool toppers_bt_spp_begin(const char *device_name);

/* Tear the whole stack back down. Safe to call when begin() failed. */
void toppers_bt_spp_end(void);

/* True while a remote device holds an SPP connection. */
bool toppers_bt_spp_connected(void);

/* Bytes waiting in the receive ring. */
size_t toppers_bt_spp_available(void);

/* One byte, or -1 when the ring is empty. */
int toppers_bt_spp_read(void);

/* Up to len bytes; returns how many were taken. */
size_t toppers_bt_spp_read_bytes(uint8_t *buf, size_t len);

/*
 * Hand len bytes to the peer. Returns how many were accepted: 0 when there is
 * no connection, or when the peer has congested the link.
 */
size_t toppers_bt_spp_write(const uint8_t *buf, size_t len);

/*
 * Bytes the receive ring had to drop because the sketch did not read fast
 * enough. Monotonic; a sketch that cares can watch it move.
 */
uint32_t toppers_bt_spp_dropped(void);

#ifdef __cplusplus
}
#endif

#endif /* TOPPERS_BT_SPP_H */
