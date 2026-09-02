/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  プロセッサ間割込み（IPI）のチップ依存部（無印ESP32 / Xtensa LX6用）ヘッダ
 *  （非公開作業記録/20260706-smp/）
 */

#ifndef TOPPERS_CHIP_IPI_H
#define TOPPERS_CHIP_IPI_H

/*
 *  IPI種別（ipi_reasonメールボックスのビットマスク）
 *
 *  ESP32-S3のFROM_CPUソフトウェア割込みはエッジ情報しか運ばないため、
 *  種別は内蔵SRAM上のper-coreメールボックス ipi_reason[] で伝える
 *  （ESP-IDFのcrosscore_int reason[]方式）。
 */
#define IPI_DISPATCH   0x1U   /* ディスパッチ要求 */
#define IPI_EXT_KER    0x2U   /* カーネル終了要求 */
#define IPI_SET_HRT    0x4U   /* 高分解能タイマイベント設定要求 */

/*
 *  IPIに使うCPU割込み線：INT13
 *  （XTHAL_INTTYPE_EXTERN_LEVEL・level-1・本プロジェクトで未使用。
 *   tick=INT6と別線。core-isa.hのXCHAL_INT13_LEVEL=1/TYPE=EXTERN_LEVEL）
 */
#define XT_IPI_INTNUM  13
#define INT_IPI_MASK   (1U << XT_IPI_INTNUM)   /* 0x2000 */

#ifndef TOPPERS_MACRO_ONLY

/*
 *  IPI初期化（per-core inirtn）：自コアのFROM_CPU受信を割込みマトリクスで
 *  INT13へ配線し、自コアのINTENABLEでINT13を許可する。
 */
extern void ipi_initialize(intptr_t exinf);

/*
 *  prcidのコアへIPIを送る：種別bitをipi_reasonへ立て、FROM_CPUをトリガ。
 */
extern void chip_ipi_send(uint_t prcid, uint32_t bit);

/*
 *  IPI受信ハンドラ：_kernel_l1int_dispatch（target_timer.c）がINT13
 *  ペンディング時に呼ぶ。FROM_CPUをクリアし、ipi_reasonの種別に応じて
 *  request_dispatch_retint/ext_ker_handler/set_hrt_event_handlerを呼ぶ。
 */
extern void _kernel_ipi_irq_handler(void);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CHIP_IPI_H */
