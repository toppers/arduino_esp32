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
 *  xthal_window_spill_nw 自前実装（無印ESP32(Xtensa LX6) QEMU PoC用）
 *
 *  [2026-07-13追記] 当初はプレースホルダスタブ（`j .`で即停止、未到達を
 *  前提）だったが、advisorレビューを受けてESP32-S3(LX7)のROM実体
 *  （esp-rom-elfs/20241011/esp32s3_rev0_rom.elf、アドレス0x40053d60、
 *  design.md §6参照）を`objdump -d`で逆アセンブルし、その命令列を
 *  そのまま転記した。
 *
 *  転記が正当である理由：本ルーチンが使う命令・特殊レジスタ
 *  （WINDOWBASE/WINDOWSTART/SAR、rotw/bbsi/bbci/nsau/extui/src等）は
 *  Xtensa windowed ABIコア（Windowedオプション）のコア共通機能であり、
 *  LX6/LX7間で変わるチップ固有ペリフェラルレジスタは一切参照しない
 *  （T1の静的検証で、xtensa-esp32-elf-gccがcore_support.S全体を無改造で
 *  アセンブルできたことも、これらの命令がLX6でも利用可能であることの
 *  間接的な裏付けになっている）。
 *
 *  重要な前提（core_support.Sの呼び出し規約）：呼び出し元（dispatcher_1/
 *  dispatcher_2/migrate系、core_support.S）はPSのWOE(Window Overflow
 *  Enable)を明示的にoffにしてから`call0`で本ルーチンを呼ぶ。すなわち
 *  本ルーチンはWindow Overflow例外に頼らず、WINDOWBASE/WINDOWSTARTを
 *  自前で読み歩いて全ライブウィンドウを手動でスタックへ退避する
 *  （xthal_window_spill_nwの"_nw"="no window exception"の由来）。
 *  この前提を壊す変更（呼び出し元でWOEをoffにし忘れる等）は本ルーチンの
 *  正しさを崩すため注意。
 *
 *  QEMU上でのdispatch()経路の動的検証はこの実装が前提
 *  （非公開作業記録/20260713-esp32-classic-poc/design.md §7「次フェーズへ進む
 *  場合の主要な先行課題」1.への対応）。
 */

	.text
	.align	4
	.global	xthal_window_spill_nw
xthal_window_spill_nw:
	rsr.windowbase	a2
	addi.n	a2, a2, 1
	ssr	a2
	rsr.windowstart	a3
	srl	a2, a3
	sll	a3, a3
	bgez	a3, .Lxwsn_only_one_frame
	extui	a3, a3, 16, 16
	or	a2, a2, a3
	neg	a3, a2
	and	a3, a3, a2
	nsau	a3, a3
	ssl	a3
	srl	a2, a2
	wsr.windowstart	a2
	rsr.windowbase	a2
	addi	a2, a2, 31
	sub	a3, a2, a3
	wsr.windowbase	a3
	rsync
	rsr.windowstart	a2
.Lxwsn_loop:
	beqz	a2, .Lxwsn_single_left
	bbsi	a2, 0, .Lxwsn_case1
	bbsi	a2, 1, .Lxwsn_case2
	bbsi	a2, 2, .Lxwsn_case3
	j	.Lxwsn_case_ge4

.Lxwsn_case1:
	addi	a3, a9, -16
	s32i.n	a4, a3, 0
	s32i.n	a5, a3, 4
	s32i.n	a6, a3, 8
	s32i.n	a7, a3, 12
	srli	a6, a2, 1
	rotw	1
	j	.Lxwsn_loop

.Lxwsn_case2:
	addi	a3, a13, -16
	s32i.n	a4, a3, 0
	s32i.n	a5, a3, 4
	s32i.n	a6, a3, 8
	s32i.n	a7, a3, 12
	addi	a3, a5, -12
	l32i.n	a3, a3, 0
	addi	a3, a3, -32
	s32i.n	a8, a3, 0
	s32i.n	a9, a3, 4
	s32i.n	a10, a3, 8
	s32i.n	a11, a3, 12
	srli	a10, a2, 2
	rotw	2
	j	.Lxwsn_loop

.Lxwsn_case3:
	rotw	1
	addi	a13, a13, -16
	s32i.n	a0, a13, 0
	s32i.n	a1, a13, 4
	s32i.n	a2, a13, 8
	s32i.n	a3, a13, 12
	addi	a3, a1, -12
	l32i.n	a3, a3, 0
	addi	a13, a13, 16
	addi	a3, a3, -48
	s32i.n	a4, a3, 0
	s32i.n	a5, a3, 4
	s32i.n	a6, a3, 8
	s32i.n	a7, a3, 12
	s32i.n	a8, a3, 16
	s32i.n	a9, a3, 20
	s32i.n	a10, a3, 24
	s32i.n	a11, a3, 28
	rotw	-1
	srli	a14, a2, 3
	rotw	3
	j	.Lxwsn_loop

.Lxwsn_single_left:
	rotw	1
	rsr.windowbase	a2
	ssl	a2
	movi.n	a2, 1
	sll	a2, a2
	wsr.windowstart	a2
	rsync
	movi.n	a2, 0
	ret.n

.Lxwsn_only_one_frame:
	movi.n	a2, 1
	ret.n

.Lxwsn_case_ge4:
	slli	a2, a2, 1
	addi.n	a2, a2, 1
	rsr.windowbase	a3
	bbsi	a2, 15, .Lxwsn_ge4_done
.Lxwsn_ge4_loop:
	addi.n	a3, a3, -1
	slli	a2, a2, 1
	bbci	a2, 15, .Lxwsn_ge4_loop
.Lxwsn_ge4_done:
	extui	a3, a3, 0, 4
	ssl	a3
	slli	a3, a2, 16
	src	a2, a2, a3
	extui	a2, a2, 0, 16
	wsr.windowstart	a2
	rsr.sar	a3
	movi.n	a2, 32
	sub	a3, a2, a3
	wsr.windowbase	a3
	rsync
	movi.n	a2, 2
	ret.n
