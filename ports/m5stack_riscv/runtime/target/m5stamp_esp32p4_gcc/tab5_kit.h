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
 *    M5Stack Tab5 ボード固有の定義
 *
 *  =========================================================================
 *  位置づけ（2026-08-15・段T1/T2）
 *  =========================================================================
 *  Tab5 は M5Stamp ESP32P4 / Unit PoE-P4 と **同一の SoC（ESP32-P4NRW32）を
 *  積んだ別基板**である。よってターゲット依存部（fmp3/target/m5stamp_esp32p4_gcc/）は
 *  共有し、ボード差だけをこのヘッダで吸収する
 *  ——「1 ターゲット + ボードマクロ」方式（ボード計画 判断点 iii-b-1・確定）。
 *
 *  判定基準: .steering/20260815-tab5-stageT12/AC.md（未同定 3 件の同定は同ファイル §8）
 *  実施記録: .steering/20260815-tab5-stageT12/README.md・IDENT.md（未同定 3 件の同定結果）
 *
 *  =========================================================================
 *  値の出典と確度（**ここを混ぜない**）
 *  =========================================================================
 *  [実測]   本 repo が Tab5 実機で測った（段T1/T2・2026-08-15）
 *  [一次]   ベンダ公式 docs か、vendored サブモジュールのソース
 *  [二次]   コミュニティ等の伝聞。**回路図で確かめていない**
 *  [未実測] 上記いずれでも本 repo が実機で確かめていないもの
 *
 *  下の定義のうち **カーネルが実際に使うのは TARGET_NAME / CORE_CLK_MHZ /
 *  SIL_DLY_TIM* の 4 個だけ**である。それ以外は「この板に何がどう付いているか」の
 *  記録であり、使い手が現れるまでコンパイル結果に影響しない
 *  （unit_poe_p4_kit.h が Ethernet のピンを同じ形で持っているのと同じ）。
 *
 *  =========================================================================
 *  コンソール（段E で解決済みの論点）
 *  =========================================================================
 *  P4 のコンソールは chip 層が内蔵 USB-Serial/JTAG を直叩きする実装
 *  （fmp3/arch/riscv_gcc/esp32p4/chip_serial.c の USJ_EP1 系レジスタ）であり、
 *  UART0 のピンには依存しない。`USE_UART0` は ESP32-P4 側ではどこからも
 *  参照されない死んだマクロなので**置かない**（段E README §3-1）。
 *  書込みも採取も同じ USB-Serial/JTAG（by-id に MAC を含む）である。[実測]
 */
#ifndef TAB5_KIT_H
#define TAB5_KIT_H

#ifdef M5STACK_TAB5

#define TARGET_NAME   "M5Stack Tab5 <HP RV32IMAFC, RISC-V>"

/*
 *  HP コア周波数。ESP32-P4NRW32 は M5Stamp ESP32P4 と同一チップであり、
 *  方式(a)（ESP-IDF ローダ殻）では IDF の esp_clk_init が 360MHz を設定してから
 *  toppers_start() へ入るので、両ボードとも同値である。mtime も同クロックで歩進する。
 *  （seam 起動では bootloader の残した 90MHz のままになる——これはボード差ではなく
 *   起動方式の差であり、段5 の宿題である。`.steering/20260814-p4-stage4/README.md` §11-1）
 */
#define CORE_CLK_MHZ  360

/*  sil_dly_nse 用のチューニング値（M5Stamp 版と同値。[未実測] 実測で調整）  */
#define SIL_DLY_TIM1  14
#define SIL_DLY_TIM2  8

/*
 *  -------------------------------------------------------------------------
 *  実機の同定（段T1/T2 で使った個体）[実測 2026-08-15]
 *  -------------------------------------------------------------------------
 *    MAC e8:f6:0a:e2:f4:45 / ESP32-P4 revision v1.3 / XTAL 40MHz / flash 16MB
 *  rev v1.3 は Unit PoE-P4・M5Stamp ESP32P4 と同じ（＝rev 3.x を要求する
 *  ESP-IDF v6.1 系の話に巻き込まれない。ボード計画 §2-1）。
 */
