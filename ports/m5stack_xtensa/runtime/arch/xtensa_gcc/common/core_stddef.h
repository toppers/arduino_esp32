/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 * 
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2020 by Embedded and Real-Time Systems Laboratory
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
 *  @(#) $Id: core_stddef.h 289 2021-08-05 14:44:10Z ertl-komori $
 */

/*
 *  t_stddef.hのコア依存部（Xtensa用）
 *
 *  このインクルードファイルは，target_stddef.h（または，そこからインク
 *  ルードされるファイル）のみからインクルードされる．他のファイルから
 *  直接インクルードしてはならない．
 *
 *  本ファイルはFMP3のarm_m_gcc（Cortex-M）移植を土台に、esp32_s3プロジェクト
 *  。
 */

#ifndef TOPPERS_CORE_STDDEF_H
#define TOPPERS_CORE_STDDEF_H

/*
 *  旧マクロ名 TOPPERS_ESP32 の検出
 *
 *  無印ESP32(LX6)を指すマクロ TOPPERS_ESP32 は TOPPERS_ESP32_LX6 へ改名した。
 *  旧名は TOPPERS_ESP32S3 の prefix であり、grep が S3用マクロにもヒットする／
 *  「TOPPERS_ESP32S3_BT_NIMBLE（現 TOPPERS_BT_HOST_NIMBLE。下記の第2次改名で
 *  リネーム）はチップ名ではなくBTホスト名」といった誤読の
 *  温床になっていた（実害あり）。
 *
 *  ★このガードの目的は「改名漏れを静かに失敗させない」こと。
 *  旧名のまま -DTOPPERS_ESP32 を渡すビルドスクリプトが残っていると、
 *  #ifdef TOPPERS_ESP32_LX6 が常にfalseになり **コンパイルは通るが機能だけが
 *  消える**。ここで #error にして一次症状で止める。
 *
 *  ★存続期間：恒久ではなく期限つき。旧名を渡す供給元が無いことを確認できたら
 *  削除してよい。
 *
 *  注意：本ガードは「定義側の残骸」しか捕捉できない。消費側の #ifdef 漏れ
 *  （旧名のまま条件が常にfalse）は検出できないので、grep での確認が別途必要。
 *  （検出パターンは \bTOPPERS_ESP32\b ではなく TOPPERS_ESP32(?![A-Za-z0-9_]) を
 *   使うこと。-DTOPPERS_ESP32 は直前が 'D'＝単語構成文字のため \b が成立せず、
 *   \b 版では定義側を取りこぼす。)
 */
#ifdef TOPPERS_ESP32
#error "TOPPERS_ESP32 は TOPPERS_ESP32_LX6 へ改名されました（2026-07-17）。-DTOPPERS_ESP32 を渡している供給元（esp/build_*incflags*_esp32*.txt、esp/boot/build_*_esp32.sh）を更新してください。"
#endif

/*
 *  旧マクロ名の検出・第2次（誤読を招く名前の改名）
 *
 *  「チップ名に見えて実はチップ非依存」という同型の誤読を生んでいた2マクロを改名した。
 *  いずれも *_S3 / *_ESP32S3_ という綴りがチップ限定を強く示唆するが、実際には
 *  LX6(無印ESP32)のビルドでも定義される＝チップ非依存である。
 *
 *    TOPPERS_ESP32S3_BT_NIMBLE -> TOPPERS_BT_HOST_NIMBLE
 *        意味は「BTホスト=NimBLE」。LX6のBLEビルド
 *        （esp/build_ble_incflags_esp32{,_espidf}.txt）でも -D される。
 *        ★実害：本マクロを「S3限定」と誤読したため BLE の容疑者判定を誤った例がある。
 *    TOPPERS_SEAM_S3 -> TOPPERS_BOOT_SEAM
 *        意味は「起動方式=seam」（実ESP-IDF 2nd-stage bootloader → FMP3 直接ジャンプ）。
 *        「S3」はチップ名ではなく起動方式の呼称であり、LX6用の
 *        build_seam_s3_*_esp32.sh でも -D される。
 *
 *  ★ガードの目的は第1次と同じく「改名漏れを静かに失敗させない」こと。
 *  旧名で -D を渡す供給元が残っていると #ifdef が常にfalseになり、コンパイルは
 *  通るのに機能だけが消える。ここで一次症状として止める。
 *
 *  ★存続期間：第1次ガードと同様に期限つき。
 *
 *  注意：本ガードも「定義側の残骸」しか捕捉できない（消費側 #ifdef の改名漏れは
 *  検出できない）。検出パターンは \b を使わず
 *  TOPPERS_SEAM_S3(?![A-Za-z0-9_]) 等を使うこと（-DTOPPERS_SEAM_S3 は直前が
 *  'D'＝単語構成文字のため \b が成立せず、\b 版では定義側を取りこぼす）。
 */
#ifdef TOPPERS_ESP32S3_BT_NIMBLE
#error "TOPPERS_ESP32S3_BT_NIMBLE は TOPPERS_BT_HOST_NIMBLE へ改名されました（2026-07-17）。チップ名ではなく「BTホスト=NimBLE」の意であり、LX6のBLEビルドでも定義されます。-D を渡している供給元（esp/build_ble_incflags*.txt）を更新してください。"
#endif

#ifdef TOPPERS_SEAM_S3
#error "TOPPERS_SEAM_S3 は TOPPERS_BOOT_SEAM へ改名されました（2026-07-17）。「S3」はチップ名ではなく起動方式(seam)の呼称であり、LX6用の build_seam_s3_*_esp32.sh でも定義されます。-D を渡している供給元（esp/boot/build_seam_s3_*.sh）を更新してください。"
#endif

/*
 *  TOPPERS_ESP32C3_WIFI は「死語」。どこにも定義されておらず、
 *  esp/shim/esp_shim.c の #endif ラベルとして残っていただけで、実際に
 *  閉じていた条件は #ifdef TOPPERS_ESP_WIFI_WPA2 だった（＝ラベルが条件と不一致）。
 *  2026-07-17 にラベルを実態へ修正した（条件は変更していない＝プリプロセス結果は不変）。
 *  本リポジトリに C3 ターゲットは無い。もし -DTOPPERS_ESP32C3_WIFI を渡す供給元が
 *  現れた場合、それは「定義すれば何か有効になる」という誤解であり、実際には何も
 *  起きない（黙って無視される）ため、ここで止める。
 *  WPA2/PSA 関連を有効化したい場合は TOPPERS_ESP_WIFI_WPA2 を使うこと。
 */
#ifdef TOPPERS_ESP32C3_WIFI
#error "TOPPERS_ESP32C3_WIFI は定義しても効果がありません（死語。2026-07-17 整理）。esp_shim.c の該当条件は TOPPERS_ESP_WIFI_WPA2 です。そちらを使ってください。"
#endif

/*
 *  ターゲットを識別するためのマクロの定義
 */
#define TOPPERS_XTENSA           /* コア略称 */

/*
 *  スタックの型
 *  Xtensa windowed ABIでは，スタックポインタを16byte境界に配置する必要が
 *  ある（ESP-IDF xtensa_context.hに明記。ARM-Mの8byte要件より厳しい）。
 *  riscv_gcc/arm64_gccは__int128（自然アラインメント16byte）を用いるが、
 *  xtensa-esp32s3-elf-gccは__int128非対応（ビルドで実測確認）のため、
 *  明示的に16byteアラインを指定した構造体で代用する。
 */
#ifndef TOPPERS_MACRO_ONLY
typedef struct { long long even, odd; } toppers_stk_t __attribute__((aligned(16)));
#endif /* TOPPERS_MACRO_ONLY */
#define TOPPERS_STK_T  toppers_stk_t
#define TOPPERS_EMPTY_LABEL(type, var) type __attribute__((section(".empty." #var))) var[0]

#endif /* TOPPERS_CORE_STDDEF_H */
