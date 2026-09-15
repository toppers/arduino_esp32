/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  libcグローバルシンボルの提供（Wi-Fi統合用）
 *
 *  ツールチェーンにnewlibが無い環境のため，wpa_supplicant／mbedtls／
 *  blobが参照するlibc関数のうち，ROM（esp32c3.rom.newlib.ld等）に
 *  無いものをここで提供する：
 *    - malloc系：shimヒープへ委譲（ROMのmallocはROM専用ヒープ前提の
 *      ため使わない．こちらの定義が強シンボルとしてPROVIDEに勝つ）
 *    - ファイルI/O：ASP3にファイルシステムは無いため失敗を返すスタブ
 *      （mbedtlsのMBEDTLS_FS_IO経路＝実行時には使われない想定）
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>		/* SIL_PRE_LOC/SIL_LOC_INT/SIL_UNL_INT（1-3のCAS保護） */
#include <stddef.h>
#include <string.h>
#include <sys/lock.h>
#include <errno.h>
#include <limits.h>		/* INT_MAX（sprintf の上限） */
#include "esp_shim.h"
#include "esp_heap_caps.h"

/*
 *  errno実体（hal_stub/include/errno.hはexternのみ．BSDソケットAPI
 *  （lwip/src/api/sockets.c）が参照するため必要．単一グローバル＝
 *  タスク単位ではない点はerrno.h先頭コメントに既知の制限として記載
 *  済み（本ターゲットのソケット利用は現状1タスクのみのため実害なし）．
 */
int errno;

void *
malloc(size_t size)
{
	return(esp_shim_malloc(size));
}

void
free(void *ptr)
{
	esp_shim_free(ptr);
}

void *
calloc(size_t n, size_t size)
{
	return(esp_shim_calloc(n, size));
}

void *
realloc(void *ptr, size_t size)
{
	return(esp_shim_realloc(ptr, size));
}

int
rand(void)
{
	return((int)(esp_shim_random() & 0x7FFFFFFFU));
}

void
abort(void)
{
	syslog(LOG_EMERG, "abort() called");
	while (1) ;
}

void
__assert_func(const char *file, int line, const char *func, const char *expr)
{
	syslog(LOG_EMERG, "assert failed: %s:%d %s", file, (int_t)line, expr);
#ifdef ESP_SHIM_ASSERT_DRAIN_DELAY
	/*
	 *  診断ビルド専用（golden非対象）: UART送出済みバイト列の
	 *  ドレインを待ってから停止する。実機キャプチャで「assert発火後も
	 *  ログが続く」現象がドレインかどうかを切り分けるための一時的な
	 *  遅延（2026-08-08、LM-D-4 btclassic実機調査）。
	 */
	sil_dly_nse(2000000000UL);
#endif /* ESP_SHIM_ASSERT_DRAIN_DELAY */
	while (1) ;
}

/*
 *  ファイルI/O（スタブ）
 */
struct __FILE;

struct __FILE *
fopen(const char *path, const char *mode)
{
	(void) path; (void) mode;
	return(NULL);
}

int
fclose(struct __FILE *fp)
{
	(void) fp;
	return(-1);
}

size_t
fread(void *ptr, size_t size, size_t n, struct __FILE *fp)
{
	(void) ptr; (void) size; (void) n; (void) fp;
	return(0);
}

size_t
fwrite(const void *ptr, size_t size, size_t n, struct __FILE *fp)
{
	(void) ptr; (void) size; (void) n; (void) fp;
	return(0);
}

int
fseek(struct __FILE *fp, long offset, int whence)
{
	(void) fp; (void) offset; (void) whence;
	return(-1);
}

long
ftell(struct __FILE *fp)
{
	(void) fp;
	return(-1);
}

int
ferror(struct __FILE *fp)
{
	(void) fp;
	return(1);
}

/*
 *  esp_log系（blob／esp-halソースが参照．syslogへ折返し）
 */
#include <stdarg.h>
#include <stdio.h>

