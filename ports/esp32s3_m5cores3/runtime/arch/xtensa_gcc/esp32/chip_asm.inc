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

#ifndef TOPPERS_CHIP_ASM_INC
#define TOPPERS_CHIP_ASM_INC

/*
 * dst に自プロセッサのインデックス（0オリジン）を代入するマクロ．
 * dst = get_my_prcidx()
 *
 * XtensaのPRID特殊レジスタのbit13でコアを識別（core0/core1）。ESP32-S3
 * （LX7）で確認済みのこの識別方式は無印ESP32（LX6）でも共通と判断
 * （非公開作業記録/20260713-esp32-classic-poc/design.md §2）。本PoCは単一コア
 * のみだが、共通コードとしてそのまま流用する。
 */
.macro my_prcidx reg
    rsr.prid \reg
    extui    \reg, \reg, 13, 1
.endm

/*
 * 起動時のウォッチドッグ早期無効化（無印ESP32(Xtensa LX6)、生レジスタ書込み）
 *
 * [2026-07-13追記] dispatch()の動的検証（step1/lx6_dispatch_probe/）で
 * QEMU上で謎のSW_RESETループが発生し、原因調査の一環としてESP32-S3版と
 * 同様の早期WDT無効化を追加した（design.md §6の「QEMUはWDTを強制しない」
 * という想定は未検証の憶測だった可能性があるため、切り分けの一環として
 * 追加。他の原因だった場合も無害な保険として残す）。
 *
 * レジスタアドレスは無印ESP32のもの（ESP-IDF soc/{rtc_cntl,timer_group}_reg.h、
 * DR_REG_RTCCNTL_BASE=0x3ff48000, DR_REG_TIMERGROUP0_BASE=0x3ff5f000,
 * DR_REG_TIMERGROUP1_BASE=0x3ff60000）。無印ESP32にはESP32-S3のSuper WDT
 * (RTC_CNTL_SWD)が存在しないため、その部分は移植していない。
 */
#define CHIP_WDT_EARLY_DISABLE_DEFINED
.macro chip_wdt_early_disable
    movi    a2, 0x3ff480a4          /* RTC_CNTL_WDTWPROTECT */
    movi    a3, 0x50D83AA1
    s32i    a3, a2, 0               /* unlock */
    movi    a2, 0x3ff4808c          /* RTC_CNTL_WDTCONFIG0 = 0 */
    movi    a3, 0
    s32i    a3, a2, 0
    movi    a2, 0x3ff5f064          /* TIMG0_WDTWPROTECT */
    movi    a3, 0x50D83AA1
    s32i    a3, a2, 0
    movi    a2, 0x3ff5f048          /* TIMG0_WDTCONFIG0 = 0 */
    movi    a3, 0
    s32i    a3, a2, 0
    movi    a2, 0x3ff60064          /* TIMG1_WDTWPROTECT */
    movi    a3, 0x50D83AA1
    s32i    a3, a2, 0
    movi    a2, 0x3ff60048          /* TIMG1_WDTCONFIG0 = 0 */
    movi    a3, 0
    s32i    a3, a2, 0
    memw
.endm

/*
 * CPU例外エントリ(core_exc_entry, common/core_support.S)冒頭のチップ上書き
 * フック（無印ESP32/Xtensa LX6用）。
 *
 * LX6 ROMの_UserExceptionVector(0x40000340)はCPU例外時に a0・中断時SP を
 * 一切退避しない（addmiでフレーム確保後 a2/a3/a4 のみ退避。S3(LX7)ROMは
 * wsr/rsr excsave1でa0を、mov+s32iで中断SPを F_a0(80)/F_oldsp(84) へ退避
 * するのと異なる。両ROMの逆アセンブル比較は
 * 非公開作業記録/20260713-esp32-classic-poc/design.md §10）。
 *
 * common/core_support.S の core_exc_entry はS3 ROMの上記挙動を前提に
 * F_a0/F_oldsp が退避済みと仮定しており、出口 .Lret_int_r
 * （実行中タスクコンテキストで発生したCPU例外の自己復帰経路）がこの2ワードを
 * 読んでa0/a1を復元する。LX6では退避されておらずフレームの当該オフセットに
 * 残る不定値が読まれ、a1(SP)が無効値になって直後のスタックアクセスで
 * LoadStorePIFAddrError→ROM _DebugExceptionVector自己スピンでハングする
 * （Level-1 tickのbusy-loopプリエンプトと同一機序。lx6_cpuexc_probeで
 * 修正前ハングを実証：PC=0x40000288, EXCCAUSE=0xf, EXCVADDR=0xffffff10,
 * a1=0xffffff00）。
 *
 * 本マクロは、S3 ROMが行うはずだった2ワードの退避をソフトウェアで肩代わり
 * する（Level-1で導入した chip_l1int_entry_trampoline と同一ロジックの
 * CPU例外版）。core_exc_entry冒頭で呼ばれるため：
 *   a0 = 中断時a0（LX6 ROMはa0を一切触らずjxするので常にライブ）、
 *   a1 = FB（例外フレーム基底 = 中断SP - EXC_FRAME_SIZE）、
 *   a2 = ROMがF_a2へ退避済み（スクラッチ可。直後の rsr a2,PS で上書きされる）。
 *
 * S3(esp32s3)はこのマクロを定義せずcore_asm.incのデフォルト空定義=no-opを
 * 使うため、S3の挙動は完全に不変。
 *
 * 実装ノート：本マクロ本体は F_a0/F_oldsp/EXC_FRAME_SIZE（core_asm.incで
 * #define）を使うため、実体は core_asm.inc の #include より後で定義する。
 * ここではガードマクロ CHIP_EXC_ENTRY_FIXUP_DEFINED を先に定義し、
 * core_asm.inc 側の #ifndef デフォルト空定義を確実に抑止する。
 */
#define CHIP_EXC_ENTRY_FIXUP_DEFINED

/*
 * コア依存部で共通な定義（デフォルトの my_prcidx_boot は my_prcidx に委譲）
 */
#include "core_asm.inc"

/* chip_exc_entry_fixup の実体（F_a0等の#defineはcore_asm.incで供給済み） */
.macro chip_exc_entry_fixup
    s32i    a0, a1, F_a0            /* F_a0 = 中断時のa0（LX6 ROMは未退避） */
    addmi   a2, a1, EXC_FRAME_SIZE  /* a2 = a1(FB) + 0x100 = 中断時の実SP */
    s32i    a2, a1, F_oldsp         /* F_oldsp = 中断時の実SP（LX6 ROMは未退避） */
.endm

#endif /* TOPPERS_CHIP_ASM_INC */