#define TAB5_CHIP_REV_MAJOR  1
#define TAB5_CHIP_REV_MINOR  3

/*
 *  -------------------------------------------------------------------------
 *  I2C（内部バス）[一次: M5GFX.cpp:2526-2531・M5Unified.cpp:115]
 *  -------------------------------------------------------------------------
 *  段T2 はこのピンで実機を掃引する（結果は README §4 が正本）。
 *  TP INT は GT911 の I2C アドレス選択にも効く（high=0x14 / low=0x5D。
 *  M5GFX.cpp:2528-2530）。**段T2 では TP INT を駆動しない**（読み取りのみの規律）。
 */
#define TAB5_I2C_INT_SDA_GPIO   31
#define TAB5_I2C_INT_SCL_GPIO   32
#define TAB5_I2C_TP_INT_GPIO    23
/*  外部（Port A・HY2.0-4P）[一次: M5Unified.cpp:115]。段T2 の negative control に使う  */
#define TAB5_I2C_EXT_SDA_GPIO   53
#define TAB5_I2C_EXT_SCL_GPIO   54

/*
 *  -------------------------------------------------------------------------
 *  内部 I2C に居るデバイス
 *  -------------------------------------------------------------------------
 *  **アドレスはすべて 7bit。** 段T2 の ACK マップと ID 読みで確かめたものは [実測]、
 *  ライブラリの既定値のまま確かめていないものは [未実測] と書く。
 *  実測の詳細（ACK マップ全体・想定との差分）は
 *  `.steering/20260815-tab5-stageT12/README.md` §4 が正本。
 */
/*  IO エキスパンダ PI4IOE5V6408 x2 [一次: M5GFX.cpp:173-174] / [実測: 両方 ACK・
 *  reg0x01(Device ID) がどちらも 0xa0]
 *
 *  【重要・安全】この 2 個は **LCD リセット・タッチリセット・C6 電源**などの
 *  電源系を握っている（M5GFX.cpp:2539-2568 が bit4=LCD Reset / bit5=Touch Reset を
 *  出力として駆動する）。段T2 は**読み取りのみ**である（AC §4-1）。
 *  書く段（表示・C6）を作るときは、何をどの順で書くかを先に決めてから触ること。  */
#define TAB5_I2C_ADDR_PI4IOE_1  0x43
#define TAB5_I2C_ADDR_PI4IOE_2  0x44
/*  電力監視 INA226 [実測 2026-08-15: **0x41**]
 *
 *  【訂正】M5Unified のクラス既定値（INA226_Class.hpp:35）は 0x40 で、ボード計画 §2-3 も
 *  それを想定に使っていたが、**この板では 0x41 に居る**。同定の根拠は Manufacturer ID
 *  reg0xFE = 0x5449（"TI"）と Die ID reg0xFF = 0x2260。0x40 も ACK するが全レジスタが
 *  0xff を返し INA226 ではない（0x40 の正体は未同定）。
 *  ⇒ **ライブラリのクラス既定値を「その板の実配線」として引用しないこと。**  */
#define TAB5_I2C_ADDR_INA226    0x41
/*  RTC RX8130CE [二次: M5Unified RX8130_Class.hpp:14 の既定値] / [実測: 0x32 は ACK する。
 *  ただし RX8130CE には ID レジスタが無く、**同定はしていない**]  */
#define TAB5_I2C_ADDR_RX8130    0x32
/*  IMU BMI270 [実測 2026-08-15: **0x68**]
 *
 *  【訂正】M5Unified のクラス既定値（BMI270_Class.hpp:80）は 0x69 だが、**この板では
 *  0x68**。同定の根拠は reg0x00(CHIP_ID) = 0x24。0x69 は NACK。  */
#define TAB5_I2C_ADDR_BMI270    0x68
/*  タッチ（旧世代 GT911）[一次: Touch_GT911.hpp:30-31]。TP INT の状態で選ばれる。
 *  [実測: この個体では 0x14・0x5D とも NACK]——この板は下の ST7121 世代である  */
#define TAB5_I2C_ADDR_GT911_H   0x14
#define TAB5_I2C_ADDR_GT911_L   0x5D
/*  タッチ（新世代 ST7121/ST7123）[一次: Touch_ST7123.hpp:30] /
 *  [実測: 0x55 が ACK・FW version = 1 ＝ **ST7121**]  */
