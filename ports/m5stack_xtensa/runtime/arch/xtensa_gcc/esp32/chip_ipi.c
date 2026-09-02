/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  プロセッサ間割込み（IPI）のチップ依存部（無印ESP32 / Xtensa LX6用）
 *  （非公開作業記録/20260714-esp32-classic-wifi-bt/ W0）
 *
 *  無印ESP32(classic)のFROM_CPUソフトウェア割込みを使う。トリガ/クリアと
 *  割込みマトリクスMAPは**DPORT系**（0x3FF00000基点）で、S3の
 *  SYSTEM_CPU_INTR_FROM_CPU_n（0x600C0030系）とはアドレス体系が別。
 *  コア0(PRO_CPU)はFROM_CPU_0、コア1(APP_CPU)はFROM_CPU_1を受信する。
 *  種別は内蔵SRAM上のper-coreメールボックス ipi_reason[] で伝える
 *  （コア間コヒーレント、memwフェンスのみで足りる。S3のchip_ipi.cと同一設計）。
 *
 *  一次情報（ESP-IDF v5.5, ~/tools/esp-idf）：
 *   - components/soc/esp32/register/soc/dport_reg.h
 *       DPORT_CPU_INTR_FROM_CPU_0_REG          = DR_REG_DPORT_BASE + 0x0DC
 *       DPORT_CPU_INTR_FROM_CPU_1_REG          = DR_REG_DPORT_BASE + 0x0E0
 *       DPORT_PRO_CPU_INTR_FROM_CPU_0_MAP_REG  = DR_REG_DPORT_BASE + 0x164
 *       DPORT_APP_CPU_INTR_FROM_CPU_1_MAP_REG  = DR_REG_DPORT_BASE + 0x27C
 *       （PRO_CPUバンク=0x164+n*4、APP_CPUバンク=0x278+n*4。各コアは自コア
 *        番号のFROM_CPU_idxを自コアのマトリクスバンクでINT13へ配線する）
 *   - DR_REG_DPORT_BASE = 0x3FF00000（reg_base.h）
 *   FROM_CPUはbit0でassert / 0書きでクリア（level型・非自動クリア）。
 *
 *  ■既知の未解決事項（W0）
 *  APP_CPU(コア1)へFROM_CPU_1でIPIを送るとINT13は正しくpendするが、APP_CPU
 *  のLevel-1割込みディスパッチが自前トランポリンに到達せずROMの
 *  _xtos_l1int_handler→_xtos_unhandled_interrupt(break)へ落ちる不具合が
 *  残る（QEMU/実機で再現。steering §W0調査ノート参照）。IPI配線・
 *  メールボックス自体は正しい（INT13 pend確認済み）。
 */

#include "kernel_impl.h"
#include "time_event.h"
#include "target_timer.h"	/* _xtos_ints_on 他 */
#include "chip_ipi.h"
#include <sil.h>

#if TNUM_PRCID >= 2

/*
 * FROM_CPU_n トリガ/クリアレジスタ（idx=受信コアのprcidx。1=assert, 0=clear）
 *   FROM_CPU_0 = 0x3FF000DC, FROM_CPU_1 = 0x3FF000E0（+idx*4）
 */
#define IPI_FROMCPU_REG(idx)  ((void *)(uintptr_t)(0x3FF000DCU + (idx) * 4U))
/*
 * 割込みマトリクスMAPレジスタ：受信コアidxがFROM_CPU_idxを受けるCPU
 * 割込み線番号[4:0]を書く。PRO_CPU(コア0)とAPP_CPU(コア1)でバンクが別：
 *   コア0：DPORT_PRO_CPU_INTR_FROM_CPU_0_MAP = 0x3FF00164
 *   コア1：DPORT_APP_CPU_INTR_FROM_CPU_1_MAP = 0x3FF00278 + idx*4 = 0x3FF0027C
 */
#define IPI_MAP_REG(idx) \
	((void *)(uintptr_t)(((idx) == 0U) ? 0x3FF00164U \
	                                   : (0x3FF00278U + (idx) * 4U)))

/* IPI種別メールボックス（内蔵SRAM、コア間コヒーレント） */
volatile uint32_t	ipi_reason[TNUM_PRCID];

/*
 *  IPI初期化（per-core inirtn。CLS_PRC1/CLS_PRC2でATT_INI登録）
 */
void
ipi_initialize(intptr_t exinf)
{
	uint_t	idx = get_my_prcidx();

	sil_wrw_mem(IPI_FROMCPU_REG(idx), 0U);			/* 保留クリア */
	sil_wrw_mem(IPI_MAP_REG(idx), (uint32_t) XT_IPI_INTNUM);	/* FROM_CPU_idx→INT13(自コア) */
	/*
	 * IPI(INT13)の許可はenable_intで行う（ソフトウェア許可マスクと対で更新）。
	 * ROMの_xtos_ints_onはINTENABLEを内部管理値で上書きし、CFG_INT/enable_intで
	 * 許可した他割込みのビットを消すため使わない（core_kernel_impl.h参照）。
	 */
	enable_int(XT_IPI_INTNUM);
}

/*
 *  prcidのコアへIPI送信
 *
 *  [改変] 2026-07-16 レビュー指摘#1修正：ipi_reason[idx]への `|=` は
 *  load-or-storeの非アトミックRMWであり、受信側のread-then-clearと
 *  競合するとビットを取りこぼす（lost-update race。S3側同型実装で
 *  mtrans2実機ハングの有力原因と判明）。S32C1Iベースのcore_cas()で
 *  CASループ化しアトミックORにする（esp32s3のchip_ipi.cと同一方針）。
 */
void
chip_ipi_send(uint_t prcid, uint32_t bit)
{
	uint_t		idx = INDEX_PRC(prcid);
	uint32_t	old;

	do {
		old = ipi_reason[idx];
	} while (!core_cas(&ipi_reason[idx], old, old | bit));
	Asm("memw" ::: "memory");
	sil_wrw_mem(IPI_FROMCPU_REG(idx), 1U);
}

/*
 *  IPI受信ハンドラ（_kernel_l1int_dispatchからINT13ペンディング時に呼ばれる）
 *
 *  FROM_CPUはlevel型なので先にクリアする。ディスパッチ要求は
 *  request_dispatch_retint()で割込み復帰時ディスパッチに委ねる。
 *
 *  [改変] 2026-07-16 レビュー指摘#1修正：read（r=ipi_reason[idx]）と
 *  clear（ipi_reason[idx]=0）を別々の非アトミック命令で行っていたため、
 *  read直後・clear直前の窓で他コアのchip_ipi_send()が割り込むと、そこで
 *  セットされたビットがclearで消える。core_cas()によるatomic
 *  fetch-and-clearに変更（esp32s3側と同一方針）。
 */
void
_kernel_ipi_irq_handler(void)
{
	uint_t		idx = get_my_prcidx();
	uint32_t	r;

	sil_wrw_mem(IPI_FROMCPU_REG(idx), 0U);		/* 先にクリア（level型） */
	do {
		r = ipi_reason[idx];
	} while (!core_cas(&ipi_reason[idx], r, 0U));
	Asm("memw" ::: "memory");

	if ((r & IPI_DISPATCH) != 0U) {
		request_dispatch_retint();
	}
	if ((r & IPI_EXT_KER) != 0U) {
		ext_ker_handler();
	}
	if ((r & IPI_SET_HRT) != 0U) {
		set_hrt_event_handler();
	}
}

#endif /* TNUM_PRCID >= 2 */
