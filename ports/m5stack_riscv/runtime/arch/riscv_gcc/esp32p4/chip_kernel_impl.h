/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2024 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: chip_kernel_impl.h 335 2023-04-18 10:50:40Z ertl-honda $
 */

/*
 *    kernel_impl.hのチップ依存部（ESP32-P4 用）
 *
 *  このヘッダファイルは，target_kernel_impl.h（または，そこからインク
 *  ルードされるファイル）のみからインクルードされる．他のファイルから
 *  直接インクルードしてはならない．
 */

#ifndef TOPPERS_CHIP_KERNEL_IMPL_H
#define TOPPERS_CHIP_KERNEL_IMPL_H

/*
 *  ESP32-P4 のハードウェア資源の定義
 */
#include "esp32p4.h"

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
 *  plic_kernel.trbの生成スクリプトにも存在する 
 */

/*
 *  自プロセッサのインデックス（0オリジン）の取得
 *    ESP32-P4: core0=hart0, core1=hart1 → pidx = mhartid（master=0）
 */
Inline uint_t
get_my_prcidx(void)
{
    return(riscv_read_mhartid());
}

/*
 *  自プロセッサの CLIC コンテキストINDEXの取得
 *    CLIC の割込み優先度マスク(CLIC_INT_THRESH)は自コアのメモリマップドレジスタ
 *    のため，コンテキストINDEXは実質使用しないが，I/F互換のため mhartid を返す．
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
 *  あり最大番号ではない．TMAX_INTNO を本数に等値すると VALID_INTNO が範囲外の
 *  intno=48 を有効と誤判定し，clr_int/ras_int/ena_int/dis_int/prb_int で
 *  p_intcfg_table[48](サイズ48=添字0..47)の 1バイト OOB 参照→不定値で E_OK を
 *  誤返却する(本来は範囲外で E_PAR)．最大有効番号 = 本数-1 とする(TTSP3 適合性
 *  スイートの割込みエラーパステストで検出した off-by-one)．
 */
#define TMAX_INTNO  (CLIC_TNUM_INTNO - UINT_C(1))

/*
 *  割込みハンドラ番号の数
 */
#define TNUM_INHNO  CLIC_TNUM_INTNO

/*
 *  外部表現(PRI, TMIN_INTPRI(-7)..TMAX_INTPRI(-1))と内部表現(CLIC_INT_CTRL/
 *  CLIC_INT_THRESH の CTL[31:24]バイト値)の変換．
 *  CLIC は NLBITS=3 のため，レベル(0..7)は CTL バイトの上位3ビット[7:5]に
 *  ある(下位5ビットは WARL でハード強制1)．そのためレベル値は <<5 で
 *  バイト内の正しい位置へ置く必要がある．シフトを欠くと非ゼロ優先度でも
 *  実効レベルが0(CLICでは配信されない)になる．ESP32-S31移植
 *  (arch/riscv_gcc/esp32s31/chip_kernel_impl.h, コミットb20d1c81)で実機
 *  発見・修正済みの既知バグと同一クラス(SDMMC等の周辺(外部)割込みが
 *  CFG_INT経由でこの変換を通るため，修正無しでは疑似割込みストームに至る．
 *  ESP32-P4実機でJTAG調査により確認: 修正無しの状態でCFG_INT(INTNO_SDMMC,...)
 *  の割込みが秒間約82万回のペースでpend/dmapとも0のまま再突入し続けていた)．
 */
/* 外部表現への変換 */
#define EXT_IPM(pri)  (-(PRI)((pri) >> 5))

