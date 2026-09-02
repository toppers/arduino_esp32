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
 *  自前ベクタ表（無印ESP32 / Xtensa LX6・SMP W0）
 *  作業単位: 非公開作業記録/20260714-esp32-lx6-custom-vecbase/
 *
 *  【目的】無印ESP32(LX6)のROM VECBASE（0x40000000）依存と，ROMの
 *  xtos_exc_handler_table / _xtos_l1int_handler 依存を排除する。IRAM上の
 *  自前ベクタ表（.kernel_vectorsセクション、1KB整列）を新設し，両コアで
 *  VECBASE をここへ向ける（chip_set_vecbase、chip_kernel_impl.h）。
 *  これにより，classic ESP32のROMベクタがAPP_CPUのLevel-1割込み(EXCCAUSE=4)を
 *  自前ハンドラへ渡さずROMデフォルト(_xtos_l1int_handler→break→0x40000288
 *  スピン)へ落とす不具合（W0ブロッカー，実機JTAG確定）を解消する。
 *
 *  【窓ベクタの方式（design §1.a からの逸脱・重要）】
 *  design.md は WindowOverflow/Underflow ベクタを「ROM窓ハンドラ番地
 *  (0x40000000〜0x140)へ j するだけ」（承認1.a）としていたが，これは
 *  **実装不能**であることが判明した：本ベクタ表はIRAM(0x40080000近傍)に
 *  配置され，ROM窓ハンドラ(0x40000000)までの距離は約0x80000(512KB)で，
 *  Xtensa の J 命令の到達範囲(±128KB)を超える。jx を使うにも窓例外ハンドラ
 *  内には自由なスクラッチレジスタが無い。
 *  そこで ESP-IDF / NuttX / Zephyr が全て採る標準手法＝**窓ハンドラの実体を
 *  ベクタ枠内へ直接埋め込む**方式に変更した。埋め込む命令列は無印ESP32
 *  ROM(esp32_rev300_rom.elf, 0x40000000〜0x40000167)を objdump で逆アセンブル
 *  して転記した正準の窓ハンドラであり，s32e/l32e/rfwo/rfwu 等の Windowed
 *  オプションのコア共通命令のみを使う（レジスタ相対・位置独立でIRAM上でも
 *  そのまま正しく動作する）。各枠(0x40=64B)に収まる（最大 WindowOverflow12
 *  で 13命令≒39B）。
 *
 *  【VECBASE相対の固定オフセット（LX6 config標準）】
 *    0x000 WindowOverflow4   0x040 WindowUnderflow4
 *    0x080 WindowOverflow8   0x0C0 WindowUnderflow8
 *    0x100 WindowOverflow12  0x140 WindowUnderflow12
 *    0x180 Level2  0x1C0 Level3  0x200 Level4  0x240 Level5
 *    0x280 Debug(L6)  0x2C0 NMI  0x300 KernelException
 *    0x340 UserException  0x3C0 DoubleException
 *  （ROM実測でオフセット一致を確認済み）
 */

#include "target_asm.inc"	/* core_asm.inc: F_a0/F_oldsp/F_a2..a4/EXC_FRAME_SIZE */
#include "core_rename.h"	/* core_exc_entry → _kernel_core_exc_entry 等の名前解決 */

	.section .kernel_vectors, "ax"
	.align	4
	.global	_kernel_vectors
_kernel_vectors:

/* ---- 0x000 WindowOverflow4 ---- */
	.org	0x000
	.global	_kernel_WindowOverflow4
_kernel_WindowOverflow4:
	s32e	a0, a5, -16
	s32e	a1, a5, -12
	s32e	a2, a5, -8
	s32e	a3, a5, -4
	rfwo

/* ---- 0x040 WindowUnderflow4 ---- */
	.org	0x040
	.global	_kernel_WindowUnderflow4
_kernel_WindowUnderflow4:
	l32e	a0, a5, -16
	l32e	a1, a5, -12
	l32e	a2, a5, -8
	l32e	a3, a5, -4
	rfwu

/* ---- 0x080 WindowOverflow8 ---- */
	.org	0x080
	.global	_kernel_WindowOverflow8
