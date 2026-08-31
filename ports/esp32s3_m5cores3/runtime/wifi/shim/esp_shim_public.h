/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  esp_shim 公開ヘッダ（外部コンシューマ向けの集約エントリポイント）
 *
 *  背景：
 *    M5ライブラリ資産（M5GFX/M5Unified）や freertos 互換ヘッダ層
 *    （m5/compat/freertos/）は，同期プリミティブ・ヒープ・タスク遅延
 *    等を **既存の esp_shim_* 実装へ写像** して利用する（新しいプリミティブ
 *    は発明しない）。その写像先である esp_shim の公開 API を，1本の
 *    「公開ヘッダ」に集約して外部から include できるようにする。
 *
 *  方針（★重要）：
 *    - 本ヘッダは **宣言の集約のみ**。esp_shim.c 等の実装は一切変更しない。
 *    - Cコンシューマ：実プロトタイプの二重管理（＝乖離）を避けるため，独自に
 *      再宣言せず既存の esp_shim.h をそのまま取り込む（single source of truth）。
 *      これにより型（size_t / uint32_t / int32_t / bool_t / TMO）と ABI が
 *      esp_shim.c の定義と機械的に一致することを保証する。esp_shim.h は
 *      <kernel.h> を取り込むため，本ヘッダを include する C 翻訳単位は
 *      FMP3 カーネルヘッダの探索パス上でビルドされる前提（既存の BT スタブ
 *      esp/bt/stub/include/freertos/task.h も <kernel.h> を include するのと
 *      同じ運用）。
 *
 *    - ★C++コンシューマ：<kernel.h>→<t_stddef.h> は
 *      `#define alignof(type) ...` / `#define offsetof(type, member) ...`
 *      という**関数マクロ**を持つ（C99以前互換のための定義）。C++では
 *      `alignof` は**言語キーワード**であり，マクロで上書きされると
 *      `<type_traits>`（`std::alignment_of` 等）を含む C++ 標準ヘッダの
 *      **構文が壊れる**。
 *      そのため **C++ 翻訳単位では esp_shim.h（→kernel.h）を include せず**，
 *      POD 型（void* / size_t / uint32_t / int32_t / int64_t / bool_t=int /
 *      関数ポインタ）のみで構成される esp_shim.h の公開シグネチャを
 *      `extern "C"` で個別に前方宣言する。esp_shim.h 非公開の内部型
 *      （kernel.h 由来の ID / PRI 等）を要求する API は存在しないため，
 *      この前方宣言だけで ABI・型とも esp_shim.c の実装と一致する。
 *      ★esp_shim.h の当該宣言を変更した場合は，本ブロックも追随させること
 *      （二重管理になるが，C++ 側で kernel.h を回避する以上ここは避けられない）。
 *
 *  公開している主な API（詳細な契約は esp_shim.h の各宣言のコメント参照）：
 *    - ヒープ：      esp_shim_malloc / calloc / realloc / free
 *                    （operator new/delete の写像先。m5/compat/cxx_runtime.cpp）
 *    - タスク遅延：  esp_shim_task_delay（tick=1ms。vTaskDelay の写像先）
 *    - タスク：      esp_shim_task_create / delete / yield / get_current
 *    - セマフォ：    esp_shim_sem_create / take / give / delete
 *    - ミューテックス：esp_shim_mutex_create / lock / unlock / delete
 *    - キュー：      esp_shim_queue_create / send / recv / delete
 *    - 割込み禁止：  esp_shim_int_disable / restore
 *                    （taskENTER/EXIT_CRITICAL の写像先。コア0固定規約）
 *    - 時刻：        esp_shim_time_us
 */

#ifndef ESP_SHIM_PUBLIC_H
#define ESP_SHIM_PUBLIC_H

#ifdef __cplusplus

/*
 *  C++ 翻訳単位向け：esp_shim.h（→kernel.h→t_stddef.h の alignof/offsetof
 *  マクロ汚染）を回避するための前方宣言。esp_shim.h:50-146 の該当宣言から
 *  機械的に転記（1-4 実測時点）。bool_t は t_stddef.h で
 *  `#define TOPPERS_bool int` / `typedef TOPPERS_bool bool_t` と定義される
 *  単純な int 別名であることを確認済み。
 */
#include <stdint.h>
#include <stddef.h>

typedef int bool_t;

extern "C" {

/* ヒープ（operator new/delete の写像先。m5/compat/cxx_runtime.cpp） */
extern void *esp_shim_malloc(size_t size);
extern void *esp_shim_calloc(size_t n, size_t size);
extern void *esp_shim_realloc(void *ptr, size_t size);
extern void esp_shim_free(void *ptr);

/* セマフォ */
extern void *esp_shim_sem_create(uint32_t max, uint32_t init);
extern void esp_shim_sem_delete(void *sem);
extern int32_t esp_shim_sem_take(void *sem, uint32_t block_time_tick);
extern int32_t esp_shim_sem_give(void *sem);

/* ミューテックス */
extern void *esp_shim_mutex_create(bool_t recursive);
extern int32_t esp_shim_mutex_lock(void *mtx);
extern int32_t esp_shim_mutex_unlock(void *mtx);

/* キュー */
extern void *esp_shim_queue_create(uint32_t len, uint32_t item_size);
extern void esp_shim_queue_delete(void *que);
extern int32_t esp_shim_queue_send(void *que, void *item,
									uint32_t block_time_tick, bool_t to_front);
extern int32_t esp_shim_queue_send_from_isr(void *que, void *item);
extern int32_t esp_shim_queue_recv(void *que, void *item,
									uint32_t block_time_tick);
extern uint32_t esp_shim_queue_msg_waiting(void *que);
extern uint32_t esp_shim_queue_spaces_available(void *que);
extern void esp_shim_queue_reset(void *que);

/* 割込み禁止（taskENTER/EXIT_CRITICAL の写像先。コア0固定規約） */
extern uint32_t esp_shim_int_disable(void);
extern void esp_shim_int_restore(uint32_t state);

/* タスク */
extern int32_t esp_shim_task_create(void (*entry)(void *), const char *name,
									 uint32_t stack_size, void *param,
									 uint32_t freertos_prio, void **task_handle);
extern void esp_shim_task_delay(uint32_t tick);
extern void esp_shim_task_delete(void *task_handle);  /* NULL=自タスク */
extern void *esp_shim_task_get_current(void);
/*  ★FreeRTOS の task notification（M5Unified の Speaker/Mic が使う）。 */
extern uint32_t esp_shim_task_notify_take(int clear_on_exit, uint32_t timeout_ms);
extern void esp_shim_task_notify_give(void *task_handle);
extern void esp_shim_task_yield(void);

/* 時刻 */
extern int64_t esp_shim_time_us(void);   /* 起動からのμs（SYSTIMER） */

/*
 *  ログ（LOG_EMERG）：C++ 側は kernel.h 由来の syslog() を直接呼べないため，
 *  この折返し経由で顕在化させる（design.md §3：確保失敗はLOG_EMERG+abort）。
 */
extern void esp_shim_log_emerg(const char *format, ...);

/* 実行中コアの0起点ID（xPortGetCoreID の写像先） */
extern int32_t esp_shim_get_core_id(void);

} /* extern "C" */

#else /* !__cplusplus */

/* Cコンシューマ：従来どおり esp_shim.h をそのまま取り込む（single source of truth） */
#include "esp_shim.h"

#endif /* __cplusplus */

#endif /* ESP_SHIM_PUBLIC_H */
