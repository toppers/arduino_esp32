/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  SDIO（外付け ESP32-C6 / esp-hosted スレーブ）の**構成値**
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ============================================================================
 *  このファイルは何か（申し送り iii-c の実装）
 *  ============================================================================
 *  移植元（P4 repo `wifi_p4_module/sdio_host/fmp3_sdmmc.c:62-68`）は
 *  スロット番号とピン番号を **`.c` に直書き**していた。ボードが 1 枚しか
 *  無かったからである。
 *
 *  ボード計画（`.steering/20260814-p4-boards-plan/PLAN.md` §8 判断点 (iii)）の
 *  申し送りは「**段 7a への SDIO ピン・スロット可変化**」であり、本ファイルが
 *  その実装である。方針:
 *
 *    - **既定は移植元の実績値**（M5Stamp ESP32P4 + Stamp-AddOn C6。
 *      slot1 / CLK=43 / CMD=44 / D0-D3=45-48 / スレーブリセット=42）。
 *      ボードを指定しなければ、実績のある構成がそのまま出る。
 *    - **ボード固有値は `kit.h` から来る**（1 ターゲット＋ボードマクロ方式。
 *      判断点 (iii) 確定事項）。Tab5 は `tab5_kit.h` の `TAB5_C6_SDIO_*`。
 *    - **数字はここにしか無い**（`.c` にも `.cfg` にも書かない）。
 *      Xtensa 側で `.c` と `.cfg` が別々に線番号を持って乖離した事故
 *      （`esp/shim/esp_shim_intr_clic_lines.h:18-20` の記録）を繰り返さない。
 *    - **CMake からの上書き**は `A1_P4_SDIO_PINS="clk,cmd,d0,d1,d2,d3"` で行う
 *      （`cmake/a1_p4_stage1.cmake`）。6 個そろっていなければ configure で止まる
 *      ——部分指定が黙って既定と混ざる事故を作らない。
 *
 *  ============================================================================
 *  Tab5 の値の一次確認（2026-08-16・段 7a AC-P1）
 *  ============================================================================
 *  段T2 は `tab5_kit.h` の C6 SDIO 値を「[二次・未実測]」と明記し、
 *  「使う段では**まず一次確認から始めること**」と申し送っていた。本段で
 *  **2 つの一次情報が一致して確定した**（詳細は
 *  `.steering/20260816-p4-7a-sdio/README.md` §1 の突合表）:
 *
 *    (1) 回路図 `Tab5_Schematics_PDF.pdf`（M5Stack 公式・2026-08-16 取得）
 *        p.1 の SoC シンボル: pin8=GPIO8→`SDIO2_D3` / pin10=GPIO9→`SDIO2_D2` /
 *        pin11=GPIO10→`SDIO2_D1` / pin12=GPIO11→`SDIO2_D0` /
 *        pin13=GPIO12→`SDIO2_CLK` / pin14=GPIO13→`SDIO2_CMD` /
 *        pin16=GPIO15→`SOC_EXTRF_RST`（→R4 1K→`RF_C6_RST`→C6 U2 pin8 `EN`）
 *    (2) 公式ファクトリデモ `m5stack/M5Tab5-UserDemo`（HEAD 68b19d37）
 *        `platforms/tab5/sdkconfig:2885-2896`:
 *          `ESP_HOSTED_SDIO_PIN_CMD=13` / `_CLK=12` / `_D0=11` / `_D1=10` /
 *          `_D2=9` / `_D3=8` / `_GPIO_RESET_SLAVE=15` / `_RESET_ACTIVE_LOW=y` /
 *          `_BUS_WIDTH=4` / `_CLOCK_FREQ_KHZ=40000`
 *
 *  補強（3 本目・C6 側はシリコン固定）:
 *    `esp-idf/components/soc/esp32c6/include/soc/sdio_slave_pins.h:8-13`
 *    CMD=18 / CLK=19 / D0=20 / D1=21 / D2=22 / D3=23 は、回路図 p.1 の
 *    C6 モジュール U2 の pin24(IO18)〜pin29(IO23) と一致する。
 */

#ifndef P4SDIO_PINS_H
#define P4SDIO_PINS_H

#include "target_syssvc.h"		/* ボード kit.h（tab5_kit.h 等）を引く */

/*
 *  C6 の電源制御の種類
 */
#define P4SDIO_PWR_NONE			0	/* 常時給電（ボード側で入っている） */
#define P4SDIO_PWR_EXPANDER		1	/* I2C IO エキスパンダ配下 */

#if defined(A1_P4_SDIO_PINS_OVERRIDE)
/*
 *  ---------------------------------------------------------------------------
 *  CMake からの明示上書き（`-DA1_P4_SDIO_PINS="clk,cmd,d0,d1,d2,d3"`）
 *  ---------------------------------------------------------------------------
 *  6 個そろっているかは CMake 側で検査済み（configure で fail-closed）。
 *  ここでは「上書きが効いている」ことだけを表現する。
 */
#define P4SDIO_GPIO_CLK			A1_P4_SDIO_PIN_CLK
#define P4SDIO_GPIO_CMD			A1_P4_SDIO_PIN_CMD
#define P4SDIO_GPIO_D0			A1_P4_SDIO_PIN_D0
#define P4SDIO_GPIO_D1			A1_P4_SDIO_PIN_D1
#define P4SDIO_GPIO_D2			A1_P4_SDIO_PIN_D2
#define P4SDIO_GPIO_D3			A1_P4_SDIO_PIN_D3

