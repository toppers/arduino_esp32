/*
 *  attachInterrupt / detachInterrupt
 *
 *  上流 fmp3_esp_idf の arduino/core/arduino_interrupt.c を本ポートへ移した。
 *  構造（共有ディスパッチ ISR 1 本＋ピン別コールバック表、レベル線ゆえの
 *  単純なディスパッチ）は上流のままである。
 *
 *  本ポートで変えたのは 1 点だけ:
 *    上流は esp_shim_intr_lines.h（Wi-Fi シムが動的に取る線の一覧）と
 *    ビルド時照合していたが、本ポートにその表は無い。代わりに、本ポートで
 *    実際に固定されている線と照合する（下の #error 群）。
 *
 *  ★これは動的オブジェクト生成（acre_isr）の最初の実利用である。
 *    ISR は cfg で静的に置かず、attachInterrupt が呼ばれた時点で作る。
 *
 *  制約（利用者向け）:
 *    - **attachInterrupt はタスク文脈からのみ**（acre_isr がサービスコール）。
 *    - モード定数は IDF の gpio_int_type_t と同値（Arduino.h 参照）＝写像は恒等。
 *    - 本ファイルは ESP-IDF の HAL ヘッダ（ヘッダ内 inline のみ）を使うため、
 *      SDK の include を持つ profile でだけビルドする
 *      （m5-unified／wifi-scan／wifi-connect。CMakeLists.txt が決める）。
 *
 *  ディスパッチの形（レベル線なので単純）:
 *    status 読み -> クリア -> 配送 -> 空になるまでループ。
 *    クリア漏れがあってもレベル線は即再入するので取りこぼしにならない。
 */
#include <stdint.h>
#include <kernel.h>
#include <t_syslog.h>
#include <hal/gpio_ll.h>
#include <sil.h>
#include <soc/interrupt_core0_reg.h>	/* 割込みマトリクスの MAP レジスタ */

#include "target_serial.h"		/* USART_INTNO（コンソール線との衝突検査） */
#include "chip_ipi.h"			/* XT_IPI_INTNUM */
#include "target_timer.h"		/* XT_TIMER_INTNUM */
#include "arduino_interrupt.h"	/* ARD_GPIO_INTNO / ARD_CORE_ID */

#define ARD_GPIO_HW		GPIO_LL_GET_HW(0)

/*
 *  ソース→CPU割込み線のルーティング（割込みマトリクスの MAP レジスタ）。
 *
 *  ★上流は ROM の intr_matrix_set() を呼ぶ。本ポートは
 *    wifi/shim/esp_wifi_adapter.c が直接レジスタ書込みで同じことをしているので
 *    その流儀に合わせるが、**アドレスは真似てはいけない**。
 *    あちらの `0x3FF00104 + src*4` は無印ESP32(LX6)の DPORT のもので、
 *    ESP32-S3 では無効な番地である。S3 は
 *    DR_REG_INTERRUPT_CORE0_BASE + src*4 で、SDK が
 *    INTERRUPT_CORE0_GPIO_INTERRUPT_PRO_MAP_REG として与えている。
 *    ★esp_wifi_adapter.c 側の扱いも併せて確認すること
 *      （Wi-Fi は動いているので本作業では触っていない）。
 */
#define ARD_GPIO_MAP_REG	INTERRUPT_CORE0_GPIO_INTERRUPT_PRO_MAP_REG

/*
 *  ------------------------------------------------------------------
 *  ビルド時の機械照合
 *  ------------------------------------------------------------------
 *  cfg 側の CFG_INT/ENA_DYNISR とこの値がずれたら acre_isr が E_OBJ を返し、
 *  attachInterrupt が下の syslog で大声で報告する（沈黙しない）。
 *  一方、**他の使用者との線の衝突は実行時には静かに壊れる**
 *  （tick が奪われる／コンソールが止まる）ので、ここで先に落とす。
 *
 *  (0) 参照する定義が見えていること。見えていないと `#if X == Y` が
 *      0==0＝真 や 0==17＝偽 に化けて検査が素通りする。
 */
