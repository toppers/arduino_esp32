/*
 *  M5.begin() 段階到達トレース（M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ★存在理由（S3 / 2026-07-21）
 *    `M5.begin(config_t)` は M5Unified.hpp の **inline 関数**であり、その中身は
 *    アプリ TU（m5_unified_smoke.cpp）へ展開される。段階1 の最大の教訓は
 *    **「沈黙は両義的」**（＝止まったのかまだ動いているのか区別できない）なので、
 *    begin() の内部でどこまで進んだかを「到達したら痕跡が残る」形で観測したい。
 *    しかし M5Unified / M5GFX submodule は**無改変**が絶対条件である。
 *
 *  【方式】リンカの `--wrap=<mangled>` を使う。begin() が呼ぶ非 inline メンバ関数は
 *    アプリ TU から見て **未定義参照**なので、`--wrap` が確実に横取りできる
 *    。
 *    ラッパは入口 / 出口で 1 行ずつ即時出力し、`__real_<mangled>` を呼ぶ。
 *
 *  ★この方式が「黙って無効化される」ことは無い（fail-closed）:
 *    `--wrap=X` の X が実在しない（綴り違い・mangle 変化・submodule 更新）場合、
 *    `__real_X` は**未定義参照のまま残りリンクが失敗する**。
 *    ＝「-D が黙って無視された」型の無効 NC にはなり得ない。
 *    （S3 で綴りを 1 文字壊してリンクが落ちることを実演済み。）
 *
 *  ★出力は target_fput_log 直（logtask 非経由・printf 非経由）。
 *    理由は m5_stub_trace.c 冒頭と同じ：直後にハングすると syslog は過去分ごと消える。
 *
 *  ★ABI について
 *    C から C++ のメンバ関数を呼ぶ。Xtensa windowed ABI では `this` が第1引数
 *    （a2）に載る通常の引数渡しなので、`(void *self, ...)` の C 関数として
 *    宣言すれば呼出し・返却とも一致する。列挙型 board_t は int として渡る。
 *    `_setup_pinmap` だけは **static メンバ**なので `this` を取らない（実ソース
 *    M5Unified.hpp:657 `static void _setup_pinmap(board_t);` で確認）。
 */

/*
 *  ★S5：出力は m5_putc_now_ext（有界。実体 m5_diag.c）を通す。target_fput_log 直だと
 *  A1_CONSOLE_USJ=OFF（実機 Port C 採取）の UART0 経路が**無界スピン**で、
 *  計装自身がハングし観測が消える（m5_stub_trace.h の宣言部コメント参照）。
 */
#include "m5_stub_trace.h"

/*  ------------------------------------------------------------------
 *  低レベル出力（依存は target_fput_log のみ）
 *  ------------------------------------------------------------------ */

static unsigned int		m5_bt_nenters;	/* 入口を踏んだ延べ回数 */
static unsigned int		m5_bt_nleaves;	/* 出口を踏んだ延べ回数 */
/*  ★S6-a：1 なら**出力だけ**抑止する（カウンタは数え続ける。下の
 *  m5_begin_trace_quiet のコメント参照）。 */
static unsigned int		m5_bt_quiet;

static void
m5_bt_puts(const char *s)
{

	if (s == 0) {
		s = "(null)";
	}
	while (*s != '\0') {
		m5_putc_now_ext(*s++);
	}
}

static void
m5_bt_puti(int v)
{
	char			buf[12];
	unsigned int	i = 0U;
	unsigned int	u;

	if (v < 0) {
		m5_putc_now_ext('-');
		u = (unsigned int)(-(long)v);
	} else {
		u = (unsigned int)v;
	}
	if (u == 0U) {
		m5_putc_now_ext('0');
		return;
	}
	while ((u != 0U) && (i < sizeof(buf))) {
		buf[i++] = (char)('0' + (u % 10U));
		u /= 10U;
	}
	while (i > 0U) {
		m5_putc_now_ext(buf[--i]);
	}
}

/*
 *  ★ここを no-op にすると nenters が 0 のままになる＝
 *    m5_qemu_main.c の check_assert(m5_begin_trace_nenters() > 0) が落ちる。
 *    S3 の negative control（NC=4）はこの 1 箇所を壊して実演する。
 */
void
m5_bt_enter(const char *name)
{

#if defined(M5_QEMU_NC) && (M5_QEMU_NC == 4)
	/*  NC=4: 記録機構を意図的に殺す（ハーネスが本当に落ちるかの実演）。 */
	(void) name;
	return;
#else
	m5_bt_nenters++;
	if (m5_bt_quiet == 0U) {
		m5_bt_puts("[BEGIN] -> ");
		m5_bt_puts(name);
		m5_putc_now_ext('\n');
	}
#endif
}

void
m5_bt_leave(const char *name, int has_val, int val)
{

	m5_bt_nleaves++;
	if (m5_bt_quiet != 0U) {
		return;
	}
	m5_bt_puts("[BEGIN] <- ");
	m5_bt_puts(name);
	if (has_val != 0) {
		m5_bt_puts(" = ");
		m5_bt_puti(val);
	}
	m5_putc_now_ext('\n');
}

