/*
 *  Wi-Fi blobが要求する残シンボルのスタブ（ESP32-S3 / FMP3, scan=B-2a用）
 *
 *  scanでは未使用の機能（mesh/WPA supplicant/gpio予約）はリンク解決のための
 *  最小スタブとする。esp_timerはblobが周期処理に使うため最小実装（ets_timer
 *  相当のshimタイマ機構へ委譲するのが本筋だが、まずリンク成立を優先し
 *  no-op＋ESP_OKとする。printf系はsyslogへ折り返す。
 */
#include <kernel.h>
#include <t_syslog.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

typedef int esp_err_t;
#define ESP_OK 0

/* printf系(pp/net80211/phy/mesh_printf)はlib_printf.c(実体)が提供する。 */
int coex_pti_print(uint32_t a, uint32_t b) { (void)a;(void)b; return(0); }

/* ---- DPORT読みエラッタ回避（無印ESP32のみ）----
 * blob(pp/coex/phy)が esp_dport_access_reg_read(reg) を呼ぶ。S3ではROM
 * (esp32s3.rom.ld)が提供するため定義しない（多重定義回避）。無印ESP32は
 * ROMに無いので W0-4 の chip_dport_read（DPORT SMP安全読み）へ委譲する。 */
#if defined(TOPPERS_ESP32_LX6)
#include "chip_dport.h"
uint32_t esp_dport_access_reg_read(uint32_t reg) { return(chip_dport_read(reg)); }

/* ---- 無印ESP32 blob 残ギャップ（S3ではROM/S3-blob提供、無印は要供給）----
 * いずれもW1(Wi-Fi STA, PS_NONE)で安全な最小実装。BT併用(W2以降)で実体が要る
 * ものはその時に差し替える。全てTOPPERS_ESP32_LX6ガードでS3非回帰。 */

/*  RTC XTAL周波数。libphyが較正基準に使う。rtc_clk.cのrtc_clk_xtal_freq_get()は
 *  RTC_XTAL_FREQ_REG(ブートローダ設定のスクラッチ)を読むが、Direct Bootでは
 *  未設定でゼロ/誤値になりうる。WROVER-KITはXTAL=40MHz固定なので定数40
 *  (RTC_XTAL_FREQ_40M)を返す方が確実。 */
int rtc_get_xtal(void) { return(40); }

/*  RTC clock/sleep prep。librtc(PHY)がmodem sleep経路で参照するが、
 *  esp_wifi_set_ps(WIFI_PS_NONE)＝スリープ無効のため実行されない。no-op。
 *  （ESP32 ROM常駐関数だが公式rom.ldに載らず、無印ソースツリーにも実体なし。） */
void rtc_init_clk(void) { }
void rtc_slp_prep(void) { }

/*  PHY PLLトラッキング有効化。無印ESP32はDISABLE_PLL_TRACK=1方針のため
 *  no-op（=トラッキング無効）で整合。phy_common.cが phy_param_track_tot(
 *  wifi_track, ble_track) を呼ぶ。 */
void phy_param_track_tot(_Bool en_wifi, _Bool en_ble_154) { (void)en_wifi; (void)en_ble_154; }

/*  coex PTI取得。libcoexistが参照。WiFi単独では調停不要。0返し。 */
int coex_pti_get(uint32_t event, uint8_t *pti) { (void)event; if(pti)*pti=0; return(0); }

/*  BT RF/BB レジスタ初期化。libcoexistが参照。W1(WiFi単独)では未使用なので
 *  no-op で足りるが、bt-classic では libbtdm_app.a が本物を持つ（そちらを
 *  使わないと RF が上がらない）。二重定義になるのでこちらを畳む。 */
#if !defined(TOPPERS_BT_CLASSIC)
void btdm_rf_bb_reg_init(void) { }
#endif

/*  coex関数テーブルへのポインタ。S3ではROM変数(coexist_funcs=0x3fcef828)だが
 *  無印はROM/blob非提供。esp_coex_adapter.c が coex_pre_init() で正しい実体を
 *  設定し、失敗時のみ dummy_coexist_table(全no-op) へフォールバックする。
 *  ここでは実体変数を用意する（初期NULL）。 */
void *coexist_funcs;

/*  newlib リエントラント構造体getter（★無印ESP32/LX6専用）。
 *
 *  ★訂正：旧コメントは「S3ではROM/newlib経路で解決される」
 *  としていたが**誤り**。S3のld（esp/ld/esp32s3.rom.newlib.ld・rom.libc.ld）は
 *  __getreent を PROVIDE も絶対代入もしない（ROMに実体はあるがエクスポート
 *  されない）。S3も__getreentの実体を自前で用意する必要があり、放置した結果
 *  test_mtrans2 が ROM rand()→ROM __getreent stub→syscall_table_ptr(NULL)
 *  →LoadProhibited で落ちていた。
 *
 *  S3側の実装は arch/xtensa_gcc/esp32s3/chip_rom_libc.c へ新設した
 *  （タスク毎_reent／39メンバのstub table／software_init_hookで初期化）。
 *  本ファイルはesp/boot系ビルドにしかリンクされず、カーネルテストのビルドに
 *  は入らないため、S3の実装をここに置くことはできない（＝chip層へ置いた理由）。
 *
 *  以下は無印+Direct Boot用：単一_reentを共有（マルチタスクだが本ポートの
 *  newlib使用はstrtol/errno等の非状態依存が主。将来TLS化する場合は要見直し）。 */
