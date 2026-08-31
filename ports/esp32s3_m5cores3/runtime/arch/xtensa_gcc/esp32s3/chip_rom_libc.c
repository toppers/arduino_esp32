/*
 *  ESP32-S3 ROM newlib サポート（syscall stub table / per-task _reent）
 *
 *  ■何を解決するか（test_mtrans2 実機クラッシュの真因）
 *
 *  ESP32-S3のROMはnewlibの一部（rand/strtol/malloc等）を実装している。
 *  ROM側のこれらの関数は、リエントラント構造体を得るために
 *  **ROMの__getreent stub (0x40050c28)** を呼び、それは
 *  **syscall_table_ptr (0x3fceffd4)** が指すstub tableの先頭メンバ
 *  __getreent を辿る。このポインタが未初期化(NULL)だとNULL参照になる。
 *
 *  実測：
 *    test_mtrans2.c:98 get_rand() → rand()
 *      → ROM 0x400014a0 (jump table) → ROM 0x400552f0 (rand本体)
 *        → call8 0x40050c28 (ROM __getreent stub)
 *          l32r   a8, (0x400508f0)  ; a8 = 0x3fceffd4 = &syscall_table_ptr
 *          l32i.n a8, a8, 0         ; a8 = syscall_table_ptr = 0  ★NULL
 *          l32i.n a10, a8, 0        ; ★FAULT EPC1=0x40050c30
 *                                   ;   EXCCAUSE=0x1c(LoadProhibited)
 *                                   ;   EXCVADDR=0
 *  両コアのtask2/task3が同時にget_rand()を呼ぶため両コア同時panicになる。
 *
 *  ■なぜ自前のrand（test_ovr/rand_stub.c）が効かなかったか
 *
 *  rand_stub.cはweakな自前rand()を提供し、リンクもされている。しかし
 *  リンク時に張る esp/ld/esp32s3.rom.newlib.ld:21 の
 *      rand = 0x400014a0;
 *  は**PROVIDEではないハード代入**なので、weak/strongに関係なく黙って
 *  勝ち、rand()呼出しはすべてROM実装に解決される（実測：
 *  build/seam_s3_smp_lx7/fmp_xip.map に "rand = 0x400014a0"）。
 *  → rom.newlib.ldを張り続ける方針（ESP-IDF準拠）である以上、
 *    ROM newlibが動く土台（stub table）を用意するのが正しい解。
 *
 *  ■ESP-IDFでの等価物
 *
 *  esp-idf/components/newlib/src/newlib_init.c の esp_libc_init()。
 *  ESP32-S3では `syscall_table_ptr = &s_stub_table;` の1本のみ
 *  （_pro/_app はS3のROMには存在しない。esp32s3.rom.libc.ld:52）。
 *
 *  ■__getreentの実装方針（per-task _reent）
 *
 *  ESP-IDFのFreeRTOS版__getreent（freertos_tasks_c_additions.h）は
 *  「現在のタスク → そのTCBのTLSブロック」を返す。本ポートも同じ発想で、
 *  「自コアのPCB → p_runtsk（実行中タスクのTCB）→ TCB内のreent」を返す。
 *  FMP3のTSKCTXBに_reentを埋め込んである（core_kernel_impl.h）。
 *  Xtensa THREADPTRは使わない：本ポートは文脈切替でTHREADPTRを退避・
 *  復元しておらず、使うにはcore_support.S（アセンブラ）の変更が要る。
 *  p_runtsk方式ならarch層アセンブラは無改変で済む。
 */

/*
 *  ★includeの順序に意味がある：
 *   1. kernel_impl.h を最初に読む。これがt_stddef.h→target_stddef.h→
 *      chip_stddef.h を引き、_REENT_BACKWARD_BINARY_COMPAT /
 *      _REENT_SDIDINIT を <sys/reent.h> より前に定義する（chip_stddef.h
 *      のコメント参照。struct _reent のレイアウトがROMの決め打ちと
 *      一致することの担保）。
 *   2. libcのヘッダはこのファイルの中だけで読む。<sys/reent.h> は
 *      newlibの<assert.h>を推移的に引き込み、TOPPERSのassert()を
 *      newlib版で上書きする（chip_rom_libc.hのコメント参照）。本ファイル
 *      はassert()を使わない（レイアウト検証はtypedefによるコンパイル時
 *      アサートで行う）ので影響しない。
 */
#include "kernel_impl.h"
#include "task.h"
#include "chip_rom_libc.h"
#include <t_syslog.h>
#include <stddef.h>
#include <stdarg.h>