#elif defined(M5STACK_TAB5)
/*
 *  ---------------------------------------------------------------------------
 *  M5Stack Tab5（ESP32-C6-MINI-1U を SDIO2 6 線で接続）
 *  ---------------------------------------------------------------------------
 *  値は `tab5_kit.h`（本段で一次確認済みへ更新）から。
 */
#define P4SDIO_GPIO_CLK			TAB5_C6_SDIO_CLK_GPIO
#define P4SDIO_GPIO_CMD			TAB5_C6_SDIO_CMD_GPIO
#define P4SDIO_GPIO_D0			TAB5_C6_SDIO_D0_GPIO
#define P4SDIO_GPIO_D1			TAB5_C6_SDIO_D1_GPIO
#define P4SDIO_GPIO_D2			TAB5_C6_SDIO_D2_GPIO
#define P4SDIO_GPIO_D3			TAB5_C6_SDIO_D3_GPIO

#else
/*
 *  ---------------------------------------------------------------------------
 *  既定 = 移植元（P4 repo）の実績値
 *  ---------------------------------------------------------------------------
 *  出典: `~/TOPPERS/ESP32/esp32_p4/wifi_p4_module/sdio_host/fmp3_sdmmc.c:62-68`
 *        （M5Stamp ESP32P4 + Stamp-AddOn C6。同 repo
 *          `docs/research/m5stamp_addon_c6_hw_findings.md:11-19` が回路図由来と記載）
 */
#define P4SDIO_GPIO_CLK			43
#define P4SDIO_GPIO_CMD			44
#define P4SDIO_GPIO_D0			45
#define P4SDIO_GPIO_D1			46
#define P4SDIO_GPIO_D2			47
#define P4SDIO_GPIO_D3			48
#endif

/*
 *  スロット番号
 *    P4 の SDMMC スロット 0 は IOMUX 固定（GPIO39-48。
 *    `esp-idf/.../soc/esp32p4/include/soc/sdmmc_pins.h`）で、**スロット 1 だけが
 *    GPIO マトリクス経由＝任意ピン**である（P4 repo
 *    `docs/research/esp32p4_sdmmc_host_findings.md:22-27`）。
 *    C6 は両ボードとも任意ピンなので slot1 で固定する。
 *    Tab5 の microSD（G39-G44）はスロット 0 の IOMUX 割当てと一致しており、
 *    **本ドライバは microSD に触れない**（スロットが違う）。
 */
#define P4SDIO_SLOT				1

/*
 *  スレーブ（C6）のハードリセット線
 *    どちらのボードも C6 の `EN` へ（直結／1K 直列）。**active low**。
 */
#if defined(M5STACK_TAB5)
#define P4SDIO_SLAVE_RST_GPIO	TAB5_C6_SDIO_RST_GPIO
#else
#define P4SDIO_SLAVE_RST_GPIO	42		/* P4 repo fmp3_sdmmc.c:790 */
#endif

/*
 *  C6 の電源投入
 */
#if defined(M5STACK_TAB5)
#define P4SDIO_PWR_KIND			P4SDIO_PWR_EXPANDER
#define P4SDIO_PWR_I2C_SDA		TAB5_I2C_INT_SDA_GPIO
#define P4SDIO_PWR_I2C_SCL		TAB5_I2C_INT_SCL_GPIO
#define P4SDIO_PWR_ADDR			TAB5_C6_PWR_EXPANDER	/* 0x44 */
#define P4SDIO_PWR_BIT			TAB5_C6_PWR_BIT			/* P0 = WLAN_PWR_EN */
#else
#define P4SDIO_PWR_KIND			P4SDIO_PWR_NONE
#endif

/*
 *  パッドのドライブ強度（IO_MUX FUN_DRV。0=最弱 … 3=最強・リセット既定は 2）
 *    Tab5 は公式デモが SDIO の 7 本すべてを `GPIO_DRIVE_CAP_0` へ落としている
 *    （`M5Tab5-UserDemo/platforms/tab5/main/hal/hal_esp32.cpp:129-171`。
 *      配列 `_driver_gpios` に G8-G13,G15 が「esp-hosted esp32c6」として並ぶ）。
 *    22R の直列抵抗が入っている配線なのでリンギング対策と読めるが、
 *    **理由は公式ソースに書かれていない**（推測）。値そのものは一次情報である。
 *    移植元ボードには相当する記述が無いので既定は「触らない」。
 */
#if defined(M5STACK_TAB5)
#define P4SDIO_PAD_DRV			0
#endif

/*
 *  運用クロック（kHz）
 *    移植元の実績値は 20000（`fmp3_sdmmc.c` の呼出し元
 *    `os_adapter/fmp3_hosted_osi.c:521` / `apps/fmp_sdio_probe/fmp_sdio_probe.c:202`）。
 *    Tab5 公式は 40000（`sdkconfig:2889`）だが、**段 7a は低い方で撃つ**
 *    ——速い方で失敗したとき「配線が悪いのか速度が高いのか」を切り分ける
 *    材料が無いため（AC §2-4）。40MHz 化は 7b 以降の課題。
 */
#ifndef P4SDIO_FREQ_KHZ
#define P4SDIO_FREQ_KHZ			20000
#endif

#endif /* P4SDIO_PINS_H */
