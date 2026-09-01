/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2005-2020 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．（条件略）
 *
 *  本ソフトウェアは，無保証で提供されているものである．
 */

/*
 * ターゲット依存部モジュール（ESP32-S3用）
 *
 * 本作業単位は単一コアのみ対象（SMP/cross-core interruptは別作業単位）。
 * TNUM_PRCID>=2の分岐（下記ロック定義）はrp2350_pico2_gcc（デュアルコア、
 * SIOハードウェアスピンロック使用）からの引用で、ESP32-S3向けの
 * cross-core interrupt実装時に置き換える必要がある（現時点では未使用の
 * 参考コードとして残す。TNUM_PRCID<2のため実際にはコンパイルされない）。
 */

#ifndef TOPPERS_CHIP_KERNEL_IMPL_H
#define TOPPERS_CHIP_KERNEL_IMPL_H

#ifndef TOPPERS_MACRO_ONLY

/*
 * 自プロセッサのインデックス（0オリジン）の取得
 *
 * XtensaのPRID特殊レジスタのbit13でコアを識別する（ESP32-S3：
 * PRO_CPU=core0=0xCDCD(bit13=0), APP_CPU=core1=0xABAB(bit13=1)）。
 * FMP3はここが返す0オリジンindexをID_PRC()で1オリジンPRCID(PRC1/PRC2)へ
 * 対応付ける（core0→PRC1＝MASTER）。ESP-IDFのxt_utils_get_core_idと同じ方法。
 */
Inline uint_t
get_my_prcidx(void)
{
	uint32_t id;
	Asm("rsr.prid %0 \n\t"
	    "extui %0, %0, 13, 1" : "=a"(id));
	return (uint_t) id;
}

#endif /* TOPPERS_MACRO_ONLY */

/*
 *  コア依存モジュール（Xtensa用）
 *  core_kernel_impl.h が get_my_prcidx() に依存しているので，ここで読み込む．
 */
#include <core_kernel_impl.h>

#ifndef TOPPERS_MACRO_ONLY

/*
 * VECBASE（例外/割込みベクタベース）を自前ベクタ表 _kernel_vectors
 * （chip_vectors.S、.kernel_vectors セクション）へ設定する。
 *
 * 無印ESP32(LX6)のROM VECBASE依存を排除し、両コアでLevel-1割込みが確実に
 * 自前ハンドラへ到達するようにする（W0ブロッカー解消。作業単位
 * 非公開作業記録/20260714-esp32-lx6-custom-vecbase/）。VECBASEはコア毎のCPU特殊
 * レジスタのため、各コアで（割込み禁止下・スタック確立後・最初の例外前に）
 * 呼ぶ。切替直後の最初の窓オーバーフローは自前窓ベクタ（chip_vectors.S）で
 * 捌かれる。
 */
extern char _kernel_vectors[];		/* リンカ配置：ベクタ表先頭＝VECBASE値 */

Inline void
chip_set_vecbase(void)
{
	Asm("wsr %0, vecbase\n\t"
	    "isync"
	    :: "a"((uint32_t)(uintptr_t) _kernel_vectors) : "memory");
}

#if TNUM_PRCID >= 2

/*
 * ロックに関する定義と操作（Xtensa S32C1I ソフトウェアスピンロック）
 *
 * ESP32-S3にはRP2350のようなSIOハードウェアスピンロックが無いため、
 * Xtensaのアトミック命令 S32C1I（SCOMPARE1と比較して条件付きストア）で
 * ソフトウェアスピンロックを実装する。LOCK型はロック変数そのもの
 * （0=空き、非0=取得中。値は取得コアのPRIDを入れる）。
 *
 * ロック変数は内蔵SRAM（DRAM=.bss/.data）に置くこと。ESP32-S3の
 *   2コアのL1キャッシュは外部フラッシュ/PSRAMのみをバックし、内蔵SRAMは
 *   キャッシュを介さず直接アクセスされコア間コヒーレント。memwフェンス
 *   のみで足り、キャッシュ操作は不要（design.md §0）。
 *
 * ATOMCTL(SR#99)はROMが内蔵RAMのS32C1Iを許可する値に初期化済みの前提
 * （ESP-IDFも明示設定しない）。try系は共通契約に合わせ「取得失敗
 * （＝既ロック）」でtrueを返す。
 */
