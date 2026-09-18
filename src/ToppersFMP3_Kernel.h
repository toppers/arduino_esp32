#ifndef TOPPERS_FMP3_KERNEL_H
#define TOPPERS_FMP3_KERNEL_H

/*
 *  スケッチから使える FMP3 カーネル API（薄いラッパの宣言）
 *
 *  実体は board platform の minimal stage にある
 *  （ports の各 runtime/arduino/arduino_kernel_api.c）。**Minimal 構成専用**で、
 *  他の FMP3 Runtime を選ぶと未定義シンボルになる。
 *
 *  ★カーネルの型（ID / ER / T_CTSK …）はここに出さない。スケッチからは
 *    kernel.h が見えず（platform の include 路に入っていない）、見せると
 *    カーネルの ABI が配布物の公開面になって fmp3_core の更新で利用者の
 *    スケッチが壊れうるため。素の C の型だけで足りる形にしてある。
 *
 *  ★戻り値は FMP3 の ER をそのまま返す（0 = E_OK、負 = エラー）。
 *    タスク生成だけは成功時に**タスク ID（正）**を返す。
 *
 *  ★時間の単位はマイクロ秒。このポートの RELTIM がマイクロ秒で、
 *    FreeRTOS の tick とも素の TOPPERS の慣行とも違うため、名前にも書いた。
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t toppers_fmp3_task_create(void (*body)(intptr_t), intptr_t exinf,
                                 int32_t priority, void *stack,
                                 size_t stack_size);
int32_t toppers_fmp3_task_activate(int32_t task_id);
int32_t toppers_fmp3_task_wakeup(int32_t task_id);
int32_t toppers_fmp3_task_sleep(void);
int32_t toppers_fmp3_task_delay_us(uint32_t micro_seconds);
int32_t toppers_fmp3_task_self(int32_t *task_id);
int32_t toppers_fmp3_task_change_priority(int32_t task_id, int32_t priority);

/*  周期は cfg で固定（変更する API は TOPPERS に無い）。 */
#define TOPPERS_FMP3_CYCLIC_PERIOD_US 1000000U

int32_t toppers_fmp3_cyclic_start(void (*callback)(void));
int32_t toppers_fmp3_cyclic_stop(void);
int32_t toppers_fmp3_alarm_start(void (*callback)(void), uint32_t after_us);
int32_t toppers_fmp3_alarm_stop(void);

#ifdef __cplusplus
}
#endif

#endif  /* TOPPERS_FMP3_KERNEL_H */
