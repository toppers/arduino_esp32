/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 * 
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2020 by Embedded and Real-Time Systems Laboratory
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
 *  @(#) $Id: core_kernel.h 289 2021-08-05 14:44:10Z ertl-komori $
 */

/*
 *		kernel.hのコア依存部（ARM-M用）
 *
 *  このインクルードファイルは，target_kernel.h（または，そこからインク
 *  ルードされるファイル）のみからインクルードされる．他のファイルから
 *  直接インクルードしてはならない．
 */

#ifndef TOPPERS_CORE_KERNEL_H
#define TOPPERS_CORE_KERNEL_H

/*
 *  サポートする機能の定義
 */
#define TOPPERS_TARGET_SUPPORT_DIS_INT		/* dis_intをサポートする */
#define TOPPERS_TARGET_SUPPORT_ENA_INT		/* ena_intをサポートする */
#define TOPPERS_TARGET_SUPPORT_CLR_INT		/* clr_intをサポートする */
#define TOPPERS_TARGET_SUPPORT_RAS_INT		/* ras_intをサポートする */
#define TOPPERS_TARGET_SUPPORT_PRB_INT		/* prb_intをサポートする */

#define TMAX_INTPRI		(-1)		/* 割込み優先度の最大値（最低値）*/

/*
 *  ターゲット定義のタスク属性
 *
 *  TA_FPU：タスクコンテキストにFPU（Xtensa LX7コプロセッサCP0、
 *  f0〜f15＋FCR＋FSR）を含める。本ポートはeager保存方式：
 *  TA_FPU付きタスクはディスパッチ毎に常時保存・復元し、TA_FPU無しの
 *  タスクはCPENABLE=0で実行され、FPU命令の実行はコプロセッサ例外
 *  （EXCCAUSE=32、Coprocessor0Disabled）＝バグとして検出される。
 *  値はFMP3のarm_gcc/arm64_gccポートと同じ0x08（TA_ACT=0x01/
 *  TA_NOACTQUE=0x02と衝突しない。task.trbがTARGET_TSKATRとして
 *  マスク許容することはkernel/task.trb 58-59行で確認済み）。
 */
#define TA_FPU			UINT_C(0x08)	/* FPUレジスタをコンテキストに含める */

#ifndef TOPPERS_MACRO_ONLY

/*
 *  CPU例外ハンドラに渡す情報（p_excinfが指すCPU例外フレーム）
 *
 *  core_exc_entry（core_support.S）が割込み／CPU例外フレーム上に構築する。
 *  各フィールドのオフセットはcore_asm.incのF_*と一致させること
 *  （pc=0, ps=4, exccause=88, exncnt=104, intpri=108 等）。DEF_EXCの
 *  CPU例外ハンドラおよびPREPARE_RETURN_CPUEXC（test）はpcを、
 *  exc_sense_*（core_kernel_impl.h）はexccause/ps/exncnt/intpriを参照する。
 */
typedef struct t_excinf {
	uint32_t	pc;			/* 0:   EPC1（復帰PC。ill跳ばしはpc+=3） */
	uint32_t	ps;			/* 4:   発生時PS（INTLEVEL→CPUロック判定） */
	uint32_t	sar;		/* 8 */
	uint32_t	pad12;		/* 12 */
	uint32_t	a2;			/* 16 */
	uint32_t	a3;			/* 20 */
	uint32_t	a4;			/* 24 */
	uint32_t	a5;			/* 28 */
	uint32_t	a6;			/* 32 */
	uint32_t	a7;			/* 36 */
	uint32_t	a8;			/* 40 */
	uint32_t	a9;			/* 44 */
	uint32_t	a10;		/* 48 */
	uint32_t	a11;		/* 52 */
	uint32_t	a12;		/* 56 */
	uint32_t	a13;		/* 60 */
	uint32_t	a14;		/* 64 */
	uint32_t	a15;		/* 68 */
	uint32_t	pad72;		/* 72 */
	uint32_t	pad76;		/* 76 */
	uint32_t	a0;			/* 80 */
	uint32_t	oldsp;		/* 84 */
	uint32_t	exccause;	/* 88:  EXCCAUSE */
	uint32_t	lbeg;		/* 92 */
	uint32_t	lend;		/* 96 */
	uint32_t	lcount;		/* 100 */
	uint32_t	exncnt;		/* 104: 発生時の割込みネスト（非タスク判定） */
	uint32_t	intpri;		/* 108: 発生時のIPM（_kernel_ipm） */
} T_EXCINF;

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_CORE_KERNEL_H */
