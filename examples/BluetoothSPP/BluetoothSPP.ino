#include <ToppersFMP3_ArduinoBridge.h>

// This example ships in the package, so File > Examples offers it whichever
// board is selected - the three boards share one architecture, and Arduino has
// no way to show a library example for only one of them. Say so here rather
// than letting the wrong board discover it as a wall of undefined references
// to the Bluetooth stack, which is what happens when the bt-classic stage is
// not the one being linked.
//
// ARDUINO_M5STACK_CORE is passed by the platform's recipe as -DARDUINO_ +
// build.board, so it is the board actually selected in the IDE, not the chip.
#if !defined(ARDUINO_M5STACK_CORE)
#error "BluetoothSPP runs on the M5Core only. The ESP32-S3 has no Bluetooth Classic radio, so the CoreS3 and StickS3 boards do not offer the Bluetooth Classic (SPP) runtime. Select M5Core (TOPPERS/FMP3) and its Bluetooth Classic (SPP) option."
#endif

#include <ToppersFMP3_BT.h>

// Bluetooth Classic SPP echo server for the M5Stack Core (ESP32).
//
// Select the M5Stack Core board with the Bluetooth Classic runtime profile,
// upload, then pair with "M5Stack-SPP" from a phone or PC and open the serial
// port it creates. Anything you type comes back with "echo: " in front of it.
//
// ESP32-S3 has no Bluetooth Classic, so this example does not build for the
// CoreS3 board.
//
// SECURITY: this server requires no authentication to connect. It starts with
// ESP_SPP_SEC_NONE, so anything in range can open the link and exchange data
// without pairing at all - measured on hardware on 2026-09-02: with the bond
// cleared on the PC, the connection succeeded, no bond was created, and no
// pairing confirmation appeared on the device (SSP "Just Works"). The code
// also auto-accepts numeric comparison and answers legacy pairing with PIN
// 1234, but neither is reached on that path. Fine for trying the link out,
// not fine for anything you would not broadcast.

const char DEVICE_NAME[] = "M5Stack-SPP";

static bool started;
static bool wasConnected;
static uint32_t reportedDrops;

// One line's worth of what the peer sent, assembled a byte at a time.
static char lineBuffer[128];
static size_t lineLength;

static void sendEcho(const char *text, size_t length)
{
    static const char prefix[] = "echo: ";

    BT.write(reinterpret_cast<const uint8_t *>(prefix), sizeof(prefix) - 1U);
    BT.write(reinterpret_cast<const uint8_t *>(text), length);
    BT.write(static_cast<uint8_t>('\r'));
    BT.write(static_cast<uint8_t>('\n'));
}

void setup()
{
    started = BT.begin(DEVICE_NAME);
    if (started) {
        toppersBtLog("[BluetoothSPP] discoverable as M5Stack-SPP");
    } else {
        toppersBtLog("[BluetoothSPP] BT.begin failed; see the log above");
    }
}

void loop()
{
    if (!started) return;

    const bool connectedNow = BT.connected();
    if (connectedNow != wasConnected) {
        toppersBtLog(connectedNow ? "[BluetoothSPP] peer connected"
                                  : "[BluetoothSPP] peer disconnected");
        wasConnected = connectedNow;
        lineLength = 0U;
    }
    if (!connectedNow) return;

    while (BT.available() > 0) {
        const int byte = BT.read();
        if (byte < 0) break;

        if (byte == '\n' || byte == '\r') {
            if (lineLength > 0U) {
                sendEcho(lineBuffer, lineLength);
                lineLength = 0U;
            }
        } else if (lineLength < sizeof(lineBuffer)) {
            lineBuffer[lineLength++] = static_cast<char>(byte);
        } else {
            // Longer than the buffer: send what there is and keep going rather
            // than dropping the rest silently.
            sendEcho(lineBuffer, lineLength);
            lineLength = 0U;
        }
    }

    // Bytes the receive buffer had to drop because this loop did not keep up.
    // Reported once per new drop so a slow sketch is visible rather than
    // looking like a flaky link.
    const uint32_t drops = BT.droppedBytes();
    if (drops != reportedDrops) {
        toppersBtLog("[BluetoothSPP] receive buffer overflowed; bytes lost");
        reportedDrops = drops;
    }
}