/*
 *  ★ROM ABIの型は本ファイル内でローカルに定義する（環境のlibcヘッダに
 *    依存させない）
 *
 *  ■なぜこの方針が要るか（★守らないとS3のWi-Fiビルドが壊れる）
 *
 *  本ファイルはKERNEL_COBJS（Makefile.chip）に置かれ**S3の全構成で
 *  コンパイルされる**。しかし構成によってincludeパスが違う：
 *
 *   (1) カーネル／QEMU機能テスト（scripts/run_tests.sh）
 *       … ツールチェーンの**本物の**libcヘッダが見える。
 *   (2) Wi-Fi/BLE/BTビルド（esp/build_incflags_esp32s3_espidf.txt 等）
 *       … `esp/config/esp32/hal_stub_include` がincludeパスに載る。ここは
 *         mbedtls/wpa_supplicant向けの**意図的に簡略化したスタブ**であり、
 *         本物のlibcヘッダを**shadowする**（time.h / stdio.h / string.h /
 *         sys/lock.h / sys/types.h ほか）。スタブの time.h は clock_t も
 *         struct tms も持たず、スタブの sys/lock.h の _LOCK_T は本物
 *         （struct __lock *）と違い int である。
 *
 *  旧実装は <time.h>/<stdio.h>/<sys/lock.h> を include して clock_t・FILE・
 *  _LOCK_T で struct syscall_stub_table を組み立てていた。その結果 (2) では
 *  スタブ time.h に shadow されて clock_t が未定義になり、**構造体定義が
 *  途中で切れてS3のWi-Fi/BLE/BTビルドが全滅**する。★(1) はこのincludeパスを
 *  使わないため **QEMU回帰は緑のまま**で、この退行を検出できない。
 *
 *  ■なぜローカル定義が正しいか（その場しのぎではない）
 *
 *  ROMのstub tableは**シリコンに焼かれた固定ABI**である。その型を
 *  「includeパス上にたまたま在るヘッダ」から貰う設計がそもそも筋が悪い
 *  （環境が変わるとABIの解釈が変わってしまう）。必要な型をROM ABI準拠で
 *  ここに固定すれば、どのincludeパス構成でも同一の解釈になる。
 *  レイアウトの正しさは下のコンパイル時アサート（156バイト／各offset）が
 *  引き続き担保する。
 *
 *  なお本ファイルが実体を張るメンバ（__getreent/_malloc_r/ロック群/
 *  __assert_func/__sinit/_cleanup_r）はこれらの型を使わない。これらの型が
 *  現れるメンバ（_times_r/_gettimeofday_r/_stat_r/_fstat_r/_printf_float/
 *  _scanf_float）はすべてNULLのままであり、**型はレイアウト（各4バイトの
 *  関数ポインタ）を正典と対応付けて読めるようにするためだけに存在する**。
 *
 *  ■struct _reent だけは本物の <sys/reent.h> から貰う（写さない）
 *
 *  ROMが辿るのは struct _reent の実体レイアウトそのものなので、自前定義で
 *  写すとツールチェーン更新に追随できず無音でズレる。reent.h はスタブ側に
 *  同名が無いため (1)(2) どちらでも本物が読まれる。reent.h はスタブの
 *  <sys/lock.h> を推移的に読むが、_LOCK_RECURSIVE_T が int(4B) でも本物の
 *  struct __lock *(4B) でもサイズが同じなので struct _reent のレイアウトは
 *  変わらない（offsetof(_r48)==56 のコンパイル時アサートで担保）。
 */

/*
 *  ROM ABI: newlib の clock_t
 *  （xtensa-esp-elf 14.2.0: sys/_types.h の _CLOCK_T_ = unsigned long ＝4B）
 */
typedef unsigned long   rl_clock_t;

/*
 *  ROM ABI: 正典でポインタとしてしか現れない型
 *  （順に struct tms / struct timeval / struct stat / FILE に対応）。
 *  本ポートは実体を使わないので不透明のままでよく、不透明であるがゆえに
 *  環境のヘッダ（本物・スタブどちらか）に依存しない。
 *  ★ファイルスコープで前方宣言する：関数プロトタイプ内で初出にすると
 *    「declared inside parameter list」警告になるため。
 */
struct rl_tms;
struct rl_timeval;
struct rl_stat;
struct rl_file;

/*
 *  ROM ABI: retargetable locking の _LOCK_T
 *
 *  本物のnewlibは _RETARGETABLE_LOCKING 有効で
 *  `typedef struct __lock *_LOCK_T;` ＝**ポインタ**である
 *  （ツールチェーンの sys/lock.h:33-34）。スタブ sys/lock.h の
 *  `typedef int _LOCK_T` はサイズ(4B)こそ同じだが型が違い、そのまま使うと
 *  構成によって型検査の結果が変わる。ROM ABI準拠のポインタ型で固定する。
 */
struct rl_lock;
typedef struct rl_lock *rl_lock_t;

