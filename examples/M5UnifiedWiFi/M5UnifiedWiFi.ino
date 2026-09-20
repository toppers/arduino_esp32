/*
 *  M5UnifiedWiFi - 画面と Wi-Fi を 1 つのスケッチで使う（試作）
 *
 *  Tools > FMP3 Runtime > All-in-one (experimental) を選んでビルドする。
 *  この構成は M5Unified（M5GFX + SMP）と Wi-Fi スタックを 1 つのステージに
 *  まとめたもので、既定のパッケージには入っていない（BUILDING.md の
 *  --profiles all-in-one でステージを作った場合にだけメニューに出る）。
 *
 *  やること:
 *    - LCD を上げて、Wi-Fi のスキャン結果を画面に並べる
 *    - WIFI_SSID を書いておけば STA 接続まで行い、IP を画面に出す
 *    - 1 秒ごとに画面を書き替え、15 秒ごとに再スキャンしながら、シムの
 *      動的オブジェクト計器（セマフォの生存数・acre_sem の失敗数）を
 *      シリアルへ出す
 *
 *  最後の 1 つがこの例題の主目的である。画面と Wi-Fi を同時に使うと
 *  動的セマフォがいくつ要るのかは、この構成を作った時点では測られて
 *  いなかった（fmp_app/allinone/allinone_app.cfg の AID_SEM のコメント）。
 *  足りなくなると acre_sem が失敗し、fail が増える。
 */

#include <ToppersFMP3_M5Unified.h>

#if defined(TOPPERS_FMP3_RUNTIME_SELECTED) \
    && !defined(TOPPERS_FMP3_RUNTIME_ALL_IN_ONE)
#error "M5UnifiedWiFi needs the all-in-one runtime. Select Tools > FMP3 Runtime > All-in-one (experimental). M5Unified and the Wi-Fi stack are linked into the same stage only there."
#endif

#include <ToppersFMP3_WiFi.h>

//  接続したい AP を書く。空のままなら scan だけを行う。
char WIFI_SSID[33] = "";
char WIFI_PASSWORD[65] = "";

extern "C" void toppers_fmp3_wifi_log_line(const char *message);
extern "C" void esp_shim_task_delay(uint32_t tick);

//  シムの診断カウンタ（wifi/shim/esp_shim_sem.c の公開変数）。
extern "C" volatile uint32_t esp_shim_sem_live;
extern "C" volatile uint32_t esp_shim_sem_acre_fail;
extern "C" volatile uint32_t esp_shim_sem_del_fail;

namespace {

//  snprintf は使わない（この構成の libc は最小限で、例題に持ち込みたくない）。
char line[128];
uint32_t lineLength;

void put(const char *text)
{
    for (const char *p = text; *p != '\0'; ++p) {
        if (lineLength + 1U >= sizeof(line)) return;
        line[lineLength++] = *p;
    }
    line[lineLength] = '\0';
}

void putNumber(int32_t value)
{
    char digits[12];
    int index = 0;
    if (value < 0) {
        put("-");
        value = -value;
    }
    do {
        digits[index++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && index < 11);
    while (index > 0) {
        if (lineLength + 1U >= sizeof(line)) return;
        line[lineLength++] = digits[--index];
    }
    line[lineLength] = '\0';
}

void beginLine(const char *text)
{
    lineLength = 0;
    line[0] = '\0';
    put(text);
}

void flushLine()
{
    toppers_fmp3_wifi_log_line(line);
}

int16_t scanResult = -1;
uint32_t ticks;
uint32_t semLivePeak;

void showScanOnDisplay()
{
    M5.Display.fillScreen(0x0000);
    M5.Display.setCursor(0, 0);
    M5.Display.setTextSize(2);
    M5.Display.print("FMP3 all-in-one\n");
    M5.Display.setTextSize(1);
    if (scanResult <= 0) {
        M5.Display.print("scan: none\n");
        return;
    }
    const int16_t shown = (scanResult < 8) ? scanResult : 8;
    for (int16_t i = 0; i < shown; ++i) {
        M5.Display.printf("%-20.20s %4d ch%d\n",
                          WiFi.SSID(static_cast<uint8_t>(i)),
                          static_cast<int>(WiFi.RSSI(static_cast<uint8_t>(i))),
                          static_cast<int>(WiFi.channel(static_cast<uint8_t>(i))));
    }
}

}  // namespace

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setTextSize(2);
    M5.Display.fillScreen(0x0000);
    M5.Display.print("FMP3 all-in-one\n");

    beginLine("[AIO] display board=");
    putNumber(static_cast<int32_t>(M5.getBoard()));
    put(" w=");
    putNumber(M5.Display.width());
    put(" h=");
    putNumber(M5.Display.height());
    flushLine();

    scanResult = WiFi.scanNetworks();
    beginLine("[AIO] scan=");
    putNumber(scanResult);
    flushLine();
    showScanOnDisplay();

    if (WIFI_SSID[0] != '\0') {
        const uint8_t began = WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        beginLine("[AIO] begin=");
        putNumber(static_cast<int32_t>(began));
        flushLine();
    }
}

void loop()
{
    M5.update();
    ++ticks;

    if (esp_shim_sem_live > semLivePeak) {
        semLivePeak = esp_shim_sem_live;
    }

    //  画面と Wi-Fi を同時に叩くのがこの例題の眼目なので、15 秒ごとに
    //  勝手に再スキャンする（人が居なくても計器として成立させるため）。
    //  BtnA（M5Stack Basic の左ボタン）でも走る。
    if (M5.BtnA.isPressed() || (ticks % 15U) == 0U) {
        scanResult = WiFi.scanNetworks();
        showScanOnDisplay();
    }

    const uint8_t status = WiFi.status();
    if (status == ToppersFMP3WiFiClass::WL_CONNECTED) {
        const uint32_t ip = WiFi.localIP();
        M5.Display.setCursor(0, M5.Display.height() - 16);
        M5.Display.setTextSize(2);
        M5.Display.printf("%u.%u.%u.%u   ",
                          (unsigned)(ip & 0xFFU), (unsigned)((ip >> 8) & 0xFFU),
                          (unsigned)((ip >> 16) & 0xFFU), (unsigned)((ip >> 24) & 0xFFU));
        M5.Display.setTextSize(1);
    }

    beginLine("[AIO] alive=");
    putNumber(static_cast<int32_t>(ticks));
    put(" status=");
    putNumber(static_cast<int32_t>(status));
    put(" sem_live=");
    putNumber(static_cast<int32_t>(esp_shim_sem_live));
    put(" sem_peak=");
    putNumber(static_cast<int32_t>(semLivePeak));
    put(" sem_acre_fail=");
    putNumber(static_cast<int32_t>(esp_shim_sem_acre_fail));
    put(" sem_del_fail=");
    putNumber(static_cast<int32_t>(esp_shim_sem_del_fail));
    flushLine();

    esp_shim_task_delay(1000);
}
