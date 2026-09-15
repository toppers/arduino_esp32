# `esp/shim/`・`esp/app/`・`esp/bt/stub/` の ESP32-C6 分岐の出典と改変（段4 Task 3）

- 出典（donor）: asp3_esp_idf `6471444`（`~/TOPPERS/asp3_esp_idf`, branch main）の
  `esp/c6/wifi/{esp_wifi_adapter.c,esp_shim_blobglue.c,esp_shim.c,esp_shim_chip_regs.h}`
  および `esp/c6/esp_wifi.cmake`。読むだけ（無改変）。取込み 2026-09-14。
- 方針: 共通ファイル（S3/LX6/P4 と共有）への変更は **`#if defined(TOPPERS_ESP32C6)`
  （または `#elif`）で囲んだ追加のみ**。S3/LX6/P4 の前処理結果は不変
  （`seam-s3-wifi` / `seam-lx6-wifi` / `seam-p4-hosted-wifi` の golden sha256 一致で機械判定。
  `.steering/20260913-c6-stage4/README.md` 3 節）。
- 棚卸し（port / new / measure / skip の判定）: `.steering/20260913-c6-stage4/asp3-c6-inventory.md`。
  本表はそのうち**実際にコードへ入れたもの**と、その出典行。
- 照合コマンド: `sed -n '<行>p' ~/TOPPERS/asp3_esp_idf/esp/c6/wifi/<f>` と本 repo の該当箇所を並べて読む。

## 1. `esp/shim/esp_wifi_adapter.c`（C6 分岐を追加）

| 関数 / マクロ | asp3 出典（`esp/c6/wifi/esp_wifi_adapter.c`） | 差分（asp3 -> FMP3 C6） |
|---|---|---|
| `PLICMX_BASE_ADDR` / `PLICMX_ENABLE_REG` / `PLICMX_TYPE_REG` / `PLICMX_PRI_REG(n)` | :76-80 | `INTMTX_BASE_ADDR` は持ち込まない（配線は `esp_shim_intmtx_route()` へ委譲するため） |
| `set_intr_wrapper` | :83-110 | INTMTX の MAP 直書き（asp3 :106）を `esp_shim_intmtx_route(src, intno)`（`esp/shim/esp_shim_intr_intmtx.c`。kernel の `esp32c6_intmtx_route` 経由で `intmtx_srcmask` も更新し、線 1..15 の使用中帳簿に記す）へ置換。失敗（範囲外 / `esp_intr_alloc` 済みの線）なら PRI/TYPE を書かずに戻る。PRI=2 固定・TYPE bit クリア（LEVEL）は同一 |
| `clear_intr_wrapper` | :113-117 | MAP=0 の直書きを `esp_shim_intmtx_unroute()` へ |
| `ints_on_wrapper` / `ints_off_wrapper` | :126-141 | 同一（`#elif defined(TOPPERS_ESP32C6)` 分岐として追加） |
| `phy_enable_wrapper` | :571-598 | (d) `esp_phy_enable(PHY_MODEM_WIFI)` + `phy_wifi_enable_set(1)` のみ（S3 と共通の中身）。(a) `0x600af018 \|= 0x7` は **measure**（Task 4）、(b) `0x600af008/0x600af048 = 0x314` は **skip**（asp3 自身が「検証のため」と書く投機的書込み）、(c) RTC RAM 計数は **skip**（診断）。代わりに入口/出口で `c6_modem_clk_readback()`（新規。LPCON CLK_CONF / WIFI_LP_CLK_CONF / LPCON・SYSCON の CLK_CONF_POWER_ST / PMU ICG_MODEM の 5 レジスタを syslog）を呼ぶ |
| `wifi_reset_mac_wrapper` | :614-616（と :42-52 の注記） | 同一（`modem_clock_module_mac_reset(PERIPH_WIFI_MODULE)` のみ。S3/LX6 の SYSCON/DPORT 直叩きと `periph_module_reset()` は C6 では呼ばない） |
| `wifi_clock_enable_wrapper` | :687-747 | (2) 初回のみ `modem_clock_deselect_all_module_lp_clock_source()` + `modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE, MODEM_CLOCK_LPCLK_SRC_RC_SLOW, 0)` と (4) `wifi_module_enable()` を port。(1) `esp_shim_modem_icg_init()`、(3) `_regi2c_ctrl_ll_master_enable_clock(true)` + `regi2c_ctrl_ll_master_configure_clock()`、(5) `0x600af018 \|= 0x7` は **measure**（seam では bootloader が行うはず。読み戻しを見てから Task 4 で決める）。入口/出口で `c6_modem_clk_readback()` |
| `slowclk_cal_get_wrapper` | :767-781 | 番地だけ `#if` で差替え（`0x600B1004` = LP_AON_STORE1） |
| `_Static_assert(sizeof(wifi_osi_funcs_t) == 0x1e8)` | 無し（asp3 は目視。`esp_wifi.cmake:209-212` に実測記録） | 新規（AC-3g）。`_regdma_link_set_write_wait_content` / `_sleep_retention_find_link_by_id` は asp3 と同じく NULL のまま（表に行を書かない） |
| `#include "soc/periph_defs.h"` / `"esp_private/esp_modem_clock.h"` | :35-36 | 同一（C6 分岐内）。`hal/regi2c_ctrl_ll.h` / `hal/modem_lpcon_ll.h` / `hal/modem_syscon_ll.h` / `hal/pmu_ll.h`（:37-40）は measure 項目 (1)(3) 用なので持ち込まない |