/*
 *  ★<sys/reent.h> に <sys/lock.h> を引かせない（_flock_t を自前で与える）
 *
 *  newlibの<sys/reent.h>は
 *      #ifndef __machine_flock_t_defined
 *      #include <sys/lock.h>
 *      typedef _LOCK_RECURSIVE_T _flock_t;
 *      #endif
 *  という構造で、**_flock_t（struct __sFILE の _lock メンバの型）を得るためだけ**
 *  に <sys/lock.h> を引く（reent.h 内で _LOCK_RECURSIVE_T を使うのはこの1箇所だけ。
 *  実測：reent.h:33-35、_flock_t の使用は :197 と :263 の `_flock_t _lock;` のみ）。
 *  __machine_flock_t_defined を定義して _flock_t を自前で与えれば、この include
 *  ごと回避できる（newlibがmachine依存部のために用意している**正規の拡張点**）。
 *
 *  ■なぜ回避が要るか
 *
 *   (1) カーネル／QEMU機能テスト
 *       … ツールチェーンの本物（newlib.h:376 で _RETARGETABLE_LOCKING 有効
 *         → sys/lock.h:33-35 で _LOCK_RECURSIVE_T = _LOCK_T = struct __lock *）
 *   (2) Wi-Fi/BLE の espidf 系ビルド（esp/build_incflags_esp32s3_espidf.txt）
 *       … $REPO/esp/config/esp32/hal_stub_include/sys/lock.h が先勝ちする
 *         （_LOCK_T / _LOCK_RECURSIVE_T を int で供給。2026-07-16 に
 *          hwcrypto 対応で追加されたもの）
 *   (3) Wi-Fi/BLE の非 espidf 系ビルド（esp/build_incflags.txt）
 *       … **別プロジェクト**（$HAL＝asp3_esp_idf、C3/ASP3側）の
 *         asp3/target/esp32c3_espidf/hal_stub/include/sys/lock.h が先勝ちする。
 *         これは _lock_t（void *）**のみ**を供給し _LOCK_RECURSIVE_T を持たない
 *         → reent.h:35 が `unknown type name '_LOCK_RECURSIVE_T'` となり
 *         **build_seam_s3_wifi_esp32s3.sh / build_seam_s3_ble_esp32s3.sh が
 *         コンパイル不能**（2026-07-17 実測。起票時は clock_t エラーの陰に
 *         隠れて見えていなかった第2の経路）
 *
 *  (3) のスタブは**本リポジトリ外**のファイルなので、こちらから直せない。
 *  _flock_t を自前で与えて <sys/lock.h> を一切引かなくすれば、(1)(2)(3) の
 *  どの構成でも同一に通る（＝ROM ABIを環境に依存させない本ファイルの方針と一致）。
 *
 *  ★レイアウト等価性：本物の _flock_t は _LOCK_RECURSIVE_T ＝ _LOCK_T ＝
 *    `struct __lock *`（ポインタ4B）。下の定義も同じポインタ4Bなので
 *    struct __sFILE / struct _reent のレイアウトは変わらない。担保は下の
 *    コンパイル時アサート（sizeof(struct _reent)==sizeof(TCB内領域)==240 と
 *    offsetof(_r48)==56）。ズレたら無音のメモリ破壊ではなくビルドエラーになる。
 */
#define __machine_flock_t_defined
typedef struct rl_lock *_flock_t;

/*
 *  ★libcヘッダはここから下でのみ読む（上の _flock_t 定義より後）。
 *   <sys/reent.h> は newlib の <assert.h> を推移的に引き込み TOPPERS の
 *   assert() を上書きするため、共通ヘッダには持ち込まない
 *   （chip_rom_libc.h のコメント参照）。本ファイルは assert() を使わない。
 */
#include <sys/reent.h>
#include <string.h>

/*
 *  ESP32-S3 ROM の syscall stub table のレイアウト
 *
 *  正典：esp-idf/components/esp_rom/esp32s3/include/esp32s3/rom/libc_stubs.h
 *  :36-77（39メンバ・156バイト・retargetable locking版）。
 *  カーネルarch層からesp-idfのヘッダをincludeできない（seamビルドは
 *  カーネルオブジェクトにesp-idfのインクルードパスを与えない）ため、
 *  同一のレイアウトをここに写す。**順序・メンバ数を厳守すること**
 *  （ROMがオフセット直値で辿るため、1つずれると全滅する）。
 *
 *  ★無印ESP32(LX6)の同名構造体とは別物：LX6は36メンバ・非retargetable
 *    locking版で、index 24以降がまるごと違う
 *    （esp/shim/wifi_stubs.c の struct esp32_syscall_stub_table）。
 */
