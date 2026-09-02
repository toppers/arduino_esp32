/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2014 by Embedded and Real-Time Systems Laboratory
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
 *   カーネル用のスタートアップモジュール(Xtensa用)
 *
 *  ESP32-S3のブートROMがVECBASE等CPU状態を初期化済みの状態で、
 *  本エントリポイント（_start）へ制御を渡す想定（VECBASEは変更しない
 *  方針）。ROMからの
 *  制御移行がcallx系（windowed call）かjx系（フラットジャンプ）かは
 *  未確認のため、`entry`命令を使わず、レジスタ直接操作のみで自前の
 *  カーネルスタックを確立してから、以降は通常のwindowed ABI呼出し
 *  （callx4）でCコードへ進む（ARM-M版start.Sと同じ構成方針）。
 */
#define TOPPERS_MACRO_ONLY
#define UINT_C(val)		(val)		/* uint_t型の定数を作るマクロ */
#define ULONG_C(val)	(val)		/* ulong_t型の定数を作るマクロ */
#define CAST(type, val)	(val)		/* 型キャストを行うマクロ */

#include "kernel_impl.h"
#include "target_asm.inc"

	.text
	.align 4
	.global _start
	.type   _start, @function
_start:
	/*
	 * ウォッチドッグ早期無効化（チップ依存マクロ。ROMがWDTを有効化する
	 * チップでのみ実体を持つ。ESP32-S3ではRTC/TIMG0/TIMG1 WDTを止める。
	 * レジスタウィンドウ初期化やwindowed callより前に、生レジスタ書込みで
	 * 実行する。詳細はchip_asm.incを参照）。
	 */
	chip_wdt_early_disable

#ifdef TOPPERS_BOOT_SEAM
	/*
	 * seam-S3（実ESP-IDF 2nd-stage bootloader経由）専用：レジスタウィンドウ状態を
	 * リセットする（windowstart=1 / windowbase=0＝現在ウィンドウのみlive）。
	 *
	 * 実bootloaderはwindowed ABIで多数のフレームを使った後に_startへジャンプするため、
	 * ウィンドウファイルにbootloader由来のフレーム（SP不定/0）が残る。そのまま深い
	 * window overflowが起きると、その残骸ウィンドウ（a1=SP≈0）をspillして0付近
	 * （0xffffffe0=SP-32）へストア→StoreProhibited→WindowOverflowハンドラ内での再例外＝
	 * 二重例外でDoubleExceptionVector dead-loopに陥る（Wi-Fiのsta_ker初期化＝深い呼出し
	 * 連鎖で実機確認。helloは浅く非発症）。ここでリセットして以降のspillが必ず有効なSPを
	 * 使うようにする。
	 *
	 * ※Direct Boot/QEMUはROM/最小エントリでウィンドウがほぼクリーンなため不要で、
	 *   かつ過去に無条件リセットがtest_cpuexc回帰を招いたのでseam-S3限定とする（上記
	 *   の撤去経緯・下の注参照）。windowed callより前・raw書込みで行う。
	 */
	movi    a2, 1
	wsr     a2, WINDOWSTART
	movi    a2, 0
	wsr     a2, WINDOWBASE
	rsync

	/*
	 * seam-S3専用：VECBASEをROMベクタ(0x40000000)へ戻す。
	 *
	 * 実bootloaderはVECBASEを自前ベクタ(bootloaderのIRAM/DRAM)へ変更した状態で
	 * _startへジャンプする。S3(LX7)のarch層は「VECBASE非変更＝ROM常駐の
	 * Window Overflow/Underflowハンドラ(VECBASE=0x40000000)を利用」する設計のため、
	 * seam経由ではVECBASEがbootloader値のまま残り、最初のwindowed `entry`が誘発する
	 * window overflow例外が不正/上書き済のbootloaderベクタへ飛んでIllegalInstruction
	 * (hardware_init_hook冒頭で実機確認)。ここでROM VECBASEへ戻し、ROM窓ハンドラを回復する。
	 *
	 * ※classic(LX6)は自前VECBASE(chip_vectors.S)をchip_set_vecbaseで後から設定するため、
	 *   ここでの0x40000000設定は後で上書きされ無影響(classic ROMも0x40000000で窓ハンドラ有効)。
	 * ※windowed callより前・raw書込みで行う(entry発火前)。
	 */
	movi    a2, 0x40000000
	wsr     a2, VECBASE
	rsync
