/*
 *  esp-esp/esp-event/mbedtls用の"platform/os.h"コンパイル用スタブ
 *
 *  esp-hal-3rdpartyには本ヘッダが2種類実在する：
 *    - components/esp_system/include/platform_port/platform/os.h
 *      （ESP-IDF本来版．FreeRTOSヘッダ一式に依存）
 *    - nuttx/include/platform/os.h
 *      （NuttXポート版．NuttXカーネル内部ヘッダ<nuttx/clock.h>
 *        <nuttx/sched.h><sched/sched.h>等に依存）
 *  本リポジトリはFreeRTOSもNuttXカーネルも同梱しない（esp-hal-3rdparty
 *  はhal/soc等RTOS非依存の下層のみを使う方針．docs/dev/esp-idf-
 *  integration.md参照）ため，どちらも採用できない．
 *
 *  実体（OSキュー・スピンロック等）はPhase B-2のos_adapter shimで
 *  ASP3のAPI（loc_cpu/get_tim/snd_dtq等）に接続する．本スタブは
 *  「コンパイルを通す」ためだけの宣言（シンボルの型・マクロのみ）。
 */
#ifndef TOPPERS_HAL_STUB_PLATFORM_OS_H
#define TOPPERS_HAL_STUB_PLATFORM_OS_H

#include <stddef.h>
#include <stdint.h>

/*
 *  esp_wpa3.c等が期待する戻り値（NuttX版os.hのOS_PASS/OS_FAILに相当）。
 *  OS_BLOCKはwpa_supplicant/port/include/os.hのOSI_FUNCS_TIME_BLOCKING
 *  経由で別途定義されるため，ここでは重複定義しない。
 */
#define OS_PASS   1
#define OS_FAIL   0

/*
 *  esp_task.h（Bluetooth統合．Phase D-1）が
 *  ESP_TASK_BT_CONTROLLER_PRIO等をOS_TASK_PRIO_MAX基準で計算するために
 *  参照する．ESP-IDF既定のconfigMAX_PRIORITIES(=25)相当を採用する
 *  （本ビルドは実際にはesp_shim.cの固定優先度プールへ委譲するため，
 *  この値自体はマクロ計算を成立させるためだけに使われる）。
 */
#define OS_TASK_PRIO_MAX  25

/*
 *  esp_private/wifi.h（esp_os_queue_handle_t等の型のみ．
 *  実体はesp_private/wifi_os_adapter.h側のwifi_osi_funcs_tが握る）
 */
typedef void *esp_os_queue_handle_t;
typedef uint32_t esp_os_tick_type_t;
typedef void *esp_os_intr_handle_t;
typedef void (*esp_os_intr_handler_t)(void *arg);

/*
 *  esp_event.hはTickType_t/UBaseType_t/BaseType_tを本来FreeRTOSの
 *  型として使うが，`#ifdef __NuttX__` 節でのみNuttX向け代替定義
 *  （uint32_t/uint32_t/int32_t）を用意している．本ビルドは__NuttX__を
 *  定義しない（esp_mbedtls.hのNuttX向けシンボルリネームを誘発し，
 *  mbedtls実体のシンボル名と不整合を起こすため．詳細はesp_wifi.cmake
 *  コメント参照）ので，同じ代替定義をここで肩代わりする。
 */
typedef uint32_t TickType_t;
typedef uint32_t UBaseType_t;
typedef int32_t  BaseType_t;

/*
 *  esp_private/critical_section.h（esp_hw_support/periph_ctrl.c等が使う
 *  クリティカルセクションマクロ）向けのシングルコア実装。
 *  OS_SPINLOCK を定義しないため（本ビルドはESP32-C3単一コア），
 *  同ヘッダは自動的に「スピンロック無し＝割込み禁止のみ」の
 *  _NO_LOCK系マクロを選択する（NuttX版platform/os.hの
 *  nuttx_enter_critical()と同じ発想）。実体はASP3のloc_cpu/unl_cpu
 *  （ネストは呼び出し側の対構造で保証される想定．periph_ctrl.c内の
 *  各関数は enter〜exit を必ず対で閉じたブロックとして使うため十分）。
 */
#ifndef TOPPERS_MACRO_ONLY
/*
 *  <kernel.h>（<t_stddef.h>経由）はassert()を「TOPPERS_assert_fail()＋
 *  TOPPERS_assert_abort()を呼ぶ版」に（再）定義する．本ヘッダを
 *  esp_err.h（<assert.h>＝hal_stub/include/assert.hのno-op版を
 *  #include済み）より後に取り込むhal/配下のファイル（例：
 *  esp_phy/src/phy_init.c）では，この再定義が以降のassert()呼び出し
 *  （phy_init.c自身のassert()や，esp_cpu.h等が使うHAL_ASSERT経由の
 *  assert()）に伝播し，t_syslog.hを#includeしていないTU（TOPPERS_
 *  assert_failがマクロ化されず素の関数参照になる＝ASP3側で実体を
 *  持たない）ではリンクエラーになる．push_macro/pop_macroで
 *  <kernel.h>取り込み前後のassert()定義を保存・復元し，hal/側コード
 *  からは常にno-op版のassert()が見えるようにする（既にassert()が
 *  定義されている場合のみ．未定義の場合はTOPPERS版をそのまま採用＝
 *  ASP3の他ファイルからの利用と同じ挙動）．
 */