## 2. `esp/shim/esp_shim_blobglue.c`

| 項目 | asp3 出典（`esp/c6/wifi/esp_shim_blobglue.c`） | 差分 |
|---|---|---|
| `g_misc_nvs` / `misc_nvs_init` / `misc_nvs_deinit` / `mesh_sta_auth_expire_time` / `g_log_level` のガード | （asp3 は :77-132 に定義を持つが、`--start-group` 無し・`core` が `net80211` より前のリンク順で gc されるため衝突しない） | FMP3 の `#ifndef TOPPERS_ESP32S3` を `#if !defined(TOPPERS_ESP32S3) && !defined(TOPPERS_ESP32C6)` に（C6 は libcore.a / libmesh.a の定義を使う。stage-4 README 0-2 節 1.） |
| `EFUSE_RD_MAC_SPI_SYS_0/1_REG` | :202-203 | `#elif defined(TOPPERS_ESP32C6)` で番地だけ追加（`esp_read_mac` 本体は S3/LX6/C6 共通のまま） |
| `esp_sleep_pd_config` / `esp_sleep_clock_config` | :334-346 | asp3 は `ESP_OK` の空実装。FMP3 は **`ESP_ERR_NOT_SUPPORTED`**（Low#1、未実装を戻り値で示す。呼出し元 `modem_clock.c:560-561,615` は戻り値を捨てる） |
| `putchar` | :440-443 | asp3 は `return c`。FMP3 は **`EOF`**（Low#2、文字出力は行わない） |
| `floor` | :452-459（手書き） | **持ち込まない**。`-lm`（newlib libm）から取る（`cmake/a1_c6_stage1.cmake`。`nm` で `T floor` @ flash を確認） |
| `rtc_clk_xtal_freq_get` | （asp3は esp-idf `rtc_clk.c` 原本をコンパイル） | 新規 1 関数（esp-idf `esp_hw_support/port/esp32c6/rtc_clk.c:382-390` と同じ意味: `clk_ll_xtal_load_freq_mhz()`、0 なら 40MHz）。`rtc_clk.c` 原本は PLL 較正経路（regi2c）を引き込むので持ち込まない |
| `vPortEnterCritical` / `vPortExitCritical` | 無し（asp3 は hal 版 phy_init.c ではなく esp-idf 原本 + 自前 stub を使う） | 新規。`esp_private/critical_section.h` の `OS_SPINLOCK==0` 分岐がこの名前へ展開する（詳細は `esp/bt/stub/include/freertos/FreeRTOS.h` の C6 分岐）。写像先は S3 と同じ `esp_shim_bt_enter/exit_critical(NULL)` |
| `phy_get_max_pwr` / `esp_wifi_sta_get_ie` / `esp_wifi_is_wpa3_compatible_mode_enabled` | :471-519 | **skip**（C6 v5.5.4 の libphy.a が `phy_get_max_pwr` を定義。残り 2 つは誰も参照しない） |

## 3. `esp/shim/esp_shim.c`

