/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  クラッシュ/ハング診断用の常設recorder（無印ESP32 / Xtensa LX6 / FMP3 用）
 *
 *  背景：WiFiバイナリblob内の境界外書込みバグ（パッチ不可）でカーネル
 *  データが破壊されフリーズする問題がある（esp/debug/JTAG_DEBUG.md
 *  追記41〜47）。JTAGに頼らずハング直前の状態を事後回収するため、
 *  リセット/再起動を跨いで消えないDRAM領域（.diag_noinit、実体は
 *  target/esp32s3_devkitc_gcc/esp32s3_xip.ldで定義）へ、
 *    (1) 軽量イベントリング（diag_event）
 *    (2) per-coreハートビート（diag_heartbeat）
 *    (3) パニック/致命例外時のCPUスナップショット（diag_panic_snapshot）
 *  を記録し、次回起動時にダンプする（diag_boot_check_and_dump）。
 *
 *  設計上の制約：
 *  - 記録API（diag_event/diag_heartbeat/diag_panic_snapshot）はロック
 *    フリー（短い割込み禁止区間のみ）・printf/syslog/カーネルAPI非依存。
 *    IRAM/SRAM常駐コードのみで完結する（WiFi blobのバグが割込み文脈や
 *    カーネルデータ破壊直後に発生し得るため、これらに依存しない経路が
 *    必要）。
 *  - ダンプ処理（diag_boot_check_and_dump）だけはsyslogを使ってよい
 *    （カーネル起動の初期化ルーチン内で呼ばれ、カーネルオブジェクトは
 *    既に初期化済みの文脈）。
 *
 *  永続化領域(.diag_noinit)は実機flash-XIPビルド（esp32s3_xip.ld、
 *  ESP_PLATFORM定義下）でのみ有効。QEMU/実機カーネルオブジェクトテスト
 *  ビルド（esp32s3_devkitc.ld、ESP_PLATFORM未定義）では通常の.bss上の
 *  変数にフォールバックし、記録API自体は同じコードパスで安全に動作する
 *  （cross-reset永続性は無いが、機能・ビルドは影響を受けない）。
 */

#ifndef TOPPERS_DIAG_RECORDER_H
#define TOPPERS_DIAG_RECORDER_H

#include <stdint.h>

#ifndef TOPPERS_MACRO_ONLY

/*
 *  イベントID（診断リング用、8bit）
 */
#define DIAG_EV_NONE            0x00U  /* 未使用エントリ */
#define DIAG_EV_BOOT            0x01U  /* diag_boot_check_and_dumpが新epoch開始時に書く先頭マーカ */
#define DIAG_EV_SEM_TAKE        0x02U  /* esp_shim_sem_take */
#define DIAG_EV_SEM_GIVE        0x03U  /* esp_shim_sem_give */
#define DIAG_EV_Q_SEND          0x04U  /* esp_shim_queue_send */
#define DIAG_EV_Q_SEND_ISR      0x05U  /* esp_shim_queue_send_from_isr */
#define DIAG_EV_Q_RECV          0x06U  /* esp_shim_queue_recv */
#define DIAG_EV_NET_TX          0x07U  /* netif low_level_output */
#define DIAG_EV_NET_RX          0x08U  /* netif wifi_rx_cb */
#define DIAG_EV_PBUF_ALLOC_FAIL 0x09U  /* netif wifi_rx_cb: pbuf_alloc失敗 */

/*
 *  軽量イベント記録（ロックフリー、割込み禁止の短区間のみ）
 *
 *  id:   上記DIAG_EV_*
 *  arg1/arg2: イベント固有の引数（ポインタや長さ等、呼び出し側で決める）
 *
 *  printf/syslog/カーネルAPIには依存しない。IRAM/SRAM常駐。
 */
extern void diag_event(uint8_t id, uint32_t arg1, uint32_t arg2);

/*
 *  per-coreハートビート（tickハンドラから1ストアのみ呼ばれる想定）
 *
 *  自コアの「最終生存tick」（CCOUNT下位32bit）を診断領域へ書く。
 */
extern void diag_heartbeat(void);

/*
 *  パニック/致命例外時のCPUスナップショット保存
 *
 *  p_excinfは実際にはconst T_EXCINF*（core_kernel.h）だが、本ヘッダは
 *  kernel_impl.hに依存しないためvoid*で受ける（core_kernel_impl.c内で
 *  キャストする）。呼び出し側はEXCVADDR/WINDOWBASE/WINDOWSTART等
 *  T_EXCINFに含まれないSFRも読むため、例外発生直後（他の例外を挟まない
 *  時点）で呼ぶこと。
 *
 *  割込み禁止・最小処理・printf非依存。呼び出し後の既存panic挙動
 *  （停止ループ等）は変更しない。
 */
extern void diag_panic_snapshot(const void *p_excinf);

/*
 *  ブート時の診断領域チェック・前回内容ダンプ・再初期化
 *
 *  ATT_INI（target_diag.cfg）から呼ばれる（マスタプロセッサのみ、
 *  カーネルオブジェクト初期化済みの文脈）。syslogを使ってよい。
 */
extern void diag_boot_check_and_dump(intptr_t exinf);

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_DIAG_RECORDER_H */
