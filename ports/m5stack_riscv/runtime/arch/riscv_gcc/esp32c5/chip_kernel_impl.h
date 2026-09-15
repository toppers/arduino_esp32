/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 * 
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 * 
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 * 
 */

/*
 *    kernel_impl.hのチップ依存部（ESP32-C5 用）
 *
 *  出典: fmp3/arch/riscv_gcc/esp32p4/chip_kernel_impl.h（P4 CLIC 層。
 *  fmp3_core pristine の複製）を単一コア向けに縮めたもの。
 *  差分: (1) esp32p4.h -> esp32c5.h、(2) 共通 clic_kernel_impl.h ではなく chip 側
 *  複製 esp32c5_clic_kernel_impl.h を include（閾値 CSR/MMIO の 2 経路）、
 *  (3) t_set_ipm の MSI/MTI（CLINT msip/mtimer）の mie 操作を削除（C5 層は
 *  CLINT を使わない: HRT は target 側 systimer、単一コアで IPI 無し）、
 *  (4) lazy PIE / HWLP のフック（save_context / release_context）は
 *  コプロセッサを持たないため空マクロ固定。
 *  D1（INT_IPM の |0x1F）は P4 のまま（C5 も NLBITS=3 の標準 CLIC で、線側
 *  CTL バイトの下位 5 ビットがハード強制 1 なのは clic_reg.h の CLIC_INT_CTL
 *  default 8'h1f が示す。IDF の NLBITS_TO_BYTE と同じ符号化）。
 *
 *  このヘッダファイルは，target_kernel_impl.h（または，そこからインク
 *  ルードされるファイル）のみからインクルードされる．他のファイルから
 *  直接インクルードしてはならない．
 */

#ifndef TOPPERS_CHIP_KERNEL_IMPL_H
#define TOPPERS_CHIP_KERNEL_IMPL_H

/*
 *  ESP32-C5 のハードウェア資源の定義
 */
#include "esp32c5.h"

/*
 *  RISC-Vの定義
 */
#include "riscv.h"

/*
 *  デフォルトの非タスクコンテキスト用のスタック領域の定義
 */
#ifndef DEFAULT_ISTKSZ
#define DEFAULT_ISTKSZ  0x2000U    /* 8KB */
#endif /* DEFAULT_ISTKSZ */

/*
 *  デフォルトのアイドル処理用のスタック領域の定義
 */
#define DEFAULT_IDSTKSZ  0x0400U    /* 1KB */

#ifndef TOPPERS_MACRO_ONLY

/*
 *  各種IDの変換
 * 
 *  chip_asm.inc のアセンブラ用の各種IDの取得関数と内容を統一すること．
 */

/*
 *  自プロセッサのインデックス（0オリジン）の取得
 *    ESP32-C5: 単一コア（mhartid=0）-> pidx = mhartid（master=0）
 */
Inline uint_t
get_my_prcidx(void)
{
    return(riscv_read_mhartid());
}

/*
 *  自プロセッサの CLIC コンテキストINDEXの取得
 *    CLIC の割込み優先度マスクは自コアの閾値のため，コンテキストINDEXは
 *    実質使用しないが，I/F互換のため mhartid を返す．
 */
Inline uint_t
get_my_clic_cidx(void)
{
    return(riscv_read_mhartid());
}

#endif /* TOPPERS_MACRO_ONLY */

/*
 *  割込み番号の数，最小値と最大値
 */
#define TNUM_INTNO  CLIC_TNUM_INTNO
#define TMIN_INTNO  UINT_C(1)
/*
 *  最大割込み番号．CLIC_TNUM_INTNO は線の「本数」(=48, 有効線番号は 0..47)で
 *  あり最大番号ではない（P4 で TTSP3 が検出した off-by-one。詳細は P4 の
 *  chip_kernel_impl.h）．最大有効番号 = 本数-1 とする．
 */
#define TMAX_INTNO  (CLIC_TNUM_INTNO - UINT_C(1))

/*
 *  割込みハンドラ番号の数
 */
#define TNUM_INHNO  CLIC_TNUM_INTNO

/*
 *  外部表現(PRI, TMIN_INTPRI(-7)..TMAX_INTPRI(-1))と内部表現(CLIC_INT_CTRL/
 *  閾値の 8bit バイト値)の変換．
 *  CLIC は NLBITS=3 のため，レベル(0..7)はバイトの上位3ビット[7:5]に
 *  ある(下位5ビットは WARL でハード強制1)．そのためレベル値は <<5 で
 *  バイト内の正しい位置へ置く必要がある．
 */
/* 外部表現への変換 */
#define EXT_IPM(pri)  (-(PRI)((pri) >> 5))

