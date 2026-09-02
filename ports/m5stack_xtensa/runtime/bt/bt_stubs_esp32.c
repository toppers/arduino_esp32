/*
 *  無印ESP32(Xtensa LX6) BLE(W2)専用スタブ／グルー
 *
 *  esp32 BTコントローラ(bt/controller/esp32：BR/EDR+BLEデュアルモード)の
 *  osi_funcs とblobが参照するが、S3(c3-family)コントローラには無かった
 *  esp32固有シンボルを補う。全て TOPPERS_ESP32_LX6 ガード＝S3ビルドには不参加。
 *
 *  設計対応：
 *   - xt_set_interrupt_handler/xt_ints_on … esp32コントローラの割込み登録経路。
 *     S3コントローラは esp_intr_alloc(bt_shim.c)を使うが、esp32は osi_funcs の
 *     _set_isr=xt_set_interrupt_handler / _ints_on=xt_ints_on でCPU割込み線へ
 *     直接インストールする。FMP3 arch の既存プリミティブ(esp_shim_set_isr /
 *     ena_int)へ薄く写像する（bt_shim の esp_intr_alloc と同じ配線ロジック）。
 *   - DPORT割込みマトリクス(source→CPU線)の配線は esp32 では blob/コントローラ
 *     内部(intr_matrix_set)が行い、ここは CPU線へのハンドラ設置＋許可のみ。
 */
#ifdef TOPPERS_ESP32_LX6

#include <kernel.h>
#include <stdint.h>
#include <stddef.h>
#include <t_syslog.h>
#include <stdint.h>
#include "esp_shim.h"			/* esp_shim_set_isr */
#include "target_timer.h"		/* TOPPERS_ESP32_CPU_FREQ_MHZ */

typedef int esp_err_t;
#ifndef ESP_OK
#define ESP_OK 0
#endif
typedef void (*xt_handler)(void *);

/*
 *  xt_set_interrupt_handler：CPU割込み番号 n にハンドラ f(arg) を設置する。
 *  戻り値は旧ハンドラ（esp32コントローラは使わないので NULL 可）。
 */
xt_handler
xt_set_interrupt_handler(int n, xt_handler f, void *arg)
{
	esp_shim_set_isr((int32_t) n, (void *) f, arg);
	return(NULL);
}

/*
 *  xt_ints_on：mask で指定されたCPU割込み線を許可する。
 *  FMP3 の ena_int() は1本ずつなので、立っているビットを走査して許可する。
 */
/*  何を許可しようとしたか。プローブ(-DTOPPERS_BT_INTR_PROBE)が読む。 */
volatile uint32_t	bt_probe_ints_on_mask;
volatile uint32_t	bt_probe_ints_on_calls;
volatile int32_t	bt_probe_ena_ercd[32];
volatile uint32_t	bt_probe_intenable_after;


void
xt_ints_on(unsigned int mask)
{
	unsigned int	i;

	bt_probe_ints_on_mask |= mask;
	bt_probe_ints_on_calls++;
	for (i = 0U; i < 32U; i++) {
		if ((mask & (1UL << i)) != 0U) {
			ER	ercd;

			ercd = ena_int((INTNO) i);

			bt_probe_ena_ercd[i] = (int32_t) ercd;
			__asm__ __volatile__ ("rsr.intenable %0"
								  : "=a" (bt_probe_intenable_after));
			/*  ★旧実装はこの戻り値を捨てていた。ena_int() は cfg が
			 *  宣言していない線では失敗し、コントローラは黙って待ち続ける。
			 *  2026-09-02 に線 29 がこれで有効化されていなかった。 */
			if (ercd < 0) {
				syslog_2(LOG_ERROR,
						 "bt: ena_int(%d) failed (%d); declare it in esp_shim.cfg",
						 (intptr_t) i, (intptr_t) ercd);
			}
		}
	}

}

/*
 *  xPortCanYield：現コンテキストで即時yield可能か。タスク文脈なら可、
 *  非タスク(ISR/CPU例外/初期化)文脈なら不可。sns_ctx()==TRUE で非タスク。
 */
int
xPortCanYield(void)
{
	return(sns_ctx() == false);
}

/*
 *  esp_clk_cpu_freq：CPUクロック[Hz]。W0で240MHz化済み。
 */
int
esp_clk_cpu_freq(void)
{
	return((int)(TOPPERS_ESP32_CPU_FREQ_MHZ * 1000000));
}

/*  シャットダウンハンドラ登録（BTブリングアップでは未使用の no-op）。 */
esp_err_t
esp_register_shutdown_handler(void (*handle)(void))
{
	(void) handle;
	return(ESP_OK);
}

esp_err_t
esp_unregister_shutdown_handler(void (*handle)(void))
{
	(void) handle;
	return(ESP_OK);
}

/*
 *  s_wifi_mac_time_update_cb：PHY(phy_init.o)がWi-Fi MAC時刻補正用に参照する
 *  コールバック。BLE単独ではWi-Fiが無いので NULL。W1ではesp_wifi_adapter.cが
 *  供給していたが、BLEビルドは同ファイルを含まないため本スタブで補う。
 */
void (*s_wifi_mac_time_update_cb)(uint32_t) = NULL;

/*
 *  hci_per_inq_mode_cmd_handler：デュアルモードblob(lm_task)がBR/EDR周期
 *  問合せ(Periodic Inquiry Mode)HCIコマンド用に無条件参照する。BLE単独では
 *  到達しない。未サポート(0)を返すスタブ。
 */
int
hci_per_inq_mode_cmd_handler(void *p1, void *p2)
{
	(void) p1; (void) p2;
	return(0);
}

#endif	/* TOPPERS_ESP32_LX6 */
