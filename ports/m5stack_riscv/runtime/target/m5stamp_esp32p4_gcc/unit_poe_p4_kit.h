/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  利用条件は TOPPERS ライセンス（polarfire_soc_kit.h と同一）。無保証。
 */

/*
 *    M5Stack Unit PoE-P4 ボード固有の定義
 *
 *  =========================================================================
 *  位置づけ（2026-08-15・段E）
 *  =========================================================================
 *  Unit PoE-P4 は M5Stamp ESP32P4 と **同一の SoC（ESP32-P4NRW32）を積んだ
 *  別基板**である。よってターゲット依存部（fmp3/target/m5stamp_esp32p4_gcc/）は
 *  共有し、ボード差だけをこのヘッダで吸収する
 *  ——「1 ターゲット + ボードマクロ」方式（ボード計画 判断点 iii-b-1・確定）。
 *
 *  出典:
 *    - 骨格は P4 repo の `target/unit_poe_p4_gcc/unit_poe_p4_kit.h`
 *      （/home/honda/TOPPERS/ESP32/esp32_p4、HEAD b298550。読み取りのみ）。
 *      ただし同ディレクトリは「一度もビルド・実機投入されていない・古いベースからの
 *      複製・使うなら作り直せ」と自ら注記している（同 target_design.md:20-45）ので、
 *      **複製ではなく本 repo の m5stamp_esp32p4_kit.h を起点に書き直した**。
 *    - Ethernet（RMII/SMI/PHY）のピン値は P4 repo の
 *      `docs/research/unit_poe_p4_hw_reference.md` §1（M5Stack 公式サンプルの
 *      生成済み sdkconfig を一次資料として確定させた表）。同じ値で
 *      **実機 PASS 済み**（DHCP/ping/UDP/TCP/HTTP。esp32_p4/HANDOFF.md:1249-1263）。
 *
 *  =========================================================================
 *  コンソールについて（P4 repo 版からの訂正・2026-08-15 実測）
 *  =========================================================================
 *  P4 repo 版はここに `#define USE_UART0`（U0TXD=GPIO37/U0RXD=GPIO38）を置き
 *  「別基板なので実配線が異なる可能性・要実機確認」と注記していた。
 *  **その心配は不要である**——本 repo の実測（`grep -rn USE_UART0`）では、
 *  USE_UART0 を参照しているのは polarfire_soc_kit_gcc の 2 ファイルだけで、
 *  ESP32-P4 側では**どこからも参照されていない死んだマクロ**である。
 *  P4 のコンソールは chip 層が内蔵 USB-Serial/JTAG を直叩きする実装
 *  （fmp3/arch/riscv_gcc/esp32p4/chip_serial.c の USJ_EP1 系レジスタ）であり、
 *  UART0 のピンには一切依存しない。書込みと採取も同じ USB-Serial/JTAG である。
 *  ⇒ 混乱を残さないため USE_UART0 は**置かない**（置いても効かないため）。
 */
#ifndef UNIT_POE_P4_KIT_H
#define UNIT_POE_P4_KIT_H

#ifdef UNIT_POE_P4

#define TARGET_NAME   "M5Stack Unit PoE-P4 <HP RV32IMAFC, RISC-V>"

/*
 *  HP コア周波数。ESP32-P4NRW32 は M5Stamp ESP32P4 と同一チップであり、
 *  方式(a)（ESP-IDF ローダ殻）では IDF の esp_clk_init が 360MHz を設定してから
 *  toppers_start() へ入るので、両ボードとも同値である。mtime も同クロックで歩進する。
 *  （seam 起動では bootloader の残した 90MHz のままになる——これはボード差ではなく
 *   起動方式の差であり、段5 の宿題である。`.steering/20260814-p4-stage4/README.md` §11-1）
 */
#define CORE_CLK_MHZ  360

/*  sil_dly_nse 用のチューニング値（M5Stamp 版と同値。TODO: 実測で調整）  */
#define SIL_DLY_TIM1  14
#define SIL_DLY_TIM2  8

/*
 *  -------------------------------------------------------------------------
 *  Ethernet（内蔵 EMAC ＋ IP101GRI PHY・RMII）
 *  -------------------------------------------------------------------------
 *  値の出典は上記 unit_poe_p4_hw_reference.md §1。特に注意すべきは 3 点:
 *
 *   (1) MDIO は **GPIO52**。ESP-IDF の P4 既定は 18 であり、M5Stack が
 *       基板設計で上書きしている。既定値のままでは SMI が通らない。
 *   (2) PHY アドレスは **1**。基板ストラップで 0 ではない。
 *   (3) REF_CLK は **PHY→P4 への入力**（EMAC_CLK_EXT_IN）。P4 が
 *       クロックを出す構成ではない。ここを取り違えるのは無印 ESP32 の
 *       Ethernet で頻出する事故である。
 *
 *  RMII 信号は P4 では GPIO マトリクス経由なので、ピン番号はシリコンの制約では
 *  なく基板設計の値である（同 §2）。よって「ボード固有値」としてここに置く。
 */
#define UNIT_POE_P4_ETH_RMII_TXEN     49
#define UNIT_POE_P4_ETH_RMII_TXD0     34
#define UNIT_POE_P4_ETH_RMII_TXD1     35
#define UNIT_POE_P4_ETH_RMII_CRS_DV   28
#define UNIT_POE_P4_ETH_RMII_RXD0     29
#define UNIT_POE_P4_ETH_RMII_RXD1     30
#define UNIT_POE_P4_ETH_RMII_REF_CLK  50    /* PHY -> P4 の入力（EMAC_CLK_EXT_IN） */
#define UNIT_POE_P4_ETH_SMI_MDC       31
#define UNIT_POE_P4_ETH_SMI_MDIO      52    /* IDF 既定 18 ではない */
#define UNIT_POE_P4_ETH_PHY_RST_GPIO  51
#define UNIT_POE_P4_ETH_PHY_ADDR      1     /* 0 ではない */

#endif /* UNIT_POE_P4 */

#endif /* UNIT_POE_P4_KIT_H */
