/*
 *  M5 表示スモーク（ESP32-S3 CoreS3 / FMP3）— 段階1（1-4）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

#ifndef M5_DISPLAY_SMOKE_H
#define M5_DISPLAY_SMOKE_H

#include <kernel.h>

#define MAIN_PRIORITY	10
#define STACK_SIZE		8192
/*
 *  ★音声 worker タスク（M5_AUDIO_TSK1/2）専用のスタックサイズ。
 *  【なぜ MAIN と分けるか】DRAM が足りないから。音声を有効にすると
 *  `.kernel_bss` が **3,944 バイト**溢れた。MAIN と同じ 8,192 を 2 本取ると
 *  16,384 バイトになるが、実際に要るのは M5Unified の計算式
 *  `1280 + dma_buf_len * 4`（既定 dma_buf_len=256 なら **約 2.3 KB**）である。
 *  ⇒ 4,096 なら約 1.8 倍の余裕があり、2 本で 8,192 バイト空く（不足分の倍以上）。
 *  ★`frame_buf`（153,600 B）を消す案もあったが、**カメラの解析判定が依存している**ので
 *  こちらを先に選んだ（小さい変更で足りるならそちらを採る）。
 *  ★要求より小さければ `esp_shim_task_create` が警告を出す（黙って溢れさせない）。
 */
#define M5_AUDIO_STACK_SIZE	4096

/*  M5GFX は単一コア固定（SHIM_LOCK 規約）。本アプリは PRC1 の 1 タスクのみ。 */

#ifndef TOPPERS_MACRO_ONLY
extern void main_task(EXINF exinf);
/*  ★音声 worker タスクの共通エントリ（cfg の M5_AUDIO_TSK1/2 から。
 *  実体は m5/shim/m5_kernel_shim.c）。exinf = プールのスロット番号。 */
extern void m5_audio_task_entry(EXINF exinf);

/*
 *  ★1-12：スピン中 SPI レジスタ ダンプ用の周期ハンドラ（CRE_CYC M5_SPIN_CYC）。
 *  非タスク文脈から target_fput_log 直で出力する（安全性の根拠は
 *  m5_display_smoke.c の当該コメント参照）。armed 中のみ動作する。
 */
extern void m5_spin_cyc_handler(EXINF exinf);
extern void m5_spin_probe_arm(void);
extern void m5_spin_probe_disarm(void);

/*
 *  ★G-1：ESP-IDF 割込み確保シムの ISR（esp/shim/esp_shim_intr.c）。
 *  cfg の CRE_ISR(ISR_ESPINTR0..3) が参照するので、生成された kernel_cfg.c から
 *  見えるようにここで宣言する。exinf にスロット番号(0..3)が入る。
 */
extern void esp_shim_intr_isr(EXINF exinf);

/*  ★AC-G6：同一 intno（線 4）に載せる 2 本目の ISR。実体は m5_gdma_m2m.c。
 *  シムとは無関係な独立関数であることが本試験の要点。 */
extern void m5_chain_probe_isr(EXINF exinf);

/*  即時出力ログ（logtask 非経由・target_fput_log 直）と WDT read-back。
 *  ★実体は m5_diag.c（seam アプリと QEMU ハーネスで共有する診断 TU）。 */
extern void m5_log_now(const char *msg);
extern void m5_log_now_u32(const char *msg, unsigned int v);
extern void m5_wdt_dump(const char *tag);

/*  到達点マーカ（スタブの入口/出口ブラケット・1 行・即時出力）。 */
extern void m5_mark(const char *tag, unsigned int id);
extern void m5_mark_u32(const char *tag, unsigned int id, unsigned int v);
/*  出力しない到達点マーカ（ホットパス用。周期ハンドラが最後の id/seq を再出力）。 */
extern void m5_ckpt_set(unsigned int id);
/*  ★1-15：出力を一切伴わない到達点マーカ（GPIO スタブ等のホットパス用）。 */
extern void m5_ckpt_set_quiet(unsigned int id);
/*  ★1-15：診断出力の打ち切り回数／観測ピン(GPIO17,18)保護の発動回数。 */
extern volatile unsigned int m5_uart_stall;
extern volatile unsigned int m5_gpio_console_guard;

