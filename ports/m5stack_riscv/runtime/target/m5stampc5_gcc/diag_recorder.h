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
 *  クラッシュ/ハング診断用 recorder（M5Stamp-C5 / ESP32-C5 用・FMP3）
 *
 *  段4 Task 3（2026-09-14）: esp/shim/esp_shim.c・esp_shim_isr_ctx.c・
 *  esp/wifi/net/netif_esp32s3.c が無条件に #include "diag_recorder.h" し
 *  diag_event() を呼ぶ（S3/LX6 の target ディレクトリが持つ同名ヘッダの
 *  API）。C6 の Wi-Fi 構成でそれらをリンクするために、同じ API を
 *  target 側に置く。出典（API の形）: fmp3/target/esp32s3_devkitc_gcc/
 *  diag_recorder.h（イベント ID と 4 関数の宣言は同一）。
 *
 *  実装（diag_recorder.c）は S3 版の**縮小版**である:
 *    - イベントリング（64 エントリ）は通常の .bss に置く。リセットを跨ぐ
 *      永続化（S3 の .diag_noinit セクションと checksum/magic 検証）は
 *      **行わない**（Low#3。esp32c5_xip.ld にその領域を切っていない）。
 *    - diag_panic_snapshot() は呼出し回数と p_excinf の番地だけを残す
 *      （S3 版の Xtensa SFR スナップショットは C6 には無い。Low#4）。
 *    - diag_boot_check_and_dump() は ATT_INI に繋いでいない（永続化が
 *      無いので「前回の内容」が存在しない。Low#5）。
 *  リング自体は動く（JTAG/gdb で diag_region を読める）。
 */

#ifndef TOPPERS_DIAG_RECORDER_H
#define TOPPERS_DIAG_RECORDER_H

#include <stdint.h>

#ifndef TOPPERS_MACRO_ONLY

/*
 *  イベントID（S3/LX6 版と同じ値）
 */
#define DIAG_EV_NONE            0x00U
#define DIAG_EV_BOOT            0x01U
#define DIAG_EV_SEM_TAKE        0x02U  /* esp_shim_sem_take */
#define DIAG_EV_SEM_GIVE        0x03U  /* esp_shim_sem_give */
#define DIAG_EV_Q_SEND          0x04U
#define DIAG_EV_Q_SEND_ISR      0x05U
#define DIAG_EV_Q_RECV          0x06U
#define DIAG_EV_NET_TX          0x07U  /* netif low_level_output */
#define DIAG_EV_NET_RX          0x08U  /* netif wifi_rx_cb */
#define DIAG_EV_PBUF_ALLOC_FAIL 0x09U  /* netif wifi_rx_cb: pbuf_alloc 失敗 */
#define DIAG_EV_USJ_DROP        0x0AU

extern void diag_event(uint8_t id, uint32_t arg1, uint32_t arg2);
extern void diag_heartbeat(void);
extern void diag_panic_snapshot(const void *p_excinf);
extern void diag_boot_check_and_dump(intptr_t exinf);

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_DIAG_RECORDER_H */
