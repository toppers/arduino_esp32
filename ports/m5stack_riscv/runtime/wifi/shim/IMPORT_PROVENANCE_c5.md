# `esp/shim/`・`esp/app/`・`esp/bt/stub/` の ESP32-C5 分岐の出典と改変（計画 3 段4 Task 3）

- 出典（donor）: asp3_esp_idf `6471444`（`~/TOPPERS/asp3_esp_idf`）の
  `esp/c5/wifi_v8/{esp_wifi_adapter.c,esp_shim_blobglue.c,esp_shim_chip_regs.h}`、
  `esp/c5/esp_wifi_v8.cmake`、`asp3/target/esp32c5_espidf/target_kernel_impl.c`（APM/TEE）。
  読むだけ（無改変）。取込み 2026-09-16。C5 固有部の棚卸し（asp3 C6 -> C5 の差分 20 項目）は
  `.steering/20260915-c5-plan/stage4/asp3-c5-inventory.md`。
- 方針: 共通ファイル（S3/LX6/P4/C6 と共有）への変更は **18 golden をバイト不変に保つ 3 形**に限る
  （同 4 節の実測。`seam-c6-wifi` の像は app_desc に ELF 全体の sha256 を持ち、行の挿入で動く）:
  (a) 既存の `#if`/`#elif`/`#else` 行の**条件だけ**を同じ行数で書き換える、
  (b) **既存の空行 1 本**を `#include "<file>_c5.inc"` に置き換え、C5 固有の本体は
  その `.inc`（全体が `#if defined(TOPPERS_ESP32C5)`）に置く、
  (c) ファイル末尾へ追記（ヘッダの include guard の内側）。
  判定は `scripts/preset-matrix-run.sh PRESETS=ALL`（Task 3 末尾で 16 MATCH + NO_ABSOLUTE_GOLDEN 2、
  `.steering/20260915-c5-plan/stage4/README.md` 3 節）。
- C5 専用の新規ファイル: `esp/shim/esp_shim_intr_c5.{c,h,cfg}`、`esp/shim/esp_shim_intr_c5_lines.h`、
  `esp/shim/esp_wifi_adapter_c5.inc`、`esp/shim/esp_shim_blobglue_c5.inc`。

## 1. `esp/shim/esp_wifi_adapter.c`（(a) 条件 8 行 + (b) 空行 1 本 -> `esp_wifi_adapter_c5.inc`）

| 場所（共通側の行） | 変更 | 理由 |
|---|---|---|
| `:90`（空行） | `#include "esp_wifi_adapter_c5.inc"` | C5 固有の定義をここで読む（PERIPH_WIFI_MODULE の定義の前、extern 宣言群の後） |
| `:92 #if defined(TOPPERS_ESP32C6)`（PERIPH_WIFI_MODULE を `#define` しない） | `\|\| defined(TOPPERS_ESP32C5)` | C5 も `soc/periph_defs.h` の enum（C6 段4 Task 4 の現行バグと同型を避ける） |
| `:194 #else`（set_intr/clear_intr の S3 既定） | `#elif !defined(TOPPERS_ESP32C5)` | C5 は `.inc` の定義 |
| `:260 #else`（set_isr の S3 本体） | `#elif !defined(TOPPERS_ESP32C5)` | 同（関数の頭は共通なので `.inc` が同名を関数形式マクロで別名へ逃がす。下表） |
| `:284` / `:302 #else`（ints_on / ints_off の S3 本体） | `#elif !defined(TOPPERS_ESP32C5)` | 同 |
| `:1187 #else`（phy_enable の S3 本体） | `#elif !defined(TOPPERS_ESP32C5)` | C5 は `.inc` の定義 |
| `:1223 #if defined(TOPPERS_ESP32C6)`（wifi_reset_mac） | `\|\| defined(TOPPERS_ESP32C5)` | `modem_clock_module_mac_reset(PERIPH_WIFI_MODULE)` は C5 も同じ（asp3 C5 :695-698。`periph_module_reset` は呼ばない = C6 罠 2） |
| `:1304 #else`（wifi_clock_enable の S3 本体） | `#elif !defined(TOPPERS_ESP32C5)` | C5 は `.inc` の定義 |
| `:1338 #if defined(TOPPERS_ESP32C6)`（slowclk_cal_get） | `\|\| defined(TOPPERS_ESP32C5)` | LP_AON_STORE1 = `0x600B1004` は C5 も同じ（`reg_base.h:95` DR_REG_LP_AON_BASE 0x600B1000 + 0x4、asp3 C5 :843-856） |

