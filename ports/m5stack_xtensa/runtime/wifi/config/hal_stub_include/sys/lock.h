/*
 *  newlib sys/lock.h スタブ
 *
 *  ツールチェーンにnewlibヘッダが無い環境のためのスタブ．
 *  esp_phy/include/esp_private/phy.h が _lock_t を要求する。
 *  実体（ロックの中身）はos_adapter shim（Phase B-2）で
 *  ASP3のミューテックス/CPUロックへ接続する。
 *
 *  ★BLE実施02（GCC14.2.0での再ビルドで発覚）：本スタブは型（_lock_t）
 *  のみを供給し，_lock_acquire/_lock_release等の関数プロトタイプを
 *  持たない．実体はesp/esp_shim_libc.cが定義済み（WiFi/BT共有）だが，
 *  esp_phy/src/phy_init.cはこのスタブ経由で`<sys/lock.h>`を解決する
 *  （ツールチェーン付属の本物のnewlib sys/lock.hより本スタブがinclude
 *  パス上で先に見つかるため——esp_timer.hスタブと同種の「意図的簡略化
 *  ヘッダが先勝ちする」構造．esp_bt.cmake/npl_os_bridge.h参照）ため，
 *  プロトタイプが無いと呼出し側（phy_init.c）でimplicit-function-
 *  declarationエラーになる．GCC14.2.0は同エラーを既定でハードエラー
 *  にする（従来ツールチェーンでは警告どまりで実害が無かった）．
 *  esp_shim_libc.cの定義に合わせてプロトタイプを追加する
 *  （strictly additive．WiFi/BT両ビルドに共通で効く）．
 */
#ifndef TOPPERS_HAL_STUB_SYS_LOCK_H
#define TOPPERS_HAL_STUB_SYS_LOCK_H

typedef void *_lock_t;

/*
 *  ★S3 hwcrypto段階3(MPI)追加(2026-07-16、strictly additive)：
 *  esp-idf mbedtls/port/bignum/esp_bignum.c は <malloc.h> を include し、
 *  それが newlib <sys/reent.h> を経由して <sys/lock.h> を取り込む。reent.h は
 *  `typedef _LOCK_RECURSIVE_T _flock_t;`（struct _reent のファイルロック型）で
 *  _LOCK_T / _LOCK_RECURSIVE_T を要求するが、本スタブは _lock_t（小文字、
 *  ESP-IDF retargetable locking用）しか持たず、これらが未定義でreent.hが
 *  コンパイルエラーになる（本スタブがツールチェーン付属の本物のnewlib
 *  sys/lock.hをinclude-path上で先勝ちshadowするため、本物の定義に届かない）。
 *  newlib既定の非retargetable版（typedef int）と同型を追加する。組込み
 *  （ファイルロック非使用のfreestanding）環境では _flock_t の実体は使われない
 *  ため int で無害。SHA/AES段階では malloc.h 経路を踏まなかったため顕在化して
 *  いなかった（追加は全ビルド共通に効くが、既存の使用箇所ゼロ＝非回帰）。
 */
#ifndef _LOCK_T
typedef int _LOCK_T;
#endif
#ifndef _LOCK_RECURSIVE_T
typedef int _LOCK_RECURSIVE_T;
#endif

#ifndef TOPPERS_MACRO_ONLY
extern void _lock_init(_lock_t *lock);
extern void _lock_init_recursive(_lock_t *lock);
extern void _lock_close(_lock_t *lock);
extern void _lock_close_recursive(_lock_t *lock);
extern void _lock_acquire(_lock_t *lock);
extern void _lock_acquire_recursive(_lock_t *lock);
extern int  _lock_try_acquire(_lock_t *lock);
extern int  _lock_try_acquire_recursive(_lock_t *lock);
extern void _lock_release(_lock_t *lock);
extern void _lock_release_recursive(_lock_t *lock);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_HAL_STUB_SYS_LOCK_H */