#endif /* TOPPERS_BOOT_SEAM */

	/*
	 * 割込みロック状態へ（PS.INTLEVELを最高レベルに設定）
	 *
	 * 注: 以前ここで windowbase=0/windowstart=1 のレジスタウィンドウ初期化を
	 * 行っていたが、QEMUでCPU例外テスト(test_cpuexc*)がハングする回帰を招き、
	 * かつ実機ブートも完全には直らなかった（深いウィンドウスピルで別途破綻）
	 * ため撤去した（Direct Boot文脈）。実機のROM残骸ウィンドウ問題の根本対処は
	 * 自前VECBASE＋自前Window Overflow/Underflowベクタで行う。
	 * ただしseam-S3では上の #ifdef ブロックで限定的にリセットする（bootloader残骸対策）。
	 */
	rsil    a2, 15

	/*
	 * ZOL（Zero-Overhead Loop）レジスタの早期サニタイズ（実機必須）
	 *
	 * ブートROMから_startへ制御が渡る時点で、LBEG/LEND/LCOUNTにはROM由来の
	 * 残置値が残り得る。LCOUNT>0 かつ LEND がこの先実行するコード範囲を指す場合、
	 * PCがLENDへ達すると暗黙にLBEGへループバックし制御が暴走する
	 * （ROM memcpy(dst=0x60)の二重フォールトストームと同一機序）。
	 * arch層のLCOUNT暴走ガード（core_support.S）は
	 * 割込み/例外エントリ経路にのみ効き、カーネル起動前のブート初期経路
	 * ——flash_xip_map()、および hardware_init_hook（flash上の.textの最初の
	 * 命令フェッチ）——はカバーしない。この残置値がflash .textの実行アドレス域を
	 * LENDが指す構成でのみ発火するため、bt_xip（hook=page0）は生き延び
	 * ble_xip（hook=page1）で再現する、というアドレス依存の症状とも整合する。
	 * 制御を受けた直後、いかなるflash/ROMコードを呼ぶより前にLCOUNT=0で
	 * ループバックを無効化し、LBEG/LEND=0で終端する（NuttX/Zephyr/ESP-IDFの
	 * Xtensa起動コードと同じ定石）。_start入口では実行中の正当なループが存在
	 * しないため、無条件クリアは常に安全（QEMU検証済みビルドにも影響しない）。
	 * a2はこの直後 my_prcidx_boot で上書きされるスクラッチ。
	 */
	movi    a2, 0
	wsr     a2, LBEG
	wsr     a2, LEND
	wsr     a2, LCOUNT
	isync

	/*
	 * スタックポインタの初期化
	 * a1 = _kernel_istkpt_table[get_my_prcidx()]
	 * （コア毎の割込みスタック。cfgが TNUM_PRCID 分生成）
	 */
	my_prcidx_boot a2
	movi    a3, _kernel_istkpt_table
	addx4   a3, a2, a3
	l32i    a1, a3, 0

	/*
	 * 最外フレーム（_start）のベース退避領域を初期化する（実機必須）
	 *
	 * windowed ABIのcall8/call12用Window Overflowハンドラ（ROM/自前とも標準
	 * コード）は、退避対象フレームの [a1-12] から「呼出し元フレームのSP」を
	 * l32eで読み、そこ基準にレジスタをスピルする。entry命令で作られるフレーム
	 * は [a1-12] が正しく設定されるが、_startはentryを使わず生でa1=istackを
	 * 設定するため [a1-12] が未初期化のまま。カーネル起動が深くネストしcall8
	 * オーバーフローが_startフレームまで及ぶと、ゴミ[a1-12]を辿ってスピル先が
	 * 不正アドレスになりStoreProhibiteで破綻する（実機で発生。QEMUのROMは
	 * 寛容なため露見せず、windowbase=11地点までは進んでいた）。
	 * [a1-12]=a1（自己参照）としてスピルがistack内の有効域に収まるようにする。
	 * a0=0でフレーム鎖も終端する。
	 */
	movi    a0, 0
	addi    a3, a1, -12             /* a3はl32i後で空き。a2(=prcidx)は温存 */
	s32i    a1, a3, 0               /* [a1-12] = a1（自己参照終端） */

	/*
	 * スレーブ（コア1、prcidx!=0）はcrt初期化（BSS/DATA/hook）を飛ばす。
	 * これらはマスタ（コア0）が既に実施済みで、スレーブが再実行すると
	 * マスタの初期化済みカーネル状態を破壊するため。スレーブはSP設定後
	 * 直接sta_kerへ入る（マスタがsta_ker内でスレーブを起動する時点で
	 * BSS/DATAは初期化済み。design.md §5）。
	 */
	bnez    a2, .Lstart_slave

