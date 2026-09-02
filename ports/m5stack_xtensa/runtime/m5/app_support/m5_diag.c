/*
 *  M5 表示スモーク（ESP32-S3 CoreS3 / FMP3）— 診断計装（共有 TU）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ★1-13：m5_display_smoke.c から診断計装だけを切り出した TU。
 *
 *  【なぜ切り出すか】
 *    m5/shim/*.c と m5_display_smoke_heap.c は QEMU ハーネス
 *    （m5/app/m5_display_smoke/qemu/）でも**そのまま**コンパイルされる。
 *    ところが 1-11/1-12 で追加した診断関数（m5_log_now / m5_wdt_dump /
 *    m5_mark_u32 / m5_spin_probe_*）は m5_display_smoke.c にしか無く、
 *    そちらは QEMU ハーネスのソース列挙に**入っていない**。結果として
 *    QEMU ハーネスは 1-12 の時点で既に **undefined reference で
 *    リンクできなくなっていた**（本セッションで実測。m5_wdt_dump /
 *    m5_mark_u32 / m5_log_now / m5_spin_probe_arm 等が未定義）。
 *    ＝1-12 以降 QEMU による回帰検証が動いていなかった（現行バグ）。
 *
 *  【対処】診断計装を本 TU へ集約し、seam アプリ・QEMU ハーネスの双方が
 *    同じ実体をリンクする。これにより QEMU 上で
 *    「周期ハンドラが CCOUNT ビジーウェイト中のタスクを横取りして
 *      [HB] を出す」ことを**実機を使わずに実証**できる（positive control
 *      そのものの検証）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <target_syssvc.h>			/* target_fput_log（低レベル即時出力） */
#ifdef TOPPERS_M5_TICK_DIAG
/*  ★M5 専用：tick(CCOMPARE0) 診断カウンタの宣言。target_timer.h は
 *  <kernel.h> だけの TU からも include できる設計（同ヘッダ
 *  xtensa_get_prcidx() のコメント参照）。 */
#include <target_timer.h>
#endif /* TOPPERS_M5_TICK_DIAG */
#include "m5_display_smoke.h"

/*
 *  ==================================================================
 *  ★1-14：tick 診断カウンタの実体（宣言は target_timer.h）
 *  ==================================================================
 *  カーネル／arch 側にはシンボル定義を持ち込まず、アプリ側 TU に置く。
 *  TOPPERS_M5_TICK_DIAG が未定義のビルド（＝golden 5 構成、および本マクロを
 *  与えない QEMU ハーネス）では配列自体が存在せず、下の [WT] は 0 を出す。
 */
#ifdef TOPPERS_M5_TICK_DIAG
volatile uint32_t	_m5_tdiag[M5_TDIAG_NUM];
#define M5_TD(i)	((unsigned int) _m5_tdiag[(i)])
#else /* TOPPERS_M5_TICK_DIAG */
#define M5_TDIAG_TICK		0U
#define M5_TDIAG_SET		1U
#define M5_TDIAG_SET_MISS	2U
#define M5_TDIAG_RESCUE		3U
#define M5_TDIAG_RAISE		4U
#define M5_TDIAG_RAISE_MISS	5U
#define M5_TDIAG_CLEAR		6U
#define M5_TDIAG_LAST_HRT	7U
#define M5_TDIAG_LAST_CMP	8U
#define M5_TDIAG_LAST_CC	9U
#define M5_TDIAG_LAST_SITE	10U
#define M5_TD(i)	0U
#endif /* TOPPERS_M5_TICK_DIAG */

/*
 *  カーネルの割込み許可マスク（arch/xtensa_gcc/common/core_kernel_impl.{c,h}）。
 *  ハードの INTENABLE が失われたのか、ソフトマスク（_kernel_intenable_mask）が
 *  失われたのか、IPM の vpri マスク（_kernel_vpri_mask）が INT6 を落としたのかを
 *  区別するために直接読む（read-only）。kernel_impl.h を include すると
 *  アプリ TU がカーネル内部に依存するので、extern 宣言だけを置く。
 */
extern volatile uint32_t	_kernel_intenable_mask[];
extern volatile uint32_t	_kernel_vpri_mask[];

/*  ★m5_log / m5_log_u32（syslog 経由の遅延出力）は本 TU には置かない。
 *  QEMU ハーネス（m5_qemu_main.c）が自前の実体を持っており多重定義になるため、
 *  seam 版は m5_display_smoke.c 側に残す。 */

/*
 *  ------------------------------------------------------------------
 *  ★即時出力ログ（m5_log_now / m5_log_now_u32）— 停止位置特定用
 *  ------------------------------------------------------------------
 *  【なぜ要るか（1-10 の構造的欠陥）】
 *    m5_log/m5_log_u32 は **かつて** FMP3 の syslog() ＝ logtask 経由の遅延出力だった
 *    （★S6/2026-07-21 で m5_log_now への委譲＋既定 OFF に変更済み。理由は
 *      m5_display_smoke.c の同関数のコメントと S5-analysis.md §7-2/§7-3）。
 *    その syslog 経路には次の構造的欠陥があった：
 *    main_task が固まると logtask へ回らず、それまでに積まれた分も含めて**以後の
 *    ログが全消失**する。実機ログが `[Autodetect]` で終わっていたのは「そこで
 *    止まった」からではなく、**それ以降のどこで止まっても同じ見え方になる**から
 *    であって、停止位置の情報を構造的に得られていなかった。
 *
 *  【方式】target_fput_log（arch/xtensa_gcc/esp32s3/chip_serial.c）を 1 文字ずつ
 *    直接叩く。これは syslog サービス自身が低レベル出力に使う関数で、
 *    ★A1_CONSOLE_USJ=ON の本構成では出力先が USB-Serial-JTAG になる（＝実機で
 *      実際に観測できる唯一の経路。生 UART0 直書きは USJ 構成では TX ピンへ出て
 *      ホストからは見えないので使ってはならない）。
 *    バッファも logtask も経由しないので、呼んだ時点で線に出る。
 *
 *  【書式は自前】FMP3 の syslog() は format と %s 引数を**ポインタのまま積んで
 *    logtask が後で整形**する（遅延整形）ため一時バッファが dangling する。
 *    また（★訂正・1-16）「vsnprintf は本構成にリンクされていない」という当初の記述は
 *    **誤り**である。m5/compat/esp_shim_m5_ext.c が vsnprintf を使って段階1 から
 *    リンクできている（newlib から解決）。本 TU が手変換の 16 進を使うのは、
 *    vsnprintf が無いからではなく、FMP3 syslog の遅延整形（%s をポインタのまま積む）を
 *    避けて **即時・自己完結**にするためである。
 *    そのまま流し、数値は下で 16 進へ手変換する。追加スタックもほぼ使わない。
 */
/*
 *  ------------------------------------------------------------------
 *  ★1-15：**有界**な即時出力（m5_putc_now）— 計装自身がハングしないために
 *  ------------------------------------------------------------------
 *  【解く問題（現行バグ・計装側）】1-14 run M の実測で、
 *    タスク文脈の `[WT]` と**周期ハンドラ文脈の `[HB]` が同一時点で同時に**
 *    途絶し、以後 130 秒間まったく出力が無いことが分かった（HB は #10 で停止＝
 *    上限 M5_HB_MAX=40 には達していない）。**二つの独立した実行文脈が同時に
 *    沈黙する**のだから、共通項＝**出力経路そのもの**が第一容疑者である。
 *
 *  【なぜ止まり得るか（ソース根拠）】実機で使うのは A1_CONSOLE_USJ=OFF の
 *    `seam-s3-m5-uart` ビルドで、この構成の target_fput_log は
 *    fmp3/arch/xtensa_gcc/esp32s3/chip_serial.c:89-99 の esp32s3_uart0_putc、すなわち
 *      do { cnt = UART_STATUS_REG >> 16 & 0x3FF; } while (cnt >= 100);
 *    という**上限の無いスピン**である（USJ 経路は 5000 回上限で bounded だが、
 *    UART 経路にはそれが無い）。UART0 の TX が掃けなくなれば――クロックゲート、
 *    ペリフェラルリセット、SCLK 源の変更のいずれでも――タスクも周期ハンドラも
 *    ここで永久に止まり、観測は完全に消える。**1-14 の観測はまさにその形をしている。**
 *
 *  【対策】診断 TU の出力だけを**有界**にする。打ち切ったら m5_uart_stall を
 *    増やして先へ進む（文字は落ちるが、システムも観測も止まらない）。
 *    ★fmp3/ 側（chip_serial.c）には手を触れない：golden 5 構成をバイト単位で
 *      不変に保つため、修正は本アプリ TU の内部に閉じる。
 *    ★USJ 構成では target_fput_log が既に bounded なのでそのまま使う。
 */
volatile unsigned int	m5_uart_stall;		/* 出力打ち切り回数（sticky） */

/*  UART0 レジスタ（一次情報：esp-idf/components/soc/esp32s3/register/soc/
 *  reg_base.h:6 DR_REG_UART_BASE=0x60000000、uart_reg.h:672 UART_STATUS_REG(i)=+0x1c）。
 *  ★USJ 構成でも [CON] ダンプが読むので #ifdef の外に置く。 */
#define M5_UART0_FIFO_REG		0x60000000U
#define M5_UART0_STATUS_REG		0x6000001CU
#define M5_UART0_TXFIFO_CNT_S	16U
#define M5_UART0_TXFIFO_CNT_V	0x3FFU
#define M5_UART0_TXFIFO_MAX		100U

#ifdef TOPPERS_S3_CONSOLE_USJ

#define M5_PUTC(c)		target_fput_log(c)

#else /* !TOPPERS_S3_CONSOLE_USJ */

/*  115200bps で 1 文字 ≒ 87us。1 周 20 サイクル・160MHz とすると 700 周で足りる。
 *  200000 周（≒25ms）は十分な余裕であり、かつ「永久」ではない。 */
#define M5_UART0_SPIN_MAX		200000U

static void
m5_putc_now(char c)
{
	uint32_t		spin = 0U;
	uint32_t		cnt;

	do {
		cnt = (sil_rew_mem((const uint32_t *) M5_UART0_STATUS_REG)
			   >> M5_UART0_TXFIFO_CNT_S) & M5_UART0_TXFIFO_CNT_V;
		if (cnt < M5_UART0_TXFIFO_MAX) {
			/*  ★1-15 修正：**ワード(32bit)書込み**でなければならない。
			 *  当初 sil_wrb_mem（バイト書込み）にしていたため UART0 FIFO に
			 *  文字が入らず、即時ログ（[NOW]/[HB]/[WT]/[CON]/[MK]）が実機で
			 *  **全滅**した（1-15 run N：通常の syslog は出るのに即時経路だけ無音）。
			 *  ESP32-S3 のペリフェラルレジスタは 32bit アクセスが前提で、
			 *  実際に出力できている chip_serial.c:98 も sil_wrw_mem を使っている。 */
			sil_wrw_mem((uint32_t *) M5_UART0_FIFO_REG, (uint32_t) (uint8_t) c);
			return;
		}
		spin++;
	} while (spin < M5_UART0_SPIN_MAX);

	/*  掃けない＝UART0 が死んでいる。文字は捨てるが呼出し元は必ず戻す。 */
	m5_uart_stall++;
}

#define M5_PUTC(c)		m5_putc_now(c)

#endif /* TOPPERS_S3_CONSOLE_USJ */

/*
 *  ★S5：**有界**出力を shim（m5_stub_trace.c / m5_begin_trace.c）と
 *  共有するための公開ラッパ。宣言は m5/shim/m5_stub_trace.h（理由も同所）。
 *  USJ 構成では target_fput_log（5000 回で bounded）、UART 構成では上の
 *  m5_putc_now（200000 周で打ち切り）に委譲する。
 */