/*
 *  ==================================================================
 *  ★S6-a：トレース**出力**の抑止（カウンタは止めない）
 *  ==================================================================
 *  【解く問題】S6-a の 60 秒生存ループは `M5.update()` を 600 回呼ぶ。
 *    `M5Unified::update` は 13 本の --wrap 対象の 1 つ（P8）なので、
 *    そのままだと **[BEGIN] 行が 1,200 本増える**。S5 のログは
 *    [BEGIN] が 36 本しか無いことで読めていたのに、それが 34 倍になる。
 *    ★S5-analysis §9 が「ログの 79.8% がノイズ」と指摘した状態を、
 *      観測系を直した直後に自分で作り直すことになる。
 *  【方式】**出力だけを止め、カウンタ（nenters/nleaves）は数え続ける。**
 *    ⇒ 「enters == leaves」という健全性判定は S6-a 区間でも有効なまま。
 *    ⇒ 抑止したこと自体を 1 行申告するので、沈黙が両義にならない。
 *  ★AC の採取（m5_unified_collect_ac）は本関数を呼ぶ**前**に終わっている
 *    ので、S5 と比較する `trace enters=0x12` の値には影響しない。
 */
void
m5_begin_trace_quiet(void)
{

	m5_bt_quiet = 1U;
}

void
m5_begin_trace_loud(void)
{

	m5_bt_quiet = 0U;
}

unsigned int
m5_begin_trace_nenters(void)
{

	return m5_bt_nenters;
}

unsigned int
m5_begin_trace_nleaves(void)
{

	return m5_bt_nleaves;
}

/*  ------------------------------------------------------------------
 *  ラッパ本体
 *
 *  mangled 名は `nm --defined-only` の実測値（推測していない）:
 *    build/seam-s3-m5/xip/objs/{M5Unified,M5GFX,Power_Class,RTC_Class,IMU_Class}.o
 *  ------------------------------------------------------------------ */

#define M5_BT_WRAP_V1(sym, label)										\
	extern void __real_##sym(void *self, int a);						\
	void __wrap_##sym(void *self, int a);								\
	void __wrap_##sym(void *self, int a)								\
	{																	\
		m5_bt_enter(label);												\
		__real_##sym(self, a);											\
		m5_bt_leave(label, 0, 0);										\
	}

#define M5_BT_WRAP_I1(sym, label)										\
	extern int __real_##sym(void *self, int a);							\
	int __wrap_##sym(void *self, int a);								\
	int __wrap_##sym(void *self, int a)									\
	{																	\
		int	r;															\
		m5_bt_enter(label);												\
		r = __real_##sym(self, a);										\
		m5_bt_leave(label, 1, r);										\
		return r;														\
	}

#define M5_BT_WRAP_VP(sym, label)										\
	extern void __real_##sym(void *self, void *p);						\
	void __wrap_##sym(void *self, void *p);								\
	void __wrap_##sym(void *self, void *p)								\
	{																	\
		m5_bt_enter(label);												\
		__real_##sym(self, p);											\
		m5_bt_leave(label, 0, 0);										\
	}

#define M5_BT_WRAP_IP(sym, label)										\
	extern int __real_##sym(void *self, void *p);						\
	int __wrap_##sym(void *self, void *p);								\
	int __wrap_##sym(void *self, void *p)								\
	{																	\
		int	r;															\
		m5_bt_enter(label);												\
		r = __real_##sym(self, p);										\
		m5_bt_leave(label, 1, r);										\
		return r;														\
	}

/*  (1) Display.init() → M5GFX::init_impl(use_reset, use_clear) : bool  */
extern int __real__ZN5m5gfx5M5GFX9init_implEbb(void *self, int rst, int clr);
int __wrap__ZN5m5gfx5M5GFX9init_implEbb(void *self, int rst, int clr);
int
__wrap__ZN5m5gfx5M5GFX9init_implEbb(void *self, int rst, int clr)
{
	int	r;

	m5_bt_enter("P1 M5GFX::init_impl (LCD autodetect/init)");
	r = __real__ZN5m5gfx5M5GFX9init_implEbb(self, rst, clr);
	m5_bt_leave("P1 M5GFX::init_impl", 1, r);
	return r;
}

/*  (2) _check_boardtype(board_t) : board_t  — 返り値が検出されたボード ID  */
M5_BT_WRAP_I1(_ZN2m59M5Unified16_check_boardtypeEN4lgfx6boards7board_tE,
			  "P2 M5Unified::_check_boardtype")

/*  (3) _setup_pinmap(board_t) : void  ★static メンバ＝this を取らない  */
extern void __real__ZN2m59M5Unified13_setup_pinmapEN4lgfx6boards7board_tE(int b);
void __wrap__ZN2m59M5Unified13_setup_pinmapEN4lgfx6boards7board_tE(int b);
void
__wrap__ZN2m59M5Unified13_setup_pinmapEN4lgfx6boards7board_tE(int b)
{

	m5_bt_enter("P3 M5Unified::_setup_pinmap");
	__real__ZN2m59M5Unified13_setup_pinmapEN4lgfx6boards7board_tE(b);
	m5_bt_leave("P3 M5Unified::_setup_pinmap", 0, 0);
}