| 項目 | asp3 出典 | 差分 |
|---|---|---|
| `esp_shim_int_disable` / `esp_shim_int_restore` | `esp/c6/wifi/esp_shim.c:76-96`（`esp_shim_enter/exit_critical`。`csrrci/csrsi mstatus, 8`） | `#if defined(TOPPERS_ESP32C6)` 分岐として追加。asp3 のネストカウンタ・保留キュー flush は持ち込まない（FMP3 の契約は S3 版と同じ save/restore 型。flush 機構は `esp_shim_isr_ctx.c` が S3 と同じものを持つ） |
| `SHIM_WDEV_RND_REG` | `esp/c6/wifi/esp_shim_chip_regs.h:23`（`0x600B2808`） | `#elif defined(TOPPERS_ESP32C6)` で番地だけ追加 |
| `esp_shim_wifi_int_dispatch(int)` | `esp/c6/wifi/esp_shim.c:264-303`（`shim_int_dispatch` + `esp_shim_inthdr_1..3`） | 新規。asp3 の `esp_shim_inthdr_n` に相当する入口は `esp/shim/esp_shim_intr_intmtx.c` の `esp_shim_intmtx_inthdr_1..15` が持ち、そこから本関数経由で既存の static `shim_int_dispatch()` へ戻る（ディスパッチの中身は S3/LX6 と同一） |
| `esp_shim_modem_icg_init` | `esp/c6/wifi/esp_shim.c:320-333` | **持ち込まない**（measure。Task 4 で読み戻しを見てから） |
| `esp_shim_isr_storm_probe` | `esp/c6/wifi/esp_shim.c:255-262, 264-298` | **skip**（診断） |

## 4. `esp/shim/esp_shim.cfg`

| 項目 | 差分 |
|---|---|
| 線 0-3（`CFG_INT`/`DEF_INH`/`ENA_DYNISR`）と線 23/27 のブロック | `#ifndef TOPPERS_ESP32C6` で包んだ（行は 1 文字も動かしていない）。C6 は `esp/shim/esp_shim_intr_intmtx.cfg`（線 1..15、`cmake/a1_c6_stage1.cmake` が `FMP3_CFG_FILES` へ足す）で置き換える。理由: C6 に線 0 は無く（cfg の `TargetCheckCfgInt` が拒否）、INHNO が `(PRC1 << 16) \| 線` の形 |

## 5. 新規ファイル（C6 専用。S3/LX6/P4 はリンクしない）

| ファイル | 型（前例） | 内容 |
|---|---|---|
| `esp/shim/esp_shim_intr_intmtx_lines.h` | `esp/shim/esp_shim_intr_clic_lines.h`（P4） | 線 1..15 の単一真実源。tick(16)/SIO(17)/18/19/20/21 との衝突を `#error` で検査 |
| `esp/shim/esp_shim_intr_intmtx.h` | `esp/shim/esp_shim_intr_clic.h`（P4） | `esp_intr_alloc` 系 5 本 + `esp_shim_intmtx_route/unroute` + 診断 + `esp_shim_intmtx_inthdr_1..15` の宣言 |
| `esp/shim/esp_shim_intr_intmtx.c` | `esp/shim/esp_shim_intr_clic.c`（P4）/ asp3 `esp/c6/wifi/esp_shim.c:264-303`（入口） | 線 1..15 の帳簿（owner = blob / alloc）。`esp_intr_alloc` は上の線から払い出す（blob は下の線を自分で選ぶため） |
| `esp/shim/esp_shim_intr_intmtx.cfg` | `esp/shim/esp_shim_intr_clic.cfg`（P4）/ asp3 `esp/common/wifi/esp_shim.cfg:120-125`（線 1-3 のみ） | `CLASS(CLS_PRC1) { CFG_INT(n, {TA_NULL, -2}); DEF_INH((PRC1<<16)\|n, {TA_NULL, esp_shim_intmtx_inthdr_n}); }` x 15 |

## 6. `esp/app/wifi_sta.c`

| 箇所 | 差分 |
|---|---|
| `sar_periph_ctrl_init()` 呼出し | `#if defined(TOPPERS_ESP32C6)` で外す（S3/LX6 改造版 `periph_ctrl.c` の S3 専用関数。C6 は esp-idf 原本の `periph_ctrl.c` を使う。asp3 C6 も呼ばない） |
| PLL 昇圧ブロック（`esp_bbpll_enable_480m` の `#else`） | `#elif defined(TOPPERS_ESP32C6)` を挿入（何もしない。seam bootloader が PLL 済み、160MHz は `esp/boot/seam_c6_clk.c`） |