#if defined(TOPPERS_ESP32C6)
/*
 *  ESP32-C6（段4 Task 4・2026-09-14）: esp_log()/esp_log_write() の整形バッファを
 *  静的ローテーションにする（esp_wifi_adapter.c の log_writev_wrapper と同じ理由）。
 *  syslog() の "%s" は文字列ポインタをそのまま積み、整形は logtask が後で行う
 *  ため、スタックローカルの buf を渡すと整形時点で内容が消えている（S3 でも
 *  同じ経路で `phy_version` 行が観測できていない）。AC-4d が要求する
 *  `phy: phy_version` 行（esp/wifi/hal_src/phy_lib_printf.c -> ESP_LOGI ->
 *  esp_log）を実機で読むために、C6 だけスロットへ写してから渡す。
 *  スロット数を超えて logtask より先に呼ばれれば古い行は上書きされる
 *  （欠落はするがダングリングにはならない）。S3/LX6/P4 の前処理結果は不変。
 */
#define ESP_LOG_RINGBUF_SLOTS	16
#define ESP_LOG_RINGBUF_MSGLEN	128

static char		esp_log_ringbuf[ESP_LOG_RINGBUF_SLOTS][ESP_LOG_RINGBUF_MSGLEN];
static uint32_t	esp_log_ringbuf_idx;

static char *
esp_log_ringbuf_slot(void)
{
	uint32_t	lock;
	uint32_t	slot;

	lock = esp_shim_int_disable();
	slot = esp_log_ringbuf_idx;
	esp_log_ringbuf_idx = (esp_log_ringbuf_idx + 1U) % ESP_LOG_RINGBUF_SLOTS;
	esp_shim_int_restore(lock);
	return(esp_log_ringbuf[slot]);
}

void
esp_log(unsigned int config, const char *tag, const char *format, ...)
{
	char	*buf = esp_log_ringbuf_slot();
	va_list	args;

	(void) config; (void) tag;
	va_start(args, format);
	vsnprintf(buf, ESP_LOG_RINGBUF_MSGLEN, format, args);
	va_end(args);
	syslog(LOG_NOTICE, "%s", buf);
}
#else /* TOPPERS_ESP32C6 */
void
esp_log(unsigned int config, const char *tag, const char *format, ...)
{
	char	buf[128];
	va_list	args;

	(void) config; (void) tag;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	syslog(LOG_NOTICE, "%s", buf);
}
#endif /* TOPPERS_ESP32C6 */

void
esp_log_level_set(const char *tag, int level)
{
	(void) tag; (void) level;
}

/*
 *  W3(BlueDroidホスト)：ESP_LOGx系マクロが最終的に呼ぶesp_log_write()
 *  （esp_log_write.h参照。esp_log()とは別の新しめのAPI，BlueDroid各所
 *  ・osi/future.c・hci層等が広く使う）。既存esp_log()と同じくsyslogへ
 *  折り返す。levelは現状無視（本ポートはsyslogのpriority別フィルタを
 *  持たないため，既存esp_log()同様LOG_NOTICE固定で足りる）。
 */
void
esp_log_write(int level, const char *tag, const char *format, ...)
{
#if defined(TOPPERS_ESP32C6)
	char	*buf = esp_log_ringbuf_slot();	/* 上の esp_log() と同じ理由 */
#else /* TOPPERS_ESP32C6 */
	char	buf[128];
#endif /* TOPPERS_ESP32C6 */
	va_list	args;

	(void) level; (void) tag;
	va_start(args, format);
#if defined(TOPPERS_ESP32C6)
	vsnprintf(buf, ESP_LOG_RINGBUF_MSGLEN, format, args);
#else /* TOPPERS_ESP32C6 */
	vsnprintf(buf, sizeof(buf), format, args);
#endif /* TOPPERS_ESP32C6 */
	va_end(args);
	syslog(LOG_NOTICE, "%s", buf);
}

unsigned int
esp_log_timestamp(void)
{
	return((unsigned int)(esp_shim_time_us() / 1000));
}

/*
 *  vsnprintf／snprintf／sprintf／puts（ROM newlib.ldに実体が無い．
 *  ツールチェーンにnewlib実体を持たない環境向けの簡易printf実装）
 *
 *  対応：%d %i %u %x %X %o %c %s %p %%．フラグ'-'（左寄せ）'0'（0埋め）．
 *  幅指定（10進数）．長さ修飾子'l'/'ll'（32/64bit）．'h'/'hh'は
 *  va_argの昇格規則によりint/unsignedとして読めば十分なため無視する．
 *  精度指定（%.*f等）・浮動小数点変換は非対応（wpa_supplicant／
 *  esp-halソースのログ文字列は整数・文字列が主で浮動小数点は
 *  使っていないため実用上十分．必要になった時点で拡張する）．
 */