void
m5_putc_now_ext(char c)
{
#ifdef TOPPERS_S3_CONSOLE_USJ
	/*  USJ 経路の target_fput_log は自身が bounded（5000 回）で、かつ '\n' の前に
	 *  '\r' を付ける。意味を変えないためそのまま委譲する。 */
	target_fput_log(c);
#else
	/*  UART 経路は chip_serial.c の target_fput_log と**同じ意味**（CR 付与）を
	 *  保ったまま、スピンだけを有界にする。 */
	if (c == '\n') {
		m5_putc_now('\r');
	}
	m5_putc_now(c);
#endif
}

unsigned int
m5_uart_stall_count(void)
{
	return((unsigned int) m5_uart_stall);
}

static void
m5_puts_now(const char *s)
{
	while (*s != '\0') {
		M5_PUTC(*s++);
	}
}

void
m5_log_now(const char *msg)
{
	m5_puts_now(msg);
	M5_PUTC('\r');
	M5_PUTC('\n');
}

void
m5_log_now_u32(const char *msg, unsigned int v)
{
	static const char	hexd[] = "0123456789abcdef";
	int					i;

	m5_puts_now(msg);
	M5_PUTC('0');
	M5_PUTC('x');
	for (i = 28; i >= 0; i -= 4) {
		M5_PUTC(hexd[(v >> i) & 0xFU]);
	}
	M5_PUTC('\r');
	M5_PUTC('\n');
}

/*
 *  ------------------------------------------------------------------
 *  ★WDT レジスタ read-back（D）— 「WDT は止まったままか」の実測
 *  ------------------------------------------------------------------
 *  本ポートは start.S:72 の chip_wdt_early_disable（chip_asm.inc:46-78）で起動時に
 *  全 WDT を停止しており、再有効化するコードはリポジトリ内に存在しない。実機ログの
 *  `rst:0x7 (TG0WDT_SYS_RST)` は**前回**のリセット理由なので、それだけでは
 *  「今回も TG0WDT で落ちた」ことの証拠にならない。
 *  → CONFIG0 が 0（＝WDT 無効）であることを直接読んで示す。0 なら
 *    「今回のハングは WDT リセットではない（＝無限ループ/例外で黙って止まっている）」
 *    と確定でき、rst:0x7 の出所を別系統として切り分けられる。read-only＝安全。
 */
#define M5_TIMG0_WDTCONFIG0		0x6001F048U
#define M5_TIMG1_WDTCONFIG0		0x60020048U
#define M5_RTC_CNTL_WDTCONFIG0	0x60008098U
/*  Super-WDT は「無効化」ではなく auto-feed 有効化（bit31）で抑止されている
 *  （chip_asm.inc:66-74）。上の 3 本と抑止のしかたが違うので別枠で読む。 */
#define M5_RTC_CNTL_SWD_CONF	0x600080B4U
#define M5_RTC_CNTL_SWD_AUTO_FEED_EN	0x80000000U

void
m5_wdt_dump(const char *tag)
{
	m5_log_now(tag);
	m5_log_now_u32("[WDT]   TIMG0_WDTCONFIG0(0=停止)=",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_TIMG0_WDTCONFIG0));
	m5_log_now_u32("[WDT]   TIMG1_WDTCONFIG0(0=停止)=",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_TIMG1_WDTCONFIG0));
	m5_log_now_u32("[WDT]   RTC_CNTL_WDTCONFIG0(0=停止)=",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_RTC_CNTL_WDTCONFIG0));
	{
		uint32_t swd = sil_rew_mem((const uint32_t *) M5_RTC_CNTL_SWD_CONF);
		m5_log_now_u32("[WDT]   RTC_CNTL_SWD_CONF(bit31=auto_feed)=", (unsigned int) swd);
		m5_log_now(((swd & M5_RTC_CNTL_SWD_AUTO_FEED_EN) != 0U)
				   ? "[WDT]   → Super-WDT は auto-feed 有効＝リセットしない"
				   : "[WDT]   ★Super-WDT の auto-feed が落ちている＝SWD リセットの可能性あり");
	}
}

/*
 *  ==================================================================
 *  ★1-12：スピン中 SPI レジスタ ダンプ（周期ハンドラ／CRE_CYC M5_SPIN_CYC）
 *  ==================================================================
 *  【解く問題】1-11 実機（1-11-realhw-runI-immediate.log）で
 *    `[NOW] >>>> display.begin() 呼出し直前 <<<<` は出るが `<<<< 復帰` が出ない。
 *    CPU 例外・未登録割込み・ABORT のいずれも出ず、WDT は全停止（CONFIG0=0、
 *    Super-WDT auto-feed 有効）＝リセットでもない。⇒ **純粋な無限スピン**。
 *    しかし main_task が CPU を離さないので、タスク文脈のログでは
 *    「スピン中の HW 状態」を一切観測できない。
 *
 *  【方式】FMP3 の周期ハンドラ（非タスク文脈＝タスクのスピンを割込みで横取り
 *    できる）から SPI2/GPIO/SYSTEM のレジスタを **read-only** でダンプする。
 *
 *  【★周期ハンドラ文脈から即時ログを呼んでよいかの確認（推測ではなくソース）】
 *    m5_log_now 系が使う target_fput_log の実体は
 *    fmp3/arch/xtensa_gcc/esp32s3/chip_serial.c:159-172。中身は
 *      - USJ 経路（本構成 A1_CONSOLE_USJ=ON）: esp32s3_usj_putc
 *        → esp32s3_usbjtag_putready()/putchar()（レジスタ直）＋ sil_dly_nse()
 *          のビジーウェイト、リトライ上限 5000 回で必ず戻る（bounded）
 *      - UART 経路: UART_STATUS_REG ポーリング＋ FIFO 書込み
 *    **カーネルサービスコールを一切呼ばず、ロックも取らず、ブロックもしない。**
 *    したがって非タスク文脈（周期ハンドラ）から呼んで安全である。
 *    ★逆に syslog() は logtask 経由の遅延出力なので、（★S6 以降 m5_log/m5_log_u32 は
 *      syslog を使わない。この注意はカーネル syslog() を直接呼ぶ場合の話である）
 *      スピン中のタスクが CPU を離さない本件では**絶対に使ってはならない**。
 *    ★既知の副作用（許容）：タスク側が target_fput_log の途中で本ハンドラに
 *      横取りされると USJ FIFO 上で文字が混ざり得る。表示が乱れるだけで
 *      機能影響は無い（スピン中はタスク側が出力していないので実害は小さい）。
 *
 *  【暴走防止】armed フラグ（begin() の前後で arm/disarm）＋ダンプ上限
 *    M5_SPIN_DUMP_MAX 回。begin() が復帰すれば止まり、復帰しなくても
 *    上限で止まる（USJ を詰まらせない）。
 */

/*  レジスタ番地（一次情報＝esp-idf/components/soc/esp32s3/register/soc/ 配下の
 *  spi_reg.h / gpio_reg.h / system_reg.h / reg_base.h）。
 *  ここは CONSUMER_OBJS 側の TU で ESP-IDF include パスを持たないため、
 *  ヘッダの値を直書きする（出典を各行に明記）。 */
#define M5_SPI2_BASE			0x60024000U	/* reg_base.h: DR_REG_SPI2_BASE */
#define M5_SPI_CMD_REG			(M5_SPI2_BASE + 0x00U)	/* spi_reg.h:15  */
#define M5_SPI_CLOCK_REG		(M5_SPI2_BASE + 0x0CU)	/* spi_reg.h:160 */
#define M5_SPI_USER_REG			(M5_SPI2_BASE + 0x10U)	/* spi_reg.h:197 */
#define M5_SPI_USER1_REG		(M5_SPI2_BASE + 0x14U)	/* spi_reg.h:347 */
#define M5_SPI_MS_DLEN_REG		(M5_SPI2_BASE + 0x1CU)	/* spi_reg.h:408 */
#define M5_SPI_CLK_GATE_REG		(M5_SPI2_BASE + 0xE8U)	/* spi_reg.h:1713 */
#define M5_SPI_USR_BIT			0x01000000U				/* spi_reg.h:20 SPI_USR=BIT(24) */

#define M5_SYSTEM_PERIP_CLK_EN0_REG	0x600C0018U	/* system_reg.h:105 */
#define M5_SYSTEM_SPI2_CLK_EN		0x00000040U	/* system_reg.h:258 BIT(6) */

#define M5_GPIO_BASE			0x60004000U	/* reg_base.h: DR_REG_GPIO_BASE */
#define M5_GPIO_PIN0_REG		(M5_GPIO_BASE + 0x074U)	/* gpio_reg.h:277  (+4*pin) */
#define M5_GPIO_FUNC0_OUT_SEL_CFG_REG	(M5_GPIO_BASE + 0x554U)	/* gpio_reg.h:7789 (+4*pin) */

#define M5_SPIN_DUMP_MAX		20U		/* ダンプ回数上限（1s 周期＝最大 20 秒） */

/*
 *  ==================================================================
 *  ★1-15：観測経路の健全性（[CON]）と I2C1 ペリフェラル状態（[I2C]）
 *  ==================================================================
 *  【[CON] が解く問題】run M では [WT]（タスク文脈）と [HB]（周期ハンドラ文脈）が
 *    同時に途絶した。原因候補は大きく 2 つあり、いま**区別できていない**：
 *      (a) 観測経路が死んだ（UART0 のクロック停止／リセット、あるいは
 *          m5_route_u0txd_portc() が張った GPIO17/18 への U0TXD 複製が剥がされた）
 *          ＝ CPU は健全なのに見えないだけ
 *      (b) CPU が割込み禁止のまま無限ループに落ちた ＝ 本当に止まっている
 *    [CON] は (a) の全構成要素を毎秒読み出し、**さらに毎秒張り直す**。
 *    ★もし「一度死んだ出力が [CON] の張り直し後に復活する」なら (a) が確定し、
 *      同時に**どのレジスタが壊されたか**が repair-hit カウンタで名指しできる。
 *      復活しないなら (a) は否定され (b) が残る。どちらに転んでも情報が出る。
 *
 *  【★本ハンドラの「read-only」規約からの意図的な逸脱】1-12 以来この周期
 *    ハンドラは read-only を規約としてきたが、[CON] の張り直しはレジスタへ
 *    **書き込む**。逸脱を承知で入れる理由は上記のとおりで、書込み対象は
 *      IO_MUX(GPIO17/18)・GPIO_FUNC_OUT_SEL(17/18)・GPIO_ENABLE bit17/18
 *    の 3 種のみ＝**診断用の複製出力チャネル専用のピン**であり、M5GFX/lgfx が
 *    機能のために使うピン（11,12=I2C／35,36,37=SPI／3,4）とは重ならない。
 *    したがって「診断が対象を壊す」危険は無い。
 *
 *  【[I2C] が解く問題】lgfx の I2C は ESP-IDF ドライバを通らず
 *    common.cpp がレジスタを直接叩く（i2c_new_master_bus はポート予約のみで、
 *    i2c_master_transmit/receive は本構成では未参照＝m5/shim/m5_idf_stubs.c:50-52）。
 *    よってバスが Low 固着したのか、ペリフェラルが busy のままなのか、
 *    ピン配線（out_sel）が剥がれたのかは**レジスタを直接見るしかない**。
 *
 *  レジスタ番地は一次情報（esp-idf/components/soc/esp32s3/register/soc/）：
 *    reg_base.h:42  DR_REG_I2C1_EXT_BASE = 0x60027000（M5GFX CoreS3 は I2C_NUM_1）
 *    i2c_reg.h:31   I2C_CTR_REG     = +0x04
 *    i2c_reg.h:151  I2C_SR_REG      = +0x08
 *    i2c_reg.h:271  I2C_FIFO_ST_REG = +0x14
 *    i2c_reg.h:383  I2C_INT_RAW_REG = +0x20
 *    gpio_reg.h:109 GPIO_ENABLE_REG = DR_REG_GPIO_BASE + 0x20
 *    gpio_reg.h:165 GPIO_IN_REG     = DR_REG_GPIO_BASE + 0x3C
 *    system_reg.h:105 SYSTEM_PERIP_CLK_EN0_REG = 0x600C0018
 *    system_reg.h:367 SYSTEM_PERIP_RST_EN0_REG = 0x600C0020
 *    system_reg.h:282 SYSTEM_UART_CLK_EN       = BIT(2)
 *    system_reg.h:186 SYSTEM_I2C_EXT1_CLK_EN   = BIT(18)
 *    uart_reg.h:672 UART_STATUS_REG(0) = 0x6000001C
 *    uart_reg.h:634 UART_CLKDIV_REG(0) = 0x60000014
 *    uart_reg.h:1407 UART_CLK_CONF_REG(0) = 0x60000078
 *  M5GFX CoreS3 の I2C ピンは SDA=GPIO12 / SCL=GPIO11（M5GFX.cpp:417-418）。
 */