/*  (4) _setup_i2c(board_t) : void  — 内部 I2C（AXP2101/AW9523/BMI270/PCF8563）  */
M5_BT_WRAP_V1(_ZN2m59M5Unified10_setup_i2cEN4lgfx6boards7board_tE,
			  "P4 M5Unified::_setup_i2c")

/*  (5) _setup_led(board_t) : void  */
M5_BT_WRAP_V1(_ZN2m59M5Unified10_setup_ledEN4lgfx6boards7board_tE,
			  "P5 M5Unified::_setup_led")

/*  (6) _begin(const config_t&) : void  — この中で Power.begin() を呼ぶ  */
M5_BT_WRAP_VP(_ZN2m59M5Unified6_beginERKNS0_8config_tE,
			  "P6 M5Unified::_begin")

/*  (6a) Power_Class::begin() : bool  — AXP2101 型判定はこの中  */
extern int __real__ZN2m511Power_Class5beginEv(void *self);
int __wrap__ZN2m511Power_Class5beginEv(void *self);
int
__wrap__ZN2m511Power_Class5beginEv(void *self)
{
	int	r;

	m5_bt_enter("P6a Power_Class::begin (PMIC detect)");
	r = __real__ZN2m511Power_Class5beginEv(self);
	m5_bt_leave("P6a Power_Class::begin", 1, r);
	return r;
}

/*  (7) _begin_audio(config_t&) : void  ※Speaker/Mic .cpp は未リンク（M5U_WITH_AUDIO=OFF）  */
M5_BT_WRAP_VP(_ZN2m59M5Unified12_begin_audioERNS0_8config_tE,
			  "P7 M5Unified::_begin_audio")

/*  (8) update() : void  — begin() 末尾で 1 回呼ばれる＋アプリからも呼ぶ  */
extern void __real__ZN2m59M5Unified6updateEv(void *self);
void __wrap__ZN2m59M5Unified6updateEv(void *self);
void
__wrap__ZN2m59M5Unified6updateEv(void *self)
{

	m5_bt_enter("P8 M5Unified::update");
	__real__ZN2m59M5Unified6updateEv(self);
	m5_bt_leave("P8 M5Unified::update", 0, 0);
}

/*  (9) _begin_rtc_imu(const config_t&) : bool  */
M5_BT_WRAP_IP(_ZN2m59M5Unified14_begin_rtc_imuERKNS0_8config_tE,
			  "P9 M5Unified::_begin_rtc_imu")

/*  (9a) RTC_Class::begin(I2C_Class*, board_t) : bool  */
extern int __real__ZN2m59RTC_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			void *self, void *i2c, int board);
int __wrap__ZN2m59RTC_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			void *self, void *i2c, int board);
int
__wrap__ZN2m59RTC_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			void *self, void *i2c, int board)
{
	int	r;

	m5_bt_enter("P9a RTC_Class::begin (PCF8563)");
	r = __real__ZN2m59RTC_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			self, i2c, board);
	m5_bt_leave("P9a RTC_Class::begin", 1, r);
	return r;
}

/*  (9b) IMU_Class::begin(I2C_Class*, board_t) : bool  */
extern int __real__ZN2m59IMU_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			void *self, void *i2c, int board);
int __wrap__ZN2m59IMU_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			void *self, void *i2c, int board);
int
__wrap__ZN2m59IMU_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			void *self, void *i2c, int board)
{
	int	r;

	m5_bt_enter("P9b IMU_Class::begin (BMI270/BMM150)");
	r = __real__ZN2m59IMU_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE(
			self, i2c, board);
	m5_bt_leave("P9b IMU_Class::begin", 1, r);
	return r;
}

/*
 *  (T) Touch_FT5x06::init() : bool  — 静電タッチ（CoreS3 = FT6336, FT5x06 系）。
 *  ★Touch_FT5x06::_check_init() は失敗しても黙る（唯一の出力 ESP_LOGV は
 *    コンパイル時に消える）。したがって「init に入ったか / 何を返したか」を
 *    外から見る手段がこれしか無い。
 *  ★**注意（両義性）**: vtable 経由の呼出しは `--wrap` を通らない。よって
 *      出た  → 確かに到達した（強い証拠）
 *      出ない → 「呼ばれていない」と「vtable 経由で呼ばれた」の**区別が付かない**
 *    後者は記録に明記すること。
 */
extern int __real__ZN4lgfx2v112Touch_FT5x064initEv(void *self);
int __wrap__ZN4lgfx2v112Touch_FT5x064initEv(void *self);
int
__wrap__ZN4lgfx2v112Touch_FT5x064initEv(void *self)
{
	int	r;

	m5_bt_enter("PT Touch_FT5x06::init");
	r = __real__ZN4lgfx2v112Touch_FT5x064initEv(self);
	m5_bt_leave("PT Touch_FT5x06::init", 1, r);
	return r;
}