typedef struct {
	char	*buf;
	size_t	size;	/* バッファ容量（NUL込み） */
	size_t	total;	/* 書き込んだ（はずの）文字数．NUL含まず */
} VSN_CTX;

static void
vsn_putc(VSN_CTX *ctx, char c)
{
	if (ctx->size > 0U && ctx->total + 1U < ctx->size) {
		ctx->buf[ctx->total] = c;
	}
	ctx->total++;
}

static void
vsn_puts_raw(VSN_CTX *ctx, const char *s)
{
	while (*s != '\0') {
		vsn_putc(ctx, *s++);
	}
}

/*
 *  符号無し整数を指定基数で出力（幅・0埋め・符号（'-'固定文字）対応）
 */
static void
vsn_putnum(VSN_CTX *ctx, uint64_t val, unsigned int base, bool_t upper,
		   int width, bool_t zero_pad, bool_t left, bool_t neg)
{
	static const char digits_lo[] = "0123456789abcdef";
	static const char digits_up[] = "0123456789ABCDEF";
	const char	*digits = upper ? digits_up : digits_lo;
	char		tmp[24];
	int			ndig = 0;
	int			pad;
	int			i;

	if (val == 0U) {
		tmp[ndig++] = '0';
	}
	while (val != 0U && ndig < (int) sizeof(tmp)) {
		tmp[ndig++] = digits[val % base];
		val /= base;
	}

	pad = width - ndig - (neg ? 1 : 0);
	if (pad < 0) {
		pad = 0;
	}

	if (left) {
		if (neg) {
			vsn_putc(ctx, '-');
		}
		for (i = 0; i < ndig; i++) {
			vsn_putc(ctx, tmp[ndig - 1 - i]);
		}
		while (pad-- > 0) {
			vsn_putc(ctx, ' ');
		}
	} else if (zero_pad) {
		if (neg) {
			vsn_putc(ctx, '-');
		}
		while (pad-- > 0) {
			vsn_putc(ctx, '0');
		}
		for (i = 0; i < ndig; i++) {
			vsn_putc(ctx, tmp[ndig - 1 - i]);
		}
	} else {
		while (pad-- > 0) {
			vsn_putc(ctx, ' ');
		}
		if (neg) {
			vsn_putc(ctx, '-');
		}
		for (i = 0; i < ndig; i++) {
			vsn_putc(ctx, tmp[ndig - 1 - i]);
		}
	}
}

