/*
 *  M5（CoreS3）用 libstdc++ pthread グルー（単一スレッド no-op・M5 ビルド専用）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  lgfx が使う std::list / std::__throw_* 等のため -lstdc++ をリンクすると，
 *  libstdc++ の内部（例外グローバルの TLS，__gthread 排他）が gthread=pthread の
 *  弱シンボルを参照する：pthread_key_create/getspecific/setspecific・
 *  pthread_mutex_init/lock/unlock。
 *
 *  本ポートの M5 経路は単一コア・単一 main_task（SHIM_LOCK 規約）で C++ オブジェクトへ
 *  触れるのは main_task 文脈のみ（design.md §3 の規約）。したがって pthread 排他は
 *  実質競合せず，TLS も 1 スロットで足りる。ここでは no-op の単一スレッド実装を置く
 *  （FMP3 カーネルには触れない＝newlib/libstdc++ 内部専用のスタブ）。
 *
 *  ★型は ABI 互換の最小宣言（pthread.h を引かない）。C リンケージなのでシンボル名と
 *    引数 ABI（key=整数・他=ポインタ）が合えばリンクは通る。
 */

#include <stddef.h>

typedef unsigned int	m5_pthread_key_t;

/*  libstdc++ の例外グローバル用 TLS：単一スレッドなので 1 スロットで代表。
 *  key の識別はしない（M5 経路で使う key は eh_globals の 1 個だけ）。 */
static void	*m5_tls_slot;

int
pthread_key_create(m5_pthread_key_t *key, void (*destructor)(void *))
{
	(void) destructor;
	if (key != NULL) {
		*key = 1U;			/* 非 0 の任意 key */
	}
	return(0);
}

int
pthread_key_delete(m5_pthread_key_t key)
{
	(void) key;
	return(0);
}

void *
pthread_getspecific(m5_pthread_key_t key)
{
	(void) key;
	return(m5_tls_slot);
}

int
pthread_setspecific(m5_pthread_key_t key, const void *value)
{
	(void) key;
	m5_tls_slot = (void *)value;
	return(0);
}

/*  単一スレッドなので排他は常に成功の no-op。 */
int pthread_mutex_init(void *mutex, const void *attr)
{ (void) mutex; (void) attr; return(0); }

int pthread_mutex_destroy(void *mutex)
{ (void) mutex; return(0); }

int pthread_mutex_lock(void *mutex)
{ (void) mutex; return(0); }

int pthread_mutex_unlock(void *mutex)
{ (void) mutex; return(0); }

int pthread_mutex_trylock(void *mutex)
{ (void) mutex; return(0); }

/*
 *  ------------------------------------------------------------------
 *  newlib syscall スタブ（_sbrk/_write/_read/_close/_fstat/_lseek）
 *  ------------------------------------------------------------------
 *  libstdc++/libc の内部（stdio バッファ・locale 等）が要求する。arch の
 *  chip_rom_libc.c はカーネル内部（per-task _reent）に密結合し単体流用できない
 *  ため，M5 では bare-metal 最小スタブを自前提供する。
 *
 *  ・_sbrk：newlib malloc 用の静的アリーナ（M5GFX の描画確保は esp_shim_malloc
 *    ＝m5_display_smoke_heap.c 経由なので，ここは libc 内部の小規模確保専用。32KB）。
 *  ・_write：★S0 で **fd 1/2 をコンソール（target_fput_log）へ配線**した（旧: 黙殺）。
 *    M5Unified の Log_Class が ::printf 経由で出すため、黙殺のままでは M5 のログが
 *    一行も見えなかった。詳細は _write の直上コメント参照。
 *  ・_read/_close/_fstat/_lseek：ファイル無し環境の定型スタブ。
 */
#include <sys/types.h>	/* ssize_t / off_t */
#include <stdint.h>		/* uintptr_t（★S6-c #4：_sbrk 呼出し元 PC の出力用） */
/*
 *  ★S5：本 TU の即時出力（_write / 自前 printf / puts / putchar）は
 *  target_fput_log 直ではなく **有界**な m5_putc_now_ext を通す。A1_CONSOLE_USJ=OFF
 *  （実機 CoreS3 を Grove Port C の外部 USB-UART で採取する S5 の構成）では
 *  target_fput_log の UART0 経路が**上限の無いスピン**で、TX が掃けなくなると
 *  計装ごと永久に止まって観測が消えるため（詳細は m5_stub_trace.h の宣言部）。
 */