typedef volatile uint32_t LOCK;

/*
 * CAS: *p == cmp なら newv をストアしtrueを返す（S32C1I）。
 *
 * 2026-08-06（フェーズ1 1-3）: 実体を `xtensa_cas.h` の `xtensa_core_cas()`
 * へ1本化した（同一命令列が11箇所に複製されており監査リストが失効していた）。
 * 不変条件はesp32s3/chip_kernel_impl.hと同一（同ヘッダに集約）。
 * 本関数は既存呼出し元（本ファイルのacquire_lock/try_lock、chip_ipi.c等）の
 * シグネチャ（`LOCK *`引数）を変えないための薄いラッパー。
 */
#include <xtensa_cas.h>

Inline bool_t
core_cas(LOCK *p, uint32_t cmp, uint32_t newv)
{
	return xtensa_core_cas((volatile uint32_t *)p, cmp, newv);
}

Inline void
initialize_lock(LOCK *p_lock)
{
	*p_lock = 0U; /* 解放状態 */
	Asm("memw" ::: "memory");
}

Inline void
acquire_lock(LOCK *p_lock)
{
	uint32_t id;
	Asm("rsr.prid %0" : "=a"(id));	/* 非0の取得者識別値 */
	while (!core_cas(p_lock, 0U, id)) {
		/* 取得できるまでスピン */
	}
	Asm("memw" ::: "memory");
}

Inline bool_t
try_lock(LOCK *p_lock)
{
	uint32_t id;
	Asm("rsr.prid %0" : "=a"(id));
	if (core_cas(p_lock, 0U, id)) {
		Asm("memw" ::: "memory");
		return false;	/* 取得成功 */
	}
	return true;		/* 取得失敗（既ロック） */
}

Inline void
release_lock(LOCK *p_lock)
{
	Asm("memw" ::: "memory");
	*p_lock = 0U;
}

Inline bool_t
refer_lock(LOCK *p_lock)
{
	return (bool_t)(*p_lock != 0U);	/* 取得中か */
}

/*
 * ジャイアントロックに関する定義と操作
 * giant_lock は内蔵SRAM上のLOCK実体（chip_kernel_impl.c）。
 */
extern LOCK giant_lock;

#define initialize_glock()	initialize_lock(&giant_lock)
#define acquire_glock()		acquire_lock(&giant_lock)
#define try_glock()			try_lock(&giant_lock)
#define release_glock()		release_lock(&giant_lock)

/*
 * ネイティブスピンロックに関する定義と操作
 * spninib の lock メンバは LOCK 変数（内蔵SRAM）を指す。
 */
#ifndef TMAX_NATIVE_SPN
#define TMAX_NATIVE_SPN 14
#endif /* TMAX_NATIVE_SPN */

#define initialize_native_spn(p_spninib)	initialize_lock((LOCK *)((p_spninib)->lock))
/*
 * ネイティブスピンロックの取得（doc/porting.txt 6-21-3-2）
 *
 * CPUロック状態で呼ばれ、取得できなければ「一旦割込みを許可→再禁止」して
 * 再試行する契約。従来は acquire_lock() をそのまま呼び、取得できるまで
 * CPUロック（PS.INTLEVEL=15）のままスピンしていたため、待つ側コアのタイマ/
 * アラームが発火できず、相手コアの解放契機を待つ test_spinlock1 がクロスコア
 * 相互待ちでデッドロックしていた（非公開作業記録/20260720-spinlock1-native-spn-deadlock）。
 * acquire_lock() は giant_lock（acquire_glock）と共用のため変更不可
 * （glock 取得中に割込みを許可することになる）。上流 arm_m_gcc/rp2350 と
 * 同型に try_lock() の再試行ループで実装する。
 *
 * 割込み許可はタスクコンテキスト（intnest==0）に限定する。デッドロック
 * （test_spinlock1）は「待つ側コアの loc_spn がタスク文脈で CPUロックし、
 * その解放契機が待つ側コア自身のアラーム発火に依存する」場合に起きるため、
 * 解放が要るのはタスク文脈。ISR文脈（アラームハンドラ間の loc_spn=
 * test_spin1 の item3/cp5）は相手コアのメモリ書込みで解放されるため
 * 割込み許可は不要で、ISR入口が INTENABLE=0 の状態で unlock_cpu すると
 * 挙動が変わり test_spin1 の cp5 が実機LX6で停止した。よってISR文脈では
 * 旧来どおり tight spin とする（＝本変更前と完全同一挙動）。
 */