#define TAB5_I2C_ADDR_ST712X    0x55
/*  コーデック ES8388 [実測: 0x10 が ACK（reg0x00 = 0x12）。0x11 は NACK。
 *  ES8388 であることの同定まではしていない]  */
#define TAB5_I2C_ADDR_ES8388    0x10
/*  カメラ SC202CS（M5Stack 呼称 "SC2356"）[同定: 2026-08-15 段T2 の続き]
 *
 *  reg0x3107(PID high)/0x3108(PID low) を読み、`0xEB`/`0x52` を得た
 *  （期待値 `SC202CS_PID=0xeb52` と一致）。出典:
 *  `esp-video-components/esp_cam_sensor/sensors/sc202cs`（Espressif OSS ドライバ）・
 *  M5Stack 公式 `docs.m5stack.com/en/product_i2c_addr`（Tab5 行）。
 *  「SC2356」と「SC202CS」が同一シリコンかは**推測**（部品表に "SC2356" という型番の
 *  ドライバが見つからず、解像度/インターフェース/SCCB アドレスが一致する SC202CS を
 *  同定に使った。詳細は `.steering/20260815-tab5-stageT12/IDENT.md` §1）。  */
#define TAB5_I2C_ADDR_CAMERA    0x36
/*  マイク前段 ES7210 [推定・同定には至らない: 2026-08-15 段T2 の続き]
 *
 *  アドレス・名前は M5Unified ソース（`M5Unified.cpp:428,1113-1160`）・M5 公式 docs と
 *  一致するが、ID レジスタ（reg0xFD/0xFE/0xFF）は全て `0xff` のまま読めなかった。
 *  M5Unified の有効化シーケンスは `reg0x00=0xFF` の全体リセット後に
 *  `RESET_CTL`/`CLK_ON_OFF` を書いて初めてチップが動く実装であり、これは辻褄が合う
 *  **推測**だが、確かめるには書込みが要るため本段（読み取りのみ）では検証できない。
 *  ⇒ **アドレスの一致だけで同定と言わない**（段T2 の教訓の継続適用）。
 *  詳細は `.steering/20260815-tab5-stageT12/IDENT.md` §2。  */
#define TAB5_I2C_ADDR_ES7210    0x40
/*  未同定の ACK（段T2 実測・2026-08-15 の続きでも変わらず）: **0x28 のみ**。
 *  M5 公式 docs・Espressif esp-bsp/esp_cam_sensor・espp・M5Unified/M5GFX 全文検索・
 *  IP2326/PI4IOE5V6408 データシート・M5Stack コミュニティフォーラムを Web で当たったが
 *  一致する一次資料が無かった（回路図は M5-Schematic に未公開）。名前だけが
 *  「0x28」に紐づく既知チップ（例: BNO055 の既定アドレス）はあるが、Tab5 がその部品を
 *  積んでいるという根拠が無いため候補にも挙げない。詳細は `IDENT.md` §3。  */

/*
 *  -------------------------------------------------------------------------
 *  表示（本段の射程外。判断点 v-2 で「最小表示まで取ってから再判断」）
 *  -------------------------------------------------------------------------
 *  5 インチ IPS 1280x720・MIPI-DSI 2 レーン。[一次: docs.m5stack.com/en/core/Tab5]
 *  DSI パラメタは M5GFX の実績値 [一次: M5GFX.cpp:2608-2614]:
 *    lane_num=2 / lane_mbps = 900(ST7121) or 1040(その他) / ldo_chan_id=3 /
 *    ldo_voltage_mv=2500
 *  パネル世代（ILI9881C+GT911 / ST7123 / ST7121）は個体で違う。**この個体は実測で
 *  ST7121 世代**（0x55 の FW version = 1）。ST7121 は他世代とパラメタが違い、
 *  M5GFX の実績値は lane_mbps=900・dpi_freq_mhz=70・porch h=40/2/40・v=24/20/200
 *  である（M5GFX.cpp:2655-2670）。詳細は README §4-5 が正本。
 */