`esp_wifi_adapter_c5.inc` の中身（asp3 `esp/c5/wifi_v8/esp_wifi_adapter.c` からの移植と差分）:

| 関数 / マクロ | asp3 出典 | 差分（asp3 -> FMP3 C5） |
|---|---|---|
| `set_intr_wrapper` | :153-200 | INTMTX MAP への直書き（`CLIC_LINE(intr_num) = intr_num + 16`）を `esp_shim_c5_wifi_route(src, intr_num)`（`esp_shim_intr_c5.c`、線 = 24 + intr_num、kernel の `esp32c5_intmtx_route` 経由）へ。CTL/ATTR バイトの明示書込み（asp3 :195-196）は cfg の `CFG_INT`（-2）と chip 層の ATTR 設定に任せて書かない。blob の prio は無視（同じ） |
| `clear_intr_wrapper` | :195-200 | MAP=0 を `esp_shim_c5_wifi_unroute()` へ |
| `set_isr_wrapper` | （asp3 は共通 esp_shim.c の `esp_shim_set_isr` + `ena_int(intno)`。asp3 の INTNO は +16 済み） | `esp_shim_c5_wifi_set_isr(n, f, arg)` = `esp_shim_set_isr(n, ...)`（shim_isr_tbl の添字は blob の n のまま）+ `ena_int(24 + n)`。共通側の同名定義は関数形式マクロで `c5_unused_set_isr_wrapper` へ逃がし、S3 本体は空（`#elif !defined(TOPPERS_ESP32C5)`）。`-Wunused-function` は参照配列 `c5_unused_keep[]`（`__attribute__((unused))`、gc される）で抑える |
| `ints_on_wrapper` / `ints_off_wrapper` | :216-240 | mask の bit n ごとに CLIC IE バイト（`CLIC_INT_CTRL(24+n)` の +1 バイト）を 1/0（asp3 の `CLIC_IE_OFF(CLIC_LINE(n))` と同じ意味、線が違う）。`esp_shim_int_disable()` 下で。共通側の同名定義は上と同じく別名へ |
| `c5_modem_clk_readback` | 新規（C6 の `c6_modem_clk_readback` の C5 版） | 番地は C5: MODEM_SYSCON_CLK_CONF_POWER_ST **0x600A9C0C**（C6 0x600A980C）、他は同じ。読むだけ（asp3 の (1) ICG init :715-729 (3) regi2c :807-808 (5) LPCON `\|= 0x7` :680,824 は **measure**、dispatch B の AC-4c） |
| `esp_wifi_adapter_c5_apm_readback` / `c5_apm_unblock` | asp3 `target_kernel_impl.c:1020-1125`（`esp32c5_r42_apm_unblock`、実施42/43）と C6 dev の `c6_apm_unblock` | 4 controller の FUNC_CTRL=0（+ CPU_APM 0x6009A0C4）と TEE 全 32 master = 0 を **それぞれ option**（`A1_C5_APM_FUNC_CTRL` / `A1_C5_APM_TEE`、既定 ON）に。asp3 の `HP_APM_M1_STATCLR` だけでなく HP_APM M0..M4 / CPU_APM M0..M1 のラッチを全部 clear し、before/after で FUNC_CTRL x4 / TEE M0・M4 / ラッチを syslog（C6 と同じ検出器） |
| `phy_enable_wrapper` | :648-680 | `esp_phy_enable(PHY_MODEM_WIFI)` + `phy_wifi_enable_set(1)`（S3/C6 と共通の中身）。asp3 の `0x600af018 \|= 0x7`（:679）は measure。入口/出口は DIAG 時のみ読み戻し |
| `wifi_clock_enable_wrapper` | :731-825 | 初回のみ `c5_apm_unblock()`（option）+ `modem_clock_deselect_all_module_lp_clock_source()` + `modem_clock_select_lp_clock_source(PERIPH_WIFI_MODULE, RC_SLOW, 0)`（:787-788）、毎回 `wifi_module_enable()`。asp3 の `esp_shim_modem_icg_init()`（:754）、regi2c（:807-808）、LPCON `\|= 0x7`（:824）は measure。最初の入口/出口の読み戻し 1 対は既定で出す |
| `_Static_assert(sizeof(wifi_osi_funcs_t) == 0x1e8)` | asp3 `esp_wifi_v8.cmake` の訂正コメント（`_magic` offset 484） | AC-3g |
| `esp_wifi_adapter_c5_diag_dump` | 新規 | `esp_shim_c5_wifi_diag_dump()` の口（`A1_C5_WIFI_DIAG` のとき中身あり） |