int
vsnprintf(char *buf, size_t size, const char *format, va_list ap)
{
	VSN_CTX	ctx;
	char	c;

	ctx.buf = buf;
	ctx.size = size;
	ctx.total = 0U;

	while ((c = *format++) != '\0') {
		bool_t	left, zero_pad, longlong, is_long;
		int		width;

		if (c != '%') {
			vsn_putc(&ctx, c);
			continue;
		}

		left = false;
		zero_pad = false;
		for (;;) {
			c = *format;
			if (c == '-') {
				left = true;
			} else if (c == '0') {
				zero_pad = true;
			} else if (c == '+' || c == ' ' || c == '#') {
				/*  未対応フラグ（符号常時表示等）は読み飛ばすのみ  */
			} else {
				break;
			}
			format++;
		}

		width = 0;
		while (*format >= '0' && *format <= '9') {
			width = width * 10 + (int) (*format - '0');
			format++;
		}

		is_long = false;
		longlong = false;
		if (*format == 'l') {
			is_long = true;
			format++;
			if (*format == 'l') {
				longlong = true;
				format++;
			}
		} else if (*format == 'h') {
			format++;
			if (*format == 'h') {
				format++;
			}
		} else if (*format == 'z' || *format == 't') {
			is_long = true;
			format++;
		}

		c = *format++;
		switch (c) {
		case 'd':
		case 'i':
			{
				int64_t	v = longlong ? va_arg(ap, long long)
							: is_long ? va_arg(ap, long) : va_arg(ap, int);
				bool_t	neg = (v < 0);
				uint64_t uv = neg ? (uint64_t) (-v) : (uint64_t) v;
				vsn_putnum(&ctx, uv, 10U, false, width, zero_pad, left, neg);
			}
			break;
		case 'u':
			{
				uint64_t v = longlong ? va_arg(ap, unsigned long long)
							: is_long ? va_arg(ap, unsigned long)
							: va_arg(ap, unsigned int);
				vsn_putnum(&ctx, v, 10U, false, width, zero_pad, left, false);
			}
			break;
		case 'x':
		case 'X':
			{
				uint64_t v = longlong ? va_arg(ap, unsigned long long)
							: is_long ? va_arg(ap, unsigned long)
							: va_arg(ap, unsigned int);
				vsn_putnum(&ctx, v, 16U, (c == 'X'), width, zero_pad, left,
						   false);
			}
			break;
		case 'o':
			{
				uint64_t v = longlong ? va_arg(ap, unsigned long long)
							: is_long ? va_arg(ap, unsigned long)
							: va_arg(ap, unsigned int);
				vsn_putnum(&ctx, v, 8U, false, width, zero_pad, left, false);
			}
			break;
		case 'p':
			vsn_puts_raw(&ctx, "0x");
			vsn_putnum(&ctx, (uint64_t) (uintptr_t) va_arg(ap, void *),
					   16U, false, 0, false, false, false);
			break;
		case 'c':
			vsn_putc(&ctx, (char) va_arg(ap, int));
			break;
		case 's':
			{
				const char *s = va_arg(ap, const char *);

				if (s == NULL) {
					s = "(null)";
				}
				if (!left && width > 0) {
					int len = (int) strlen(s);
					int pad = width - len;

					while (pad-- > 0) {
						vsn_putc(&ctx, ' ');
					}
				}
				vsn_puts_raw(&ctx, s);
				if (left && width > 0) {
					int len = (int) strlen(s);
					int pad = width - len;

					while (pad-- > 0) {
						vsn_putc(&ctx, ' ');
					}
				}
			}
			break;
		case '%':
			vsn_putc(&ctx, '%');
			break;
		case '\0':
			format--;
			break;
		default:
			vsn_putc(&ctx, '%');
			vsn_putc(&ctx, c);
			break;
		}
	}

	if (ctx.size > 0U) {
		size_t	end = (ctx.total < ctx.size - 1U) ? ctx.total : ctx.size - 1U;

		ctx.buf[end] = '\0';
	}
	return((int) ctx.total);
}

int
snprintf(char *buf, size_t size, const char *format, ...)
{
	va_list	args;
	int		ret;

	va_start(args, format);
	ret = vsnprintf(buf, size, format, args);
	va_end(args);
	return(ret);
}

int
sprintf(char *buf, const char *format, ...)
{
	va_list	args;
	int		ret;

	/*
	 *  ASP3にはファイルシステム上限の概念が無く，呼び出し元
	 *  （wifi_set_default_ssid等）は十分なバッファを渡す前提のため，
	 *  実質無制限としてvsnprintfへ委譲する．
	 *
	 *  2026-08-03: 上限を `(size_t)-1`（SIZE_MAX）から `INT_MAX` へ変えた。
	 *  【なぜ】GCC が `specified bound 4294967295 exceeds maximum object size
	 *    2147483647`（-Wformat-truncation）と警告していた。
	 *
	 *  2026-08-04（統合レビュー E-7）: **理由の書き方を直した。**
	 *  【意味論が変わらない本当の理由】この構成で呼ばれる `vsnprintf` は
	 *    **newlib のものではなく、同一 TU の自前実装**である（本ファイル上部・
	 *    `VSN_CTX`）。境界判定は `vsn_putc()` の
	 *    `ctx->size > 0U && ctx->total + 1U < ctx->size` だけで、
	 *    **`buf + n` を一度も計算しない**し、newlib のような `EOVERFLOW` 門番も無い。
	 *    ⇒ `SIZE_MAX` でも**元から壊れていなかった**。
	 *  旧コメントの「戻り値が `int` だから INT_MAX 超は表現できない」は、
	 *    **理由として弱い**（表現できないことと、境界計算が溢れないことは別問題である）。
	 *  【では何のために変えたか】**GCC は名前で `vsnprintf` を組込みと見なして
	 *    警告を出していた**（自前実装であることを知らない）。`INT_MAX` は
	 *    **「処理系が組込みだと仮定しても壊れない」側**へ寄せる変更であって、
	 *    現に在るバグを直したものではない。移植性の観点では正しい。
	 */
	va_start(args, format);
	ret = vsnprintf(buf, (size_t) INT_MAX, format, args);
	va_end(args);
	return(ret);
}