struct _reent;
extern struct _reent *_impure_ptr;
struct _reent *__getreent(void) { return(_impure_ptr); }

/*  ★無印ESP32 ROM newlib syscall stub table（freeze根因の一つ）
 *  ROMのnewlib関数（errno設定・malloc・ロック等）は内部で ROM __getreent
 *  (0x4000be8c) を呼ぶ。その実体は syscall_table_ptr_pro/app(0x3ffae024/
 *  0x3ffae020) が指す stub table の先頭 __getreent を辿るが、Direct Boot では
 *  このポインタが未初期化(0)のため NULL 参照 → LoadProhibited(EXCCAUSE=28,
 *  pc=ROM __getreent)。coex blob の esp_coex_adapter_register 内で顕在化した。
 *  ESP-IDF esp_libc_init（newlib_init.c）と同様に stub table を用意し、両コア
 *  分のポインタを張る。malloc/free/realloc/calloc は本ポートの shim ヒープへ、
 *  ロックは単一挙動 no-op（ROM ヒープ/ロックは使わないため安全）。 */
extern void *esp_shim_malloc(size_t);
extern void  esp_shim_free(void *);
extern void *esp_shim_calloc(size_t, size_t);
extern void *esp_shim_realloc(void *, size_t);

static void *rl_malloc_r(struct _reent *r, size_t sz)            { (void)r; return(esp_shim_malloc(sz)); }
static void  rl_free_r(struct _reent *r, void *p)               { (void)r; esp_shim_free(p); }
static void *rl_realloc_r(struct _reent *r, void *p, size_t sz) { (void)r; return(esp_shim_realloc(p, sz)); }
static void *rl_calloc_r(struct _reent *r, size_t n, size_t sz) { (void)r; return(esp_shim_calloc(n, sz)); }
static void  rl_abort(void)          { syslog(LOG_EMERG, "ROM newlib abort()"); for (;;) { } }
static void  rl_lock_noop(void *l)   { (void)l; }
static int   rl_try_noop(void *l)    { (void)l; return(0); }

/*  ESP32 ROM の struct syscall_stub_table レイアウト（libc_stubs.h、非
 *  retargetable locking 版・36ポインタ）。参照されるオフセットのみ実体を張り、
 *  未使用フィールドは NULL。順序厳守。
 *
 *  ★★これは**無印ESP32(LX6)専用**であり、ESP32-S3へ流用してはならない。
 *  S3のROMは39メンバ・156バイト・retargetable locking 版で、index 24以降
 *  （ロック群）が丸ごとズレる。S3用は
 *  arch/xtensa_gcc/esp32s3/chip_rom_libc.c に別途定義してある
 *  （正典：esp-idf/components/esp_rom/esp32s3/include/esp32s3/rom/libc_stubs.h）。
 *  本構造体は #if defined(TOPPERS_ESP32_LX6) 内にあり、S3ではコンパイルされない。 */
struct esp32_syscall_stub_table {
	struct _reent *(*f_getreent)(void);
	void *(*f_malloc_r)(struct _reent *, size_t);
	void  (*f_free_r)(struct _reent *, void *);
	void *(*f_realloc_r)(struct _reent *, void *, size_t);
	void *(*f_calloc_r)(struct _reent *, size_t, size_t);
	void  (*f_abort)(void);
	void *pad_system_r, *pad_rename_r, *pad_times_r, *pad_gettimeofday_r;
	void *pad_raise_r, *pad_unlink_r, *pad_link_r, *pad_stat_r, *pad_fstat_r;
	void *pad_sbrk_r, *pad_getpid_r, *pad_kill_r, *pad_exit_r, *pad_close_r;
	void *pad_open_r, *pad_write_r, *pad_lseek_r, *pad_read_r;
	void  (*f_lock_init)(void *);
	void  (*f_lock_init_recursive)(void *);
	void  (*f_lock_close)(void *);
	void  (*f_lock_close_recursive)(void *);
	void  (*f_lock_acquire)(void *);
	void  (*f_lock_acquire_recursive)(void *);
	int   (*f_lock_try_acquire)(void *);
	int   (*f_lock_try_acquire_recursive)(void *);
	void  (*f_lock_release)(void *);
	void  (*f_lock_release_recursive)(void *);
	void *pad_printf_float, *pad_scanf_float;
};