/*
 *  ★1-13：割込みが生きているかを直接答えるための計装。
 *
 *  m5_log_now_ps()   … 呼んだ「その場」の PS / INTENABLE を即時出力する
 *                      （タスク文脈から呼ぶこと。ISR 文脈は INTENABLE=0 が正常）。
 *  m5_ps_snapshot()  … PS / INTENABLE と site 番号を大域へ退避するだけ（出力しない）。
 *                      タスクがスピンに入っても、周期ハンドラが最後の退避値を再出力する。
 *  m5_ctr[]          … シムの呼出し回数。周期ハンドラが毎秒ダンプするので、
 *                      「動いていないのか（固着）／回り続けているのか（ライブロック）」
 *                      をタスクへ触れずに弁別できる。
 */
extern void m5_log_now_ps(const char *tag);
extern void m5_ps_snapshot(unsigned int site);
/*  ★1-14：タスク文脈からの差分ウォッチ（[WT] 行）。INTENABLE 系 4 値と
 *  CCOMPARE0 残サイクル・tick 回数を採り、**変化したときだけ** 1 行出す。
 *  m5_ps_snapshot() から自動的に呼ばれるので、通常は直接呼ぶ必要はない。 */
extern void m5_watch_sample(void);
extern volatile unsigned int m5_ctr[];

/*
 *  ★1-17：UART に依存しない生存記録（.diag_noinit＝リセットを跨いで残る SRAM）。
 *  main_task の冒頭（UART 再ルーティング直後）で 1 回だけ呼ぶ。
 *    (1) 前回起動の到達点（task_alive / isr_alive / ckpt / GPIO / UART0 レジスタ像）
 *        を出力する。
 *    (2) 記録域の positive / negative control を実演する。
 *    (3) 新しい epoch を開始する（以後ホットパスと周期ハンドラが記録し続ける）。
 *  ★SRAM なので**電源を落とすと消える**。観測手順は「走らせる → 電源を落とさずに
 *    リセット → 次の起動で読む」。詳細は m5_diag.c の当該コメント参照。
 */
extern void m5_surv_boot_report(void);
#endif

/*  m5_ctr[] の添字（シム側は extern 宣言だけで参照する）。 */
#define M5_CTR_MALLOC		0	/* esp_shim_malloc（operator new の写像先） */
#define M5_CTR_FREE			1	/* esp_shim_free */
#define M5_CTR_MICROS		2	/* esp_timer_get_time（lgfx micros()/millis()） */
#define M5_CTR_YIELD		3	/* esp_shim_task_yield（lgfx の I2C 完了待ちループ） */
#define M5_CTR_DELAY		4	/* esp_shim_task_delay（vTaskDelay） */
#define M5_CTR_SEMTAKE		5	/* esp_shim_sem_take */
#define M5_CTR_SEMGIVE		6	/* esp_shim_sem_give */
#define M5_CTR_GPIO			7	/* GPIO スタブ */
#define M5_CTR_I2C			8	/* I2C スタブ */
#define M5_CTR_HEAPCAPS		9	/* heap_caps_malloc */
#define M5_CTR_NUM			10

/*  m5_ps_snapshot() の site 番号（どのシムで最後に採った PS かが分かる）。 */
#define M5_PSSITE_MARK		0x01U
#define M5_PSSITE_MALLOC	0x02U
#define M5_PSSITE_SEMTAKE	0x03U
#define M5_PSSITE_YIELD		0x04U
#define M5_PSSITE_DELAY		0x05U
#define M5_PSSITE_TASKPRE	0x06U	/* begin() 直前（タスク文脈・明示） */

extern void m5_route_u0txd_portc(void);

#endif /* M5_DISPLAY_SMOKE_H */