_kernel_WindowOverflow8:
	s32e	a0, a9, -16
	l32e	a0, a1, -12
	s32e	a1, a9, -12
	s32e	a2, a9, -8
	s32e	a3, a9, -4
	s32e	a4, a0, -32
	s32e	a5, a0, -28
	s32e	a6, a0, -24
	s32e	a7, a0, -20
	rfwo

/* ---- 0x0C0 WindowUnderflow8 ---- */
	.org	0x0C0
	.global	_kernel_WindowUnderflow8
_kernel_WindowUnderflow8:
	l32e	a0, a9, -16
	l32e	a1, a9, -12
	l32e	a2, a9, -8
	l32e	a7, a1, -12
	l32e	a3, a9, -4
	l32e	a4, a7, -32
	l32e	a5, a7, -28
	l32e	a6, a7, -24
	l32e	a7, a7, -20
	rfwu

/* ---- 0x100 WindowOverflow12 ---- */
	.org	0x100
	.global	_kernel_WindowOverflow12
_kernel_WindowOverflow12:
	s32e	a0, a13, -16
	l32e	a0, a1, -12
	s32e	a1, a13, -12
	s32e	a2, a13, -8
	s32e	a3, a13, -4
	s32e	a4, a0, -48
	s32e	a5, a0, -44
	s32e	a6, a0, -40
	s32e	a7, a0, -36
	s32e	a8, a0, -32
	s32e	a9, a0, -28
	s32e	a10, a0, -24
	s32e	a11, a0, -20
	rfwo

/* ---- 0x140 WindowUnderflow12 ---- */
	.org	0x140
	.global	_kernel_WindowUnderflow12
_kernel_WindowUnderflow12:
	l32e	a0, a13, -16
	l32e	a1, a13, -12
	l32e	a2, a13, -8
	l32e	a11, a1, -12
	l32e	a3, a13, -4
	l32e	a4, a11, -48
	l32e	a5, a11, -44
	l32e	a6, a11, -40
	l32e	a7, a11, -36
	l32e	a8, a11, -32
	l32e	a9, a11, -28
	l32e	a10, a11, -24
	l32e	a11, a11, -20
	rfwu

/* ---- 0x180 Level2 （本ポート未使用：安全spin） ---- */
	.org	0x180
	j	_kernel_vec_panic

/* ---- 0x1C0 Level3 （BT等がLevel-3を使用。_kernel_l3int_entryへ） ----
 *  ROMの_Level3Vectorは `xsr.excsave3 a2; jx a2`（EXCSAVE3経由の間接分岐）
 *  だったが，自前VECBASEでは直接分岐できる。_kernel_l3int_entry
 *  （core_support.S）は中断時a2をEXCSAVE3から回収する前提のため，本ベクタで
 *  EXCSAVE3へ中断時a2を退避してから分岐する（a2自体は不変，
 *  _kernel_l3int_entry冒頭の addi a2,a1,... で上書きされる）。
 */
	.org	0x1C0
	.global	_kernel_Level3Vector
_kernel_Level3Vector:
	wsr.excsave3	a2
	j	_kernel_l3int_entry

/* ---- 0x200 Level4 ----
 *  bt-classic では BT コントローラの high-level interrupt がここへ来る
 *  （CONFIG_BTDM_CTRL_HLI=y、RWBT/RWBLE は CPU 割込み 25 = レベル4）。
 *  実体 xt_highint4 は libbt.a の hli_vectors.S.obj が持つ。ESP-IDF の
 *  _Level4Vector と同じく EXCSAVE_4 に a0 を退避して call0 する。
 *  それ以外の profile は従来どおり panic（未使用）。
 */
	.org	0x200
#if defined(TOPPERS_BT_CLASSIC)
	wsr.excsave4	a0
	call0	xt_highint4
	/* xt_highint4 は戻らない */
#else
	j	_kernel_vec_panic
#endif

/* ---- 0x240 Level5 （未使用：安全spin） ---- */
	.org	0x240
	j	_kernel_vec_panic

/* ---- 0x280 Debug(Level6) ----
 *  OCD接続時はデバッグ例外はOCDハードウェアが処理し本ベクタは通らない
 *  （ROMの0x40000288自己spinと同じく，非OCD時のフォールバックとして安全spin）。
 */
	.org	0x280
	j	_kernel_vec_panic

/* ---- 0x2C0 NMI （未使用：安全spin） ---- */
	.org	0x2C0
	j	_kernel_vec_panic