#include "m5_stub_trace.h"	/* m5_putc_now_ext（有界な即時 1 文字出力） */

#ifndef M5_SBRK_ARENA_SIZE
/*  ★★2026-07-26: **実測に基づいて縮める**。既定は 4KiB。
 *  実測（p51）: `_sbrk` の **high-water = 0xd0 = 208 バイト**・呼出し 6 回・枯渇 0。
 *  ⇒ ★**32KiB を確保して 208 バイトしか使っていなかった**（32,560 バイトの死蔵）。
 *  4KiB でも実測の **20 倍**ある。
 *  ★縮めすぎは黙って壊れない——`[HEAP-SBRK] ★枯渇(正incr拒否)回数` が検出器。 */
#define M5_SBRK_ARENA_SIZE	(4U * 1024U)
#endif

static unsigned char	m5_sbrk_arena[M5_SBRK_ARENA_SIZE];
static size_t			m5_sbrk_top;
/*
 *  ★S5：high-water 計装。
 *  _sbrk は縮まない（incr<0 を拒否する）ので m5_sbrk_top 自身が high-water だが、
 *  **枯渇回数を別に数える**ことに意味がある：枯渇したときの top は「使えた分」で
 *  あって「必要だった分」ではないので、top だけを見ると足りていたように見える。
 *  ★このアリーナは m5_display_smoke_heap.c の M5 ヒープ（esp_shim_malloc /
 *    operator new の写像先・32KB）とは**別**である §5-4）。
 *    ここを消費するのは newlib 内部（stdio バッファ・locale 等）だけ。
 */
static unsigned int		m5_sbrk_nfail;
static unsigned int		m5_sbrk_ncall;
/*
 *  ★A-3 是正：縮小要求（incr<0）の拒否を
 *  **枯渇と別に数える**。newlib は正当な用途で縮小を要求することがあり、
 *  これを m5_sbrk_nfail に混ぜると m5_sbrk_report が「★枯渇あり・増量が要る」と
 *  誤って断言する（heap 計装の存在理由＝実機 run の証跡に対する偽陽性）。
 *  枯渇の断言は「正の incr を拒否した」＝ m5_sbrk_nfail のみで行う。
 */
static unsigned int		m5_sbrk_nshrink;

/*
 *  ★S6-c #4：_sbrk の**呼出し元**を記録する。
 *  S5-analysis §10-3 の残件「_sbrk が 4 回呼ばれたが誰が呼んだか不明」に答える。
 *  newlib のどの機能（stdio バッファ／locale／malloc アリーナ拡張）が本アリーナを
 *  食っているかが分かれば、32KB という寸法の妥当性を推測ではなく実測で言える。
 *
 *  返り番地は生の PC 値で出す（firmware 側でシンボル化しない）。読み方は
 *      xtensa-esp32s3-elf-addr2line -f -C -e <fmp_xip.elf> <番地>
 *  ★記録は先頭 M5_SBRK_NREC 件のみ（以降は件数だけ増える）。実機で 4 件だった
 *    ものが増えたときに「記録が溢れた」と分かるよう nrec と ncall を別に出す。
 */
#define M5_SBRK_NREC	8
static void			   *m5_sbrk_caller[M5_SBRK_NREC];
static ptrdiff_t		m5_sbrk_reqsz[M5_SBRK_NREC];
static unsigned int		m5_sbrk_nrec;

void *
_sbrk(ptrdiff_t incr)
{
	size_t	prev = m5_sbrk_top;

	m5_sbrk_ncall++;
	if (m5_sbrk_nrec < (unsigned int) M5_SBRK_NREC) {
		m5_sbrk_caller[m5_sbrk_nrec] = __builtin_return_address(0);
		m5_sbrk_reqsz[m5_sbrk_nrec]  = incr;
		m5_sbrk_nrec++;
	}
	if (incr < 0) {
		m5_sbrk_nshrink++;		/*  ★枯渇ではない（A-3） */
		return((void *)-1);
	}
	if (prev + (size_t)incr > M5_SBRK_ARENA_SIZE) {
		m5_sbrk_nfail++;
		return((void *)-1);		/* アリーナ枯渇 */
	}
	m5_sbrk_top = prev + (size_t)incr;
	return((void *)&m5_sbrk_arena[prev]);
}