/*
 *  内部表現への変換
 *
 *  下位5ビットの「1詰め」が要る．CLIC の線側の優先度バイト CLIC_INT_CTRL[31:24]
 *  は NLBITS=3 のため下位5ビットが WARL でハード強制1になる(実測: 0x20 を書くと
 *  0x3f が読める)のに対し，閾値レジスタ CLIC_INT_THRESH[31:24] は書いた値を
 *  そのまま保持する(実測: 0x20 は 0x20 のまま)．**この2つは非対称である**．
 *  よって同じ (-ipm)<<5 を両方へ書くと，レベルLの線(実効 (L<<5)|0x1F)と
 *  レベルLの閾値((L<<5))を比べたときに線の方が 0x1F だけ大きくなり，
 *  「閾値と同じレベルの割込みだけがマスクされない」(chg_ipm(-1)がレベル1の
 *  割込みを止められない)という off-by-one になる．ESP-IDF も閾値バイトを
 *  下位5ビット1詰めで符号化している
 *  (components/riscv/include/esp_private/interrupt_clic.h:76-83
 *   NLBITS_TO_BYTE(level) = ((level)<<NLBITS_SHIFT) | ((1<<NLBITS_SHIFT)-1))．
 *  ESP32-P4実機(M5Stamp・rev v1.3)で 9 通りのレベル/閾値の組合せを直接測って
 *  確認済み．線側へ書く値は下位5ビットをハードが立てるので**この修正で不変**
 *  である(実測: 2026-08-15/16・ESP-IDF v5.5.4 の interrupt_clic.h:76-83 の
 *  NLBITS_TO_BYTE と同じ符号化)．
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
 *  PLIC依存部
 */
#include "clic_kernel_impl.h"

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
 */
Inline void
t_set_ipm(PRI intpri)
{
    uint_t cidx = get_my_clic_cidx();
    
    clic_set_context_priority(cidx, INT_IPM(intpri));

    /*
     *  MSI/MTIの設定
     */
    if (intpri == TIPM_ENAALL) {
        riscv_set_mie(MIE_MSIE | MIE_MTIE);
    }
    else {
        riscv_clear_mie(MIE_MSIE | MIE_MTIE);
    }        
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
 *  lazy PIE: マイグレーション時の追加コンテキスト保存（mig_tsk フックの実体）
 *
 *  対象タスクがこの PE の現 PIE オーナなら，物理 PIE レジスタを当該タスクの
 *  保存域（スタック底 224B = tinib->stk）へ退避し owner をクリアする．これにより
 *  移行後の PE でトラップ復元できる．非オーナなら何もしない（冪等）．
 *  共通部 kernel_impl.h の save_context() フックを本チップの実体へ解決する．
 *  TOPPERS_PIE_LAZY 未定義（eager / PIE 無効）のときは空マクロにする（eager は毎切換え
 *  pie_push/pop で退避するためフック不要．共通部のフック既定は廃止済みのため，各ターゲットで
 *  必ず定義する方針）．詳細: pie_hwlp_design.md §9.2．
 */
#ifdef TOPPERS_PIE_LAZY
extern void chip_pie_save_context(TCB *p_tcb);
#define save_context(p_tcb)		chip_pie_save_context(p_tcb)
#else /* TOPPERS_PIE_LAZY */
#define save_context(p_tcb)		((void)(p_tcb))
#endif /* TOPPERS_PIE_LAZY */

/*
 *  タスク終了時の追加コンテキスト解放（task_terminate フックの実体）
 *
 *  [lazy PIE] 終了タスクがこの PE の現 PIE オーナなら，保存はせず owner をクリアし
 *  CSR_PIE_STATE_REG を OFF にする（終了直後の新規タスクが start_r で ON を継承して
 *  幽霊オーナになるのを防ぐ）．
 *  [HWLP] hwloop 実行中の自タスク終了(ext_tsk 経路は追加退避マクロを通らない)で
 *  残留するループ CSR(count≠0)をゼロ化する(同タスク再起動に限らず，次に start_r
 *  起動される任意の新規タスクへの残留継承=偽ループ発火を防ぐ)．
 *  共通部 kernel_impl.h の release_context() フックを本チップの実体へ解決する．
 *  HWLP/lazy PIE とも無効時は空マクロ（共通部のフック既定は廃止済みのため各ターゲットで
 *  必ず定義する方針）．詳細: pie_hwlp_design.md §9.2．
 */
#if defined(TOPPERS_SUPPORT_HWLP) || defined(TOPPERS_PIE_LAZY)
extern void chip_release_context(TCB *p_tcb);
#define release_context(p_tcb)	chip_release_context(p_tcb)
#else /* TOPPERS_SUPPORT_HWLP || TOPPERS_PIE_LAZY */
#define release_context(p_tcb)	((void)(p_tcb))
#endif /* TOPPERS_SUPPORT_HWLP || TOPPERS_PIE_LAZY */

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_CHIP_KERNEL_IMPL_H */