#define M5_I2C1_BASE			0x60027000U
#define M5_I2C_CTR_REG			(M5_I2C1_BASE + 0x04U)
#define M5_I2C_SR_REG			(M5_I2C1_BASE + 0x08U)
#define M5_I2C_FIFO_ST_REG		(M5_I2C1_BASE + 0x14U)
#define M5_I2C_INT_RAW_REG		(M5_I2C1_BASE + 0x20U)
#define M5_I2C_SDA_PIN			12U
#define M5_I2C_SCL_PIN			11U

#define M5_GPIO_ENABLE_REG		(M5_GPIO_BASE + 0x20U)
#define M5_GPIO_IN_REG			(M5_GPIO_BASE + 0x3CU)
#define M5_GPIO_ENABLE_W1TS_REG	(M5_GPIO_BASE + 0x24U)

#define M5_SYSTEM_PERIP_RST_EN0_REG	0x600C0020U
#define M5_SYSTEM_UART_CLK_EN		0x00000004U		/* BIT(2)  */
#define M5_SYSTEM_I2C_EXT1_CLK_EN	0x00040000U		/* BIT(18) */

#define M5_UART0_CLKDIV_REG		0x60000014U
#define M5_UART0_CLK_CONF_REG	0x60000078U

/*  観測チャネル（U0TXD 複製）のピンと期待値。m5_display_smoke.c の
 *  m5_route_u0txd_portc() と同一（そちらが正・ここは検証と張り直し）。 */
#define M5_CON_PIN_A			17U
#define M5_CON_PIN_B			18U
#define M5_IOMUX_GPIO17_REG		0x60009048U		/* io_mux_reg.h:288 (DAC_1_U) */
#define M5_IOMUX_GPIO18_REG		0x6000904CU		/* io_mux_reg.h:293 (DAC_2_U) */
#define M5_U0TXD_OUT_IDX_V		12U				/* gpio_sig_map.h:31          */
#define M5_MCU_SEL_SHIFT		12U				/* io_mux_reg.h:65            */
#define M5_PIN_FUNC_GPIO_V		1U				/* io_mux_reg.h:140           */

/*  m5/shim/m5_gpio_stub.c が公開する「観測ピンへの書込みを阻止した回数」。 */
extern volatile unsigned int	m5_gpio_console_guard;

static unsigned int	m5_con_repairs;		/* [CON] が復旧を実施した回数（OFF でも数える） */
#ifdef M5_DIAG_VERBOSE
static uint32_t		m5_con_prev[8];
static uint32_t		m5_i2c_prev[13];	/* ★1-16: SR デコード5本を追加して 8→13 */
static bool_t		m5_diff_have_prev;

static volatile int				m5_spin_armed;
static volatile unsigned int	m5_spin_dumps;
static uint32_t					m5_spin_prev_cmd;
static bool_t					m5_spin_have_prev;
#endif /* M5_DIAG_VERBOSE */

/*  到達点マーカ（m5_mark/m5_mark_u32 が更新。周期ハンドラが再出力する）。 */
static volatile unsigned int	m5_ckpt_id;
static volatile unsigned int	m5_ckpt_seq;

/*
 *  ==================================================================
 *  ★1-13：ハートビート＋PS/INTENABLE 計装（1-12 の推論の穴を塞ぐ）
 *  ==================================================================
 *  【1-12 の欠陥】周期ハンドラは begin() 直前で arm されるまで即 return する
 *    設計だった。したがって `[SPIN]` が 1 行も出なかった事実からは
 *      (A) begin() 中に割込みが止まっている
 *      (B) 周期ハンドラがそもそも一度も発火していない（cfg 誤り・クラス違い・
 *          tick 停止など。＝計装の不良であって割込みの話ではない）
 *    を**区別できない**。positive control が無い。
 *
 *  【対策1（positive control）】周期ハンドラは armed かどうかに関わらず
 *    毎回 `[HB]`（ハートビート）を出す。上限 M5_HB_MAX 行。
 *    さらに m5_display_smoke_gfx.cpp が begin() の**前**に CCOUNT ビジーウェイトで
 *    数秒足踏みするので、`>>>> display.begin() 呼出し直前 <<<<` より**前**に
 *    `[HB]` が数行出ることが「周期ハンドラは発火する」の実証になる。
 *    ★ビジーウェイトであってスリープではない点が重要：dly_tsk で待つと
 *      「戻ってきた＝tick は生きている」しか言えず、**タスクのスピンを割込みで
 *      横取りできる**ことの実証にならない。本件が問うているのはまさに後者である。
 *
 *  【対策2（直接測定）】PS.INTLEVEL と INTENABLE を出す。
 *    - m5_log_now_ps()：呼んだその場（タスク文脈）の値を即時出力する。
 *    - m5_ps_snapshot()：シムが**タスク文脈**で退避するだけ（出力しない）。
 *      タスクがスピンに落ちても、周期ハンドラが「最後にタスク文脈で見えていた
 *      PS/INTENABLE」を再出力できる。
 *    ★周期ハンドラ自身が見る PS/INTENABLE も出すが、**そちらは INTENABLE=0・
 *      PS.INTLEVEL≧1 が正常**である（core_support.S の Level-1 エントリが再入
 *      防止のため INTENABLE=0 にしてからハンドラを呼ぶ設計。イグジットで
 *      _kernel_intenable_mask から復元する）。ISR 側の 0 をバグと読み違えないこと。
 *      判断に使うのは ps(task)/ie(task) の方である。
 *
 *  【対策3（固着 vs ライブロックの弁別）】シムの呼出し回数 m5_ctr[] を毎秒出す。
 *    lgfx の I2C 完了待ちは `do { taskYIELD(); } while (…micros()…)` という
 *    ポーリングループなので、そこで回っていれば MICROS/YIELD が毎秒万単位で
 *    増える。増えなければ「どのシムも呼ばない単一箇所での固着」である。
 */
/*  ★1-15：40→180。run M は HB #10 で途絶したが上限は 40 だったので
 *  「上限に達したから止まった」の可能性は既に排除されている。それでも
 *  3 分の採取を丸ごと覆えるよう広げておく（差分出力にしたので行数は増えない）。 */
#define M5_HB_MAX			180U	/* ハートビート出力の上限（秒） */

static volatile unsigned int	m5_hb_count;	/* 周期ハンドラの発火回数（無条件） */
static volatile unsigned int	m5_hb_prints;	/* [HB] 出力済み回数 */

/*
 *  ★[HB] の UART 出力回数を外から読む。
 *  P-1-2 のストリーミングは EOF 間隔 2.5ms を守る必要があり、115200 baud の
 *  1 行（約 2ms）が余裕をほぼ食い潰す。区間中に何回出たかを数えて、
 *  取りこぼし（n_badframe）との相関を見るために公開する。
 */
unsigned int
m5_hb_prints_get(void)
{
	return((unsigned int) m5_hb_prints);
}
static volatile unsigned int	m5_ps_task;		/* 最後にタスク文脈で見た PS */
static volatile unsigned int	m5_ie_task;		/* 同 INTENABLE */
static volatile unsigned int	m5_ps_site;		/* その退避元 site（M5_PSSITE_*） */

volatile unsigned int	m5_ctr[M5_CTR_NUM];		/* シム呼出し回数（シムが++する） */

Inline uint32_t
m5_rd_ps(void)
{
	uint32_t	v;
	__asm__ __volatile__ ("rsr %0, ps" : "=a"(v));
	return(v);
}

Inline uint32_t
m5_rd_intenable(void)
{
	uint32_t	v;
	__asm__ __volatile__ ("rsr %0, intenable" : "=a"(v));
	return(v);
}

/*
 *  タスク文脈から呼ぶ：その場の PS/INTENABLE を即時出力する（退避もする）。
 */
void
m5_log_now_ps(const char *tag)
{
	uint32_t	ps = m5_rd_ps();
	uint32_t	ie = m5_rd_intenable();

	m5_ps_task = (unsigned int) ps;
	m5_ie_task = (unsigned int) ie;
	m5_ps_site = M5_PSSITE_TASKPRE;
#ifndef M5_DIAG_VERBOSE
	/*  ★S6 観測系修復 #5：[PS] は段階1 の調査用（S5-analysis §9-1 で「既定 OFF」に
	 *  仕分けられた）。**退避（m5_ps_task/m5_ie_task）は続ける** — [HB] が
	 *  「最後にタスク文脈で見えた PS」を出すのに使うからである。出力だけを止める。 */
	(void) tag;
	return;
#else
	m5_log_now(tag);
	m5_log_now_u32("[PS]   PS(raw)=", (unsigned int) ps);
	/*  PS.INTLEVEL は bit[3:0]。0 なら全レベル許可＝CPU ロック中でない。 */
	m5_log_now_u32("[PS]   PS.INTLEVEL(0=許可)=", (unsigned int) (ps & 0xFU));
	/*  PS.EXCM(bit4)/PS.WOE(bit18) も出す（WOE=0 は窓例外が死ぬ既知の地雷）。 */
	m5_log_now_u32("[PS]   PS.EXCM=", (unsigned int) ((ps >> 4) & 0x1U));
	m5_log_now_u32("[PS]   PS.WOE(1=正常)=", (unsigned int) ((ps >> 18) & 0x1U));
	/*  INTENABLE=0 ならタスク文脈でも割込みは一切入らない（＝二度と入らない）。 */
	m5_log_now_u32("[PS]   INTENABLE(0=全禁止)=", (unsigned int) ie);
#endif /* M5_DIAG_VERBOSE */
}

