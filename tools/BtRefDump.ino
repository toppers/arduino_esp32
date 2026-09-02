// Reference dump: stock M5Stack core + BluetoothSerial.
// Reads exactly the registers the TOPPERS/FMP3 bt-classic profile reads, so a
// working Bluetooth Classic bring-up can be diffed against the one that hangs.
#include <BluetoothSerial.h>

BluetoothSerial SerialBT;

static inline uint32_t rd(uint32_t a) { return *(volatile uint32_t *)a; }

static void dump(const char *when)
{
  uint32_t intenable;
  __asm__ __volatile__ ("rsr.intenable %0" : "=a" (intenable));

  Serial.printf("== %s ==\n", when);
  Serial.printf("wifi_clk_en=%08x core_rst_en=%08x\n", rd(0x3FF000CC), rd(0x3FF000D0));
  Serial.printf("rtc_clk_conf=%08x cpu_per_conf=%08x\n", rd(0x3FF48070), rd(0x3FF0003C));
  Serial.printf("intenable=%08x dport_status0=%08x status1=%08x\n",
                intenable, rd(0x3FF000EC), rd(0x3FF000F0));
  for (int i = 0; i < 16; i++) {
    uint32_t m = rd(0x3FF00104 + i * 4) & 0x1F;
    if (m != 16) Serial.printf("map[src %d] -> cpu line %u\n", i, m);
  }
  Serial.printf("bb[0f8]=%08x bb[0a0]=%08x bb[040]=%08x em0=%08x\n",
                rd(0x3FF510F8), rd(0x3FF510A0), rd(0x3FF51040), rd(0x3FFB0000));
  Serial.println();
}

void setup()
{
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n[BtRefDump] start");
  dump("before SerialBT.begin");

  bool ok = SerialBT.begin("M5Stack-REF");
  Serial.printf("[BtRefDump] SerialBT.begin -> %d\n", ok ? 1 : 0);
  delay(500);
  dump("after SerialBT.begin");
}

void loop()
{
  static uint32_t n;
  delay(3000);
  if (n++ < 3) dump("steady state");
}