#ifdef assert
#pragma push_macro("assert")
#define TOPPERS_HAL_STUB_OS_H_SAVED_ASSERT 1
#endif
#include <kernel.h>
#ifdef TOPPERS_HAL_STUB_OS_H_SAVED_ASSERT
#pragma pop_macro("assert")
#undef TOPPERS_HAL_STUB_OS_H_SAVED_ASSERT
#endif
/*
 *  2026-07-30（#47 の対処・LX6 側）
 *
 *  【旧実装の問題】6 つとも `loc_cpu()` / `unl_cpu()` の直写像だった。
 *   `unl_cpu()` は**保存値を復元しない**——FMP3 の実装は
 *     `if (sense_lock() && p_locspn == NULL) unlock_cpu();` であり、
 *     `sense_lock()` は **PS.INTLEVEL != 0 をハードから読む**
 *     （`fmp3/arch/xtensa_gcc/common/core_kernel_impl.h:197-202`）。
 *   ⇒ **呼出し時に PS.INTLEVEL != 0 だと `unlock_cpu()`＝`rsil 0` が走り、
 *     呼出し元が張っていたマスクが途中で解ける。**
 *   ⇒ これは 2026-07-26 に PSRAM で根治した欠陥（`__atomic_fetch_*_8` が
 *     `unl_cpu` で INTLEVEL を 15→0 に落とす）と**同じ型**である。
 *
 *  【機序の訂正（2026-07-30 後追い・統合レビュー 2 の B-1）】
 *   **旧コメントは「ISR（`rsil 15` で入る）から呼ぶと発火する」と書いていたが、
 *     これは誤りである。**本ポートの L1/L3 割込みは、asm プロローグで
 *     `rsil 15` した後、**C ハンドラを INTLEVEL=0 で呼ぶ**
 *     （`fmp3/arch/xtensa_gcc/common/core_support.S:707`＝L1・`:1008`＝L3 の擬似 call4。
 *      根拠は `signal_time` 側の `assert(!sense_lock())` を満たす必要があること＝
 *      同 `:677` のコメント。再入防止は INTLEVEL ではなく **INTENABLE** で行う）。
 *   ⇒ **C で書かれた ISR 本体から呼んでも `sense_lock()` は偽であり、この欠陥は発火しない。**
 *     （`m5/compat/freertos/queue.h:123-131` に、同じ事実を敵対的検証で確かめた記録がある。）
 *   **実際の発火条件は「呼出し時に PS.INTLEVEL != 0」**＝
 *     **他の critical section の中・CPU ロック中からの呼出し**である。
 *   **`unl_cpu()` が退避値を復元しない非ネスト型である**という指摘そのものは**正**であり、
 *     **「潜在」という判定と本対処（写像先の変更）も不変**である。
 *     変わったのは**発火条件の書き方だけ**。
 *
 *  【実測（重要）】**この 6 マクロは現時点で 1 箇所も呼ばれていない。**
 *   `esp_phy` の `phy_enter_critical`/`phy_exit_critical`（`esp/wifi/hal_src/phy_init.c:232-252`）は
 *   `esp_os_enter_critical{,_isr}` を使い、`gcc -E` で展開を確かめると
 *   **両分岐とも `esp_shim_bt_enter_critical()` へ落ちる**（`os.h` のマクロは通らない）。
 *   ⇒ **現行バグではなく潜在**である。**マクロ名から意味を推測せず展開を見ること。**
 *
 *  【対処】**既に正しい実装へ写像する。**`esp_shim_bt_enter_critical/exit_critical` は
 *   「最外で退避・復元＋ネスト計数」であり（`esp/shim/esp_shim_bt_crit_wifi.c:22-40`＝Wi-Fi 構成、
 *   `esp/bt/bt_shim.c:218-290`＝BT/BLE 構成）、ISR から呼んでも INTLEVEL を落とさない。
 *   呼出し元が無いので **バイナリは 1 バイトも変わらない**（実測で確認する）。
 */
extern void	esp_shim_bt_enter_critical(void *mux);
extern void	esp_shim_bt_exit_critical(void *mux);

#define OS_ENTER_CRITICAL_NO_LOCK()         (esp_shim_bt_enter_critical(NULL))
#define OS_EXIT_CRITICAL_NO_LOCK()          (esp_shim_bt_exit_critical(NULL))
#define OS_ENTER_CRITICAL_NO_LOCK_ISR()     (esp_shim_bt_enter_critical(NULL))
#define OS_EXIT_CRITICAL_NO_LOCK_ISR()      (esp_shim_bt_exit_critical(NULL))
#define OS_ENTER_CRITICAL_NO_LOCK_SAFE()    (esp_shim_bt_enter_critical(NULL))
#define OS_EXIT_CRITICAL_NO_LOCK_SAFE()     (esp_shim_bt_exit_critical(NULL))

/*
 *  OS_IN_ISR()：esp_phy/src/phy_init.cのphy_enter_critical/
 *  phy_exit_critical（タスク文脈か非タスク文脈かでISR版クリティカル
 *  セクションマクロを使い分ける）が要求する．ASP3のsns_ctx()
 *  （TOPPERS標準API．非タスクコンテキスト＝割込みハンドラ内で真）を
 *  そのまま使う．
 */
#define OS_IN_ISR()                         (sns_ctx())
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_HAL_STUB_PLATFORM_OS_H */