## 7. `esp/bt/stub/include/freertos/FreeRTOS.h`

| 箇所 | 差分 |
|---|---|
| `vPortEnterCritical` / `vPortExitCritical` の extern 宣言 | `#if defined(TOPPERS_ESP32C6)` で追加（2 節参照） |

## 8. `cmake/a1_c6_stage1.cmake`（`A1_C6_WIFI` ブロック）

- 型: `CMakeLists.txt` の S3 wifi variant（`_seam_srcs`、`A1_HAS_BLOB` の incflags 解析、
  `A1_LINK_LIBGROUP`）と `cmake/a1_p4_stage1.cmake`（creds の `a1_creds_header_into_copt()`）。
- ROM ld 13 本と `-D` は asp3 `esp/c6/esp_wifi.cmake:356-378`（ld）、`:179`
  （`CONFIG_ESP_PHY_DISABLE_PLL_TRACK=1`）から。`MALLOC_CAP_DMA/INTERNAL`（asp3 :193-194）は
  FMP3 の hal 版 `phy_init.c` が `esp_heap_caps.h` を include するので不要（コンパイル実測）。
- esp-idf 原本 5 本（`periph_ctrl.c` / `modem_clock.c` / `modem_clock_hal.c` / `efuse_hal.c` x2）は
  asp3 `esp_wifi.cmake` section 6（:713-744）と同じ集合から `rtc_clk.c` を除いたもの。

## 9. 段4 Task 4（2026-09-14、実機初期化）で決めたこと・足したもの

measure 項目（1 節の `phy_enable_wrapper` / `wifi_clock_enable_wrapper`、3 節の
`esp_shim_modem_icg_init`）の判定。実測は `.steering/20260913-c6-stage4/README.md` 4 節。

| asp3 の項目 | 実測（真cold 3/3 と warm、`c6_modem_clk_readback` の値） | 判定 |
|---|---|---|
| (1) `esp_shim_modem_icg_init()`（PMU ICG code=2 + ICG bitmap） | `pmu_icg_modem=0x80000000`（bits[31:30]=2）が **clock_enable 入口で既に立っている**（真cold でも）。設定者は seam bootloader の `rtc_clk_init()`（`bootloader_clock_init.c:83` -> `rtc_clk_init.c:44-56`、POR/HP リセットとも実行） | **持ち込まない** |
| (3) `_regi2c_ctrl_ll_master_enable_clock(true)` + `regi2c_ctrl_ll_master_configure_clock()` | `lpcon_clk_conf` bit2（I2C_MST_EN）が clock_enable 入口で既に 1（`0x6`。bit1 COEX_EN は coex 初期化が立てる）。設定者は bootloader（`bootloader_esp32c6.c:99-100`）と `esp_phy_enable` の `modem_clock_module_enable` | **持ち込まない** |
| (5) `0x600af018 \|= 0x7` | `modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE, RC_SLOW, 0)` が正しく効けば bit0（WIFIPWR_EN）が立ち `0x7` になる（`after_lpclk_select lpcon_clk_conf=0x00000007 wifi_lp_clk=0x00000001`）。初回焼込みで `0x6`/`0x0` のままだったのは下の `PERIPH_WIFI_MODULE` の値違いが原因（asp3 の C6 adapter は `soc/periph_defs.h` の enum を使い `#define 24` を持たないので、asp3 の「select だけでは 0 のまま」とは別の事象） | **持ち込まない**（原因側を直した） |