/*
 *  ==================================================================
 *  ★1-14：タスク文脈からの差分ウォッチ（[WT]）
 *  ==================================================================
 *  【解く問題】1-13 で「[HB] が #7 で止まったのにタスクは動き続ける」＝
 *    **カーネル tick が途中で止まった**ことは確定したが、[HB] が出ない以上
 *    「止まった瞬間の状態」を周期ハンドラからは二度と観測できない。
 *    観測を**タスク文脈**へ移すしかない。
 *
 *  【何を測れば決着するか】tick が来なくなる機序は次の 2 系統しかない。
 *    (a) 割込みが届かない … ハード INTENABLE / _kernel_intenable_mask /
 *        _kernel_vpri_mask のいずれかから INT6(0x40) が落ちる、または
 *        PS.INTLEVEL が 0 に戻らない。
 *    (b) 割込み源が二度と鳴らない … CCOMPARE0 が「既に過ぎた値」に置かれる。
 *        Xtensa の CCOMPARE は**厳密一致**なので、過去に置いた瞬間、次の一致は
 *        CCOUNT が一周する 2^32 サイクル後（160MHz で 26.8 秒）になる。
 *        target_hrt_raise_event() は CCOUNT+1 を書くので**構造的に必ず取りこぼす**
 *        （rsr→wsr の間に既に数サイクル進んでいる）。
 *    → 本関数は (a) の 4 値と (b) の残りサイクル (int32)(CCOMPARE0-CCOUNT) を
 *      同時に採る。どちらが起きたかは 1 行見れば分かる。
 *
 *  【洪水対策】値が前回サンプルから変化したときだけ 1 行出す。tick カウンタも
 *    署名に入れてあるので、正常時は「tick が進むたび＝おおむね毎秒 1 行」に
 *    落ち着く（＝タスク文脈から見たハートビートにもなる）。加えて
 *    M5_WATCH_MAX 行のハード上限を置く。
 *
 *  【安全性】読むのは特殊レジスタと大域変数だけ。カーネルサービスコールを
 *    呼ばず、書込みもしない。出力は m5_log_now 系＝target_fput_log 直（bounded）。
 */
#define M5_WATCH_MAX		400U	/* [WT] 出力のハード上限 */

#ifdef M5_DIAG_VERBOSE
static volatile unsigned int	m5_watch_prints;
static unsigned int				m5_w_ie   = 0xFFFFFFFFU;	/* 初回は必ず不一致 */
static unsigned int				m5_w_lv   = 0xFFFFFFFFU;
static unsigned int				m5_w_soft = 0xFFFFFFFFU;
static unsigned int				m5_w_vpri = 0xFFFFFFFFU;
static unsigned int				m5_w_dead = 0xFFFFFFFFU;
static unsigned int				m5_w_tick = 0xFFFFFFFFU;
static unsigned int				m5_w_skip;					/* 前回出力以降の無変化回数 */
#endif /* M5_DIAG_VERBOSE */

/*
 *  ★1-14：main_task スタックの高水位（実使用量）。cfg の STACK_SIZE は 8192 で、
 *  M5GFX は C++ の深い呼出しをする。オーバフローすれば隣接するカーネルデータ
 *  （タイムイベントヒープ等）を壊せるので tick 停止の候補でもある。
 *  m5_ps_snapshot（＝シムの全ホットパス）で SP の最小/最大を記録するだけ。
 *  ★注意：これはサンプル点での最小 SP であり、真の最深点ではない（サンプル点の
 *  間でさらに深く潜っている可能性は排除できない）。「8192 に対して余裕が
 *  どの程度あるか」の下限見積りとして読むこと。
 */
static uint32_t	m5_sp_min = 0xFFFFFFFFU;
static uint32_t	m5_sp_max;

Inline uint32_t
m5_rd_sp(void)
{
	uint32_t	v;
	__asm__ __volatile__ ("mov %0, a1" : "=a"(v));
	return(v);
}

Inline uint32_t
m5_rd_ccount(void)
{
	uint32_t	v;
	__asm__ __volatile__ ("rsr %0, ccount" : "=a"(v));
	return(v);
}

Inline uint32_t
m5_rd_ccompare0(void)
{
	uint32_t	v;
	__asm__ __volatile__ ("rsr %0, ccompare0" : "=a"(v));
	return(v);
}

/*  1 行に複数フィールドを詰める（m5_log_now_u32 は 1 行 1 値なので別実装）。
 *  ★S6：[WT] 専用なので M5_DIAG_VERBOSE と同じ寿命にする（未使用警告を出さない）。 */
#ifdef M5_DIAG_VERBOSE
static void
m5_put_hex(unsigned int v)
{
	static const char	hexd[] = "0123456789abcdef";
	int					i;

	M5_PUTC('0');
	M5_PUTC('x');
	for (i = 28; i >= 0; i -= 4) {
		M5_PUTC(hexd[(v >> i) & 0xFU]);
	}
}

static void
m5_put_field(const char *name, unsigned int v)
{
	m5_puts_now(name);
	m5_put_hex(v);
	M5_PUTC(' ');
}
#endif /* M5_DIAG_VERBOSE */

void
m5_watch_sample(void)
{
	uint32_t		sp   = m5_rd_sp();

	if (sp < m5_sp_min) { m5_sp_min = sp; }
	if (sp > m5_sp_max) { m5_sp_max = sp; }

#ifndef M5_DIAG_VERBOSE
	/*
	 *  ★S6観測系修復 #5 — [WT] は既定 OFF。
	 *  S5 実機ログ（231,968 B）の実測内訳で **[WT] だけで 394 行 / 103,122 B ＝ 44.5%**
	 *  を占め、[HB] と合わせて **79.8%（185KB）**だった（S5-analysis §9-1）。
	 *  さらに [WT] はシムのホットパス（malloc/yield/delay/semtake）から呼ばれるため
	 *  出力頻度が高く、**行内混線（§7-3）の主犯**でもある。
	 *  ★ソースは消さない：段階1 の tick 停止調査（1-14）へ戻るときは
	 *    -DM5_DIAG_VERBOSE 一つで完全に復活する。
	 *  ★sp high-water の記録（上の 2 行）は OFF でも続ける — [HB]/[AC] が使う。
	 */
	return;
#else
	{
	uint32_t		ps   = m5_rd_ps();
	uint32_t		ie   = m5_rd_intenable();
	uint32_t		cc   = m5_rd_ccount();
	uint32_t		cmp  = m5_rd_ccompare0();
	unsigned int	soft = (unsigned int) _kernel_intenable_mask[0];
	unsigned int	vpri = (unsigned int) _kernel_vpri_mask[0];
	unsigned int	tick = M5_TD(M5_TDIAG_TICK);
	int32_t			rem  = (int32_t)(cmp - cc);
	/*  dead=1 … CCOMPARE0 が既に過去＝次の一致は CCOUNT 一周後（26.8s@160MHz）。 */
	unsigned int	dead = (rem <= 0) ? 1U : 0U;

	/*  ★無変化でも M5_WATCH_FORCE 回に 1 度は必ず出す。
	 *  「変化時のみ出力」だけだと**沈黙が両義的**になる（＝状態が安定して
	 *  いるのか、シムが呼ばれなくなったのか、出力経路が死んだのかを区別
	 *  できない）。強制出力があれば、沈黙は「シムが呼ばれていない」のみを
	 *  意味する。1-13 で「[HB] が出ない」ことから複数の可能性を潰せなかった
	 *  のと同じ誤りを繰り返さないための positive control。 */
#define M5_WATCH_FORCE		2000U
	if (ie == m5_w_ie && (ps & 0xFU) == m5_w_lv && soft == m5_w_soft
			&& vpri == m5_w_vpri && dead == m5_w_dead && tick == m5_w_tick
			&& m5_w_skip < M5_WATCH_FORCE) {
		m5_w_skip++;
		return;
	}

	if (m5_watch_prints < M5_WATCH_MAX) {
		m5_watch_prints++;
		m5_puts_now("[WT] ");
		m5_put_field("ie=",   (unsigned int) ie);		/* ハード INTENABLE     */
		m5_put_field("sw=",   soft);					/* _kernel_intenable_mask */
		m5_put_field("vp=",   vpri);					/* _kernel_vpri_mask    */
		m5_put_field("psl=",  (unsigned int)(ps & 0xFU));
		m5_put_field("tk=",   tick);					/* tick 割込み回数      */
		m5_put_field("rem=",  (unsigned int) rem);		/* CCOMPARE0-CCOUNT     */
		m5_put_field("dead=", dead);
		m5_put_field("cc=",   (unsigned int) cc);
		m5_put_field("cmp=",  (unsigned int) cmp);
		m5_put_field("st=",   M5_TD(M5_TDIAG_SET));
		m5_put_field("sm=",   M5_TD(M5_TDIAG_SET_MISS));
		m5_put_field("rs=",   M5_TD(M5_TDIAG_RESCUE));
		m5_put_field("rz=",   M5_TD(M5_TDIAG_RAISE));
		m5_put_field("zm=",   M5_TD(M5_TDIAG_RAISE_MISS));
		m5_put_field("cl=",   M5_TD(M5_TDIAG_CLEAR));
		m5_put_field("hr=",   M5_TD(M5_TDIAG_LAST_HRT));
		m5_put_field("si=",   M5_TD(M5_TDIAG_LAST_SITE));
		m5_put_field("skip=", m5_w_skip);
		M5_PUTC('\r');
		M5_PUTC('\n');
	}
	m5_w_ie   = (unsigned int) ie;
	m5_w_lv   = (unsigned int)(ps & 0xFU);
	m5_w_soft = soft;
	m5_w_vpri = vpri;
	m5_w_dead = dead;
	m5_w_tick = tick;
	m5_w_skip = 0U;
	}
#endif /* M5_DIAG_VERBOSE */
}

/*
 *  ==================================================================
 *  ★1-17：UART に依存しない生存記録（out-of-band 観測）
 *  ==================================================================
 *  【解く問題】UART0（Grove Port C 複製出力）という**単一チャネル**しか
 *    観測路が無いと、タスク文脈と ISR 文脈が同時に途絶したとき
 *      (A) CPU が本当に止まった（両文脈とも進んでいない）
 *      (B) CPU は動いているが**出力チャネルだけ**が壊れた
 *    を**原理的に区別できない**。両者は UART 上では同じ「無音」に見える。
 *
 *  【方式】UART を通さない記録媒体＝**リセットを跨いで残る SRAM**
 *    （`.diag_noinit`、NOLOAD かつ start.S の BSS クリア範囲外）へ、
 *      ・タスク文脈の生存カウンタ（m5_ps_snapshot＝全シムホットパスの合流点）
 *      ・ISR 文脈の生存カウンタ（周期ハンドラ）
 *      ・最終 ckpt id / seq / ccount / 最後に触れた GPIO ピン
 *      ・UART0 と観測ピン GPIO17/18 のレジスタ像（毎秒）
 *    を書き続け、**次回起動時に前回値を読み出す**。これで (A)/(B) が事後に
 *    決着する（判定表は下の m5_surv_dump_prev のコメント参照）。
 *
 *  【★重要な制約：SRAM は電源断で消える】したがって観測手順は必ず
 *    「走らせる → **電源を落とさずにリセット** → 次の起動で読む」でなければ
 *    ならない。ESP-IDF の documented な保証も同じで、`__NOINIT_ATTR` は
 *    "will not be initialized at startup and should keep its value after
 *     **software restart**"（esp-idf/docs/en/api-guides/memory-types.rst:43）
 *    であって power cycle は含まない。CHIP_PU(EN) を落とす外部リセットは
 *    デジタル LDO ごと切るため SRAM は保持されない。よって推奨は
 *    **チップ自身による software reset**（M5_SURV_SELFRESET_SEC、下記）。
 *
 *  【★領域が上書きされないことの根拠（実アドレスで確認済み）】
 *    ・本ビルドの `.diag_noinit` は 0x3FCAD1F8（fmp_xip.elf の readelf 実測）。
 *    ・start.S の BSS クリアは [__bss_start, __bss_end) で、__bss_end は
 *      .kernel_bss 終端＝0x3FCAD1F8（esp32s3_xip_m5.ld）。**.diag_noinit は
 *      その外側（後方）**なので 0 クリアされない。
 *    ・ESP-IDF 2nd-stage bootloader が使う最も低い DRAM 番地は
 *      iram_seg = 0x403C8700（bootloader.ld の実定数から算出）＝
 *      DRAM 別名 0x3FCD8700。ROM のスタックは 0x3FCEB710（soc.h:220）。
 *      いずれも 0x3FCAD1F8 より **176KB 以上上**にあり、本領域を踏まない。
 */