struct syscall_stub_table {
	struct _reent *(*__getreent)(void);
	void  *(*_malloc_r)(struct _reent *r, size_t);
	void   (*_free_r)(struct _reent *r, void *);
	void  *(*_realloc_r)(struct _reent *r, void *, size_t);
	void  *(*_calloc_r)(struct _reent *r, size_t, size_t);
	void   (*_abort)(void);
	int    (*_system_r)(struct _reent *r, const char *);
	int    (*_rename_r)(struct _reent *r, const char *, const char *);
	rl_clock_t (*_times_r)(struct _reent *r, struct rl_tms *);
	int    (*_gettimeofday_r)(struct _reent *r, struct rl_timeval *, void *);
	void   (*_raise_r)(struct _reent *r);
	int    (*_unlink_r)(struct _reent *r, const char *);
	int    (*_link_r)(struct _reent *r, const char *, const char *);
	int    (*_stat_r)(struct _reent *r, const char *, struct rl_stat *);
	int    (*_fstat_r)(struct _reent *r, int, struct rl_stat *);
	void  *(*_sbrk_r)(struct _reent *r, ptrdiff_t);
	int    (*_getpid_r)(struct _reent *r);
	int    (*_kill_r)(struct _reent *r, int, int);
	void   (*_exit_r)(struct _reent *r, int);
	int    (*_close_r)(struct _reent *r, int);
	int    (*_open_r)(struct _reent *r, const char *, int, int);
	int    (*_write_r)(struct _reent *r, int, const void *, int);
	int    (*_lseek_r)(struct _reent *r, int, int, int);
	int    (*_read_r)(struct _reent *r, int, void *, int);
	void   (*_retarget_lock_init)(rl_lock_t *lock);
	void   (*_retarget_lock_init_recursive)(rl_lock_t *lock);
	void   (*_retarget_lock_close)(rl_lock_t lock);
	void   (*_retarget_lock_close_recursive)(rl_lock_t lock);
	void   (*_retarget_lock_acquire)(rl_lock_t lock);
	void   (*_retarget_lock_acquire_recursive)(rl_lock_t lock);
	int    (*_retarget_lock_try_acquire)(rl_lock_t lock);
	int    (*_retarget_lock_try_acquire_recursive)(rl_lock_t lock);
	void   (*_retarget_lock_release)(rl_lock_t lock);
	void   (*_retarget_lock_release_recursive)(rl_lock_t lock);
	int    (*_printf_float)(struct _reent *data, void *pdata, struct rl_file *fp,
	                        int (*pfunc)(struct _reent *, struct rl_file *,
	                                     const char *, size_t len),
	                        va_list *ap);
	int    (*_scanf_float)(struct _reent *rptr, void *pdata, struct rl_file *fp,
	                       va_list *ap);
	void   (*__assert_func)(const char *file, int line, const char *func,
	                        const char *failedexpr) __attribute__((__noreturn__));
	void   (*__sinit)(struct _reent *r);
	void   (*_cleanup_r)(struct _reent *r);
};

/*
 *  TCB内の不透明領域（core_kernel_impl.hのTSKCTXB）を型付けして取り出す
 */
#define TSKCTXB_REENT(p_tcb)   ((struct _reent *)  (void *) (p_tcb)->tskctxb.reent)
#define TSKCTXB_RAND48(p_tcb)  ((struct _rand48 *) (void *) (p_tcb)->tskctxb.rand48)

/*
 *  ROM側の絶対シンボル（esp-idf/components/esp_rom/esp32s3/ld/
 *  esp32s3.rom.libc.ld:52-53。esp/ld/esp32s3.rom.libc.ld で張られる）
 *   syscall_table_ptr  = 0x3fceffd4
 *   _global_impure_ptr = 0x3fceffd0
 *
 *  ★weak宣言にする理由（本ポートには2種類のリンク構成がある）：
 *   (a) seam系ビルド（esp/boot/build_seam_s3_*.sh。実機・XIP）
 *       … esp32s3.rom.*.ld を張る。ROM newlibが実際に使われる
 *         （rom.newlib.ld:21 の `rand = 0x400014a0;` により rand() は
 *          ROM実装に解決される）ため、これらのポインタを張る必要がある。
 *   (b) QEMU機能テスト（scripts/run_tests.sh）・その他の素のmakeリンク
 *       … ROM ld を一切張らない。ROM newlibは呼ばれず（rand()は
 *         test_ovr/rand_stub.c のweak実装に解決される）、これらの
 *         シンボルも存在しない。強参照にすると未定義参照でリンクできない。
 *
 *  weak未定義シンボルはアドレス0に解決されるので、`&sym != NULL` で
 *  「このリンク構成にROMのポインタが存在するか」を実行時に判定できる。
 *  (b) では初期化をスキップする（ROM newlibを使わないので不要）。
 */
extern struct syscall_stub_table *syscall_table_ptr __attribute__((weak));
extern struct _reent *_global_impure_ptr __attribute__((weak));

/*
 *  ★レイアウトのコンパイル時検証
 *
 *  ROMは各メンバをオフセット直値で辿るため、1つでもズレると無音で
 *  暴走する（NULL参照ならまだ発見できるが、別のメンバを関数ポインタと
 *  誤認して飛ぶと解析困難になる）。ROM実測値・ESP-IDF正典と突き合わせて
 *  ここで固定する。
 *   - stub table: 39メンバ・156バイト
 *   - __getreent は先頭（ROM __getreent stub 0x40050c2e-30 の
 *     l32i.n a8,a8,0 / l32i.n a10,a8,0 ＝ offset 0）
 *   - __assert_func は offset 144
 *   - struct _reent の _r48 は offset 56
 *     （ROM rand 0x400552f6 の l32i a8,a10,56。実測evidence-11）
 *   - ROM rand の malloc サイズ 24 ＝ sizeof(struct _rand48)
 */