int
puts(const char *s)
{
	syslog(LOG_NOTICE, "%s", s);
	return(0);
}

/*
 *  setbuf（バッファリング設定．ASP3にストリームI/Oは無いためno-op）
 */
struct __FILE;

void
setbuf(struct __FILE *fp, char *buf)
{
	(void) fp; (void) buf;
}

/*
 *  remove／rename（ファイルシステム無し．常に失敗を返すスタブ）
 */
int
remove(const char *path)
{
	(void) path;
	return(-1);
}

int
rename(const char *oldpath, const char *newpath)
{
	(void) oldpath; (void) newpath;
	return(-1);
}

/*
 *  usleep／sleep（dly_tskへ委譲．ASP3のdly_tskはμs単位のRELTIM）
 */
int
usleep(unsigned long usec)
{
	dly_tsk((RELTIM) usec);
	return(0);
}

unsigned int
sleep(unsigned int seconds)
{
	dly_tsk((RELTIM) seconds * 1000000U);
	return(0);
}

/*
 *  gettimeofday（SYSTIMER起点のμsをtimevalへ変換．起動時刻=エポック0
 *  扱い＝wpa_supplicant/port/os_xtensa.cのos_get_time()はNTP等の絶対
 *  時刻ではなく単調増加時刻として使うため，これで用が足りる）
 */
struct timeval {
	long	tv_sec;
	long	tv_usec;
};
struct timezone;

int
gettimeofday(struct timeval *tv, struct timezone *tz)
{
	int64_t	us;

	(void) tz;
	if (tv == NULL) {
		return(-1);
	}
	us = esp_shim_time_us();
	tv->tv_sec = (long) (us / 1000000);
	tv->tv_usec = (long) (us % 1000000);
	return(0);
}

/*
 *  esp_fill_random（乱数バイト列．HW RNG=esp_shim_randomを繰り返す）
 */
void
esp_fill_random(void *buf, size_t len)
{
	uint8_t	*p = (uint8_t *) buf;
	size_t	i = 0U;

	while (i < len) {
		uint32_t	r = esp_shim_random();
		size_t		n = (len - i < 4U) ? (len - i) : 4U;
		size_t		j;

		for (j = 0U; j < n; j++) {
			p[i + j] = (uint8_t) (r >> (8U * j));
		}
		i += n;
	}
}

/*
 *  esp_random（32bit乱数．wpa_supplicant os_xtensa.cのos_random→esp_randomが
 *  参照する。WPA2 STA接続移植で必要。HW RNG=esp_shim_randomへ委譲）
 */
uint32_t
esp_random(void)
{
	return(esp_shim_random());
}

/*
 *  heap_caps_free（esp_shim_freeへ委譲．malloc系はmalloc/free等の
 *  ラッパと同じくshimヒープに一本化＝ESP32-C3はDMA/internal区別不要
 *  というdocs/wifi-shim.mdの設計方針どおり）
 */
void
heap_caps_free(void *ptr)
{
	esp_shim_free(ptr);
}

/*
 *  heap_caps_malloc（esp_shim_mallocへ委譲．heap_caps_freeと同じく
 *  DMA/internal属性はESP32-C3では区別不要のためcapsは無視する．
 *  esp_phy/src/phy_init.cのesp_phy_modem_init()
 *  （SOC_PM_MODEM_RETENTION_BY_BACKUPDMA=1のためPHYディジタル
 *  レジスタ退避バッファ21*4バイトをここで確保．wifi_init.cの
 *  esp_wifi_init()から呼ばれる＝到達コード）が要求する．
 */
void *
heap_caps_malloc(size_t size, uint32_t caps)
{
	(void) caps;
	return(esp_shim_malloc(size));
}

/*
 *  heap_caps_calloc（Bluetooth統合．Phase D-1．bt.cのsemphr_create_
 *  wrapper等が要求する．heap_caps_malloc/freeと同じくcapsは無視）
 */