/* ---- 0x300 KernelException ----
 *  本ポートは割込み可能コードを常にPS.UM=1で走らせるため，Level-1割込みは
 *  UserExceptionVectorへ入り，KernelExceptionは通常発生しない（ROMも0x300は
 *  break自己spin）。安全のため User と同じプロローグ＋demuxへ合流させる
 *  （万一Kernelモードで例外/割込みが起きても正しく捌ける）。
 */
	.org	0x300
	.global	_kernel_KernelExceptionVector
_kernel_KernelExceptionVector:
	j	_kernel_vec_exc_prologue

/* ---- 0x340 UserException（本丸） ---- */
	.org	0x340
	.global	_kernel_UserExceptionVector
_kernel_UserExceptionVector:
	j	_kernel_vec_exc_prologue

/* ---- 0x3C0 DoubleException （致命：安全spin） ---- */
	.org	0x3C0
	.global	_kernel_DoubleExceptionVector
_kernel_DoubleExceptionVector:
	j	_kernel_vec_panic

/*
 *  ==== ベクタ枠外の本体コード（0x400以降，同 .kernel_vectors セクション） ====
 */
	.org	0x400
	.align	4

/*
 *  User/Kernel例外の共通プロローグ＋EXCCAUSE demux（本丸）
 *
 *  入口条件（自前VECBASE，ハードウェアはEPC1/EXCCAUSE/PS.EXCMのみ設定，
 *  レジスタ退避は一切無し）：
 *    a1 = 中断SP，a0/a2-a15 = 中断コンテキストのライブ値，PS.EXCM=1
 *
 *  ROM(_UserExceptionVector)がやっていた a2/a3/a4 のフレーム退避を肩代わりし
 *  （LX6 ROMは a0/中断SP を退避しない点も含め self-contained に処理），
 *  EXCCAUSE で分岐する：
 *    =4（Level1Interrupt）→ a0/中断SP も退避し _kernel_l1int_entry 本体へ
 *    その他（CPU例外・Alloca等）→ core_exc_entry 本体へ
 *       （core_exc_entry 冒頭の chip_exc_entry_fixup が a0/中断SP を退避する。
 *        本プロローグは a2/a3/a4 を退避済みなので二重にならない）
 *
 *  F_a2/F_a3/F_a4=16/20/24 は core_asm.inc の規約＝ROM実測オフセットと一致。
 *
 *  注意（Alloca, EXCCAUSE=5）：ROMは table[5]=_xtos_alloca_handler で
 *  movsp由来のAllocaCauseを処理していたが，自前VECBASEでは core_exc_entry へ
 *  落ちる。本ポートのカーネル/テストは movsp を用いない（静的確保）ため
 *  発火しない想定。V6非回帰（PASS=36）で担保する。万一発火する場合は本demuxに
 *  Alloca経路を追加する。
 */
	.global	_kernel_vec_exc_prologue
_kernel_vec_exc_prologue:
	addmi	a1, a1, -EXC_FRAME_SIZE	/* a1 = FB = 中断SP - 0x100 */
	s32i	a2, a1, F_a2		/* ROM肩代わり：a2退避（以後a2スクラッチ可） */
	s32i	a3, a1, F_a3		/* a3退避 */
	s32i	a4, a1, F_a4		/* a4退避 */
	rsr	a2, EXCCAUSE
	bnei	a2, 4, .Lexc_cpuexc
	/* --- EXCCAUSE=4：Level-1割込み。a0/中断SPも退避して本体へ --- */
	s32i	a0, a1, F_a0		/* 中断a0（ライブ，未破壊） */
	addmi	a2, a1, EXC_FRAME_SIZE	/* a2 = FB + 0x100 = 中断SP */
	s32i	a2, a1, F_oldsp
	j	_kernel_l1int_entry
.Lexc_cpuexc:
	/* --- CPU例外（含Kernel例外）：a0=中断a0(ライブ)/a1=FB のまま。
	 *     chip_exc_entry_fixup が a0/中断SP を退避する --- */
	j	core_exc_entry

/*
 *  安全ハンドラ（未使用ベクタ・致命例外の到達点）。
 *  最小panic＝自己spin（JTAGでPC=ここ を見れば到達源が分かる）。
 */
	.align	4
	.global	_kernel_vec_panic
_kernel_vec_panic:
	j	_kernel_vec_panic