/*
 *  内部表現への変換
 *
 *  下位5ビットの「1詰め」が要る（P4 の D1/2026-08-16）．CLIC の線側の優先度
 *  バイト CLIC_INT_CTRL[31:24] は NLBITS=3 のため下位5ビットがハード強制1に
 *  なる(clic_reg.h の default 8'h1f)のに対し，閾値は書いた値をそのまま保持する．
 *  同じ (-ipm)<<5 を両方へ書くと「閾値と同じレベルの割込みだけがマスクされない」
 *  off-by-one になる．ESP-IDF も閾値バイトを下位5ビット1詰めで符号化している
 *  (components/riscv/include/esp_private/interrupt_clic.h の NLBITS_TO_BYTE)．
 *  C5 実機での確認は**未実施**（P4 rev v1.3 実機の 9 通りの実測に基づく）．
 */
#define INT_IPM(ipm)  ((((uint_t)(-(ipm))) << 5) | 0x1FU)

/*
 *  割込み番号の範囲の判定
 */
#define VALID_INTNO(prcid, intno) \
  ((TMIN_INTNO <= INTNO_MASK(intno)) \
    && (INTNO_MASK(intno) <= TMAX_INTNO))

/*
 *  割込み要求ラインのための標準的な初期化情報を生成する
 */
#define USE_INTINIB_TABLE

/*
 *  割込み要求ライン設定テーブルを生成する
 */
#define USE_INTCFG_TABLE

/*
 *  CLIC依存部（chip 側複製。閾値の CSR/MMIO 2 経路）
 */
#include "esp32c5_clic_kernel_impl.h"

#ifndef TOPPERS_MACRO_ONLY

/*
 *  割込み属性の設定のチェック
 */
Inline bool_t
check_intno_cfg(INTNO intno)
{
    uint_t prcidx = get_my_prcidx();

    return((p_intcfg_table[prcidx])[INTNO_MASK(intno)] != 0U);
}

/*
 *  clr_int/ras_int 対象として妥当かのチェック(CLIC では設定済み割込みなら可)
 */
Inline bool_t
check_intno_clear(INTNO intno)
{
    return(check_intno_cfg(intno));
}

Inline bool_t
check_intno_raise(INTNO intno)
{
    return(check_intno_cfg(intno));
}

/*
 *  割込み優先度マスクの設定
 *    P4 版に在った MSI/MTI（CLINT）の mie 操作は C5 では行わない（CLINT 未使用）．
 */
Inline void
t_set_ipm(PRI intpri)
{
    uint_t cidx = get_my_clic_cidx();
    
    clic_set_context_priority(cidx, INT_IPM(intpri));
}

/*
 *  割込み優先度マスクの参照
 */
Inline PRI
t_get_ipm(void)
{
    uint_t cidx = get_my_clic_cidx();
    
    return(EXT_IPM(clic_get_context_priority(cidx)));
}

/*
 *  割込み要求禁止フラグが操作できる割込み番号の範囲の判定
 */
#define VALID_INTNO_DISINT(prcid, intno)  VALID_INTNO(prcid, intno)

/*
 *  割込み要求のクリア/発生が操作できる割込み番号の範囲の判定
 */
#define VALID_INTNO_CLRINT(prcid, intno)  VALID_INTNO(prcid, intno)
#define VALID_INTNO_RASINT(prcid, intno)  VALID_INTNO(prcid, intno)

/*
 *  割込み要求禁止フラグのセット
 */
Inline void
disable_int(INTNO intno)
{
    clic_disable_int(INTNO_MASK(intno));
}

/* 
 *  割込み要求禁止フラグのクリア
 */
Inline void
enable_int(INTNO intno)
{
    clic_enable_int(INTNO_MASK(intno));
}

/*
 *  割込み要求のチェック
 */
Inline bool_t
probe_int(INTNO intno)
{
    return(clic_probe_pending(INTNO_MASK(intno)));
}

/*
 *  割込み要求のクリア
 */
Inline void
clear_int(INTNO intno)
{
    clic_clear_pending(INTNO_MASK(intno));
}

/*
 *  割込みの要求（software による割込み発生）
 */
Inline void
raise_int(INTNO intno)
{
    clic_raise_pending(INTNO_MASK(intno));
}

/*
 *  ペリフェラル割込みソースの CLIC 線への割付け（割込みマトリクス）
 *    target_kernel_impl.c の target_initialize から呼ぶ．MAP へ書く値は
 *    CLIC 線番号そのもの（16..47）．
 */
extern void esp32c5_intmtx_route(uint_t intsrc, INTNO intno);

/*
 *  チップ依存の初期化（マスタプロセッサ用）
 */
extern void chip_mprc_initialize(void);
     
/*
 *  チップ依存の初期化
 */
extern void chip_initialize(PCB *p_my_pcb);

/*
 *  チップ依存の終了処理
 */
extern void chip_terminate(void);

/*
 *  追加コンテキスト保存/解放フック（コプロセッサを持たないため空．
 *  共通部のフック既定は廃止済みのため各ターゲットで必ず定義する．
 *  P4 と同じく TOPPERS_MACRO_ONLY ガードの内側に置く）
 */
#define save_context(p_tcb)		((void)(p_tcb))
#define release_context(p_tcb)	((void)(p_tcb))

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_CHIP_KERNEL_IMPL_H */