#define TAB5_LCD_WIDTH        720
#define TAB5_LCD_HEIGHT      1280
#define TAB5_DSI_LANE_NUM       2
#define TAB5_DSI_LDO_CHAN_ID    3

/*
 *  -------------------------------------------------------------------------
 *  ESP32-C6（Wi-Fi/BT。SDIO 接続）——**[一次確認済み・2026-08-16／段7a]**
 *  -------------------------------------------------------------------------
 *  【2026-08-16 更新】段T2 はここを「[二次・未実測]」とし、「使う段では
 *  まず一次確認から始めること」と申し送っていた。段 7a
 *  （`.steering/20260816-p4-7a-sdio/`）で**独立な 2 つの一次情報が一致**した
 *  ので注記を外す。**値は 1 つも変わらなかった**（二次情報が正しかった）が、
 *  「合っていた」ことと「確かめた」ことは別である。
 *
 *  一次情報 (1) 回路図 `Tab5_Schematics_PDF.pdf` p.1（M5Stack 公式。
 *      `https://docs.m5stack.com/en/core/Tab5` からリンク。2026-08-16 取得）
 *      SoC シンボルのピン↔ネット:
 *        pin8 =GPIO8 →`SDIO2_D3`   pin10=GPIO9 →`SDIO2_D2`
 *        pin11=GPIO10→`SDIO2_D1`   pin12=GPIO11→`SDIO2_D0`
 *        pin13=GPIO12→`SDIO2_CLK`  pin14=GPIO13→`SDIO2_CMD`
 *        pin16=GPIO15→`SOC_EXTRF_RST`（→R4 1K→`RF_C6_RST`→C6 U2 pin8 `EN`。
 *                      `EN` は R5 10K で `WLAN_3.3V` へプルアップ＝**active low**）
 *      6 線はいずれも 22R 直列（R14/R15）＋5.1K プルアップ（R17/R18）を経由する。
 *      拡張器 U7(0x44): **P0=`WLAN_PWR_EN`** / P1,P2=NC / P3=`USB5V_EN` /
 *        **P4=`PWROFF_PLUSE`**（立てると板の電源が落ちる。触らないこと） /
 *        P5=`nCHG_QC_EN` / P6=`CHG_STAT` / P7=`CHG_EN`
 *  一次情報 (2) 公式ファクトリデモ `m5stack/M5Tab5-UserDemo`（HEAD 68b19d37）
 *      `platforms/tab5/sdkconfig:2885-2896`:
 *        `ESP_HOSTED_SDIO_PIN_CMD=13` `_CLK=12` `_D0=11` `_D1=10` `_D2=9` `_D3=8`
 *        `_GPIO_RESET_SLAVE=15` `_RESET_ACTIVE_LOW=y` `_BUS_WIDTH=4`
 *        `_CLOCK_FREQ_KHZ=40000`
 *      `platforms/tab5/main/hal/hal_esp32.cpp:129-171` が G8-G13,G15 を
 *        「esp-hosted esp32c6」として列挙し、`GPIO_DRIVE_CAP_0` を設定する。
 *      `components/m5stack_tab5/m5stack_tab5.c:319-323,482-505` が
 *        拡張器 0x44 の Output(0x05) **bit0** を `WLAN_PWR_EN` として操作する。
 *  補強 (3) C6 側は**シリコン固定**:
 *      `esp-idf/components/soc/esp32c6/include/soc/sdio_slave_pins.h:8-13`
 *      CMD=18/CLK=19/D0=20/D1=21/D2=22/D3=23 は回路図の U2 pin24-29 と一致。
 *
 *  ⇒ (1)(2) は 8 項目すべてで一致した。実機での列挙結果は
 *     `.steering/20260816-p4-7a-sdio/README.md` を参照。
 */
