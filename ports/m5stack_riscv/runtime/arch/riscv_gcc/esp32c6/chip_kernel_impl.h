/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *    kernel_impl.hのチップ依存部（ESP32-C6用）
 *
 *  このヘッダファイルは，target_kernel_impl.h（または，そこからインク
 *  ルードされるファイル）のみからインクルードされる．他のファイルから
 *  直接インクルードしてはならない．
 *
 *  ESP32-C3依存部（割込みマトリクス／INTMTX）をESP32-C6用に置き換えた
 *  もの．ESP32-C6のCPU割込み線制御レジスタ（Espressif呼称"PLIC"）は
 *  ベースアドレスが分かれているだけでC3のINTMTXと本質的に同じ方式
 *  （esp32c6.h・intmtx_kernel_impl.h冒頭コメント参照）．
 */

#ifndef TOPPERS_CHIP_KERNEL_IMPL_H
#define TOPPERS_CHIP_KERNEL_IMPL_H

/*
 *  ESP32-C6のハードウェア資源の定義
 */
#include "esp32c6.h"

/*
 *  RISC-Vの定義
 */
#include "riscv.h"

/*
 *  ブートハート（シングルコア．LPコアは対象外）
 */
#define TOPPERS_BOOT_HARTID    0

/*
 *  【重要な訂正】ESP32-C3はmie/mip CSRを実装しない（アクセスすると
 *  不正命令例外）ため，C6も同一系統と類推してTOPPERS_OMIT_MIE_INITを
 *  定義し，共通部start.Sでのクリアを抑止していた．しかし実機
 *  （ESP32-C6FH4 rev v0.2）で`csrr mie`を実際に発行して確認したところ，
 *  不正命令にはならず正常に読み出せる（リセット直後は0＝全ビット
 *  無効）ことを確認した．この誤った類推が，logtaskクラッシュ調査
 *  （docs/dev/esp32c6-target.md）で判明した「PLIC_MX側のENABLE／PRI／
 *  THRESH／EIPをどれだけ正しく設定してもCPUが一切外部割込みトラップを
 *  起こさない」現象の真因である（標準RISC-VのCSRであるmieが全ビット
 *  0のまま＝CPU自体が割込みを受け付けない状態だった）．
 *  そのためTOPPERS_OMIT_MIE_INITはC6では定義しない（start.Sの
 *  mie/mipクリアを有効化．chip_kernel_impl.cのchip_initialize()で
 *  改めてmie=~0を設定する）．
 */

/*
 *  デフォルトの非タスクコンテキスト用のスタック領域の定義
 */
#ifndef DEFAULT_ISTKSZ
#define DEFAULT_ISTKSZ  0x1000U    /* 4KB */
#endif /* DEFAULT_ISTKSZ */

/*
 *  デフォルトのアイドル処理用のスタック領域の定義
 */
#define DEFAULT_IDSTKSZ  0x0400U    /* 1KB */

#ifndef TOPPERS_MACRO_ONLY
/*
 *  自プロセッサのインデックス（0オリジン）の取得
 *  ESP32-C6 は単一コア（mhartid=0）．chip_asm.inc の my_pidx と一致させる．
 */
Inline uint_t
get_my_prcidx(void)
{
	return(riscv_read_mhartid());
}
#endif /* TOPPERS_MACRO_ONLY */

/*
 *  割込み番号の数，最小値と最大値
 *  FMP3 の割込み番号 INTNO の下位 16 ビット（INTNO_MASK）= CPU割込み線番号（1-31）．
 *  線 0 は使わないが，テーブルは 0 オリジンで 32 要素持つ（chip_kernel.py の
 *  INTNO_VALID と添字を一致させるため）．
 */
#define TNUM_INTNO  UINT_C(32)
#define TMIN_INTNO  UINT_C(1)
#define TMAX_INTNO  UINT_C(31)

/*
 *  割込みハンドラ番号の数
 */
#define TNUM_INHNO  UINT_C(32)

/* 外部表現への変換 */
#define EXT_IPM(pri)  (-(PRI)(pri))

/* 内部表現への変換 */
#define INT_IPM(ipm)  ((uint_t)(-(ipm)))

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
 *  割込みマトリクス依存部
 */
#include "intmtx_kernel_impl.h"

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
 *  割込み優先度マスクの設定
 */
Inline void
t_set_ipm(PRI intpri)
{
	intmtx_set_thresh(INT_IPM(intpri));
}

/*
 *  割込み優先度マスクの参照
 */
Inline PRI
t_get_ipm(void)
{
	return(EXT_IPM(intmtx_get_thresh()));
}

/*
 *  割込み要求禁止フラグが操作できる割込み番号の範囲の判定
 */
#define VALID_INTNO_DISINT(prcid, intno)  VALID_INTNO(prcid, intno)

/*
 *  割込み要求禁止フラグのセット
 */
Inline void
disable_int(INTNO intno)
{
	intmtx_disable_int(INTNO_MASK(intno));
}

/*
 *  割込み要求禁止フラグのクリア
 */
Inline void
enable_int(INTNO intno)
{
	intmtx_enable_int(INTNO_MASK(intno));
}

/*
 *  割込み要求のチェック
 */
Inline bool_t
probe_int(INTNO intno)
{
	return(intmtx_probe_int(INTNO_MASK(intno)));
}

/*
 *  割込み要求のクリア（clr_int用．FROM_CPUソースを割り当てた割込み
 *  線のみ）
 */
#define VALID_INTNO_CLRINT(prcid, intno)  VALID_INTNO(prcid, intno)

Inline bool_t
check_intno_clear(INTNO intno)
{
	return(intmtx_valid_raise(INTNO_MASK(intno)));
}

Inline void
clear_int(INTNO intno)
{
	intmtx_clear_int(INTNO_MASK(intno));
}

/*
 *  割込みの要求（ras_int用．FROM_CPUソースを割り当てた割込み線のみ．
 *  levelソースのため，クリアされるまで要求が保持される）
 */
#define VALID_INTNO_RASINT(prcid, intno)  VALID_INTNO(prcid, intno)

Inline bool_t
check_intno_raise(INTNO intno)
{
	return(intmtx_valid_raise(INTNO_MASK(intno)));
}

Inline void
raise_int(INTNO intno)
{
	intmtx_raise_int(INTNO_MASK(intno));
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

#endif /* TOPPERS_MACRO_ONLY */

/*
 *  追加コンテキスト保存/解放フック（コプロセッサを持たないため空）
 */
#define save_context(p_tcb)		((void)(p_tcb))
#define release_context(p_tcb)		((void)(p_tcb))

#endif /* TOPPERS_CHIP_KERNEL_IMPL_H */