/*  ★S5：_sbrk アリーナの一括報告（M5 ヒープとは別アリーナ）。 */
extern void	m5_log_now(const char *msg);
extern void	m5_log_now_u32(const char *msg, unsigned int v);

void
m5_sbrk_report(const char *tag)
{
	m5_log_now(tag);
	m5_log_now_u32("[HEAP-SBRK] arena バイト=          ",
				   (unsigned int) M5_SBRK_ARENA_SIZE);
	m5_log_now_u32("[HEAP-SBRK] high-water(=top)=      ",
				   (unsigned int) m5_sbrk_top);
	m5_log_now_u32("[HEAP-SBRK] 余裕 = arena - HW =    ",
				   (unsigned int) (M5_SBRK_ARENA_SIZE - m5_sbrk_top));
	m5_log_now_u32("[HEAP-SBRK] _sbrk 呼出し回数=      ", m5_sbrk_ncall);
	m5_log_now_u32("[HEAP-SBRK] ★枯渇(正incr拒否)回数= ", m5_sbrk_nfail);
	/*  ★A-3：縮小拒否は枯渇ではない。混ぜると偽陽性になるので別行で出す。 */
	m5_log_now_u32("[HEAP-SBRK] 縮小要求(incr<0)拒否= ", m5_sbrk_nshrink);
	/*  ★S6-c #4：呼出し元（生 PC 値）。addr2line -f -C -e <elf> で読む。
	 *  nrec < ncall なら記録が溢れている（先頭 8 件のみ）。 */
	m5_log_now_u32("[HEAP-SBRK] 呼出し元 記録件数=    ", m5_sbrk_nrec);
	{
		unsigned int	i;

		for (i = 0U; i < m5_sbrk_nrec; i++) {
			m5_log_now_u32("[HEAP-SBRK]   caller PC=      ",
						   (unsigned int) (uintptr_t) m5_sbrk_caller[i]);
			m5_log_now_u32("[HEAP-SBRK]     要求 incr=    ",
						   (unsigned int) m5_sbrk_reqsz[i]);
		}
	}
	m5_log_now((m5_sbrk_nfail == 0U)
			   ? "[HEAP-SBRK] → 枯渇なし。上の余裕はそのまま読んでよい"
			   : "[HEAP-SBRK] → ★枯渇あり。top は必要量を下回る（増量が要る）");
}

/*
 *  ---- ★S0（段階2 の前提）：stdout/stderr をコンソールへ配線する ----
 *
 *  【なぜ必要か】M5Unified の `Log_Class::output` は `vsnprintf` で整形した後
 *  **`::printf`** で出す（M5Unified/src/utility/Log_Class.cpp:84,88）。その出口が
 *  newlib stdio → 本関数である。従来ここは「黙殺」だったため、
 *  **`M5_LOGI`/`M5_LOGD`/`M5.Log.printf` が一行も出ない**状態だった
 *  （IMU・Power が多用する）。観測系が塞がったまま実機へ行くと、段階1 で繰返した
 *  「沈黙は両義的」の誤診（実機 run を仮説の墓場に費やす）を再演することになる。
 *  ⇒ M5Unified の TU を1本足すより先に、ここを開ける。
 *
 *  【方式】fd 1(stdout)/2(stderr) を `target_fput_log`（arch の低レベル即時出力。
 *  USJ 構成では USB-Serial-JTAG、UART 構成では UART0 へ出る）へ1バイトずつ流す。
 *  改行の CR 付与は `target_fput_log` 側が行う（chip_serial.c）。
 *  それ以外の fd は従来どおり黙殺し、返り値は要求バイト数（stdio を止めない）。
 *
 *  ★副作用の注意：本関数が「実際に書く」ようになると newlib stdio が行バッファを
 *  持ち始め、`_sbrk` アリーナ（M5_SBRK_ARENA_SIZE=32KB、本ファイル上部）を消費する。
 *  これは `esp_shim_malloc`（m5_display_smoke_heap.c）の M5 ヒープとは**別アリーナ**で、
 *  枯渇の切り分け時に混同しないこと。
 */