void *
heap_caps_calloc(size_t n, size_t size, uint32_t caps)
{
	(void) caps;
	return(esp_shim_calloc(n, size));
}

/*
 *  esp_timer_get_time（hal_stub/include/esp_timer.h参照．
 *  esp_shim_time_us＝SYSTIMER起点のμsへ委譲）
 */
int64_t
esp_timer_get_time(void)
{
	return(esp_shim_time_us());
}

/*
 *  ------------------------------------------------------------------
 *  newlib retargetable locking（sys/lock.h．_lock_t＝void*）
 *  ------------------------------------------------------------------
 *
 *  esp_phy/src/phy_init.cのs_phy_access_lock（PHY enable/disable/
 *  modem_init/deinitの排他制御．register_chipv7_phy()によるフル較正
 *  は数百ms〜要することがあるため，loc_cpu/unl_cpuによる割込み禁止
 *  ではなくタスクブロッキング可能な本物のミューテックスが必要）が
 *  使う．実体はesp_shim_mutex_*（wifi_shim基盤．2026-08-04・段1 で
 *  静的CRE_MTXプールから**動的生成 acre_mtx/del_mtx** へ移行．
 *  esp/shim/esp_shim_mtx.c）に委譲する．_lock_tは静的グローバル変数として
 *  0初期化される（newlib流儀）ため，_lock_acquire側で遅延生成する．
 *
 *  動的化で変わった点（2026-08-04・DESIGN-MEMO §7-5b の軸）：
 *  `acre_mtx` は `CHECK_TSKCTX_UNL`＝**タスク文脈かつCPUロック外**でしか
 *  成功しない．旧プールは `SHIM_LOCK`（rsil 15）内でスロットを配っていたので
 *  **どの文脈からでも黙って通っていた**．⇒ 禁止文脈（ISR中／CPUロック中）から
 *  `_lock_acquire` が来ると，生成は `E_CTX` で失敗し `*lock` は NULL のまま残る．
 *  これは**恒久的な破損にならない**——本関数は毎回の `_lock_acquire` から
 *  呼ばれる冪等な遅延生成なので，次にタスク文脈から来たときに生成される
 *  （self-healing）．失敗は `esp_shim_mtx_acre_fail` に数えられ診断へ出る．
 *  なお禁止文脈では `esp_shim_mutex_lock`→`loc_mtx` も同じ `E_CTX` で必ず
 *  失敗するので，**その呼出しが排他を得られない点は動的化の前後で変わらない**．
 *
 *  [改変] 2026-07-16 レビュー指摘F-1修正（migration-defect-review）：
 *  旧実装は遅延生成の競合をloc_cpu/unl_cpuで保護していたが，FMP3の
 *  loc_cpuは**自プロセッサのみ**のCPUロックでコア間排他にならない．
 *  PRC_NUM=2で2タスクが別コアから同一未初期化_lock_t（例：HWクリプトの
 *  s_crypto_sha_aes_lock）へ初回同時到達すると，両者が生成側分岐に入り
 *  mutexが二重生成される（1個リーク＋両タスクが別mutexを掴む＝以後その
 *  _lock_tの排他が恒久的に破れる）．PRC_NUM=1構成では旧実装も正しく，
 *  本バグはSMP合流（W0×Wi-Fi/TLS）で顕在化する時限バグだった．
 *
 *  新実装＝「先に生成し，_lock_tスロットへCASで敗者検出インストール」：
 *   1. *lockが非NULLなら初期化済み＝即return（fast path，従来同等）．
 *   2. esp_shim_mutex_create()でmutexを先に生成（動的化後は `acre_mtx`＝
 *      **CPUロックの外**で呼ぶ．臨界区間を最小化する意図は同じ）．
 *   3. CAS(*lock, NULL, m)：勝者はそのままインストール完了．敗者は
 *      勝者のmutexが既に*lockに入っているので，自分の生成分を
 *      esp_shim_mutex_delete()で解放（動的化後は `del_mtx`．
 *      これも**CPUロックの外**で呼ぶ必要がある——下の TNUM_PRCID<2 経路で
 *      delete が `unl_cpu()` の**後**に置いてあるのはそのためである）．
 *      リークも二重mutexも発生しない．
 *  CAS実体は`fmp3/arch/xtensa_gcc/common/xtensa_cas.h`の`xtensa_core_cas()`
 *  （2026-08-06・フェーズ1 1-3で1本化．旧「chip_kernel_impl.hはカーネル
 *  内部型依存でシム層からincludeできないため複製する」という方針は，
 *  CASだけカーネル内部型に依存しない形で切り出したことで不要になった．
 *  ATOMCTL注意＝内蔵SRAM限定，はarch側コメント参照．_lock_t静的変数は
 *  DRAM＝内蔵SRAM配置）．
 *  同時に，このCAS呼出しがxtensa_cas.hの不変条件（呼出し元が自コアの
 *  割込みを完全にマスクした文脈で呼ぶこと）に対する唯一の違反者だった
 *  ことが判明した——`_lock_acquire`（タスク文脈＝INTLEVEL=0）から
 *  `SIL_LOC_INT()`を挟まずCASを呼んでいた．CASの前後をSIL_LOC_INT()/
 *  SIL_UNL_INT()で囲んで修正した（下のTNUM_PRCID>=2節）．
 *  単一コア（TNUM_PRCID<2）はS32C1I/CAS定義を持ち込まず，従来の
 *  loc_cpu再チェック方式を維持する（単一コアでは自コアCPUロックで
 *  タスク間プリエンプション競合を防げており正しい）．
 *
 *  ロック取得後の本待ち合わせ（esp_shim_mutex_lock）は従来どおり
 *  生成区間の外＝タスクブロッキング可能．
 */