#define lock_native_spn(p_spninib)							\
	do {													\
		while (try_native_spn(p_spninib)) {					\
			if (_kernel_xtos_intnest[get_my_prcidx()] == 0U) {	\
				unlock_cpu();								\
				delay_for_interrupt();					\
				lock_cpu();								\
			}											\
		}													\
	} while (false)
#define try_native_spn(p_spninib)			try_lock((LOCK *)((p_spninib)->lock))
#define unlock_native_spn(p_spninib)		release_lock((LOCK *)((p_spninib)->lock))
#define refer_native_spn(p_spninib)			refer_lock((LOCK *)((p_spninib)->lock))

/*
 * エミュレーションされたスピンロックに関する定義
 */
#define delay_for_emulate_spn()

#else /* TNUM_PRCID >= 2 */

/*
 * ロックに関する定義と操作（ユニプロセッサ用）
 *
 * [改変] 2026-07-07: 単一コアでは実際のCPU間排他制御は不要（自コア内は
 * 割込み禁止で足りる）ため、当初は「何もしない・常にfalseを返す」ダミー
 * 実装だった。しかしTTSP3のAPIテスト（ref_spn等）は「実際にロック中か」
 * を正しく報告することを期待しており、常にfalse（未ロック）を返す実装
 * では`rspn.spnstat`がTSPN_LOCになるべき場面でもTSPN_UNLのまま報告されて
 * しまい、多数のAPIテストが失敗した。単一コアでも実績のある
 * imx8mm_evk_arm64_gccポート（PROCESSOR_NUM=1でのTTSP3実行例）が、単一
 * コアでも実際に状態追跡する実装（*p_lock!=0で判定）を採用しているのに
 * 倣い、排他制御自体は不要なままフラグ変数で状態のみを追跡する実装に
 * 変更する。
 */
typedef uint32_t LOCK;

Inline void initialize_lock(LOCK *p_lock) { *p_lock = 0U; }
Inline void acquire_lock(LOCK *p_lock) { *p_lock = 1U; }
Inline bool_t
try_lock(LOCK *p_lock)
{
	if (*p_lock != 0U) {
		return true;	/* 取得失敗（既ロック） */
	}
	*p_lock = 1U;
	return false;		/* 取得成功 */
}
Inline void release_lock(LOCK *p_lock) { *p_lock = 0U; }
Inline bool_t refer_lock(LOCK *p_lock) { return (bool_t) (*p_lock != 0U); }

extern LOCK giant_lock;

#define initialize_glock()	initialize_lock(&giant_lock)
#define acquire_glock()		acquire_lock(&giant_lock)
#define try_glock()			try_lock(&giant_lock)
#define release_glock()		release_lock(&giant_lock)

#ifndef TMAX_NATIVE_SPN
#define TMAX_NATIVE_SPN 14
#endif /* TMAX_NATIVE_SPN */

#define initialize_native_spn(p_spninib)	initialize_lock((LOCK *)((p_spninib)->lock))
#define lock_native_spn(p_spninib)			acquire_lock((LOCK *)((p_spninib)->lock))
#define try_native_spn(p_spninib)			try_lock((LOCK *)((p_spninib)->lock))
#define unlock_native_spn(p_spninib)		release_lock((LOCK *)((p_spninib)->lock))
#define refer_native_spn(p_spninib)			refer_lock((LOCK *)((p_spninib)->lock))

#define delay_for_emulate_spn()

#endif /* TNUM_PRCID >= 2 */

#endif /* TOPPERS_MACRO_ONLY */

/*
 * トレースログに関する設定
 */
#ifdef TOPPERS_ENABLE_TRACE
#include "logtrace/trace_config.h"
#endif /* TOPPERS_ENABLE_TRACE */

#endif /* TOPPERS_CHIP_KERNEL_IMPL_H */
