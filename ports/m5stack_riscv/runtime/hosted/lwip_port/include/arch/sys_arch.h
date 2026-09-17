/*
 *  ESP32-P4 Wi-Fi モジュール対応 - lwIP sys_arch FMP3 実装
 *  arch/sys_arch.h : ポート固有の型定義
 *
 *  lwIP の `lwip/sys.h` が `#include "arch/sys_arch.h"` する、ポートが必ず
 *  提供すべきヘッダ。ここでは sys_sem_t 等の「型」だけを定義し、関数の
 *  プロトタイプ自体は `lwip/sys.h` 側が宣言する。
 *  【2026-07-10 方式(D)統合済み】現在は実 lwIP ヘッダ本体を使用しており、
 *  上記の「`lwip/sys.h` 側が宣言する」は文字どおり本物の lwip/sys.h を指す。
 *  当初（フェーズ0、lwIP 本体ソース未取得の段階）は `_lwip_min_shim.h` が
 *  代わりに前方宣言していたが、同シムは統合完了で不要となり 2026-07-16 に
 *  削除済み（`sys_arch.c` 冒頭のコメント参照）。
 *
 *  【設計】各ハンドル型は `fmp3_lwip_pools.c` の静的プール要素（配列の1枠）を
 *  指す不透明ポインタ（`void *`）とする。os_adapter
 *  (`fmp3_hosted_osi.h` の `_h_create_*` 系)と同じ「ハンドル=プール要素への
 *  ポインタ」方式であり、無効値は NULL で表す。
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  sys_sem_t   : fmp3_lwip_pools.c のセマフォプール要素へのポインタ
 *  sys_mbox_t  : 同メールボックス(dtq)プール要素へのポインタ
 *  sys_mutex_t : 同ミューテックスプール要素へのポインタ
 *  sys_thread_t: 同タスクプール要素へのポインタ
 *
 *  いずれも中身の構造体定義は fmp3_lwip_pools.c 内に閉じており（外部非公開）、
 *  sys_arch.c 側は fmp3_lwip_pools.h が公開する `void *` ベースの操作関数
 *  経由でのみアクセスする。よってここでは `void *` として型定義するだけで
 *  よい（実体の構造体を知る必要が無い）。
 */
typedef void *sys_sem_t;
typedef void *sys_mbox_t;
typedef void *sys_mutex_t;
typedef void *sys_thread_t;

/**
 *  sys_prot_t : sys_arch_protect()/sys_arch_unprotect() の間で受け渡す
 *  「以前の CPU ロック状態」。sys_arch.c 実装参照。
 *  値は 0（保護前は非ロック状態だった＝このprotect呼び出しが本当にロックを
 *  取った）／ 1（保護前から既にロック済みだった＝ネストした呼び出しなので
 *  このunprotectでは実際にはアンロックしない）のいずれか。
 */
typedef int sys_prot_t;

/** NULL 相当の無効ハンドル値（os_adapter 同様、ポインタの NULL で表現）。
 *  実 lwip/sys.h は SYS_MBOX_NULL 等を必須にはしていないが、デバッグ時の
 *  可読性のために定義しておく。 */
#define SYS_MBOX_NULL	((sys_mbox_t) NULL)
#define SYS_SEM_NULL	((sys_sem_t) NULL)

#ifdef __cplusplus
}
#endif

#endif /* LWIP_ARCH_SYS_ARCH_H */