| 項目 | 出典 | 内容 |
|---|---|---|
| `PERIPH_WIFI_MODULE` の `#define 24` を C6 で無効化（1 節） | 自前（実機読み戻し + objdump） | C6 では `soc/periph_defs.h` の enum（33）。S3/LX6 向けの `#ifndef PERIPH_WIFI_MODULE / #define 24` が enum を隠し、`modem_clock_select_lp_clock_source()` / `modem_clock_module_mac_reset()` に 24 を渡していた（LP クロック未選択・WIFIPWR 未有効・MAC リセット無し・`lpclk_src[-9]` 書込み）。`#if defined(TOPPERS_ESP32C6)` で define を外した |
| `esp_log()` / `esp_log_write()` / `coexist_printf()` の静的リング（`esp/shim/esp_shim_libc.c`） | 型は本ファイル 1 節 `log_writev_wrapper` の `log_ringbuf`（S3 既存） | C6 だけ 16 x 128B のリングへ写してから `syslog("%s")` に渡す（スタック buf のダングリング対策。`phy_version` 行と coex 版数行が読めるようになった） |
| `TCNT_SYSLOG_BUFFER=256`、`ESP_SHIM_HEAP_STATS`、`TOPPERS_APP_HEAP_REPORT`（`cmake/a1_c6_stage1.cmake`） | `fmp3_core/syssvc/syslog.c` の既定 32、`CMakeLists.txt` の S3 用オプション | C6 Wi-Fi 構成のみ |
| `CONFIG_ESP_PHY_ENABLE_USB`（`esp/config/esp32c6/sdkconfig.h`） | esp-idf `esp_phy/Kconfig:113-115`（`depends on SOC_WIFI_PHY_NEEDS_USB_WORKAROUND`、C3/S3 のみ。C6 では unset） | Task 4 本体で一度足したが fix round 1 で**外した**（IDF の C6 既定 = unset。足しても外しても真cold の観測に差が無かった、README 4-4/4-7 節） |
| USB-Serial/JTAG の IN トークン監視（`fmp3/target/m5nanoc6_gcc/esp32c6_usbjtag_hal.c`） | 自前（実機のレジスタ写し）。IDF `usb_serial_jtag.c:60-135` は WR_DONE 再発行を送信完了時にしか行わない | 真cold で ROM/bootloader が host 不在のまま WR_DONE したパケットが EP1 に残り、host が後から IN トークンを送っても配送されない状態を、`IN_TOKEN_REC_IN_EP1` 割込み（未送信パケットがある間だけ有効）で WR_DONE を再発行して解放する |
| 診断（既定 OFF）: `A1_C6_BOOT_TRACE`（リセット越しの足跡・USJ レジスタ写し @0x40868000）、`A1_C6_WIFI_BOOT_DELAY_MS`、`A1_C6_USJ_REARM_PROBE` | asp3 の RTC RAM 計数（`esp/c6/wifi/esp_wifi_adapter.c:210-221`）と同じ発想、番地は HP SRAM | 真cold の無音の切り分けに使った。製品像には入らない |

## 10. 段4 Task 5（2026-09-14、scan = W1）で決めたこと・足したもの

実測は `.steering/20260913-c6-stage4/README.md` 5 節、時系列は
`.superpowers/sdd/PLAN-stage4-impl/task-5-report.md`。