/*  実 seam(XIP) ビルドだけが esp32s3_xip_m5.ld＝`.diag_noinit` 出力セクションを
 *  持つ。QEMU ハーネス（esp32s3_devkitc.ld、SRAM plain）は持たないので、
 *  orphan section を作らないよう通常 .bss へフォールバックする
 *  （diag_recorder.c の DIAG_NOINIT_ATTR と同じ作法）。フォールバック時は
 *  cross-reset 永続性だけが無く、コードパスは完全に同一である。 */
#if defined(TOPPERS_M5_SURV_NOINIT)
#define M5_SURV_ATTR	__attribute__((section(".diag_noinit")))
#else
#define M5_SURV_ATTR
#endif

#define M5_SURV_MAGIC		0x4D355356U		/* "M5SV" */
#define M5_SURV_VERSION		1U
#define M5_SURV_NREG		10U

/*  RTC_CNTL（software system reset 用）。
 *  esp-idf/components/soc/esp32s3/register/soc/reg_base.h:14 DR_REG_RTCCNTL_BASE、
 *  rtc_cntl_reg.h:22 OPTIONS0_REG(+0x0)、:25 RTC_CNTL_SW_SYS_RST=BIT(31)。 */
#define M5_RTC_CNTL_OPTIONS0_REG	0x60008000U
#define M5_RTC_CNTL_SW_SYS_RST		0x80000000U

typedef struct {
	/*  --- ヘッダ（checksum 非対象） --- */
	uint32_t	magic;
	uint32_t	version;
	uint32_t	boot_count;
	/*  --- 高頻度更新（checksum 非対象）---
	 *  ホットパス（GPIO ビットバングは 2us 刻み）から呼ばれるので、
	 *  ここで毎回 checksum を回すことはできない。diag_recorder.c が
	 *  heartbeat を checksum 対象外にしているのと同じ理由・同じ扱い。 */
	uint32_t	task_alive;		/* m5_ps_snapshot 通過回数（タスク文脈） */
	uint32_t	isr_alive;		/* 周期ハンドラ発火回数（ISR 文脈）     */
	uint32_t	ckpt_id;		/* 最終 ckpt id                         */
	uint32_t	ckpt_seq;		/* 最終 ckpt seq                        */
	uint32_t	last_ccount;	/* 最終 ccount（タスク／ISR いずれか）  */
	uint32_t	last_gpio;		/* 最後に触れた GPIO ピン(0x100|pin)    */
	/*  --- ここから 1Hz 更新＝checksum 対象（seal_seq 〜 reg[] まで） --- */
	uint32_t	seal_seq;		/* 封印通番（毎秒 +1）                  */
	uint32_t	seal_ccount;
	uint32_t	hb_count;
	uint32_t	uart_stall;
	uint32_t	gpio_guard;
	uint32_t	ps_task;
	uint32_t	ie_task;
	uint32_t	ps_site;
	uint32_t	reg[M5_SURV_NREG];
	/*  --- checksum（末尾固定・自身は非対象） --- */
	uint32_t	checksum;
} m5_surv_t;

static m5_surv_t		m5_surv M5_SURV_ATTR;

/*  記録領域そのものとは独立に、必ず通常 .bss へ置く（起動毎に 0 で始まる）。
 *  m5_surv_boot_report() が前回値を読み出す**前**にホットパスが上書きするのを
 *  防ぐガード（diag_recorder.c の diag_ready と同じ役割）。 */
static volatile bool_t	m5_surv_ready;

#define M5_SURV_SEAL_WORDS	\
	((unsigned int)((offsetof(m5_surv_t, checksum) - offsetof(m5_surv_t, seal_seq)) \
					/ sizeof(uint32_t)))

/*  封印域の簡易チェックサム。位置依存に混ぜる（単純 XOR だと全ゼロ領域や
 *  ワードの入替えを通してしまうため）。 */
static uint32_t
m5_surv_cksum(const m5_surv_t *p)
{
	const uint32_t	*w = &p->seal_seq;
	uint32_t		acc = M5_SURV_MAGIC ^ (M5_SURV_VERSION * 0x01000193U);
	unsigned int	i;

	for (i = 0U; i < M5_SURV_SEAL_WORDS; i++) {
		acc ^= (w[i] + (uint32_t) i * 0x9E3779B9U);
		acc = (acc << 1) | (acc >> 31);
	}
	return(acc);
}

/*  有効性判定：0=有効／1=magic か version が不一致／2=checksum 不一致。 */
static unsigned int
m5_surv_check(void)
{
	if ((m5_surv.magic != M5_SURV_MAGIC) || (m5_surv.version != M5_SURV_VERSION)) {
		return(1U);
	}
	if (m5_surv.checksum != m5_surv_cksum(&m5_surv)) {
		return(2U);
	}
	return(0U);
}

/*  1Hz 封印（周期ハンドラから呼ぶ）。レジスタ像も一緒に採る。 */
static void
m5_surv_seal(void)
{
	if (!m5_surv_ready) {
		return;
	}
	m5_surv.seal_seq++;
	m5_surv.seal_ccount = m5_rd_ccount();
	m5_surv.hb_count    = m5_hb_count;
	m5_surv.uart_stall  = m5_uart_stall;
	m5_surv.gpio_guard  = m5_gpio_console_guard;
	m5_surv.ps_task     = m5_ps_task;
	m5_surv.ie_task     = m5_ie_task;
	m5_surv.ps_site     = m5_ps_site;
	m5_surv.reg[0] = sil_rew_mem((const uint32_t *) M5_UART0_STATUS_REG);
	m5_surv.reg[1] = sil_rew_mem((const uint32_t *) M5_UART0_CLKDIV_REG);
	m5_surv.reg[2] = sil_rew_mem((const uint32_t *) M5_UART0_CLK_CONF_REG);
	m5_surv.reg[3] = sil_rew_mem((const uint32_t *) M5_SYSTEM_PERIP_CLK_EN0_REG);
	m5_surv.reg[4] = sil_rew_mem((const uint32_t *) M5_SYSTEM_PERIP_RST_EN0_REG);
	m5_surv.reg[5] = sil_rew_mem((const uint32_t *)
								 (M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * M5_CON_PIN_A));
	m5_surv.reg[6] = sil_rew_mem((const uint32_t *)
								 (M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * M5_CON_PIN_B));
	m5_surv.reg[7] = sil_rew_mem((const uint32_t *) M5_IOMUX_GPIO17_REG);
	m5_surv.reg[8] = sil_rew_mem((const uint32_t *) M5_IOMUX_GPIO18_REG);
	m5_surv.reg[9] = sil_rew_mem((const uint32_t *) M5_GPIO_ENABLE_REG);
	m5_surv.checksum = m5_surv_cksum(&m5_surv);
}

/*  ホットパス用（数命令）。 */
Inline void
m5_surv_task_alive(void)
{
	if (m5_surv_ready) {
		m5_surv.task_alive++;
		m5_surv.last_ccount = m5_rd_ccount();
	}
}

Inline void
m5_surv_ckpt(unsigned int id, unsigned int seq)
{
	if (m5_surv_ready) {
		m5_surv.ckpt_id  = id;
		m5_surv.ckpt_seq = seq;
		/*  GPIO スタブの ckpt id は 0x1000〜0x15FF で下位 8bit が pin
		 *  （m5/shim/m5_gpio_stub.c:92-97 の M5_CKPT_SETDIR..IOMUX）。 */
		if ((id & 0xF000U) == 0x1000U) {
			m5_surv.last_gpio = 0x100U | (id & 0xFFU);
		}
	}
}

/*
 *  前回起動の記録を出力する。
 *
 *  【判定表】
 *    INVALID(1)                     … 電源断を挟んだ／初回。手順が成立していない。
 *    VALID かつ task_alive/isr_alive が
 *      前回 [HB] 最終行の想定値より**大きく**進んでいる
 *                                   … (B) CPU は生きていた＝出力チャネルが死んだ。
 *      前回 [HB] 最終行の時点で止まっている
 *                                   … (A) 実行そのものが止まった。
 *      task_alive だけ止まり isr_alive は進む
 *                                   … タスク文脈のみ停止（無限ブロック等）。
 *      isr_alive だけ止まり task_alive は進む
 *                                   … 割込みが入らなくなった（tick 停止）。
 *    reg[] の UART0_STATUS/CLKDIV/CLK_CONF/clk_en が最後の秒で変化していれば
 *    「チャネルが壊された」の直接証拠になる。
 */
static const char *const	m5_surv_regname[M5_SURV_NREG] = {
	"[SURV] UART0_STATUS  =",	/* TXFIFO_CNT=b25:16。増えたまま減らなければ TX 停止 */
	"[SURV] UART0_CLKDIV  =",
	"[SURV] UART0_CLK_CONF=",
	"[SURV] PERIP_CLK_EN0 =",	/* bit2=UART0 クロック有効                          */
	"[SURV] PERIP_RST_EN0 =",	/* bit2=UART0 保持リセット                          */
	"[SURV] OUTSEL17      =",	/* 期待 0x0c（U0TXD）                               */
	"[SURV] OUTSEL18      =",
	"[SURV] IOMUX17       =",	/* MCU_SEL=b14:12、期待 1                           */
	"[SURV] IOMUX18       =",
	"[SURV] GPIO_ENABLE   ="	/* bit17/18=1（OE）                                 */
};

static void
m5_surv_dump_prev(void)
{
	unsigned int	st = m5_surv_check();
	unsigned int	i;

	m5_log_now("==== [SURV] 前回起動の生存記録（.diag_noinit・UART 非依存）====");
	m5_log_now_u32("[SURV] status(0=VALID,1=NO_MAGIC,2=CKSUM_NG)=", st);
	if (st == 1U) {
		m5_log_now_u32("[SURV] magic(実測)=", m5_surv.magic);
		m5_log_now("[SURV] → 前回記録なし（電源断を挟んだ／初回起動）。手順を確認せよ。");
		return;
	}
	/*  st==2 でも中身は出す（破損しているのは封印域だけかもしれない）。 */
	m5_log_now_u32("[SURV] boot_count =", m5_surv.boot_count);
	m5_log_now_u32("[SURV] task_alive =", m5_surv.task_alive);
	m5_log_now_u32("[SURV] isr_alive  =", m5_surv.isr_alive);
	m5_log_now_u32("[SURV] ckpt id    =", m5_surv.ckpt_id);
	m5_log_now_u32("[SURV] ckpt seq   =", m5_surv.ckpt_seq);
	m5_log_now_u32("[SURV] last_ccount=", m5_surv.last_ccount);
	m5_log_now_u32("[SURV] last_gpio(0x100|pin,0=未)=", m5_surv.last_gpio);
	m5_log_now_u32("[SURV] seal_seq(秒)=", m5_surv.seal_seq);
	m5_log_now_u32("[SURV] seal_ccount =", m5_surv.seal_ccount);
	m5_log_now_u32("[SURV] hb_count    =", m5_surv.hb_count);
	m5_log_now_u32("[SURV] uart_stall  =", m5_surv.uart_stall);
	m5_log_now_u32("[SURV] gpio_guard  =", m5_surv.gpio_guard);
	m5_log_now_u32("[SURV] ps(task)    =", m5_surv.ps_task);
	m5_log_now_u32("[SURV] ie(task)    =", m5_surv.ie_task);
	m5_log_now_u32("[SURV] ps_site     =", m5_surv.ps_site);
	for (i = 0U; i < M5_SURV_NREG; i++) {
		m5_log_now_u32(m5_surv_regname[i], m5_surv.reg[i]);
	}
}