#if TNUM_PRCID >= 2
#include <xtensa_cas.h>		/* 2026-08-06（フェーズ1 1-3）: CAS実体を1本化 */
#endif /* TNUM_PRCID >= 2 */

static void
esp_shim_lock_lazy_init(_lock_t *lock)
{
	void	*m;

	if (*lock != NULL) {
		return;			/* 初期化済み（fast path） */
	}
	m = esp_shim_mutex_create(true);	/* 再帰可 */
	if (m == NULL) {
		/*  プール枯渇：*lockはNULLのまま＝呼出し側（_lock_acquire）が
		 *  検出してログする（レビュー指摘F-3）。ここでは静かに返る。 */
		return;
	}
#if TNUM_PRCID >= 2
	/*  _lock_tは本ターゲットで32bit（ポインタ）。CASの型前提を固定する。 */
	_Static_assert(sizeof(_lock_t) == sizeof(uint32_t),
				   "_lock_t must be 32-bit for CAS install");
	{
		bool_t	won;
		SIL_PRE_LOC;

		/*
		 *  2026-08-06（フェーズ1 1-3・現行バグの修正）: CASの前後を
		 *  SIL_LOC_INT()/SIL_UNL_INT()（PS.INTLEVEL=15）で囲む。
		 *  `xtensa_cas.h` が明記する不変条件（`wsr scompare1` と `s32c1i`
		 *  の間でSCOMPARE1が破壊されないこと＝呼出し元が自コアの割込みを
		 *  完全にマスクした文脈で呼ぶこと）への唯一の違反者だった——
		 *  本関数は `_lock_acquire`（タスク文脈＝INTLEVEL=0）から呼ばれる。
		 *  `esp_shim_mutex_delete()` はCPUロックの外で呼ぶ規約（本関数
		 *  冒頭のコメント）なので、ロックはCASの前後だけに絞り、
		 *  delete はロックを離してから呼ぶ。
		 */
		SIL_LOC_INT();
		won = xtensa_core_cas((volatile uint32_t *) lock, 0U, (uint32_t) m);
		SIL_UNL_INT();
		if (!won) {
			/*  敗者：他コア/他タスクが先にインストール済み。勝者のmutexを
			 *  使う（*lockは既に非NULL）。自分の生成分はプールへ返却。 */
			esp_shim_mutex_delete(m);
		}
	}
#else /* TNUM_PRCID < 2 */
	{
		ER	ercd;

		ercd = loc_cpu();
		if (*lock == NULL) {
			*lock = m;
			m = NULL;		/* インストール成功＝返却不要 */
		}
		if (ercd == E_OK) {
			(void) unl_cpu();
		}
		if (m != NULL) {
			/*  プリエンプションで他タスクが先行インストールした場合 */
			esp_shim_mutex_delete(m);
		}
	}
#endif /* TNUM_PRCID >= 2 */
}