static struct esp32_syscall_stub_table s_rom_stub_table = {
	.f_getreent = __getreent,
	.f_malloc_r = rl_malloc_r,
	.f_free_r = rl_free_r,
	.f_realloc_r = rl_realloc_r,
	.f_calloc_r = rl_calloc_r,
	.f_abort = rl_abort,
	.f_lock_init = rl_lock_noop,
	.f_lock_init_recursive = rl_lock_noop,
	.f_lock_close = rl_lock_noop,
	.f_lock_close_recursive = rl_lock_noop,
	.f_lock_acquire = rl_lock_noop,
	.f_lock_acquire_recursive = rl_lock_noop,
	.f_lock_try_acquire = rl_try_noop,
	.f_lock_try_acquire_recursive = rl_try_noop,
	.f_lock_release = rl_lock_noop,
	.f_lock_release_recursive = rl_lock_noop,
};

/*  syscall_table_ptr_pro/app, _global_impure_ptr は ROM ld の絶対シンボル
 *  （それぞれ RAM 0x3ffae024 / 0x3ffae020 / 0x3ffae0b0）。値を書き込む。 */
extern void *syscall_table_ptr_pro;
extern void *syscall_table_ptr_app;
extern struct _reent *_global_impure_ptr;

/*  ROM newlib を使う前（WiFi/coex blob 呼出し前）に一度呼ぶ。main_task 入口で
 *  esp_shim_initialize より前に呼ぶ（esp_shim.c の PSA/shim も errno を触りうる）。 */
void esp32_rom_libc_init(void)
{
	syscall_table_ptr_pro = &s_rom_stub_table;
	syscall_table_ptr_app = &s_rom_stub_table;
	_global_impure_ptr = _impure_ptr;
}

/*  esp_wifi API の一部：S3のlibnet80211には在るが無印v5.5 blobには非エクスポート
 *  （blob API面のチップ差）。libsupplicantが参照するためWPA2既定で安全なスタブ。
 *   - WPA3非対応(false)／PMKキャッシュskipなし(0)／STA IE無し(NULL/0)。 */
_Bool esp_wifi_is_wpa3_compatible_mode_enabled(void) { return(0); }
_Bool esp_wifi_skip_supp_pmkcaching(void) { return(0); }
/*  ★実シグネチャは uint8_t* esp_wifi_sta_get_ie(u8 *bssid, uint8_t elem_id)
 *  （esp_wifi_driver.h）。返り値はAPのIE先頭ポインタ（無ければNULL）。以前の
 *  スタブは (void*,int,void*,int*) と誤り、未設定レジスタ(a5)へ *d=0 を書いて
 *  StoreProhibited(EXCCAUSE=29)を起こしていた。正しい2引数・ポインタ返しに修正。
 *  無印blobはこのIEストアを持たないためNULL返し（RSNXE override等はNULLで無効、
 *  WPA2-PSKのRSN検証は supplicant 側の別経路で処理される）。 */
unsigned char *esp_wifi_sta_get_ie(unsigned char *bssid, unsigned char elem_id)
{
	(void)bssid; (void)elem_id; return((unsigned char *)0);
}
#endif

/* ---- esp_timer 最小スタブ（TODO: runtime用にshimタイマへ委譲） ----
 *
 *  ★これらは「作った」と言うだけで一度も発火しない。bt-classic では
 *  esp_timer_shim.c の本物を使う。coex はタイマのコールバックで
 *  セマフォを返すので、no-op のままだと btdm_controller_enable() が
 *  永久に待つ（2026-09-02 実機で実測）。 */
#if !defined(TOPPERS_BT_CLASSIC)
esp_err_t esp_timer_create(const void *args, void **out)     { (void)args; if(out)*out=(void*)1; return(ESP_OK); }
esp_err_t esp_timer_start_periodic(void *h, uint64_t us)     { (void)h;(void)us; return(ESP_OK); }
esp_err_t esp_timer_stop(void *h)                            { (void)h; return(ESP_OK); }
esp_err_t esp_timer_delete(void *h)                          { (void)h; return(ESP_OK); }
#endif

/* ---- WPA supplicant ----
 * esp_supplicant_init/deinit, hexstr2bin, g_wifi_default_wpa_crypto_funcs は
 * 本物の wpa_supplicant(libsupplicant.a: esp_wpa_main.c / utils/common.c /
 * crypto/crypto_ops.c)がSTA接続用に提供するため、当スタブから除去した
 * （WPA2 STA接続移植。build_wpa_libs.sh でアーカイブ化しXIPリンクに追加）。 */

/* ---- GPIO（blobのアンテナ/予約チェック。scanでは無効でよい）---- */
esp_err_t gpio_config(const void *cfg)          { (void)cfg; return(ESP_OK); }
_Bool esp_gpio_is_reserved(uint64_t mask)       { (void)mask; return(0); }

/* ---- mesh 内部（scanでは未使用）---- */
void *g_mt;
int  mt_get_peer_info(void *a, void *b)                 { (void)a;(void)b; return(0); }
int  esp_mesh_send_event_internal(int id, void *d, int l){ (void)id;(void)d;(void)l; return(0); }

/* ---- esp_event 追加（unregister）---- */
esp_err_t esp_event_handler_unregister(const char *base, int32_t id, void *h)
{
	(void)base;(void)id;(void)h; return(ESP_OK);
}