/*  新しい epoch を開始する（記録開始）。 */
static void
m5_surv_init(uint32_t boot_count)
{
	unsigned int	i;
	uint32_t		*w = (uint32_t *) &m5_surv;

	for (i = 0U; i < (unsigned int)(sizeof(m5_surv) / sizeof(uint32_t)); i++) {
		w[i] = 0U;
	}
	m5_surv.magic      = M5_SURV_MAGIC;
	m5_surv.version    = M5_SURV_VERSION;
	m5_surv.boot_count = boot_count;
	m5_surv.checksum   = m5_surv_cksum(&m5_surv);
	m5_surv_ready = true;
}

/*
 *  ★positive / negative control（「動くはず」で終わらせないための実演）。
 *
 *  QEMU では意図的なリセットを起こせないので、代わりに検証器そのものを
 *  実演する：
 *    PC1（positive）… 既知の値を書いて封印し、**同じ検証器**で読み戻して
 *                      VALID かつ値が一致することを示す。
 *    NC1（negative）… 封印後に 1 ワードだけ書き換え、同じ検証器が
 *                      CKSUM_NG(2) を返すことを示す（always-pass でない証明）。
 *    NC2（negative）… magic を壊すと NO_MAGIC(1) を返すことを示す。
 */
static void
m5_surv_selftest(void)
{
	unsigned int	st;
	uint32_t		saved;

	m5_log_now("==== [SURV-PC] 記録域の positive / negative control ====");

	m5_surv_init(0xABCDU);
	m5_surv.task_alive = 0x11112222U;
	m5_surv.isr_alive  = 0x33334444U;
	m5_surv.seal_seq   = 0x55556666U;
	m5_surv.reg[9]     = 0x77778888U;
	m5_surv.checksum   = m5_surv_cksum(&m5_surv);

	st = m5_surv_check();
	m5_log_now_u32("[SURV-PC] PC1 write→read status(期待0)=", st);
	m5_log_now_u32("[SURV-PC] PC1 task_alive(期待0x11112222)=", m5_surv.task_alive);
	m5_log_now_u32("[SURV-PC] PC1 isr_alive (期待0x33334444)=", m5_surv.isr_alive);
	m5_log_now_u32("[SURV-PC] PC1 seal_seq  (期待0x55556666)=", m5_surv.seal_seq);
	m5_log_now_u32("[SURV-PC] PC1 reg[9]    (期待0x77778888)=", m5_surv.reg[9]);

	saved = m5_surv.reg[9];
	m5_surv.reg[9] ^= 1U;			/*  封印域を 1bit だけ壊す（再封印しない） */
	st = m5_surv_check();
	m5_log_now_u32("[SURV-PC] NC1 封印域1bit破壊 status(期待2)=", st);
	m5_surv.reg[9] = saved;

	m5_surv.magic ^= 1U;
	st = m5_surv_check();
	m5_log_now_u32("[SURV-PC] NC2 magic破壊     status(期待1)=", st);
	m5_surv.magic ^= 1U;

	st = m5_surv_check();
	m5_log_now_u32("[SURV-PC] 復元後 status(期待0)=", st);
}

/*
 *  main_task の冒頭（UART 再ルーティング直後）から呼ぶ。
 *  「前回の記録を出す → 自己検証 → 新 epoch 開始」の順。
 */
void
m5_surv_boot_report(void)
{
	unsigned int	st;
	uint32_t		next;

	st   = m5_surv_check();
	next = (st == 1U) ? 1U : (m5_surv.boot_count + 1U);
	m5_surv_dump_prev();
	m5_surv_selftest();				/*  ★領域を破壊するので dump の後 */
	m5_surv_init(next);
	m5_log_now_u32("[SURV] 新 epoch 開始 boot_count=", m5_surv.boot_count);
}

/*
 *  シムがタスク文脈で呼ぶ：出力せず退避のみ（ホットパスでも安い）。
 *  ★1-14：ここが M5 シムの全ホットパス（malloc/free/yield/delay/sem/ckpt）の
 *  共通合流点なので、差分ウォッチもここへ相乗りさせる（差分時のみ出力）。
 *  ★1-17：生存カウンタ（タスク文脈）もここで進める。
 */
void
m5_ps_snapshot(unsigned int site)
{
	m5_surv_task_alive();
	m5_ps_task = (unsigned int) m5_rd_ps();
	m5_ie_task = (unsigned int) m5_rd_intenable();
	m5_ps_site = site;
	m5_watch_sample();
}

/*
 *  到達点マーカ（スタブの入口/出口ブラケット用・1 行）。
 *  ★即時出力なので begin() 内でスピンに入っても、そこまでの行は必ず線に出る。
 */
/*
 *  ★1-13：出力しない到達点マーカ（ホットパス用）。
 *  出力予算を使い切った後も「最後に通過した点」と「通過回数」だけは
 *  更新し続ける。周期ハンドラの `[HB] ckpt id<<16|seq` がこれを再出力するので、
 *  「id=0x50（sem_take 入口）のまま seq も止まっている＝twai_sem の中で
 *  永久ブロック」のような判定が、出力を溢れさせずに可能になる。
 */
void
m5_ckpt_set(unsigned int id)
{
	m5_ckpt_id = id;
	m5_ckpt_seq++;
	m5_surv_ckpt(id, m5_ckpt_seq);
	m5_ps_snapshot(M5_PSSITE_MARK);
}

/*
 *  ★1-15：**一切出力しない**到達点マーカ（真のホットパス用）。
 *  m5_ckpt_set は m5_ps_snapshot → m5_watch_sample を通り、条件次第で
 *  [WT] を 1 行出す。GPIO スタブ（lgfx の I2C ビットバングは
 *  delayMicroseconds(2) 刻みでここを叩く）から呼ぶとその出力が
 *  **バスのタイミングを壊し得る**＝診断が対象を変えてしまう。
 *  よって GPIO 経路からはこちらを使う（id/seq の更新のみ・数十サイクル）。
 */
void
m5_ckpt_set_quiet(unsigned int id)
{
	m5_ckpt_id = id;
	m5_ckpt_seq++;
	/*  ★1-17：ここは lgfx の I2C ビットバング（delayMicroseconds(2) 刻み）が
	 *  叩く真のホットパスだが、生存記録の更新は「ready 判定 1 回＋数ストア」で
	 *  出力を一切伴わないため、バスのタイミングを壊さない。 */
	m5_surv_ckpt(id, m5_ckpt_seq);
}

void
m5_mark(const char *tag, unsigned int id)
{
	m5_ckpt_id = id;
	m5_ckpt_seq++;
	m5_surv_ckpt(id, m5_ckpt_seq);
	m5_ps_snapshot(M5_PSSITE_MARK);
	m5_log_now(tag);
}

void
m5_mark_u32(const char *tag, unsigned int id, unsigned int v)
{
	m5_ckpt_id = id;
	m5_ckpt_seq++;
	m5_surv_ckpt(id, m5_ckpt_seq);
	m5_ps_snapshot(M5_PSSITE_MARK);
	m5_log_now_u32(tag, v);
}

/*  begin() の前後で呼ぶ（m5_display_smoke_gfx.cpp）。
 *  ★S6：M5_DIAG_VERBOSE が無いときは [SPIN] ダンプごと無効なので no-op。
 *    ★呼出し側（gfx.cpp）は無改変のままでよい＝**外部リンケージは残す**。 */
void
m5_spin_probe_arm(void)
{
#ifdef M5_DIAG_VERBOSE
	m5_spin_dumps = 0U;
	m5_spin_have_prev = false;
	m5_spin_armed = 1;
#endif
}

void
m5_spin_probe_disarm(void)
{
#ifdef M5_DIAG_VERBOSE
	m5_spin_armed = 0;
#endif
}

/*
 *  ★1-15：差分出力ヘルパ。値が前回と同じ行は出さない（毎秒 1s 周期で
 *  20 行以上出すと 115200bps を食い潰し、UART が観測のボトルネックになるため）。
 *  ただし force（先頭 1 回と 10 回に 1 回）は全行出す＝「沈黙＝無変化」であって
 *  「沈黙＝経路が死んだ」ではないことを定期的に実証する（positive control）。
 *  ★S6：この 3 本（m5_diff_u32 / m5_console_dump / m5_i2c_dump）は [CON]/[I2C] の
 *  周期ダンプ専用で、呼出し元は m5_spin_cyc_handler の [HB] ブロックだけである。
 *  M5_DIAG_VERBOSE と同じ寿命にする（未使用警告を出さないため。★警告を残すと
 *  「診断ファイルは元から警告だらけ」になり、本物の警告が埋もれる）。
 *  ★m5_console_repair() はこの外側に置く — あれは診断ではなく**観測チャネルの
 *    自己修復**で、既定 OFF でも動き続けなければならない。
 */
#ifdef M5_DIAG_VERBOSE
static void
m5_diff_u32(const char *tag, uint32_t v, uint32_t *prev, bool_t force)
{
	if (force || !m5_diff_have_prev || v != *prev) {
		m5_log_now_u32(tag, (unsigned int) v);
	}
	*prev = v;
}
#endif /* M5_DIAG_VERBOSE */

/*
 *  ★1-15：観測チャネル（U0TXD→GPIO17/18 複製）の健全性検査と**張り直し**。
 *  出力より前に呼ぶこと（経路が死んでいる場合、直すのが先でなければ何も出ない）。
 *  戻り値：1=何かを直した（＝壊されていた）／0=健全だった。
 */
static unsigned int
m5_console_repair(void)
{
	static const uint32_t	iomux[2] = { M5_IOMUX_GPIO17_REG, M5_IOMUX_GPIO18_REG };
	static const uint32_t	pin[2]   = { M5_CON_PIN_A, M5_CON_PIN_B };
	unsigned int			fixed = 0U;	/* ★どれが壊れていたかのビットマスク */
	unsigned int			i;
	uint32_t				v;

	for (i = 0U; i < 2U; i++) {
		v = sil_rew_mem((const uint32_t *) iomux[i]);
		if (((v >> M5_MCU_SEL_SHIFT) & 7U) != M5_PIN_FUNC_GPIO_V) {
			v &= ~(7U << M5_MCU_SEL_SHIFT);
			v |= (M5_PIN_FUNC_GPIO_V << M5_MCU_SEL_SHIFT);
			sil_wrw_mem((uint32_t *) iomux[i], v);
			fixed |= 0x1U << (i * 4U);		/* IO_MUX.MCU_SEL       */
		}
		v = sil_rew_mem((const uint32_t *)
						(M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * pin[i]));
		if ((v & 0x1FFU) != M5_U0TXD_OUT_IDX_V) {
			sil_wrw_mem((uint32_t *) (M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * pin[i]),
						M5_U0TXD_OUT_IDX_V);
			fixed |= 0x2U << (i * 4U);		/* GPIO_FUNC_OUT_SEL    */
		}
		if ((sil_rew_mem((const uint32_t *) M5_GPIO_ENABLE_REG) & (1U << pin[i])) == 0U) {
			sil_wrw_mem((uint32_t *) M5_GPIO_ENABLE_W1TS_REG, 1U << pin[i]);
			fixed |= 0x4U << (i * 4U);		/* GPIO_ENABLE(OE)      */
		}
		/*  pad_driver(open-drain, GPIO_PINn bit2)が立つと TX は High を駆動できず、
		 *  外部プルアップの無い Grove Port C では線が壊れる。落とす。 */
		v = sil_rew_mem((const uint32_t *) (M5_GPIO_PIN0_REG + 4U * pin[i]));
		if ((v & 0x4U) != 0U) {
			sil_wrw_mem((uint32_t *) (M5_GPIO_PIN0_REG + 4U * pin[i]), v & ~0x4U);
			fixed |= 0x8U << (i * 4U);		/* pad_driver(open-drain) */
		}
	}
	if (fixed != 0U) {
		m5_con_repairs++;
	}
	return(fixed);
}

