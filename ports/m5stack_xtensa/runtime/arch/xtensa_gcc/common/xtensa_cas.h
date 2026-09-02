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
 *  Xtensa S32C1I CAS（compare-and-swap）の唯一の実体。
 *
 *  2026-08-06（フェーズ1 1-3）: 同一の命令列（`wsr scompare1` + `s32c1i`）が
 *  独立に11箇所へ複製されていた（chip_kernel_impl.h×2・chip_sil.h×2・
 *  xtensa_newlib_locks.c・xcore_shim_probe.c・bt_shim.c・esp_shim.c・
 *  esp_shim_libc.c・esp_shim_ring.c・m5_heap.c）。カーネル内部型（LOCK/PCB/TCB）に
 *  依存する `chip_kernel_impl.h` を直接includeできないシム層（esp/・m5/）が、
 *  そのため独自に複製する方針を採っていた——が、CAS自体はカーネル内部型に
 *  一切依存しないので、ここへ1本化すれば複製の理由が消える。
 *
 *  不変条件（全呼出し元がこれを守ること）:
 *  `wsr scompare1` と `s32c1i` の2命令の間で SCOMPARE1 が別のコードに
 *  書き換えられると、s32c1i の比較対象が破壊されCASが誤動作する。
 *  例外/割込みフレーム（core_asm.inc）は SCOMPARE1 を保存しないため、
 *  この2命令間に他コードが割り込めないこと——すなわち**呼出し元が
 *  自コアの割込みを完全にマスクした文脈（PS.INTLEVEL=15 または
 *  INTENABLE=0）でのみ本関数を呼ぶこと**——が安全性の前提である。
 *  （根拠・監査の経緯: `非公開作業記録/20260716-review-fixes/evidence-06-runtime-arch.txt`、
 *   `fmp3/arch/xtensa_gcc/esp32s3/chip_kernel_impl.h` 旧コメント）。
 *
 *  全呼出し元の監査は「個別に列挙」せず「本関数名で grep」すること
 *  （列挙は必ず腐る——監査リストが3件しか挙げていないのに実体が11箇所あった
 *  という2026-08-06時点の実測がその実例）。
 *
 *  監査は2ホップである点に注意: `grep xtensa_core_cas` はこの関数を直接
 *  呼ぶ薄いラッパー（`core_cas`/`xnl_cas`/`shim_xcore_cas`等）しか見つけない。
 *  不変条件を実際に守る責任があるのは**そのラッパーをさらに呼ぶ側**なので、
 *  各ラッパーの呼出し元まで辿って `rsil .., 15` または `INTENABLE=0` の
 *  文脈下にあることを確認すること（`esp_shim_libc.c`の不変条件違反は
 *  1ホップ目のラッパーは正しく、2ホップ目の呼出し元が割込み保護を
 *  欠いていたために起きた）。
 */

#ifndef TOPPERS_XTENSA_CAS_H
#define TOPPERS_XTENSA_CAS_H

#ifndef TOPPERS_MACRO_ONLY

Inline bool_t
xtensa_core_cas(volatile uint32_t *p, uint32_t cmp, uint32_t newv)
{
	uint32_t	old = newv;

	Asm("wsr %2, scompare1\n\t"
	    "s32c1i %0, %1, 0"
	    : "+a"(old) : "a"(p), "a"(cmp) : "memory");
	return (bool_t)(old == cmp);
}

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_XTENSA_CAS_H */