| 項目 | 出典 | 内容 |
|---|---|---|
| APM 経路フィルタの解除（`esp_wifi_adapter.c` `c6_apm_unblock`、`wifi_clock_enable_wrapper` の初回で modem を有効化する前に呼ぶ。`A1_C6_APM_UNBLOCK` 既定 ON） | esp-idf `bootloader_support/src/bootloader_mem.c:19-37` `bootloader_init_mem()` の `#if !defined(BOOTLOADER_BUILD)` 側（`apm_hal_enable_ctrl_filter_all(false)` = `hal/apm_hal.c:185-195`、`hal/esp32c6/include/hal/apm_ll.h:104-106,283-285,472-474`）。asp3 `asp3/target/esp32c6_espidf/target_kernel_impl.c:99-160` `esp32c6_r87_apm_unblock()`（実施87/88） | seam は IDF アプリ側の `cpu_start` をリンクしないので、bootloader が触らない APM（POR 既定: `HP_APM_FUNC_CTRL=0xf`、`LP_APM0=0x1`、`LP_APM=0x3`、`TEE_M4(MODEM)=3`=REE2）がそのまま残り、modem の DMA が HP SRAM へ届かない（`HP_APM M1 status=1 info0=0x00130001 info1=0x408218fc`）。3 コントローラの `FUNC_CTRL` を 0 にし、例外ラッチを clear する。asp3 が加えて行う「全 32 master を TEE (0) へ」は IDF の C6 アプリ（`SOC_APM_SUPPORT_TEE_PERI_ACCESS_CTRL` 無し）が行わないので**持ち込まない**（`tee_m4` は 3 のまま、scan は 10-13 AP） |
| PHY 初期化データを esp-idf 原本 `esp_phy/esp32c6/phy_init_data.c` に（`cmake/a1_c6_stage1.cmake`） | asp3 `esp/c6/esp_wifi.cmake:231` も原本。`esp/wifi/hal_src/phy_init_data.c` は `esp_phy/esp32s3/phy_init_data.c` と byte 同一 | 4 節（asp3-c6-inventory 4 節「`-I` で C6 版になる」）は誤りだった: v5.5.4 は配列の実体を `esp32c6/phy_init_data.c` に置く（`include/phy_init_data.h` は extern 宣言のみ）ので、C6 の像に S3 の値（先頭 `00 00`、末尾 `0x74`）が入っていた（objdump 実測）。原本に替えると `0a 00 ... 0x51`。**scan の 0 AP には無関係**（APM 解除だけで 10 AP、`logs/task5-160-flash5-s3phydata.log`）。IDF の C6 既定値に揃えるために残す |
| `esp_wifi_adapter_c6_apm_readback(tag)` | 自前（番地は `soc/esp32c6/register/soc/{hp_apm,lp_apm0,lp_apm,tee}_reg.h`） | `FUNC_CTRL` x3、`TEE_M4`、`HP_APM M0-M3` の STATUS/INFO0/INFO1 を syslog へ。unblock の前後、scan 後、末尾で呼ぶ。読むだけ |
| scan（`esp/app/wifi_sta.c` C6 分岐、`A1_C6_WIFI_SCAN` 既定 ON） | `esp/app/wifi_scan_run.c`（S3 m5 factory の scan、`esp_wifi_scan_start(NULL, false)` = active・全 ch） | `esp_wifi_start` の直後・`esp_wifi_connect` の前に 1 回。AP 数、所要時間（`get_tim` 差）、線 1 の割込み増分、AP ごとの RSSI/ch を出す。**SSID は出さない**（`<SSID-N>`） |

## 11. 段4 Task 6（2026-09-14、STA 接続 -> DHCP -> ping = W2）で決めたこと・足したもの

実測は `.steering/20260913-c6-stage4/README.md` 6 節、時系列は
`.superpowers/sdd/PLAN-stage4-impl/task-6-report.md`。**移植したものは無い**: 接続・DHCP・
ping の経路（`esp_wifi_connect`、20 秒再試行、`esp/wifi/net/netif_esp32s3.c` の DHCP/ping、
`esp/wifi/net/port/sys_arch.c`）は S3/LX6 と同一ソースのまま、Task 5 の最終像のソースで
実 AP へ初回から接続し、真cold 3/3 で `GOT IP` と `ping gateway -> OK` 85-87/85-87 に到達した。

| 項目 | 出典 | 内容 |
|---|---|---|
| `[C6-W2] <tag> t_connected_ms= t_gotip_ms=`（`esp/app/wifi_sta.c` C6 分岐） | 自前（Task 5 の scan 所要時間と同じ `get_tim` の物差し） | CONNECTED と GOT IP の時点の起動からの ms を保持し末尾で出す。採取ログに host 側タイムスタンプが無いため。非 C6 は `((void) 0)`（golden `seam-s3-wifi`/`seam-lx6-wifi` MATCH） |
| `c6_mask_peer_ids()`（`esp/boot/flash_and_capture_c6_usj.sh`） | 作法は `scripts/redact_secrets.sh`（変換器と検査器を別実装、fail-closed、selftest） | Wi-Fi blob が接続時に出す AP 側 MAC（`CCMP mgmt frame from xx:..`、`<ba-add> TAHI:0x/TALO:0x`）を `<PEER-MAC>` に伏せる。`redact_secrets.sh` の針は creds の `WIFI_STA_BSSID`（2026-08-13 以降は空）由来で AP の BSSID を知らないため rc=0 のまま通していた（README 6-5 節）。DUT 自身の MAC は残す。C6 台本に閉じた（S3/LX6/P4 の台本は未対応 = 申し送り） |
| `-DESP_SHIM_HEAP_STATS -DTOPPERS_APP_HEAP_REPORT` | Task 4 の 9 節のまま | C6 の `A1_C6_WIFI` ブロックで無条件。S3 の `A1_SHIM_HEAP_STATS`/`A1_APP_HEAP_REPORT` オプションは `CMakeLists.txt:98-100` の C6 分岐の後ろにあり C6 では読まれない（cmake は変えていない） |
