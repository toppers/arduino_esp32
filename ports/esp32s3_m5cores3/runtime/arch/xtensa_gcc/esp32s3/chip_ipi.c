/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  プロセッサ間割込み（IPI）のチップ依存部（ESP32-S3用）
 *  、design.md §4）
 *
 *  ESP32-S3のFROM_CPUソフトウェア割込み（割込みマトリクスsource 78-81）を
 *  使う。コア0はFROM_CPU_0、コア1はFROM_CPU_1を受信する。トリガ/クリアは
 *  各SYSTEM_CPU_INTR_FROM_CPU_n_REG（bit0、level型・非自動クリア）。種別は
 *  内蔵SRAM上のper-coreメールボックス ipi_reason[] で伝える（コア間
 *  コヒーレント、memwフェンスのみで足りる。design.md §0）。
 */

#include "kernel_impl.h"
#include "time_event.h"
#include "target_timer.h"	/* _xtos_ints_on */
#include "chip_ipi.h"
#include <sil.h>

#if TNUM_PRCID >= 2

/* FROM_CPU_n トリガ/クリアレジスタ（idx=受信コアのprcidx。1=assert, 0=clear） */
#define IPI_FROMCPU_REG(idx)  ((void *)(uintptr_t)(0x600C0030U + (idx) * 4U))
/*
 * 割込みマトリクスMAPレジスタ：受信コアidxがFROM_CPU_idxを受ける
 * CPU割込み線番号[4:0]を書く。core0 src0=0x600C213C, core1 src1=0x600C2940。
 */
#define IPI_MAP_REG(idx)      ((void *)(uintptr_t)(0x600C2000U + (idx) * 0x800U \
                                                   + 0x13CU + (idx) * 4U))

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
 *  レビュー指摘#1修正：ipi_reason[idx]への `|=` は
 *  load-or-storeの非アトミックRMWであり、受信側のread-then-clearと
 *  競合するとビットを取りこぼす（lost-update race。mtrans2実機ハングの
 *  有力原因）。S32C1Iベースのcore_cas()でCASループ化しアトミックORにする。
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
 *  request_dispatch_retint()で割込み復帰時ディスパッチに委ねる
 *  （_kernel_l1int_entryの復帰経路が自コアのp_schedtskへ再ディスパッチ）。
 *
 *  レビュー指摘#1修正：read（r=ipi_reason[idx]）と
 *  clear（ipi_reason[idx]=0）を別々の非アトミック命令で行っていたため、
 *  read直後・clear直前の窓で他コアのchip_ipi_send()が割り込むと、そこで
 *  セットされたビットがclearで消える（FROM_CPUトリガは立つが再入時
 *  reason=0でビット消失）。core_cas()によるatomic fetch-and-clearに変更。
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
