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
 * XtensaのPRID特殊レジスタのbit13でコアを識別（ESP32-S3：core0=0xCDCD,
 * core1=0xABAB）。0オリジンのindex（0/1）を返す。get_my_prcidx()のC実装と対。
 */
.macro my_prcidx reg
    rsr.prid \reg
    extui    \reg, \reg, 13, 1
.endm

/*
 * 起動時のウォッチドッグ早期無効化（ESP32-S3, 生レジスタ書込み）
 *
 * 本ポートは2nd-stage bootloaderを持たずflash 0x0のイメージをROMが直接起動
 * するため、ROMが有効化したRTC/TIMG0/TIMG1 WDTを自前で止める必要がある
 * （QEMUはWDTを強制しないため露見しない）。_start冒頭・レジスタウィンドウ
 * 初期化やwindowed callより前に、a2/a3のみ・呼出し無しで実行する。これに
 * より後段で何が起きてもWDTリセットのブートループに陥らず、実機JTAGを安定
 * アタッチできる（NuttX esp32s3_wdt_early_deinit相当）。キー=0x50D83AA1、
 * config0=0でEN含め全クリア。レジスタはESP-IDF soc/{rtc_cntl,timer_group}_reg.h。
 *
 * a2/a3/a4を破壊する（_start冒頭のスクラッチとして使用可）。Super-WDT(SWD)は
 * auto-feed有効化で抑止する（未対応だとリセットループが残るため必須）。
 */
#define CHIP_WDT_EARLY_DISABLE_DEFINED
.macro chip_wdt_early_disable
    movi    a2, 0x600080B0          /* RTC_CNTL_WDTWPROTECT */
    movi    a3, 0x50D83AA1
    s32i    a3, a2, 0               /* unlock */
    movi    a2, 0x60008098          /* RTC_CNTL_WDTCONFIG0 = 0 */
    movi    a3, 0
    s32i    a3, a2, 0
    movi    a2, 0x6001F064          /* TIMG0_WDTWPROTECT */
    movi    a3, 0x50D83AA1
    s32i    a3, a2, 0
    movi    a2, 0x6001F048          /* TIMG0_WDTCONFIG0 = 0 */
    movi    a3, 0
    s32i    a3, a2, 0
    movi    a2, 0x60020064          /* TIMG1_WDTWPROTECT */
    movi    a3, 0x50D83AA1
    s32i    a3, a2, 0
    movi    a2, 0x60020048          /* TIMG1_WDTCONFIG0 = 0 */
    movi    a3, 0
    s32i    a3, a2, 0
    /* Super WDT (RTC_CNTL_SWD): unlock→auto-feed有効(=リセット抑止)→relock */
    movi    a2, 0x600080B8          /* RTC_CNTL_SWD_WPROTECT */
    movi    a3, 0x8F1D312A
    s32i    a3, a2, 0
    movi    a2, 0x600080B4          /* RTC_CNTL_SWD_CONF */
    l32i    a3, a2, 0
    movi    a4, 0x80000000          /* RTC_CNTL_SWD_AUTO_FEED_EN (bit31) */
    or      a3, a3, a4
    s32i    a3, a2, 0
    movi    a2, 0x600080B8
    movi    a3, 0
    s32i    a3, a2, 0
    memw
.endm

/*
 * コア依存部で共通な定義（デフォルトの my_prcidx_boot は my_prcidx に委譲）
 */
#include "core_asm.inc"

#endif /* TOPPERS_CHIP_ASM_INC */