#if !defined(XT_TIMER_INTNUM)
#error "XT_TIMER_INTNUM が見えていない（target_timer.h）。線の衝突検査が素通りする"
#endif
#if !defined(XT_IPI_INTNUM)
#error "XT_IPI_INTNUM が見えていない（chip_ipi.h）。線の衝突検査が素通りする"
#endif
#if !defined(USART_INTNO)
#error "USART_INTNO が見えていない（target_serial.h）。線の衝突検査が素通りする"
#endif
#if (ARD_GPIO_INTNO == XT_TIMER_INTNUM)
#error "arduino_interrupt: CPU割込み線が tick(XT_TIMER_INTNUM) と衝突している"
#endif
#if (ARD_GPIO_INTNO == XT_IPI_INTNUM)
#error "arduino_interrupt: CPU割込み線が IPI(XT_IPI_INTNUM) と衝突している"
#endif
#if (ARD_GPIO_INTNO == USART_INTNO)
#error "arduino_interrupt: CPU割込み線がコンソール(USART_INTNO) と衝突している"
#endif
/*
 *  Wi-Fi シムが esp_shim.cfg で DEF_INH により固定占有している線。
 *  上流の esp_shim_intr_lines.h に相当する表が本ポートには無いので、
 *  cfg に書かれている値をここへ写して照合する。
 *  ★esp_shim.cfg の CFG_INT/DEF_INH を増やしたらここも足すこと。
 */
#if (ARD_GPIO_INTNO == 0) || (ARD_GPIO_INTNO == 1) || (ARD_GPIO_INTNO == 2) \
	|| (ARD_GPIO_INTNO == 3) || (ARD_GPIO_INTNO == 5) || (ARD_GPIO_INTNO == 7) \
	|| (ARD_GPIO_INTNO == 8) || (ARD_GPIO_INTNO == 23) || (ARD_GPIO_INTNO == 27)
#error "arduino_interrupt: CPU割込み線が Wi-Fi シム(esp_shim.cfg) と衝突している"
#endif

static void	(*ard_isr_tbl[GPIO_NUM_MAX])(void);
static ID	ard_isrid;			/* 0 = 未登録 */

/*
 *  診断カウンタ（沈黙させない。probe と検死の両方が読む）
 */
volatile uint32_t	ard_intr_n_dispatch;	/* ディスパッチ ISR の起動回数 */
volatile uint32_t	ard_intr_n_call;		/* ユーザハンドラ呼出し回数   */
volatile uint32_t	ard_intr_n_orphan;		/* ハンドラ未登録ピンの発火   */
volatile int32_t	ard_intr_acre_ercd;		/* acre_isr の生値（0=未実行） */

static void
ard_gpio_dispatch(EXINF exinf)
{
	uint32_t	status;
	uint32_t	status_hi;

	(void) exinf;
	ard_intr_n_dispatch++;
	for (;;) {
		gpio_ll_get_intr_status(ARD_GPIO_HW, ARD_CORE_ID, &status);
		gpio_ll_get_intr_status_high(ARD_GPIO_HW, ARD_CORE_ID, &status_hi);
		if (status == 0U && status_hi == 0U) {
			break;
		}
		/*  先にクリアしてから配送する。配送中の新イベントは status を再度立て、
		 *  ループの次周（またはレベル線の再入）で拾われる。  */
		gpio_ll_clear_intr_status(ARD_GPIO_HW, status);
		gpio_ll_clear_intr_status_high(ARD_GPIO_HW, status_hi);
		while (status != 0U) {
			uint32_t	pin = (uint32_t) __builtin_ctz(status);

			status &= status - 1U;
			if (ard_isr_tbl[pin] != NULL) {
				ard_intr_n_call++;
				(*ard_isr_tbl[pin])();
			}
			else {
				ard_intr_n_orphan++;
			}
		}
		while (status_hi != 0U) {
			uint32_t	pin = 32U + (uint32_t) __builtin_ctz(status_hi);

			status_hi &= status_hi - 1U;
			if (pin < (uint32_t) GPIO_NUM_MAX && ard_isr_tbl[pin] != NULL) {
				ard_intr_n_call++;
				(*ard_isr_tbl[pin])();
			}
			else {
				ard_intr_n_orphan++;
			}
		}
	}
}