#define TAB5_C6_SDIO_CLK_GPIO   12  /* [一次確認済み] SDIO2_CLK */
#define TAB5_C6_SDIO_CMD_GPIO   13  /* [一次確認済み] SDIO2_CMD */
#define TAB5_C6_SDIO_D0_GPIO    11  /* [一次確認済み] SDIO2_D0 */
#define TAB5_C6_SDIO_D1_GPIO    10  /* [一次確認済み] SDIO2_D1 */
#define TAB5_C6_SDIO_D2_GPIO     9  /* [一次確認済み] SDIO2_D2 */
#define TAB5_C6_SDIO_D3_GPIO     8  /* [一次確認済み] SDIO2_D3 */
#define TAB5_C6_SDIO_RST_GPIO   15  /* [一次確認済み] SOC_EXTRF_RST → C6 EN（active low）*/
/*  C6 の電源 = 拡張器 0x44 の Output(0x05) bit0 = `WLAN_PWR_EN` [一次確認済み]  */
#define TAB5_C6_PWR_EXPANDER    TAB5_I2C_ADDR_PI4IOE_2
#define TAB5_C6_PWR_REG         0x05
#define TAB5_C6_PWR_BIT         0x01
/*  **触ってはならない**: 同じ 0x05 の bit4 = `PWROFF_PLUSE`（板の電源断）  */
#define TAB5_EXP2_PWROFF_BIT    0x10

/*
 *  -------------------------------------------------------------------------
 *  microSD [二次: M5Unified.cpp:198 の表。列の意味は確かめていない・未実測]
 *  -------------------------------------------------------------------------
 *  M5 公式は「G39-G44」[一次] としか書いていない。個別割当は使う段で一次確認すること。
 */
#define TAB5_SD_GPIO_LOW       39
#define TAB5_SD_GPIO_HIGH      44

/*
 *  =========================================================================
 *  【重要】ここから下は **ファイル末尾に足すこと**（2026-08-18・段T6 の実測）
 *  =========================================================================
 *  P4 の `app_xip.bin` は esptool `elf2image` が **ELF 全体の sha256** を
 *  `esp_app_desc_t.app_elf_sha256` へ書き込む（段P4G）。ELF には DWARF が入るので、
 *  **本ヘッダの途中に行を挿すと機械語が同一でも golden の sha256 が動く**
 *  （段T6 で `esp/p4disp/p4disp_i2c.h` の途中に 1 ブロック挿して実際に
 *   2 構成が DIFF になった。`.steering/20260818-tab5-t6-touch/logs/
 *   29-matrix-ABORTED-headerdrift.txt`）。**足すなら末尾へ。**
 *
 *  -------------------------------------------------------------------------
 *  タッチ（段T6・2026-08-18 実測。記録 `.steering/20260818-tab5-t6-touch/`）
 *  -------------------------------------------------------------------------
 *  上の `TAB5_I2C_ADDR_ST712X`(0x55) の実体を、Sitronix の一次資料
 *  （`ST7123 Touch Screen Controller Interface Protocol A` V01.11・**M5Stack 自身が
 *  配布**: `https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/
 *  ST7123-TDDI-Interface-Protocol-V01.11.pdf.pdf`・2026-08-18 取得・sha256
 *  `40db033ce31fa5785dac1698b9271561382609150e6d3e7b1f5fcdb9f838efce`）の
 *  レジスタ地図で読み出した。**[実測]**:
 *      FW version(0x0000) = 1（M5GFX の Tab5 autodetect が **ST7121** と呼ぶ値）
 *      Status(0x0001)     = 0x00（Device Status=Normal / Error Code=No Error）
 *      Max X(0x0005/6)    = **720** ／ Max Y(0x0007/8) = **1280**（パネル寸法と一致）
 *      Max Touches(0x0009)= **10**
 *      FW Revision(0x000C..0x000F) = 01 50 01 10
 *  **ST712x にはチップ ID レジスタが無い**ので「ID が一致したから ST7121」とは
 *  言えない。言えるのは「Sitronix Touch IC の既定アドレス 0x55 に居て、protocol doc
 *  §6 のレジスタ地図がそのとおり読める」ことまで（型番はシリコンから読んでいない）。
 *
 *  **レジスタアドレスは 16bit**（doc §5.3.2。`S|addr+W|RegH|RegL|…` の形）。
 *  実装は `esp/p4touch/p4touch_st7123.{c,h}`（`-DA1_P4_TOUCH=ON` で opt-in）。
 */