## 2. `esp/shim/esp_shim_intr_c5.{c,h,cfg}` + `esp_shim_intr_c5_lines.h`（新規）

型は C6 の `esp_shim_intr_intmtx.*`（線の静的宣言 + route + DEF_INH 入口）と P4 の
`esp_shim_intr_clic_lines.h`（予約線表と `#error` 検査）。asp3 C5 `esp_wifi_adapter.c:130-148`
（`CLIC_LINE`/`CLIC_IE_OFF`/`CLIC_CTL_OFF`）の写像を FMP3 の INTNO 体系（= CLIC 線）へ。
`esp_intr_alloc` 系（P4 型のスロット）は Wi-Fi 構成に呼び手が無いので持たない（C6 段4 3-5 節）。

## 3. `esp/shim/esp_shim_blobglue.c`（(a) 条件 6 行 + (b) 空行 1 本 -> `esp_shim_blobglue_c5.inc`）

| 場所 | 変更 | 理由 |
|---|---|---|
| `:62` / `:111` / `:124`（`g_misc_nvs` 等の LX6 用スタブのガード） | `&& !defined(TOPPERS_ESP32C5)` | C5 は `g_misc_nvs` = ROM ld `esp32c5.rom.net80211.ld:87`（0x4085ff7c、asp3 C5 も `extern`）、`misc_nvs_*` / `mesh_sta_auth_expire_time` / `g_log_level` は blob 内（`blob-supply-table.md`） |
| `:174`（空行） | `#include "esp_shim_blobglue_c5.inc"` | 7. MAC 読み出しの前 |
| `:203 #elif defined(TOPPERS_ESP32C6)`（EFUSE_RD_MAC_SPI_SYS_0/1_REG） | `\|\| defined(TOPPERS_ESP32C5)`、値を `ESP_SHIM_RISCV_EFUSE_BASE + 0x44/0x48` に | C6 の値は不変（0x600B0800 + 0x44）。C5 は 0x600B4800 + 0x44/0x48（`reg_base.h:107`、asp3 C5 :201-202）。ベースは `esp_shim.h` 末尾 |

`esp_shim_blobglue_c5.inc` の中身:

| 記号 | asp3 出典 / 原本 | 差分 |
|---|---|---|
| `esp_sleep_pd_config` / `esp_sleep_clock_config` | C6 11-a と同じ判断 | `ESP_ERR_NOT_SUPPORTED`（Low#1） |
| `putchar` | C6 11-b と同じ判断 | `-1`（EOF、Low#2） |
| `rtc_clk_xtal_freq_get` | esp-idf `esp_hw_support/port/esp32c5/rtc_clk.c:500-505`（asp3 は原本をコンパイル） | PCR `sysclk_conf.clk_xtal_freq` を読む（`clk_ll_xtal_get_freq_mhz`）。40/48 以外なら syslog して 48（原本は assert） |
| `vPortEnterCritical` / `vPortExitCritical` | C6 11-d と同じ | `esp_shim_bt_enter/exit_critical(NULL)` |
| `esp_clk_tree_enable_src` | esp-idf `esp_hw_support/port/esp32c5/esp_clk_tree.c:107-139`（asp3 は原本 + 4 本をコンパイル） | seam では `esp_clk_tree_initialized` が真になる経路が無いので原本も無動作で `ESP_OK` = 同じ意味の 1 関数。PCR `pll_160m_clk_en`（0x60096128 bit1、default 1）が 0 なら 1 度だけ syslog（Low#3。ゲート管理は未実装） |
| `__errno` | 新規（newlib の `w_log10.c` が要求） | `&errno`（`esp_shim_libc.c` の flat な errno）。C6 は floor しか引かないので不要だった |

## 4. `esp/shim/esp_shim.c`（(a) 条件 3 行 + `#define` の右辺 1 行）

| 場所 | 変更 |
|---|---|
| `:170 #if defined(TOPPERS_ESP32C6)`（`esp_shim_int_disable/restore` = `csrrci/csrsi mstatus,8`） | `\|\| defined(TOPPERS_ESP32C5)`（asp3 C5 `esp_shim.c` も同じ csr） |
| `:390 #elif defined(TOPPERS_ESP32C6)` / `:395 #define SHIM_WDEV_RND_REG` | `\|\| defined(TOPPERS_ESP32C5)` / 値を `ESP_SHIM_RISCV_WDEV_RND_REG`（`esp_shim.h` 末尾: C6 0x600B2808 / C5 **0x600B2828** = `LPPERI_RNG_DATA_SYNC_REG`、asp3 C5 `esp_shim_chip_regs.h:20`） |
| `:2610 #if defined(TOPPERS_ESP32C6)`（`esp_shim_wifi_int_dispatch`） | `\|\| defined(TOPPERS_ESP32C5)`（`esp_shim_intr_c5.c` の DEF_INH 入口が呼ぶ） |