void
_lock_init(_lock_t *lock)
{
	*lock = esp_shim_mutex_create(true);
}

void
_lock_init_recursive(_lock_t *lock)
{
	*lock = esp_shim_mutex_create(true);
}

void
_lock_close(_lock_t *lock)
{
	if (*lock != NULL) {
		esp_shim_mutex_delete(*lock);
		*lock = NULL;
	}
}

void
_lock_close_recursive(_lock_t *lock)
{
	_lock_close(lock);
}

/*
 *  [改変] 2026-07-16 レビュー指摘F-3対応（最小）：旧実装は
 *  esp_shim_mutex_lock()の失敗（mutexプール枯渇で*lock==NULL、
 *  非タスク文脈でのloc_mtx失敗等）を(void)で黙殺し、**排他なしで
 *  黙って続行**していた。newlibロックの排他消失はサイレントな
 *  データ破壊につながるため、失敗を必ずsyslogへ可視化する
 *  （挙動は従来どおり続行＝呼出し規約void。ログのみ追加）。
 */
static void
shim_libc_lock_acquire_common(_lock_t *lock)
{
	esp_shim_lock_lazy_init(lock);
	if (esp_shim_mutex_lock(*lock) == 0) {
		syslog(LOG_ERROR,
			   "esp_shim_libc: _lock_acquire FAILED (lock=%p mtx=%p) - "
			   "proceeding WITHOUT exclusion", (void *) lock, *lock);
	}
}

void
_lock_acquire(_lock_t *lock)
{
	shim_libc_lock_acquire_common(lock);
}

void
_lock_acquire_recursive(_lock_t *lock)
{
	shim_libc_lock_acquire_common(lock);
}

int
_lock_try_acquire(_lock_t *lock)
{
	/*  レビュー指摘#10：newlibの_lock_try_acquireは「取得できなければ
	 *  即座に失敗を返す」非ブロッキング契約。以前はesp_shim_mutex_lock
	 *  （loc_mtx委譲、取得できるまでタスクブロッキング）を流用しており
	 *  この契約に違反していた（現状は本APIの実呼び出し元が無くlatentだが、
	 *  将来newlib内部やライブラリコードが本当に「非ブロッキングでの
	 *  試行」を期待して呼ぶと、ブロッキングして意図しない待ち合わせに
	 *  なる恐れがあった）。FMP3のploc_mtx（ポーリング版ロック要求、
	 *  取得できなければ即E_OBJ相当で返る非ブロッキングAPI）に委譲する
	 *  esp_shim_mutex_trylockへ差し替え、契約を満たすようにした。 */
	esp_shim_lock_lazy_init(lock);
	return((esp_shim_mutex_trylock(*lock) != 0) ? 0 : -1);
}

int
_lock_try_acquire_recursive(_lock_t *lock)
{
	return(_lock_try_acquire(lock));
}

void
_lock_release(_lock_t *lock)
{
	if (*lock != NULL) {
		(void) esp_shim_mutex_unlock(*lock);
	}
}

void
_lock_release_recursive(_lock_t *lock)
{
	_lock_release(lock);
}

/*
 *  coexist_printf（libcoexist.aが参照．syslogへ折返し）
 */
int
coexist_printf(const char *format, ...)
{
#if defined(TOPPERS_ESP32C6)
	/*  段4 Task 4: esp_log() と同じ理由（スタック buf のダングリング）。
	 *  coex_init() の版数行が実機で文字化けしていた（logs/task4-160-flash3.log）。  */
	char	*buf = esp_log_ringbuf_slot();
#else /* TOPPERS_ESP32C6 */
	char	buf[128];
#endif /* TOPPERS_ESP32C6 */
	va_list	args;

	va_start(args, format);
#if defined(TOPPERS_ESP32C6)
	vsnprintf(buf, ESP_LOG_RINGBUF_MSGLEN, format, args);
#else /* TOPPERS_ESP32C6 */
	vsnprintf(buf, sizeof(buf), format, args);
#endif /* TOPPERS_ESP32C6 */
	va_end(args);
	syslog(LOG_NOTICE, "%s", buf);
	return(0);
}
