#include "ToppersFMP3_BT.h"

/*
 * The runtime side lives in the prebuilt FMP3 stage
 * (ports/m5stack_xtensa/runtime/bt/adapter/toppers_bt_spp.c) and is present
 * only in the bt-classic profile, which is an M5Stack Core (ESP32) board
 * option. Selecting any other profile and calling these leaves the link with
 * undefined toppers_bt_spp_* symbols, which is the intended failure: the
 * capability is not there to be had.
 */
extern "C" {
bool     toppers_bt_spp_begin(const char *device_name);
void     toppers_bt_spp_end(void);
bool     toppers_bt_spp_connected(void);
size_t   toppers_bt_spp_available(void);
int      toppers_bt_spp_read(void);
size_t   toppers_bt_spp_read_bytes(uint8_t *buf, size_t len);
size_t   toppers_bt_spp_write(const uint8_t *buf, size_t len);
uint32_t toppers_bt_spp_dropped(void);
void     toppers_bt_spp_log_line(const char *message);
}

ToppersFMP3BTClass BT;

extern "C" bool toppersBtBegin(const char *device_name)
{
    return toppers_bt_spp_begin(device_name);
}

extern "C" void toppersBtEnd(void)
{
    toppers_bt_spp_end();
}

extern "C" bool toppersBtConnected(void)
{
    return toppers_bt_spp_connected();
}

extern "C" int toppersBtAvailable(void)
{
    return static_cast<int>(toppers_bt_spp_available());
}

extern "C" int toppersBtRead(void)
{
    return toppers_bt_spp_read();
}

extern "C" size_t toppersBtReadBytes(uint8_t *buf, size_t len)
{
    return toppers_bt_spp_read_bytes(buf, len);
}

extern "C" size_t toppersBtWrite(const uint8_t *buf, size_t len)
{
    return toppers_bt_spp_write(buf, len);
}

extern "C" uint32_t toppersBtDropped(void)
{
    return toppers_bt_spp_dropped();
}

extern "C" void toppersBtLog(const char *message)
{
    toppers_bt_spp_log_line(message);
}

bool ToppersFMP3BTClass::begin(const char *deviceName)
{
    return toppersBtBegin(deviceName);
}

void ToppersFMP3BTClass::end() { toppersBtEnd(); }
bool ToppersFMP3BTClass::connected() { return toppersBtConnected(); }
int ToppersFMP3BTClass::available() { return toppersBtAvailable(); }
int ToppersFMP3BTClass::read() { return toppersBtRead(); }

size_t ToppersFMP3BTClass::readBytes(uint8_t *buf, size_t len)
{
    return toppersBtReadBytes(buf, len);
}

size_t ToppersFMP3BTClass::write(const uint8_t *buf, size_t len)
{
    return toppersBtWrite(buf, len);
}

size_t ToppersFMP3BTClass::write(uint8_t byte)
{
    return toppersBtWrite(&byte, 1U);
}

uint32_t ToppersFMP3BTClass::droppedBytes() { return toppersBtDropped(); }