/*  タッチのリセット = IO エキスパンダ 0x43(U6) の **P5 = `TP_RST`**。
 *  [回路図・段T6 で netlist から再確認]: `TP_RST` は **U6-5 と J3-7 の 2 ピンだけ**の網で、
 *  **プルアップもプルダウンも無い**＝ I2C で 0x43 を叩く以外に触る手段が無い。  */
#define TAB5_EXP1_TP_RST_BIT    0x20
/*  タッチの INT = **GPIO23**（既に `TAB5_I2C_TP_INT_GPIO` にある）。
 *  [回路図]: `TP_INT_GPIO23` は U1-25(GPIO23) / J3-8 / R92-1 の 3 ピンの網で、
 *  **R92 = 5.1K で `SOC_3.3V` へプルアップ**。レベルシフトも ESD も無い。
 *  [実測 段T6]: 無タッチ時は **High で一定**（200 回の観測で変化 0 回）。
 *  内部プルアップの有無で値が変わらない（外部 5.1K が効いている）ことも実測。
 *  **極性は Sitronix の doc に明記が無い**ので「無タッチ＝High」の実測のみを記録し、
 *  「active low」と断定はしない。  */

/*
 *  -------------------------------------------------------------------------
 *  0x28 の決着（段T2 からの持ち越し・段T6 で**居場所**が確定した）
 *  -------------------------------------------------------------------------
 *  上の「未同定の ACK: 0x28 のみ」は**取り下げない**（型番は今も未同定）。
 *  ただし **居場所は確定した**:
 *
 *  (a) [回路図・netlist の閉じた数え上げ] 内部 I2C（`xG31_SYS_SDA`/`xG32_SYS_SCL`）に
 *      繋がるピンは **16 + 16 本ちょうど**で、その全部を分類すると基板上の
 *      I2C スレーブは U6(0x43)/U7(0x44)/U13 ES7210(0x40)/U14 ES8388(0x10)/
 *      U19 BMI270(0x68)/U30 RX8130CE(0x32)/U31 INA226(0x41) の **7 個**だけ。
 *      設計中の全 38 個の U 番号を突き合わせても、I2C 網に居るのはこの 7 個
 *      ＋ U1(マスタ)・U36(TXS0102 レベル変換)・Q13(BMI270 用) のみ。
 *      **基板上に 0x28 になり得る IC は無い。** 残る居場所は基板外の
 *      J3（LCD+タッチ FPC）／J4（カメラ FPC）／J7／BUS1。
 *  (b) [実測 段T6] `TP_RST`（= J3 の先だけを殺す）を assert すると
 *      **0x28 は 0x55 と一緒に ACK しなくなり、解除すると一緒に戻る**。
 *      同時に基板上の 7 個は ACK し続ける（positive/negative control つき）。
 *
 *  ⇒ **0x28 は J3 の先＝タッチパネル FPC 側に居る。**
 *  **型番は依然として未同定**（「ST7121 の第 2 アドレス」とは言わない
 *  ——Sitronix protocol doc V01.11 §5.3 には既定 0x55 しか書かれておらず、
 *  第 2 アドレスの記述が無い）。
 */

/*
 *  -------------------------------------------------------------------------
 *  カメラ(0x36) がビットバン I2C から見えない（段T6 の新しい未解決）
 *  -------------------------------------------------------------------------
 *  段T2 は 0x36 が ACK し PID `0xEB52` まで読めたと実測しているが、
 *  それは **ESP-IDF のハードウェア I2C マスタ**（`esp/p4probe/i2c_scan`）で採ったもの。
 *  段T6 の **ビットバン I2C**（`esp/p4disp/p4disp_i2c.c`）では **0x36 だけが ACK しない**
 *  （他の 9 個は ACK する）。**拡張器へ 1 バイトも書く前**の時点で既に ACK しないので、
 *  本 repo の書込みが原因ではない（段T6 run5 で切り分け済み）。
 *  0x36 は内部 I2C 上で唯一 **TXS0102 レベル変換器（U36）の向こう側**に居るので
 *  「変換器と遅いビットバンの相性」が疑わしい——が、**これは推測であって未確認**。
 *  カメラを使う段で確かめること。
 */

#endif /* M5STACK_TAB5 */

#endif /* TAB5_KIT_H */