/*  ★1-15：観測チャネルと UART0 本体の状態（差分出力）。 */
#ifdef M5_DIAG_VERBOSE
static void
m5_console_dump(unsigned int fixed, bool_t force)
{
	uint32_t	perip = sil_rew_mem((const uint32_t *) M5_SYSTEM_PERIP_CLK_EN0_REG);
	uint32_t	rst   = sil_rew_mem((const uint32_t *) M5_SYSTEM_PERIP_RST_EN0_REG);

	if (fixed != 0U) {
		/*  ★これが出たら「観測経路は外部から壊されていた」＝(a) が真。 */
		/*  ★これが出たら「観測経路は外部から壊されていた」＝(a) が真。
		 *  bit[3:0]=GPIO17／bit[7:4]=GPIO18、各ニブルは
		 *    b0=IO_MUX.MCU_SEL  b1=FUNC_OUT_SEL  b2=OE  b3=pad_driver(OD)。 */
		m5_log_now_u32("[CON] ★REPAIRED what(nib:17,18)=", fixed);
		m5_log_now_u32("[CON] ★REPAIRED 累計=", m5_con_repairs);
	}
	m5_diff_u32("[CON] OUTSEL17(期待0x0c)=",
				sil_rew_mem((const uint32_t *)
							(M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * M5_CON_PIN_A)),
				&m5_con_prev[0], force);
	m5_diff_u32("[CON] OUTSEL18(期待0x0c)=",
				sil_rew_mem((const uint32_t *)
							(M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * M5_CON_PIN_B)),
				&m5_con_prev[1], force);
	m5_diff_u32("[CON] IOMUX17(MCU_SEL=bit14:12,期待1)=",
				sil_rew_mem((const uint32_t *) M5_IOMUX_GPIO17_REG),
				&m5_con_prev[2], force);
	m5_diff_u32("[CON] GPIO_ENABLE(bit17/18=1)=",
				sil_rew_mem((const uint32_t *) M5_GPIO_ENABLE_REG),
				&m5_con_prev[3], force);
	m5_diff_u32("[CON] UART0_STATUS(TXcnt=b25:16)=",
				sil_rew_mem((const uint32_t *) M5_UART0_STATUS_REG),
				&m5_con_prev[4], force);
	m5_diff_u32("[CON] UART0_CLKDIV=",
				sil_rew_mem((const uint32_t *) M5_UART0_CLKDIV_REG),
				&m5_con_prev[5], force);
	m5_diff_u32("[CON] UART0_CLK_CONF=",
				sil_rew_mem((const uint32_t *) M5_UART0_CLK_CONF_REG),
				&m5_con_prev[6], force);
	m5_diff_u32("[CON] UARTclk_en/rst(1=en,1=保持リセット)=",
				(((perip & M5_SYSTEM_UART_CLK_EN) != 0U) ? 0x10000U : 0U)
				| (((rst & M5_SYSTEM_UART_CLK_EN) != 0U) ? 1U : 0U),
				&m5_con_prev[7], force);
	/*  ★出力の打ち切り回数。0 でなければ「UART0 の TX が掃けなかった」＝
	 *  旧 target_fput_log なら**ここで永久に止まっていた**ことの直接証拠。 */
	m5_log_now_u32("[CON] uart_stall(0=正常)=", m5_uart_stall);
	m5_log_now_u32("[CON] gpio_guard(0=未接触)=", m5_gpio_console_guard);
}

/*  ★1-15：I2C1 ペリフェラルとバス線の実状態（差分出力）。 */
static void
m5_i2c_dump(bool_t force)
{
	uint32_t	gin   = sil_rew_mem((const uint32_t *) M5_GPIO_IN_REG);
	uint32_t	perip = sil_rew_mem((const uint32_t *) M5_SYSTEM_PERIP_CLK_EN0_REG);
	uint32_t	sr    = sil_rew_mem((const uint32_t *) M5_I2C_SR_REG);

	/*
	 *  ★1-16 訂正：従来ラベル "SR(b24=bus_busy,b3=SCL固着)" は**誤り**だった。
	 *  esp-idf/components/soc/esp32s3/register/soc/i2c_reg.h の実定義は
	 *      BUS_BUSY = bit4 / ARB_LOST = bit3 / STRETCH_CAUSE = bit[15:14]
	 *      SCL_MAIN_STATE_LAST = bit[26:24] / SCL_STATE_LAST = bit[30:28]
	 *  であり、bit24 は bus_busy ではなく SCL_MAIN_STATE_LAST の最下位ビットである。
	 *  この誤ラベルのため run M/O の SR=0x3700c001 が「bus_busy=1＝バスがハング」と
	 *  読まれたが、実際は **BUS_BUSY=0（バスは非占有）** であった。以後は生値に加えて
	 *  デコード済みフィールドを出し、同じ誤読が起きないようにする。
	 */
	m5_diff_u32("[I2C] SR(raw)=", sr, &m5_i2c_prev[0], force);
	m5_diff_u32("[I2C]  bus_busy(b4)=", (sr >> 4) & 1U, &m5_i2c_prev[8], force);
	m5_diff_u32("[I2C]  arb_lost(b3)=", (sr >> 3) & 1U, &m5_i2c_prev[9], force);
	/*  stretch_cause=3 が既定＝クロックストレッチ未発生。 */
	m5_diff_u32("[I2C]  stretch_cause(3=無)=", (sr >> 14) & 3U, &m5_i2c_prev[10], force);
	m5_diff_u32("[I2C]  scl_main_st_last=", (sr >> 24) & 7U, &m5_i2c_prev[11], force);
	m5_diff_u32("[I2C]  scl_st_last=", (sr >> 28) & 7U, &m5_i2c_prev[12], force);
	m5_diff_u32("[I2C] INT_RAW=",
				sil_rew_mem((const uint32_t *) M5_I2C_INT_RAW_REG), &m5_i2c_prev[1], force);
	m5_diff_u32("[I2C] CTR=",
				sil_rew_mem((const uint32_t *) M5_I2C_CTR_REG), &m5_i2c_prev[2], force);
	m5_diff_u32("[I2C] FIFO_ST=",
				sil_rew_mem((const uint32_t *) M5_I2C_FIFO_ST_REG), &m5_i2c_prev[3], force);
	m5_diff_u32("[I2C] I2C1_CLK_EN(期待1)=",
				((perip & M5_SYSTEM_I2C_EXT1_CLK_EN) != 0U) ? 1U : 0U,
				&m5_i2c_prev[4], force);
	/*  ★SDA=GPIO12 / SCL=GPIO11 の**実レベル**。0b00 なら両方 Low 固着＝
	 *  バスがハングしている。0b11 ならアイドル（正常）。 */
	m5_diff_u32("[I2C] 実レベル(b1=SDA12,b0=SCL11)=",
				(((gin >> M5_I2C_SDA_PIN) & 1U) << 1) | ((gin >> M5_I2C_SCL_PIN) & 1U),
				&m5_i2c_prev[5], force);
	m5_diff_u32("[I2C] OUTSEL_SDA12(raw)=",
				sil_rew_mem((const uint32_t *)
							(M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * M5_I2C_SDA_PIN)),
				&m5_i2c_prev[6], force);
	m5_diff_u32("[I2C] OUTSEL_SCL11(raw)=",
				sil_rew_mem((const uint32_t *)
							(M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * M5_I2C_SCL_PIN)),
				&m5_i2c_prev[7], force);
	/*  pad_driver（open-drain）は I2C では 1 が正常。0 なら push-pull＝競合の危険。 */
	m5_log_now_u32("[I2C] pad_driver(b1=SDA12,b0=SCL11,期待0x3)=",
				   ((sil_rew_mem((const uint32_t *)
					 (M5_GPIO_PIN0_REG + 4U * M5_I2C_SDA_PIN)) >> 2) & 1U) << 1
				   | ((sil_rew_mem((const uint32_t *)
					   (M5_GPIO_PIN0_REG + 4U * M5_I2C_SCL_PIN)) >> 2) & 1U));
}
#endif /* M5_DIAG_VERBOSE */

/*
 *  周期ハンドラ本体（CRE_CYC M5_SPIN_CYC・CLASS(CLS_PRC1)・1s 周期）。
 *  ★カーネルサービスコールを呼ばない（非タスク文脈で安全な操作のみ）。
 *  ★1-15 でレジスタ書込みを 1 箇所だけ導入した（m5_console_repair。
 *    対象は診断用の複製出力ピン GPIO17/18 のみ。理由は上の [CON] 節参照）。
 */
