/*
 *  Opt-in BT interrupt probe: -DTOPPERS_BT_INTR_PROBE.
 *
 *  Reports BT interrupt activity from a cyclic handler.
 *
 *  esp_bt_controller_enable() does not return, so the Arduino task is parked
 *  inside setup() and cannot print anything. A cyclic handler runs regardless,
 *  which is the only way to see whether the controller's interrupt lines are
 *  firing at all while it waits.
 */
#ifdef TOPPERS_BT_INTR_PROBE

#include <kernel.h>
#include <t_syslog.h>
#include <stdint.h>
#include "syssvc/logtask.h"

extern volatile uint32_t	esp_shim_int_count[];
extern volatile int32_t		esp_shim_bt_last_intr_source[2];
extern volatile uint32_t	esp_shim_bt_last_intr_line[2];
extern volatile uint32_t	esp_shim_bt_intr_alloc_count;
extern volatile uint32_t	bt_probe_ints_on_mask;
extern volatile uint32_t	bt_probe_ints_on_calls;
extern volatile int32_t		bt_probe_ena_ercd[32];
extern volatile uint32_t	bt_probe_intenable_after;

void
bt_intr_probe_cyc(intptr_t exinf)
{
	uint32_t	intenable;

	(void) exinf;
	__asm__ __volatile__ ("rsr.intenable %0" : "=a" (intenable));
	syslog_4(LOG_NOTICE, "bt-probe: int5=%d int7=%d int8=%d intenable=%08x",
			 (intptr_t) esp_shim_int_count[5],
			 (intptr_t) esp_shim_int_count[7],
			 (intptr_t) esp_shim_int_count[8],
			 (intptr_t) intenable);
	syslog_4(LOG_NOTICE, "bt-probe: int23=%d int27=%d int29=%d allocs=%d",
			 (intptr_t) esp_shim_int_count[23],
			 (intptr_t) esp_shim_int_count[27],
			 (intptr_t) esp_shim_int_count[29],
			 (intptr_t) esp_shim_bt_intr_alloc_count);
	/*  DPORT の割込みマトリクス。ESP32 の BT ソースは
	 *  4=BT_BB 5=BT_BB_NMI 6=RWBT 7=RWBLE 8=RWBT_NMI 9=RWBLE_NMI。
	 *  16 はリセット値＝どの CPU 線にも載っていない。 */
	{
		uint32_t	i;

		for (i = 0U; i < 16U; i++) {
			uint32_t	m = *(volatile uint32_t *)(0x3FF00104U + i * 4U) & 0x1FU;

			if (m != 16U) {
				syslog_2(LOG_NOTICE, "bt-probe: map[src %d] -> cpu line %d",
						 (intptr_t) i, (intptr_t) m);
			}
		}
	}
	syslog_4(LOG_NOTICE, "bt-probe: alloc src0=%d line0=%d src1=%d line1=%d",
			 (intptr_t) esp_shim_bt_last_intr_source[0],
			 (intptr_t) esp_shim_bt_last_intr_line[0],
			 (intptr_t) esp_shim_bt_last_intr_source[1],
			 (intptr_t) esp_shim_bt_last_intr_line[1]);
	syslog_4(LOG_NOTICE,
			 "bt-probe: ints_on calls=%d mask=%08x ercd[5]=%d ercd[8]=%d",
			 (intptr_t) bt_probe_ints_on_calls,
			 (intptr_t) bt_probe_ints_on_mask,
			 (intptr_t) bt_probe_ena_ercd[5],
			 (intptr_t) bt_probe_ena_ercd[8]);
	/*  DPORT_PRO_INTR_STATUS_0 (0x3FF000EC) はソース 0..31 の保留状態。
	 *  ここに BT のビット(4/6/7)が立つのに CPU が取らないなら CPU 側のマスク、
	 *  立たないなら周辺側が上げていない。 */
	syslog_3(LOG_NOTICE,
			 "bt-probe: intenable_after_ena=%08x dport_status0=%08x status1=%08x",
			 (intptr_t) bt_probe_intenable_after,
			 (intptr_t) *(volatile uint32_t *) 0x3FF000ECU,
			 (intptr_t) *(volatile uint32_t *) 0x3FF000F0U);
	/*  DPORT_WIFI_CLK_EN_REG(0x3FF000CC) と DPORT_CORE_RST_EN_REG(0x3FF000D0)。
	 *  BT クロック 0x00030BC9、BT リセット bit3/4/9/10 が 0 であること。 */
	syslog_2(LOG_NOTICE, "bt-probe: wifi_clk_en=%08x core_rst_en=%08x",
			 (intptr_t) *(volatile uint32_t *) 0x3FF000CCU,
			 (intptr_t) *(volatile uint32_t *) 0x3FF000D0U);
	/*  BT ベースバンドのレジスタ。0x3FF510F8 は r_rf_btdm_bb_intc_init() が
	 *  割込みイネーブルを書く先（逆アセンブルで確認）。全部 0 や 0xffffffff なら
	 *  ペリフェラルがクロック/リセット的に生きていない。 */
	syslog_4(LOG_NOTICE, "bt-probe: bb[0f8]=%08x bb[0a0]=%08x bb[040]=%08x em0=%08x",
			 (intptr_t) *(volatile uint32_t *) 0x3FF510F8U,
			 (intptr_t) *(volatile uint32_t *) 0x3FF510A0U,
			 (intptr_t) *(volatile uint32_t *) 0x3FF51040U,
			 (intptr_t) *(volatile uint32_t *) 0x3FFB0000U);
	/*
	 *  ★タスク状態をここから読むことはできない。ref_tsk() は周期ハンドラ
	 *  （非タスク文脈）から E_CTX(-25) を返す。2026-09-02 に試して確認済み
	 *  なので、次に調べる人は同じことを試さないこと。
	 */
	(void) logtask_flush(0U);
}

#endif /* TOPPERS_BT_INTR_PROBE */