typedef char chip_rom_libc_assert_stub_size[
	(sizeof(struct syscall_stub_table) == 156) ? 1 : -1];
typedef char chip_rom_libc_assert_getreent_off[
	(offsetof(struct syscall_stub_table, __getreent) == 0) ? 1 : -1];
typedef char chip_rom_libc_assert_malloc_off[
	(offsetof(struct syscall_stub_table, _malloc_r) == 4) ? 1 : -1];
typedef char chip_rom_libc_assert_assert_off[
	(offsetof(struct syscall_stub_table, __assert_func) == 144) ? 1 : -1];
typedef char chip_rom_libc_assert_r48_off[
	(offsetof(struct _reent, _r48) == 56) ? 1 : -1];
typedef char chip_rom_libc_assert_rand48_size[
	(sizeof(struct _rand48) == 24) ? 1 : -1];

/*
 *  TCB内に確保した不透明領域（core_kernel_impl.hのTSKCTXB）が、実際の
 *  struct _reent / struct _rand48 を過不足なく収容できることを固定する。
 *  ツールチェーン更新でサイズ・アラインが変わった場合、無音のメモリ破壊
 *  ではなくここでビルドエラーになる。
 */
typedef char chip_rom_libc_assert_tcb_reent_size[
	(sizeof(((TCB *) 0)->tskctxb.reent) == sizeof(struct _reent)) ? 1 : -1];
typedef char chip_rom_libc_assert_tcb_rand48_size[
	(sizeof(((TCB *) 0)->tskctxb.rand48) == sizeof(struct _rand48)) ? 1 : -1];
typedef char chip_rom_libc_assert_reent_align[
	(__alignof__(struct _reent) <= __alignof__(uint64_t)) ? 1 : -1];
typedef char chip_rom_libc_assert_rand48_align[
	(__alignof__(struct _rand48) <= __alignof__(uint64_t)) ? 1 : -1];

/*
 *  グローバル（非タスク文脈用）のリエントラント構造体
 *
 *  ★newlibの_GLOBAL_REENT（＝&_impure_data）は使えない：参照すると
 *    libc_a-impure.o が引き込まれ、その静的初期化子が newlib の stdio
 *    FILE テーブル（findfp.o/fflush.o）を芋づるで引き込み、結果として
 *    _close/_read/_write/_lseek/_fstat/_sbrk/_exit といった newlib
 *    syscall が未定義参照になってリンクできない。ESP-IDFが_GLOBAL_REENTを使える
 *    のはVFSで全syscallを実装しているから。
 *
 *  そこで等価な役割（ディスパッチ前・アイドル中などp_runtskがNULLの
 *  文脈で使うグローバル_reent）を自前の静的実体で提供する。ROM stdioは
 *  使わないためstdin/stdout/stderrはNULLのままでよい。
 */
static struct _reent  s_global_reent;
static struct _rand48 s_global_rand48;

/*
 *  ROM newlibが致命的エラー時に呼ぶ経路（__assert_func / _abort / __sinit）
 *
 *  newlibのabort()は使えない：raise/_exit等のnewlib syscallスタブを
 *  芋づるで要求し、本ポート（OSレス相当・syscall未提供）ではリンクでき
 *  ないため。ここで自前のnoreturnパニックを提供する。
 */
static void
rom_libc_panic(const char *msg) __attribute__((noreturn));

static void
rom_libc_panic(const char *msg)
{
	syslog(LOG_EMERG, "ROM newlib panic: %s", msg);
	for (;;) {
	}
}

static void
rl_abort(void) __attribute__((noreturn));

static void
rl_abort(void)
{
	rom_libc_panic("abort()");
}

/*
 *  newlib syscallスタブ（_exit / _kill / _getpid）
 *
 *  上の注記のとおり本ポートは newlib の syscall 群を提供しない方針だが、
 *  この3本だけは**リンク側から要求される**ので実体が要る。
 *  flash上のnewlib（ROM側ではない）の abort() → raise() → _kill_r/_getpid_r と、
 *  __stack_chk_fail() → _exit が参照するためである。
 *
 *  2026-08-22に実際に踏んだ：スケッチからフォント選択APIを呼ぶと
 *  LGFXBase::setFont 経路が --gc-sections で落ちなくなり、そこから
 *  abort()/__stack_chk_fail が引き込まれて
 *  `undefined reference to _kill / _getpid / _exit` でリンクが失敗した。
 *  スケッチがローカル配列やstd::を使えば同じ経路に入るので、
 *  利用者側で普通に起こる。
 *
 *  ★スタブを置くこと自体は何も引き込まない。避けているのは _GLOBAL_REENT を
 *  **参照**することであって、syscallの実体を与えることではない。
 */
