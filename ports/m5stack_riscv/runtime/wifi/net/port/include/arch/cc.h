/*
 *  lwIP プラットフォーム適合層（TOPPERS/FMP3 用．NO_SYS=0）
 *
 *  2026-07-22 訂正（外部からの指摘。`../esp32_s31/docs/reference_repo_issues.md` S3-2）
 *    本ヘッダは長らく「**ASP3用．NO_SYS=1**」と書かれていたが、**どちらも誤り**だった：
 *      - カーネルは **FMP3**（ASP3 ではない）
 *      - `NO_SYS` は同ディレクトリの `lwipopts.h:25` で **`0`**（OS あり・tcpip_thread 経由）
 *    `NO_SYS` は **lwIP の動作モデルそのもの**を決める設定で、0（OS あり）と
 *      1（ベアメタル）では設計が全く違う。**このコメントを信じると根本的に誤読する。**
 *    実際、本リポジトリを参照元にしている別リポジトリ（esp32_s31）がこの記述に
 *    引っかかり、同種のコメントを引き継いでいないか確認する羽目になっている。
 *    由来: `esp/wifi/net/` 一式は ESP32-C3/ASP3 の祖先からの移植で、
 *      **コードは移植されたがコメントが祖先のまま残った**（同文書の「参照するときの作法」
 *      が挙げる典型例）。
 *
 *  型・PACK_STRUCT・エンディアンはlwip/arch.hのGCC既定に委ねる
 *  （<stdint.h>/<stddef.h>はツールチェーン同梱のfreestandingヘッダで
 *  充足）．ここではlwip/arch.hが「ポート固有」として要求する項目
 *  （診断出力・assert・乱数・エンディアン）と本構成で不足する
 *  ヘッダ回避（ctype.h／unistd.h＝本ツールチェーンに実体が無い）のみ
 *  上書きする．esp_shim.h／kernel.hはここではincludeしない（全lwIP
 *  翻訳単位に波及するため，2関数の宣言のみ切り出す）．
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>

#define BYTE_ORDER LITTLE_ENDIAN

#define LWIP_NO_CTYPE_H   1
#define LWIP_NO_UNISTD_H  1

/*
 *  SYS_ARCH_PROTECT/UNPROTECT用の型（実体は port/sys_arch.c）．
 *
 *  2026-07-24 訂正（レビュー Part E-4）．旧コメントは
 *    「NO_SYS=1でも arch/sys_arch.h を介さず本ヘッダで型を提供する必要がある」
 *    と書いていたが，本ヘッダ冒頭で訂正したとおり**本構成は NO_SYS=0** であり，
 *    ここに NO_SYS=1 の話が残っていると冒頭の訂正を打ち消してしまう
 *    （下流リポジトリを誤らせたのはまさにこの文字列である）．
 *    また `arch/sys_arch.h` は**本ポートに存在し現に使われている**
 *    （同ディレクトリ．sys_sem_t/sys_mbox_t/sys_thread_t を定義）ので，
 *    「介さず」という説明も実態と合っていなかった．
 *
 *  sys_prot_t を cc.h 側に置くのは NO_SYS とは無関係で，lwIP ポートの慣行
 *    である（ESP-IDF 自身も port/esp32xx/include/arch/cc.h に置いている）．
 *    sys_arch.h へ移さないこと．参照は lwip/sys.h の SYS_LIGHTWEIGHT_PROT=1
 *    経路（lwipopts.h:26）．
 */
typedef int sys_prot_t;

#ifdef __cplusplus
extern "C" {
#endif

extern void esp_shim_log_write(const char *format, ...);
extern uint32_t esp_shim_random(void);

/*
 *  assert の後始末（実体は port/sys_arch.c）．**戻らない**．
 *  ここで関数へ委譲しているのは 2026-08-14 の BL-H-8 の帰結である
 *  （詳細は sys_arch.c の実装コメントと .steering/20260814-lx6-r2b-hang/）．
 *  旧実装は
 *      esp_shim_log_write(...);  for (;;) { }
 *  というマクロで，(a) 申告が syslog＝logtask 経由のため**実測で 1 行も
 *  線に出ず**，(b) その場で**タスク優先度のまま無限ループ**するため，
 *  tcpip_thread（優先度 4）で発火すると**それより下の全タスクが永久に
 *  飢餓する**．結果として「カーネルは生きているのに MAIN_TASK が二度と
 *  走らない・原因を示す行は 1 行も出ない」という現行バグになっていた．
 */
extern void lwip_port_assert_fail(const char *msg, int line, const char *file);

#ifdef __cplusplus
}
#endif

#define LWIP_PLATFORM_DIAG(x)  do { esp_shim_log_write x; } while (0)

#define LWIP_PLATFORM_ASSERT(x) \
	do { \
		lwip_port_assert_fail((x), (int) __LINE__, __FILE__); \
	} while (0)

#define LWIP_RAND() ((u32_t) esp_shim_random())

#endif /* LWIP_ARCH_CC_H */