void
m5_spin_cyc_handler(EXINF exinf)
{
	unsigned int	con_fixed;
	bool_t			force;
	uint32_t		cmd;
	unsigned int	i;
	static const unsigned int	m5_spin_pins[3] = { 35U, 36U, 37U };

	(void) exinf;

	/*
	 *  ---- ★1-13 ハートビート（armed に**関わらず**無条件）----
	 *  これが 1 行でも出れば「周期ハンドラは発火する＝割込みは入る」が実証される。
	 *  begin() より前に出ることが positive control、begin() 中に止まることが
	 *  「begin() 中は割込みが入らない」の対比になる。
	 */
	m5_hb_count++;

	/*
	 *  ★1-17：ISR 文脈の生存記録。**出力より前**に、UART に一切触れずに
	 *  済ませる（出力側が壊れていても、この 1 秒分の記録は必ず残る）。
	 *  m5_surv_seal() が UART0/GPIO のレジスタ像も一緒に採るので、
	 *  「無音になった瞬間のチャネルの状態」が次回起動時に読み出せる。
	 */
	if (m5_surv_ready) {
		m5_surv.isr_alive++;
		m5_surv.last_ccount = m5_rd_ccount();
	}
	m5_surv_seal();

#if defined(M5_SURV_SELFRESET_SEC) && (M5_SURV_SELFRESET_SEC > 0)
	/*
	 *  ★1-17：自前 software reset（SRAM を保持したままリセットする唯一の
	 *  確実な手段）。ホスト側の外部リセットは CHIP_PU(EN) を落とし得るため
	 *  .diag_noinit が消える。指定秒数のあと自分でリセットし、次回起動の
	 *  [SURV] ダンプで前回到達点を読み出す。
	 *  ★封印は上で済ませてある（この時点の記録が最終値として残る）。
	 */
	if (m5_surv.isr_alive >= (uint32_t)(M5_SURV_SELFRESET_SEC)) {
		m5_log_now("[SURV] ★self-reset: RTC_CNTL SW_SYS_RST を発行する");
		sil_wrw_mem((uint32_t *) M5_RTC_CNTL_OPTIONS0_REG,
					sil_rew_mem((const uint32_t *) M5_RTC_CNTL_OPTIONS0_REG)
					| M5_RTC_CNTL_SW_SYS_RST);
		for (;;) {
		}
	}
#endif /* M5_SURV_SELFRESET_SEC */

	/*
	 *  ★1-15：**出力より前に**観測チャネルを張り直す。
	 *  経路が壊されていた場合、直してからでなければ以後の 1 行も線に出ない。
	 *  健全なら全比較が一致して書込みは 0 回＝完全な no-op である。
	 */
	con_fixed = m5_console_repair();
	force = ((m5_hb_count == 1U) || ((m5_hb_count % 10U) == 0U)) ? true : false;

	/*
	 *  ★**「出力しない」ときは必ず理由を 1 行申告する。**
	 *  沈黙＝ハンドラ未発火、と一意に読めなければ、止まっていたのが採取窓なのか
	 *  ハンドラなのかを区別できない。
 */
#ifndef M5_DIAG_VERBOSE
	/*
	 *  ★S6観測系修復 #5 — [HB] 全レジスタダンプは既定 OFF。
	 *  S5 実機ログの実測で **[HB] は 2,869 行 / 81,803 B ＝ 35.3%**（[WT] と合わせて
	 *  79.8%）。1 回の発火で 30 行以上出す設計は、段階1 の tick 停止調査
	 *  （1-13〜1-17）のためのものであり、段階2 の AC には 1 行も要らない。
	 *  ★ただし**生存確認そのものは残す**（S5-analysis §9-1 の「生存確認は 1 行で
	 *    足りる」）。10 秒に 1 行だけ通番を出す。これが途切れた位置が停止位置。
	 *  ★m5_console_repair()（上の行）は OFF でも動かし続ける — これは診断ではなく
	 *    **観測チャネルの自己修復**であり、止めると「出力経路が壊れたときに
	 *    二度と戻らない」という段階1 の欠陥へ回帰する。
	 */
	(void) force;
	if (((m5_hb_count % 10U) == 0U) && (m5_hb_prints < M5_HB_MAX)) {
		m5_hb_prints++;
		if (con_fixed != 0U) {
			m5_log_now_u32("[HB] ★観測チャネルを修復した what(nib:17,18)=", con_fixed);
		}
		m5_log_now_u32("[HB] alive #", m5_hb_count);
	}
#else
	if (m5_hb_prints >= M5_HB_MAX) {
		/*  上限到達は 1 回だけ申告する（以後は真に無音＝これ自体が識別子）。 */
		if (m5_hb_prints == M5_HB_MAX) {
			m5_hb_prints++;
			m5_log_now_u32("[HB] LIMIT 上限到達につき以後抑止。発火回数=",
						   m5_hb_count);
		}
	}
	if (m5_hb_prints < M5_HB_MAX) {
		uint32_t	ps_isr = m5_rd_ps();
		uint32_t	ie_isr = m5_rd_intenable();

		m5_hb_prints++;
		m5_log_now_u32("[HB] #", m5_hb_count);
		/*  ★1-16：経過秒。採取終了時刻とハンドラ停止時刻の混同を防ぐ。 */
		m5_log_now_u32("[HB]  経過秒(ccount/160MHz)=",
					   (unsigned int)(m5_rd_ccount() / 160000000U));
		/*  ★タスク文脈で最後に見えた値（判断に使うのはこちら）。 */
		m5_log_now_u32("[HB]  ps(task)=", m5_ps_task);
		m5_log_now_u32("[HB]  ps(task).INTLEVEL=", m5_ps_task & 0xFU);
		m5_log_now_u32("[HB]  ie(task)=", m5_ie_task);
		m5_log_now_u32("[HB]  ps_site=", m5_ps_site);
		/*  ★ハンドラ自身の値。ie(isr)=0・ps.INTLEVEL≧1 が**正常**（Level-1
		 *    エントリが再入防止で INTENABLE=0 にする設計。バグではない）。 */
		m5_log_now_u32("[HB]  ps(isr)=", (unsigned int) ps_isr);
		m5_log_now_u32("[HB]  ie(isr,0=正常)=", (unsigned int) ie_isr);
		m5_log_now_u32("[HB]  ckpt id<<16|seq=",
					   ((m5_ckpt_id & 0xFFFFU) << 16) | (m5_ckpt_seq & 0xFFFFU));
		/*  ★1-17 positive control：生存記録が**実際に**両文脈で進んでいることを
		 *  UART 上でも見せる。次回起動の [SURV] 行と突き合わせれば、
		 *  「無音になった後も進み続けたか」が事後に判定できる。 */
		m5_log_now_u32("[HB]  surv task_alive=", m5_surv.task_alive);
		m5_log_now_u32("[HB]  surv isr_alive =", m5_surv.isr_alive);
		m5_log_now_u32("[HB]  surv seal_seq  =", m5_surv.seal_seq);
		/*  ★固着 vs ライブロックの弁別：毎秒この値が増えるかを見る。 */
		m5_log_now_u32("[HB]  ctr micros=", m5_ctr[M5_CTR_MICROS]);
		m5_log_now_u32("[HB]  ctr yield =", m5_ctr[M5_CTR_YIELD]);
		m5_log_now_u32("[HB]  ctr delay =", m5_ctr[M5_CTR_DELAY]);
		m5_log_now_u32("[HB]  ctr malloc=", m5_ctr[M5_CTR_MALLOC]);
		m5_log_now_u32("[HB]  ctr free  =", m5_ctr[M5_CTR_FREE]);
		m5_log_now_u32("[HB]  ctr semtk =", m5_ctr[M5_CTR_SEMTAKE]);
		m5_log_now_u32("[HB]  ctr semgv =", m5_ctr[M5_CTR_SEMGIVE]);
		m5_log_now_u32("[HB]  ctr gpio  =", m5_ctr[M5_CTR_GPIO]);
		m5_log_now_u32("[HB]  ctr i2c   =", m5_ctr[M5_CTR_I2C]);
		/*  ★1-14：tick 側の実測。tk が [HB] と同数で進むなら「tick は来ている」、
		 *  進まないなら「tick 自体が来ていない」。dead=1 なら CCOMPARE0 が
		 *  過去に置かれた（＝次の一致は CCOUNT 一周後）。 */
		m5_log_now_u32("[HB]  tick cnt =", M5_TD(M5_TDIAG_TICK));
		m5_log_now_u32("[HB]  ccompare0=", (unsigned int) m5_rd_ccompare0());
		m5_log_now_u32("[HB]  ccount   =", (unsigned int) m5_rd_ccount());
		m5_log_now_u32("[HB]  cmp-cc   =",
					   (unsigned int)(m5_rd_ccompare0() - m5_rd_ccount()));
		m5_log_now_u32("[HB]  set/miss =",
					   (M5_TD(M5_TDIAG_SET) << 16) | (M5_TD(M5_TDIAG_SET_MISS) & 0xFFFFU));
		m5_log_now_u32("[HB]  raise/mis=",
					   (M5_TD(M5_TDIAG_RAISE) << 16) | (M5_TD(M5_TDIAG_RAISE_MISS) & 0xFFFFU));
		m5_log_now_u32("[HB]  sw mask  =", (unsigned int) _kernel_intenable_mask[0]);
		m5_log_now_u32("[HB]  vpri mask=", (unsigned int) _kernel_vpri_mask[0]);
		/*  ★1-14：main_task スタック実使用量（sp_max - sp_min。cfg STACK_SIZE=8192）。 */
		m5_log_now_u32("[HB]  sp min   =", (unsigned int) m5_sp_min);
		m5_log_now_u32("[HB]  sp max   =", (unsigned int) m5_sp_max);
		m5_log_now_u32("[HB]  sp used  =", (unsigned int)(m5_sp_max - m5_sp_min));

		/*  ★1-15：観測経路の健全性と I2C1 の実状態（差分出力・force 時は全行）。 */
		m5_console_dump(con_fixed, force);
		m5_i2c_dump(force);
		m5_diff_have_prev = true;
	}
#endif /* M5_DIAG_VERBOSE */

#ifndef M5_DIAG_VERBOSE
	/*  ★S6 観測系修復 #5：[SPIN]（段階1 の SPI 固着調査、S5 実機で 95 行）も既定 OFF。
	 *  以下のダンプは丸ごとコンパイルされない（未使用変数/関数の警告も出ない）。 */
	(void) cmd;
	(void) i;
	(void) m5_spin_pins;
#else
	if (m5_spin_armed == 0 || m5_spin_dumps >= M5_SPIN_DUMP_MAX) {
		return;
	}
	m5_spin_dumps++;

	m5_log_now_u32("[SPIN] ---- dump #", m5_spin_dumps);

	cmd = sil_rew_mem((const uint32_t *) M5_SPI_CMD_REG);
	m5_log_now_u32("[SPIN] SPI_CMD      =", (unsigned int) cmd);
	m5_log_now_u32("[SPIN]  USR(1=転送中)=",
				   (unsigned int) ((cmd & M5_SPI_USR_BIT) != 0U ? 1U : 0U));
	if (m5_spin_have_prev) {
		/*  0 なら前回ダンプから CMD が 1 ビットも動いていない＝完全固着。 */
		m5_log_now_u32("[SPIN]  CMD^prev(0=固着)=",
					   (unsigned int) (cmd ^ m5_spin_prev_cmd));
	}
	m5_spin_prev_cmd = cmd;
	m5_spin_have_prev = true;

	m5_log_now_u32("[SPIN] SPI_CLK_GATE(期待0x7)=",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_SPI_CLK_GATE_REG));
	m5_log_now_u32("[SPIN] SPI_CLOCK    =",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_SPI_CLOCK_REG));
	m5_log_now_u32("[SPIN] SPI_USER     =",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_SPI_USER_REG));
	m5_log_now_u32("[SPIN] SPI_USER1    =",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_SPI_USER1_REG));
	m5_log_now_u32("[SPIN] SPI_MS_DLEN  =",
				   (unsigned int) sil_rew_mem((const uint32_t *) M5_SPI_MS_DLEN_REG));
	{
		uint32_t perip = sil_rew_mem((const uint32_t *) M5_SYSTEM_PERIP_CLK_EN0_REG);
		m5_log_now_u32("[SPIN] PERIP_CLK_EN0=", (unsigned int) perip);
		m5_log_now_u32("[SPIN]  SPI2_CLK_EN(期待1)=",
					   (unsigned int) ((perip & M5_SYSTEM_SPI2_CLK_EN) != 0U ? 1U : 0U));
	}

	/*  GPIO35(MISO/DC 兼用)/36(SCLK)/37(MOSI) の配線。
	 *  ★OUT_SEL_CFG は**生 32bit** を出す。func_out_sel は bit[8:0]（9bit）で、
	 *    「GPIO そのまま出力」を表す SIG_GPIO_OUT_IDX は **256(0x100)** ＝ 9bit 目が
	 *    立つ（gpio_sig_map.h:449）。8bit へ切り詰めると 0 と区別できなくなるため
	 *    抽出せずそのまま出す。期待値: 36→0x65(FSPICLK_OUT_IDX=101),
	 *    37→0x67(FSPID_OUT_IDX=103), 35→0x100(SIG_GPIO_OUT_IDX=256, DC 手動駆動)。
	 *  pad_driver（1=open-drain）は 3 ピン分を 1 行にまとめる（bit0=35,1=36,2=37）。 */
	{
		uint32_t	od = 0U;

		for (i = 0U; i < 3U; i++) {
			uint32_t	pin  = m5_spin_pins[i];
			uint32_t	osel = sil_rew_mem((const uint32_t *)
								  (M5_GPIO_FUNC0_OUT_SEL_CFG_REG + 4U * pin));
			uint32_t	pcfg = sil_rew_mem((const uint32_t *)
								  (M5_GPIO_PIN0_REG + 4U * pin));

			od |= ((pcfg >> 2) & 0x1U) << i;
			m5_log_now_u32((pin == 35U) ? "[SPIN] OUTSEL35(raw)=" :
						   (pin == 36U) ? "[SPIN] OUTSEL36(raw)=" :
										  "[SPIN] OUTSEL37(raw)=",
						   (unsigned int) osel);
		}
		m5_log_now_u32("[SPIN] PAD_DRIVER b0=35,b1=36,b2=37 =", (unsigned int) od);
	}

	/*  直近の到達点マーカ（id<<16 | seq）。seq が前回ダンプから増えていなければ
	 *  「マーカ点を 1 つも通過していない＝同じ場所で回り続けている」。 */
	m5_log_now_u32("[SPIN] CKPT id<<16|seq=",
				   ((m5_ckpt_id & 0xFFFFU) << 16) | (m5_ckpt_seq & 0xFFFFU));
#endif /* M5_DIAG_VERBOSE */
}