void
_exit(int status)
{
	syslog(LOG_EMERG, "libc: _exit(%d)", status);
	for (;;) {
	}
}

int
_kill(int pid, int sig)
{
	(void) pid;
	syslog(LOG_EMERG, "libc: _kill(sig=%d)", sig);
	for (;;) {
	}
}

int
_getpid(void)
{
	/*  プロセスの概念が無いので固定値。newlibはraise()経路でしか使わない。 */
	return 1;
}

/*
 *  ROM newlibのassert失敗。ROM rand()は _r48==NULL かつ malloc失敗時に
 *  ここへ来る（本実装では_r48を静的に与えるため到達しないはずだが、
 *  他のROM newlib経路のために実体を張る。NULLのままだと第2のNULL参照に
 *  なる＝stub tableのoffset 144）。
 */
static void
rl_assert_func(const char *file, int line, const char *func,
               const char *failedexpr) __attribute__((noreturn));

static void
rl_assert_func(const char *file, int line, const char *func,
               const char *failedexpr)
{
	syslog(LOG_EMERG, "ROM newlib assert: %s:%d: %s: %s",
	       (file != NULL) ? file : "?", line,
	       (func != NULL) ? func : "?",
	       (failedexpr != NULL) ? failedexpr : "?");
	for (;;) {
	}
}

/*
 *  __sinit（ROM stdio初期化）
 *
 *  ESP-IDF（newlib_init.c:123）と同じくabort相当にする。本ポートは
 *  ROM stdioを使わず、各_reentのSDIDINITを1にして「初期化済み」と
 *  見せるため、ROM側から__sinitが呼ばれることは無いはずである。
 *  呼ばれたら設計前提が崩れているのでパニックさせて可視化する。
 */
static void
rl_sinit(struct _reent *r) __attribute__((noreturn));

static void
rl_sinit(struct _reent *r)
{
	(void) r;
	rom_libc_panic("__sinit called (ROM stdio path unexpected)");
}

static void
rl_cleanup_r(struct _reent *r)
{
	/*
	 *  ROM stdioを使わないためクローズすべきFILEは無い。no-op。
	 *  （_reent側の__cleanupにも同じものを入れる）
	 */
	(void) r;
}

/*
 *  ROM newlib用の最小ヒープ（バンプアロケータ）
 *
 *  本ポートのカーネルテストビルドはヒープを持たない：newlibのmallocを
 *  参照すると _sbrk_r 等のsyscallスタブ群が芋づるで要求されリンクでき
 *  ない（test_ovr/rand_stub.cのコメントに同じ経緯の記録がある）。
 *
 *  一方でstub tableの_malloc_r(offset 4)をNULLのままにすると、ROM側が
 *  mallocを呼ぶ経路で第2のNULL参照になる。ROM newlibのmalloc使用は
 *  「_reentの遅延初期化領域を一度だけ確保して以後解放しない」型なので、
 *  解放不要の小さなバンプアロケータで十分かつ安全である。
 *
 *  ★本命の rand() 経路はここを使わない：reent._r48にTCB内の静的実体
 *    （tskctxb.rand48）を与えてあるため、ROM rand()はmallocを呼ばずに
 *    計算経路へ直行する（ROM 0x400552fb の bnez.n が成立する）。
 *    このプールは他のROM newlib経路が踏んだ場合の保険。
 */
#define ROM_LIBC_HEAP_SIZE   256U

static uint8_t  rom_libc_heap[ROM_LIBC_HEAP_SIZE] __attribute__((aligned(8)));
static size_t   rom_libc_heap_used;

static void *
rom_libc_bump_alloc(size_t size)
{
	SIL_PRE_LOC;
	void   *p = NULL;
	size_t  aligned = (size + 7U) & ~((size_t) 7U);

	SIL_LOC_INT();
	if ((aligned >= size) && (aligned <= (ROM_LIBC_HEAP_SIZE - rom_libc_heap_used))) {
		p = &rom_libc_heap[rom_libc_heap_used];
		rom_libc_heap_used += aligned;
	}
	SIL_UNL_INT();

	if (p == NULL) {
		syslog(LOG_WARNING, "ROM newlib heap exhausted (req=%d used=%d)",
		       (int) size, (int) rom_libc_heap_used);
	}
	return(p);
}

static void *
rl_malloc_r(struct _reent *r, size_t size)
{
	(void) r;
	return(rom_libc_bump_alloc(size));
}

static void
rl_free_r(struct _reent *r, void *p)
{
	/* バンプアロケータのため解放しない（ROMの使い方では解放されない） */
	(void) r;
	(void) p;
}

static void *
rl_realloc_r(struct _reent *r, void *p, size_t size)
{
	/*
	 *  縮小・拡大とも新規確保＋コピーで済ませる（旧ブロックは解放不能）。
	 *  ROM newlibのrealloc使用は想定していないため簡易実装。
	 */
	void *q;

	(void) r;
	q = rom_libc_bump_alloc(size);
	if ((q != NULL) && (p != NULL)) {
		memcpy(q, p, size);
	}
	return(q);
}