## 5. `esp/shim/esp_shim.h`（(c) 末尾に追記）

`ESP_SHIM_RISCV_WDEV_RND_REG` / `ESP_SHIM_RISCV_EFUSE_BASE`（C6/C5 の値。C6 の値は従来の
リテラルと同じ）。include guard の内側。

## 6. `esp/shim/esp_shim.cfg`（(a) 2 行）

`:161` / `:243` の `#ifndef TOPPERS_ESP32C6` -> `#if !defined(TOPPERS_ESP32C6) && !defined(TOPPERS_ESP32C5)`
（S3/LX6 の線 0-3・23/27 ブロックを C5 でも外す。C5 は `esp_shim_intr_c5.cfg`）。

## 7. `esp/shim/esp_shim_libc.c`（(a) 5 行）

`#if defined(TOPPERS_ESP32C6)` x5（`esp_log`/`esp_log_write`/`coexist_printf` の静的リング化。
C6 段4 Task 4）-> `\|\| defined(TOPPERS_ESP32C5)`。中身はチップ非依存。

## 8. `esp/bt/stub/include/freertos/FreeRTOS.h`（(a) 1 行）

`:94 #if defined(TOPPERS_ESP32C6)`（`vPortEnterCritical` の宣言）-> `\|\| defined(TOPPERS_ESP32C5)`。

## 9. `esp/app/wifi_sta.c`（(a) 7 行）

| 場所 | 変更 |
|---|---|
| `:335`（`WIFI_DBG_MARK` を C6 で no-op） | `\|\| defined(TOPPERS_ESP32C5)`（C5 の 0x50000000 も LP SRAM） |
| `:1236`（`sar_periph_ctrl_init()` を C6 では呼ばない） | `\|\| defined(TOPPERS_ESP32C5)`（C5 も esp-idf 原本の periph_ctrl.c） |
| `:1281 #elif defined(TOPPERS_ESP32C6)`（クロック昇圧の分岐、no-op） | `\|\| defined(TOPPERS_ESP32C5)`（240MHz は `esp/boot/seam_c5_clk.c`） |
| `:1320` / `:1399` / `:1442` / `:1528 #if !defined(TOPPERS_ESP32C6)`（0x50000004/8 への生の書込み） | `&& !defined(TOPPERS_ESP32C5)` |

C6 の計器ブロック（`:74-247`、`WIFI_STA_C6_*`）は C6 のまま。C5 は `#else` の no-op を使う
（dispatch B の scan/計器は `esp/app/wifi_sta_c5.inc` の形で足す予定 = 同じ 3 形）。

## 10. 持ち込まなかったもの

`asp3-c5-inventory.md` 5 節と 1 節の skip/measure 項目（BBPLL 再較正、PMA、pmu_instance、
`ESP32C5_R40_CLKREORDER`、`wifi_trace`、`log10` の手書き（`-lm`）、asp3 の C5 固有 esp-idf 6 本
（`rtc_clk.c` / `esp_clk_tree.c` / `esp_clk_tree_common.c` / `clk_tree_hal.c` / `rtc_time.c` / `esp_clk.c`））。

## 11. 未実装（失敗値を返すもの。asp3 の Low# の型）

| # | 記号 | 戻り値 | 呼び手 | 備考 |
|---|---|---|---|---|
| Low#1 | `esp_sleep_pd_config` / `esp_sleep_clock_config` | `ESP_ERR_NOT_SUPPORTED` | esp-idf `modem_clock.c`（戻り値を捨てる） | C6 と同じ |
| Low#2 | `putchar` | `-1`（EOF） | libpp / libnet80211（戻り値を捨てる） | C6 と同じ |
| Low#3 | `esp_clk_tree_enable_src` | `ESP_OK`（原本の未初期化経路と同値。ゲート管理は未実装） | `modem_clock.c:214`（`ESP_ERROR_CHECK`）、`regi2c_ctrl.h:36` | C5 だけ。`pll_160m_clk_en`=0 なら 1 度 syslog |
| Low#4-6 | `diag_recorder.c` の縮小（`diag_panic_snapshot` / `diag_boot_check_and_dump`） | - | C6 の Low#3-5 と同じ（段1 で C6 target から写した） | |
