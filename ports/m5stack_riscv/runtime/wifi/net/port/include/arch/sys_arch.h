/*
 *  lwIP sys_arch型定義（TOPPERS/FMP3用．NO_SYS=0）
 *
 *  2026-07-24 訂正: 「ASP3用」は誤り（arch/cc.h で 2026-07-22 に訂正済みの
 *  ものと同じ、祖先の ESP32-C3/ASP3 ポートから残ったコメント）。本ポートの
 *  カーネルは **FMP3** である。以下の「ASP3」も同様に FMP3 と読むこと
 *  （ID が int_t・TMIN_xxxID=1 以上という性質はどちらも同じなので、
 *  記述の中身自体は有効）。
 *
 *  sys_sem_t／sys_mbox_t／sys_thread_tはFMP3のID（int_t）と同じ表現を
 *  持つ素のintとして定義する（<kernel.h>はここではincludeしない．
 *  lwip/sys.h経由でほぼ全lwIP翻訳単位に波及するため，実体（kernel.h
 *  依存の関数群）はport/sys_arch.c側にのみ閉じ込める）．
 *  0はFMP3の有効ID範囲外（TMIN_xxxID=1以上）のため無効値として使う．
 *
 *  LWIP_COMPAT_MUTEX=1（lwipopts.h）のため，sys_mutex_tはlwip/sys.hが
 *  sys_sem_tへ自動的にエイリアスする（本ヘッダでの定義は不要）．
 */
#ifndef LWIP_ARCH_SYS_ARCH_H
#define LWIP_ARCH_SYS_ARCH_H

typedef int sys_sem_t;
typedef int sys_mbox_t;
typedef int sys_thread_t;

#define SYS_SEM_NULL   ((sys_sem_t) 0)
#define SYS_MBOX_NULL  ((sys_mbox_t) 0)

#endif /* LWIP_ARCH_SYS_ARCH_H */