static void *
rl_calloc_r(struct _reent *r, size_t n, size_t size)
{
	void   *p;
	size_t  total = n * size;

	(void) r;
	if ((n != 0U) && ((total / n) != size)) {
		return(NULL);            /* 乗算オーバフロー */
	}
	p = rom_libc_bump_alloc(total);
	if (p != NULL) {
		memset(p, 0, total);
	}
	return(p);
}

/*
 *  retargetable locking（stub table index 24〜33）
 *
 *  ROMのnewlibロックは実質2個の変数へエイリアスされる。本ポートが踏む
 *  ROM newlibは rand() のみで、rand()はロックを取らないためno-opで足りる。
 *  他のROM newlib（arc4random/atexit/env/sfp/tz）を使い始める場合は
 *  実ロック（ESP-IDF components/newlib/src/locks.c のmagic方式）が要る。
 *  NULLのままにはしない（踏んだ瞬間にNULL参照になるため）。
 */
static void
rl_lock_noop(rl_lock_t lock)
{
	(void) lock;
}

static void
rl_lock_init_noop(rl_lock_t *lock)
{
	(void) lock;
}

static int
rl_lock_try_noop(rl_lock_t lock)
{
	(void) lock;
	return(0);                   /* 0 = 取得成功（newlibの規約） */
}

/*
 *  ESP32-S3 ROM の struct syscall_stub_table
 *  （esp-idf/components/esp_rom/esp32s3/include/esp32s3/rom/libc_stubs.h:36-77
 *    と同一レイアウト。39メンバ・156バイト・retargetable locking版）
 *
 *  ★esp/shim/wifi_stubs.c の struct esp32_syscall_stub_table は
 *    無印ESP32(LX6)用の36メンバ・非retargetable版であり、index 24以降が
 *    まるごとズレる。流用してはならない（LX6専用として据え置く）。
 *
 *  本ポートで実体が要るメンバ：
 *    offset 0   __getreent    ROM __getreent stubが最初に辿る
 *    offset 4   _malloc_r     ROM rand()が _r48==NULL 時に呼ぶ
 *    offset 144 __assert_func ROM rand()がmalloc失敗時に呼ぶ
 *  残りはROMが実際に呼ぶ経路のみ実体を張り、本ポートが提供できない
 *  ファイル系syscall（_open_r等）はNULLのままとする（呼ばれたら設計
 *  前提が崩れている＝ROM stdioを使っている、ということなので、NULL参照
 *  で即座に落ちる方が無音の誤動作より望ましい）。
 */
static struct syscall_stub_table s_stub_table = {
	.__getreent    = &__getreent,
	._malloc_r     = &rl_malloc_r,
	._free_r       = &rl_free_r,
	._realloc_r    = &rl_realloc_r,
	._calloc_r     = &rl_calloc_r,
	._abort        = &rl_abort,

	._retarget_lock_init                  = &rl_lock_init_noop,
	._retarget_lock_init_recursive        = &rl_lock_init_noop,
	._retarget_lock_close                 = &rl_lock_noop,
	._retarget_lock_close_recursive       = &rl_lock_noop,
	._retarget_lock_acquire               = &rl_lock_noop,
	._retarget_lock_acquire_recursive     = &rl_lock_noop,
	._retarget_lock_try_acquire           = &rl_lock_try_noop,
	._retarget_lock_try_acquire_recursive = &rl_lock_try_noop,
	._retarget_lock_release               = &rl_lock_noop,
	._retarget_lock_release_recursive     = &rl_lock_noop,

	.__assert_func = &rl_assert_func,
	.__sinit       = &rl_sinit,
	._cleanup_r    = &rl_cleanup_r,
};

/*
 *  リエントラント構造体の取得（ROM newlib / ツールチェーンlibc 共通）
 *
 *  本ツールチェーンは __DYNAMIC_REENT__ が有効なので、
 *  ROM側だけでなくリンクされたlibc側の _REENT も本関数経由になる。
 *
 *  ★割込み禁止が必要な理由（ESP-IDF tasks.c:4977-4981 と同趣旨）：
 *    「自コアのPCBを引く」→「そのPCBのp_runtskを読む」の2段階の間に
 *    プリエンプトされて別コアへ移送されると、**移送前のコアのp_runtsk**
 *    （＝今や別のタスク）を読んでしまう。自コア割込み禁止で原子化する。
 *    p_runtskを読み終えた後は、それが自タスク自身である以上、以後移送
 *    されてもreentポインタは正しいままなので、禁止区間は最小で足りる。
 *
 *  ★lock_cpu()/unlock_cpu()は使わない：unlock_cpu()は無条件に割込みを
 *    許可するため、既に割込み禁止の文脈（ISR内・カーネル内部）から
 *    呼ばれると勝手に許可してしまう。SIL_LOC_INT/SIL_UNL_INTは元の
 *    状態を保存・復元するのでどの文脈からでも安全。
 *
 *  ★p_runtsk==NULL（sta_ker前・ディスパッチ前・アイドル中）は
 *    グローバル_reentを返す（ESP-IDFも_GLOBAL_REENTを返す）。
 */