#ifdef TOPPERS_FLASH_XIP
	/*
	 * flash XIP：フラッシュ上の.textを呼ぶ前に、SRAM常駐のflash_xip_map()で
	 * フラッシュをROMのcache/MMUへマップする（esp/boot/flash_cache_init.c）。
	 * 本コード自身とflash_xip_mapは.iram_boot（SRAM）に置かれている。
	 */
	movi    a2, flash_xip_map
	callx4  a2
#endif /* TOPPERS_FLASH_XIP */

	/*
	 * hardware_init_hookの呼び出し（0でない場合）
	 *
	 * ターゲットハードウェアに依存して必要な初期化処理がある場合
	 * は，hardware_init_hook という関数を用意する
	 */
	movi    a2, hardware_init_hook
	beqz    a2, .Lstart_1
	callx4  a2

.Lstart_1:
#ifndef OMIT_BSS_INIT
	/*
	 *  BSSセクション初期化
	 */
	movi    a2, BSS_START
	movi    a3, BSS_END
	bgeu    a2, a3, .Lstart_3
	movi    a4, 0
.Lstart_2:
	s32i    a4, a2, 0
	addi    a2, a2, 4
	bltu    a2, a3, .Lstart_2
#endif /* OMIT_BSS_INIT */

.Lstart_3:
#ifndef OMIT_DATA_INIT
	/*
	 *  DATAセクション初期化
	 *  __idata_start から __idata_end までを，__data_start 以降に
	 *  コピーする
	 */
	movi    a2, IDATA_START
	movi    a3, IDATA_END
	bgeu    a2, a3, .Lstart_5
	movi    a4, DATA_START
.Lstart_4:
	l32i    a5, a2, 0
	s32i    a5, a4, 0
	addi    a2, a2, 4
	addi    a4, a4, 4
	bltu    a2, a3, .Lstart_4
#endif /* OMIT_DATA_INIT */

.Lstart_5:
	/*
	 *  software_init_hook を呼出し（0 でない場合）
	 *
	 *  ソフトウェア環境（特にライブラリ）に依存して必要な初期化処
	 *  理がある場合は，software_init_hook という関数を用意すれば
	 *  よい．
	 */
	movi    a2, software_init_hook
	beqz    a2, .Lstart_8
	callx4  a2

.Lstart_8:
	movi    a2, sta_ker
	callx4  a2

.Lend:
	j       .Lend

	/*
	 * スレーブ（コア1）エントリの続き。crt初期化を飛ばしてsta_kerへ。
	 * ROMのAPP_CPU起動経路がPS.WOEを設定済みの想定だが、windowed
	 * callx4のため念のためWOEを立てる（INTLEVELは冒頭のrsil 15のまま）。
	 * a0=0でフレーム鎖を終端する。
	 */
.Lstart_slave:
	movi    a2, 0x00040000          /* PS.WOE */
	rsr     a3, PS
	or      a3, a3, a2
	wsr     a3, PS
	rsync
	movi    a0, 0
	j       .Lstart_8

	.size _start, . - _start