void
attachInterrupt(uint8_t pin, void (*fn)(void), int mode)
{
	if (pin >= (uint8_t) GPIO_NUM_MAX || fn == NULL) {
		return;
	}
	/*
	 *  モードの範囲検査（上流の `mode < RISING || mode > ONHIGH` と同じ）。
	 *  本 TU は Arduino.h を読まないので、同値である IDF 側の名前で書く
	 *  （RISING=GPIO_INTR_POSEDGE=1 … ONHIGH=GPIO_INTR_HIGH_LEVEL=5）。
	 *  弾かないと静かに壊れる:
	 *    - 0(=GPIO_INTR_DISABLE) は「登録できたのに永久に発火しない」
	 *    - ONLOW_WE(0x0C)/ONHIGH_WE(0x0D) など 6 以上は
	 *      gpio_ll_set_intr_type が未定義の型を書き込む
	 */
	if (mode <= (int) GPIO_INTR_DISABLE || mode >= (int) GPIO_INTR_MAX) {
		return;
	}
	if (ard_isrid == 0) {
		T_CISR	cisr;
		ER_ID	erid;

		cisr.isratr = TA_NULL;
		cisr.exinf  = (EXINF) 0;
		cisr.intno  = (INTNO) ARD_GPIO_INTNO;
		cisr.isr    = ard_gpio_dispatch;
		cisr.isrpri = 1;
		erid = acre_isr(&cisr);
		ard_intr_acre_ercd = (int32_t) erid;
		if (erid < 0) {
			/*  沈黙させない。ここで失敗すると attach しても永久に届かない。
			 *  よくある原因: cfg に arduino_interrupt.cfg が入っていない
			 *  （E_OBJ）や AID_ISR 不足（E_NOID）。  */
			syslog(LOG_ERROR, "arduino_interrupt: acre_isr failed ercd=%d",
				   (int_t) erid);
			return;
		}
		ard_isrid = (ID) erid;
		/*  GPIO ソースを線 18 へ配線（PRO=CORE0 の GPIO 割込み）。  */
		sil_wrw_mem((void *)(uintptr_t) ARD_GPIO_MAP_REG,
					(uint32_t) ARD_GPIO_INTNO);
		syslog(LOG_NOTICE, "arduino_interrupt: dispatch isr id=%d on intno %u",
			   (int_t) ard_isrid, (uint_t) ARD_GPIO_INTNO);
	}
	ard_isr_tbl[pin] = fn;
	gpio_ll_set_intr_type(ARD_GPIO_HW, (uint32_t) pin, (gpio_int_type_t) mode);
	if (pin < 32U) {
		gpio_ll_clear_intr_status(ARD_GPIO_HW, 1UL << pin);
	}
	else {
		gpio_ll_clear_intr_status_high(ARD_GPIO_HW, 1UL << (pin - 32U));
	}
	gpio_ll_intr_enable_on_core(ARD_GPIO_HW, ARD_CORE_ID, (uint32_t) pin);
}

void
detachInterrupt(uint8_t pin)
{
	if (pin >= (uint8_t) GPIO_NUM_MAX) {
		return;
	}
	gpio_ll_intr_disable(ARD_GPIO_HW, (uint32_t) pin);
	gpio_ll_set_intr_type(ARD_GPIO_HW, (uint32_t) pin, GPIO_INTR_DISABLE);
	ard_isr_tbl[pin] = NULL;
}
