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
 *  chip_l1int_entry_trampoline（無印ESP32(Xtensa LX6) QEMU PoC用）
 *
 *  [2026-07-14追記] lx6_ctxsw_probeでCRE_CYC周期を10ms化し「busy-loop中の
 *  実タスクへtickが割込む」経路を初めて動的に検証したところハング
 *  （EXCCAUSE=15 LoadStorePIFAddrErrorCause、a1が0xffffff10相当の
 *  無効値）が発生した。GDB(`xtensa-esp32-elf-gdb`)でQEMUの生きた凍結
 *  状態を読み、advisorレビューでROM実測差異に的を絞った上で、S3/LX6
 *  両ROMの_UserExceptionVector(0x40000340)を`objdump -d`で比較して
 *  根本原因を特定した：
 *
 *    S3 ROM  : wsr.excsave1 a0 ; mov.n a0,a1 ; addmi a1,a1,-0x100 ;
 *              s32i a0,a1,84(=F_oldsp) ; rsr.excsave1 a0 ;
 *              s32i a0,a1,80(=F_a0) ; s32i a2,a1,16 ; ...
 *    LX6 ROM : addmi a1,a1,-0x100 ; s32i.n a2,a1,16 ; s32i.n a3,a1,20 ;
 *              ...（a0・旧SPの退避が一切無い）
 *
 *  common/core_support.Sの_kernel_l1int_entry（design.md/core_asm.incの
 *  コメント「F_a0/F_oldspはROMが退避済み」）はS3 ROMの上記挙動を前提に
 *  書かれている。LX6 ROMはa0とoldSPを退避しないため、.Lret_do_restore
 *  （_kernel_l1int_entryの出口、同じタスクへの遅延再ディスパッチ
 *  `.Lret_int_r`経由で必ず通る）がF_a0/F_oldspからゴミ値を読み、
 *  a1(SP)に無効値が入って直後のスタックアクセスでLoadStorePIFAddrError
 *  になる。
 *
 *  この差異が「tickが必ずidle中にしか発火しない」1秒周期のテストでは
 *  露呈しなかった理由：p_runtsk==NULLでの中断（アイドル中断）は
 *  _kernel_l1int_entryのTCB保存をスキップする`.Lint_norm`経路を通り、
 *  実タスクのTCB_sp/TCB_pcを書き換えないまま新規タスクへ通常dispatchが
 *  かかる（＝F_a0/F_oldsp経由の復元＝`.Lret_int_r`を一度も通らない）。
 *  busy-loop中の実タスクへtickが割込んで初めて`.Lret_int_r`（同一タスク
 *  への遅延再選択・自己復帰）が実行され、この経路でのみF_a0/F_oldspが
 *  読まれる。
 *
 *  common/はS3・LX6の両方で共有されており、S3側の動作を壊さずに直接
 *  書き換えることはできない。そこでチップ層（本ファイル）に薄い
 *  トランポリンを置き、xtos_exc_handler_table[EXCCAUSE_LEVEL1_INTERRUPT]
 *  には_kernel_l1int_entryを直接ではなく本トランポリンを登録する
 *  （target/esp32_devkitc_gcc/target_timer.c）。ROMからjxされた直後、
 *  a0は中断時点のライブ値のまま（LX6 ROMはa0を一切触らない）、
 *  a1はROMがEXC_FRAME_SIZE(0x100)だけ減算済みのFBであることを利用し、
 *  S3 ROMが行うはずだった2ワードの退避をソフトウェアで肩代わりしてから
 *  _kernel_l1int_entryへjmpする。a2はROMがフレーム[F_a2]へ退避済み
 *  （_kernel_l1int_entry冒頭のコメントと同じ前提）なのでスクラッチに
 *  使ってよい。
 */

#include "target_asm.inc"

	.text
	.align	4
	.global	chip_l1int_entry_trampoline
chip_l1int_entry_trampoline:
	s32i	a0, a1, F_a0            /* F_a0 = 中断時のa0（LX6 ROMは未退避） */
	addmi	a2, a1, EXC_FRAME_SIZE  /* a2 = a1(FB) + 0x100 = 中断時の実SP */
	s32i	a2, a1, F_oldsp         /* F_oldsp = 中断時の実SP（LX6 ROMは未退避） */
	j	_kernel_l1int_entry