#ifndef M5_USE_ESP_SHIM	/* combined build: esp/shim defines these */
ssize_t _write(int fd, const void *buf, size_t count)
{
	if ((fd == 1) || (fd == 2)) {
		const char	*p = (const char *) buf;
		size_t		i;

		for (i = 0U; i < count; i++) {
			m5_putc_now_ext(p[i]);
		}
	}
	return((ssize_t)count);
}

#endif /* !M5_USE_ESP_SHIM */
ssize_t _read(int fd, void *buf, size_t count)
{ (void) fd; (void) buf; (void) count; return(0); }		/* EOF */

int _close(int fd)
{ (void) fd; return(-1); }

int _lseek(int fd, int offset, int whence)
{ (void) fd; (void) offset; (void) whence; return(-1); }

int _fstat(int fd, void *st)
{ (void) fd; (void) st; return(-1); }

/*
 *  ---- ★S0-(3)：newlib stdio を経由しない自前 printf ----
 *
 *  【なぜ必要か】本構成で `::printf` を呼ぶと **LoadProhibited でクラッシュする**
 *  （QEMU 実測）。原因は newlib stdio の `_impure_ptr` / `stdout` の FILE 構造体が
 *  この bare-metal 構成で初期化されていないこと。`_write` を配線しても解決しない
 *  （_write を黙殺へ戻しても同じクラッシュが起きることを実測で確認＝原因は printf 側）。
 *
 *  ★重要：事前調査は「M5Unified のログは黙殺される」と結論していたが、実測では
 *  **黙殺ではなくクラッシュ**だった。M5Unified の Log_Class は vsnprintf → ::printf で
 *  出力する（M5Unified/src/utility/Log_Class.cpp:84,88）ため、これは段階2 で
 *  M5Unified を入れた瞬間に踏む**現行バグ**である（潜在バグではない）。
 *
 *  【方式】`printf`/`puts`/`putchar`/`fflush` を自前定義して newlib のものより優先させ、
 *  整形は vsnprintf（実績あり・esp_shim_m5_ext.c が使用）で行い、出力は
 *  `target_fput_log` へ直接流す。FILE* には一切触れない。
 *  ★`fflush` は**定義しない**：newlib の libc_a-fflush.o が別経路で引き込まれており
 *    多重定義になるため。自前 printf は都度出力するのでフラッシュ自体が不要。
 *
 *  ★B-4 是正：整形結果が M5_PRINTF_BUFSZ を超えた
 *    ときは **打切りマーカ `[...]\n` を付けて欠落を可視化する**。返り値は
 *    「整形に必要だった長さ」（vsnprintf の意味）を保つので、返り値と実出力量の
 *    食い違いが黙って起きる状態を解消する。マーカが無いと、長い M5_LOGx が
 *    途中で切れていても「そこで止まった」のか「切られた」のか区別できない
 *    （＝ §2「沈黙は両義的」と同型の欠陥）。
 */
#include <stdarg.h>

#ifndef M5_PRINTF_BUFSZ
#define M5_PRINTF_BUFSZ		256
#endif

int	vsnprintf(char *, size_t, const char *, va_list);	/* newlib（整形のみ利用） */

int
printf(const char *fmt, ...)
{
	char	buf[M5_PRINTF_BUFSZ];
	va_list	ap;
	int		n, i, lim;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n <= 0) {
		return(n);
	}
	lim = (n < (int)sizeof(buf) - 1) ? n : (int)sizeof(buf) - 1;
	for (i = 0; i < lim; i++) {
		m5_putc_now_ext(buf[i]);
	}
	/*
	 *  ★B-4：切り詰めが起きた（必要長 n がバッファに収まらなかった）ことを
	 *  出力自身に残す。これが無いと欠落が不可視になる。
	 */
	if (n >= (int)sizeof(buf)) {
		static const char	trunc[] = "[...]\n";
		const char			*t;

		for (t = trunc; *t != '\0'; t++) {
			m5_putc_now_ext(*t);
		}
	}
	return(n);
}

#ifndef M5_USE_ESP_SHIM	/* combined build: esp/shim defines these */
int
puts(const char *s)
{
	if (s != NULL) {
		while (*s != '\0') {
			m5_putc_now_ext(*s++);
		}
	}
	m5_putc_now_ext('\n');
	return(0);
}

#endif /* !M5_USE_ESP_SHIM */
int
putchar(int c)
{
	m5_putc_now_ext((char) c);
	return(c);
}