struct _reent *
__getreent(void)
{
	SIL_PRE_LOC;
	TCB *p_runtsk;

	SIL_LOC_INT();
	p_runtsk = get_my_pcb()->p_runtsk;
	SIL_UNL_INT();

	if (p_runtsk == NULL) {
		return(&s_global_reent);
	}
	return(TSKCTXB_REENT(p_runtsk));
}

/*
 *  タスクのper-task _reent初期化
 *
 *  ESP-IDF esp_reent_init()（components/newlib/src/reent_init.c:22-30）
 *  相当：memsetの後、stdin/stdout/stderr/__cleanup/SDIDINIT を
 *  _GLOBAL_REENT から複製する（各タスクにFILEを3つ持たせない）。
 *  FreeRTOS非依存の処理なのでそのまま等価実装できる。
 *
 *  加えて本ポート固有：_r48にTCB内の静的実体を与えて初期化する
 *  （ROM rand()のmalloc(24)経路を発生させない。ヒープが無いため）。
 */
void
chip_rom_libc_reent_init(struct task_control_block *p_tcb)
{
	struct _reent *r = TSKCTXB_REENT(p_tcb);

	memset(r, 0, sizeof(*r));
	_REENT_STDIN(r)    = _REENT_STDIN(&s_global_reent);
	_REENT_STDOUT(r)   = _REENT_STDOUT(&s_global_reent);
	_REENT_STDERR(r)   = _REENT_STDERR(&s_global_reent);
	_REENT_CLEANUP(r)  = _REENT_CLEANUP(&s_global_reent);
	_REENT_SDIDINIT(r) = _REENT_SDIDINIT(&s_global_reent);

	/*
	 *  _r48の実体をTCB内に静的に確保して初期化する。これにより
	 *  ROM rand()（0x400552f6の l32i a8,a10,56 → 0x400552fb の bnez.n）は
	 *  malloc経路に入らない。オフセット56という決め打ちは
	 *  _REENT_BACKWARD_BINARY_COMPAT により保証される（chip_stddef.h）。
	 */
	r->_r48 = TSKCTXB_RAND48(p_tcb);
	_REENT_INIT_RAND48(r);
}

/*
 *  ROM newlibの初期化（コア0のみ・コア1解放前・ROM libc初使用前）
 *
 *  ESP-IDF esp_libc_init()（newlib_init.c:130-155）相当。
 *  順序はESP-IDF（system_init_fn.txt:35→43、newlib_init.c:137→141→154）
 *  に合わせる：（ヒープ＝本実装は静的プールで初期化不要）→ stub table →
 *  _global_impure_ptr → locks（本実装はno-opのため初期化不要）。
 */
void
chip_rom_libc_init(void)
{
	/*
	 *  1. グローバル_reentを初期化する（stub tableを張るより前に済ませる。
	 *     張った瞬間からROM側が__getreent経由でこれを触りうるため）。
	 *     ROM stdioは使わないのでstdin/stdout/stderrはNULLのまま。
	 *     SDIDINIT=1で「stdio初期化済み」に見せ、ROMから__sinitが
	 *     呼ばれる経路を塞ぐ（ESP-IDF newlib_init.c:144-145 と同趣旨）。
	 */
	memset(&s_global_reent, 0, sizeof(s_global_reent));
	_REENT_CLEANUP(&s_global_reent)  = &rl_cleanup_r;
	_REENT_SDIDINIT(&s_global_reent) = 1;

	/*
	 *     _r48にも静的実体を与える（p_runtsk==NULLの文脈——sta_ker前や
	 *     アイドル中——からrand()が呼ばれてもROMのmalloc経路に入らない）。
	 */
	s_global_reent._r48 = &s_global_rand48;
	_REENT_INIT_RAND48(&s_global_reent);

	/*
	 *  2. _global_impure_ptr（ROMが参照するグローバル_reent）
	 *     ESP-IDF newlib_init.c:141 の `_global_impure_ptr = _GLOBAL_REENT`
	 *     に相当（本ポートは自前のグローバル_reentを使う。上記参照）。
	 */
	if (&_global_impure_ptr != NULL) {
		_global_impure_ptr = &s_global_reent;
	}

	/*
	 *  3. stub tableを張る。ESP32-S3はポインタ1本のみ
	 *     （_pro/_appはS3のROMに存在しない）。両コア共有の1本でよい。
	 *     ここを最後に張ることで、「ROMがstub tableを辿れるようになった
	 *     時点では、その先の_reentは既に初期化済み」という順序を保証する。
	 */
	if (&syscall_table_ptr != NULL) {
		syscall_table_ptr = &s_stub_table;
	}
}
