/*
 *  ESP32-P4 Wi-Fi モジュール対応 - lwIP sys_arch FMP3 実装
 *  arch/cc.h : コンパイラ抽象化層（最小限）
 *
 *  lwIP が各ポートに要求する「コンパイラ/プラットフォーム差異の吸収」ヘッダ。
 *  `lwip/arch.h` が本ファイルを include し、ここで定義されなかった型・マクロは
 *  `lwip/arch.h` 側の既定値にフォールバックする（lwIP の一般的な作法）。
 *
 *  対象: riscv32-esp-elf-gcc（ESP32-P4, RISC-V, リトルエンディアン, 32bit）。
 *  FMP3 本体（`t_stddef.h` 等）には依存させない（lwip_port は独立モジュール、
 *  os_adapter とも依存関係を持たせない方針のため、標準ヘッダのみを使う）。
 *
 *  【2026-07-10 方式(D)統合済み】本ファイルは本物の `lwip/arch.h` の
 *  include パスから自動的に include される（`_lwip_min_shim.h` 経由では
 *  ない。同シムは統合完了で不要となり 2026-07-16 に削除済み）。内容自体は
 *  統合前後で差し替え不要だったが、実際の lwipopts.h 側の LWIP_NOASSERT 等の
 *  設定と整合するかは今後も変更時に確認すること。
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/*
 *  【2026-07-10 socket 有効化】LWIP_TIMEVAL_PRIVATE=0（lwipopts.h）を選んだので、
 *  lwip/sockets.h が要求するとおり cc.h で <sys/time.h> を include し、システム
 *  （newlib）の struct timeval を lwip_select() 等へ供給する。これをしないと
 *  sockets.c の lwip_select が「struct timeval 未定義」でコンパイル失敗する
 *  （sockets.h 524-535 のコメントが明示）。
 *  PRIVATE=1（lwIP 自前 struct timeval）でなく 0 を選ぶ理由: 方式(D)では lwip
 *  ヘッダ上書きコンポーネント経由で IDF 側コンポーネントも lwip/sockets.h を
 *  間接 include し得る。PRIVATE=1 だと、そのコンポーネントが <sys/time.h> も
 *  含むと struct timeval が二重定義になる。PRIVATE=0 + ここでの sys/time.h
 *  include なら常に newlib の1定義に揃い衝突しない（IDF lwip port と同方式）。 */
#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  =====================================================================
 *  基本型（u8_t 等）
 *  =====================================================================
 *  lwip/arch.h は「cc.h が定義していなければ既定 typedef を使う」実装に
 *  なっているため、本来は省略可能だが、意図を明確にするため明示的に定義する。
 */
typedef uint8_t		u8_t;
typedef int8_t		s8_t;
typedef uint16_t	u16_t;
typedef int16_t		s16_t;
typedef uint32_t	u32_t;
typedef int32_t		s32_t;
typedef uintptr_t	mem_ptr_t;

/*
 *  =====================================================================
 *  printf 用フォーマット指定子
 *  =====================================================================
 *  u16_t/s16_t/u32_t/s32_t/uintptr_t(mem_ptr_t) を実体型どおりに出力するための
 *  書式マクロ。riscv32 (LP32/ILP32) では int=32bit, long=32bit のため、
 *  u32_t(uint32_t)は %"u"（int同幅）で出力できる。
 */
#define U16_F	"hu"
#define S16_F	"hd"
#define X16_F	"hx"
#define U32_F	"u"
#define S32_F	"d"
#define X32_F	"x"
#define SZT_F	"u"

/*
 *  =====================================================================
 *  エンディアン
 *  =====================================================================
 *  RISC-V (rv32imafc, ESP32-P4) はリトルエンディアン。
 *  【2026-07-10】BYTE_ORDER は <machine/endian.h>/<sys/types.h> 等が既に
 *  定義済みのことがある（本 lwip 上書きヘッダが mbedtls 等 lwIP 以外の
 *  コンポーネントからも間接 include されるため、-Werror で redefine 衝突する）。
 *  IDF の lwip port 同様、未定義時のみ定義する。 */
#ifndef BYTE_ORDER
#define BYTE_ORDER	LITTLE_ENDIAN
#endif

/*
 *  =====================================================================
 *  構造体パッキング
 *  =====================================================================
 *  GCC の __attribute__((packed)) を使う（多くの lwIP ポートと同じ定石）。
 */
#define PACK_STRUCT_FIELD(x)	x
#define PACK_STRUCT_STRUCT		__attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/*
 *  =====================================================================
 *  診断出力・アサート
 *  =====================================================================
 *  【設計判断】lwip_port は独立モジュール（FMP3 の syslog/t_syslog.h にも
 *  os_adapter にも依存させない）という方針のため、ここでは libc の
 *  fprintf(stderr,...)/abort() 相当ではなく、**関数宣言のみ**を行い、
 *  実体（`fmp3_lwip_platform_diag`/`fmp3_lwip_platform_assert_fail`）は
 *  統合時にアプリ側（FMP3 の syslog 等に橋渡しする）で定義してもらう
 *  想定とする（TODO: 統合時に実装し、mapping.md に追記すること）。
 *  これにより本ヘッダ単体は他モジュールへの依存を持たない。
 */
void fmp3_lwip_platform_diag(const char *fmt, ...);
void fmp3_lwip_platform_assert_fail(const char *msg, const char *file, int line) __attribute__((noreturn));

#define LWIP_PLATFORM_DIAG(x)		do { fmp3_lwip_platform_diag x; } while (0)
#define LWIP_PLATFORM_ASSERT(x)		fmp3_lwip_platform_assert_fail((x), __FILE__, __LINE__)

/* LWIP_PLATFORM_DIET: 不要（RISC-V/newlib で printf 系は使用可能なため）。 */
/* #define LWIP_PLATFORM_DIET */

/*
 *  =====================================================================
 *  未使用引数の抑止
 *  =====================================================================
 */
#define LWIP_UNUSED_ARG(x)	((void) (x))

#ifdef __cplusplus
}
#endif

#endif /* LWIP_ARCH_CC_H */
