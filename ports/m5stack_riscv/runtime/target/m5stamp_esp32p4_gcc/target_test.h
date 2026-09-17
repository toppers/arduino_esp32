/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 * 
 *  Copyright (C) 2007-2018 by Embedded and Real-Time Systems Laboratory
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
 *  $Id: target_test.h 166 2019-08-28 07:54:41Z ertl-honda $
 */

/*
 *		テストプログラムのターゲット依存部（ZYBO_Z7用）
 */

#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

/*
 *  コアで共通な定義（チップ依存部は飛ばす）
 */
#include "core_test.h"
#include "esp32p4.h"

/*
 *  割込みテスト(test_int1)用の割込み番号と属性
 *    ESP32-P4 では CLIC 外部線(16〜47)の空き線をテスト用に使う。
 *    16 は USB-Serial/JTAG が使用するため 17 を割当てる。
 *    software で割込みを発生(ras_int=IP セット)させるためエッジトリガ(TA_EDGE)。
 *    割込みマトリクスのソースは割付けない(software IP のみで発生)。
 */
#define INTNO1          17
/*
 *  C-3 項目2（2026-08-15）: 優先度を (-4) から TMAX_INTPRI(-1) へ変更した。
 *  S3/LX6 の INTNO1 も TMAX_INTPRI（xtensa の XCHAL_INT7_LEVEL=1 相当）を使う。
 *  P4 の CLIC はレベル選択が自由（xtensa と違いハードで固定されない）ので、
 *  (-4) は本ポートが独自に選んだ値だった。fmp3/test_ovr/test_ipmmask.c は
 *  S3 向けに書かれており chg_ipm(-1) が「INTNO1 の優先度をマスクする」ことを
 *  ハードコードで前提にしている（CLIC は inclusive threshold なので
 *  chg_ipm(-N) は「優先度 -N 以下」しか止めず、INTNO1 が (-4) のままだと
 *  chg_ipm(-1) では止まらず FAIL する。実測で確認・
 *  .steering/20260815-c3-smallitems/item2-p4-test-ipmmask.md）。
 *  test_int1/test_dcre5 は INTNO1 の優先度の数値そのものに依存しないため、
 *  この変更で振る舞いは変わらない（実機で両方 PASS を再確認済み）。
 */
#define INTNO1_INTPRI   TMAX_INTPRI
#define INTNO1_INTATR   (TA_ENAINT | TA_EDGE)

/*
 *  割込みソースのクリア(ISR から呼ばれる)。
 *    software raise したエッジ割込みの IP を CLIC レジスタ直接操作でクリア。
 *    esp32p4.h の CLIC_INT_CTRL/CLIC_INT_IP_BIT を使い自己完結させる。
 */
#define intno1_clear() \
    (*(volatile uint32_t *)CLIC_INT_CTRL(INTNO1) &= ~CLIC_INT_IP_BIT)

/*
 * ============================================================================
 *  ここから下は **svn からの意図的な乖離**（fmp3_esp_idf_dev・2026-08-15 段5）
 * ============================================================================
 *  出典・乖離の記録: fmp3/target/m5stamp_esp32p4_gcc/IMPORT_PROVENANCE.md
 *  判定基準:         .steering/20260815-p4-stage5/AC.md AC-4
 *
 *  なぜ足すか（事実）:
 *    上流（svn 3.4）の本ファイルは INTNO1 しか定義していない。そのため
 *    複数の割込み番号を要求するテストが**建たない**——段3 で `test_dcre5` が
 *    `'INTNO3' undeclared` 等 6 件のエラーで rc=2 になることを実演した
 *    （.steering/20260814-p4-stage3/README.md §2-1）。上流は P4 で test/ を
 *    走らせる際に対象を絞ることで回避していたが、本 repo は
 *    「fmp3_core/test/ の全件を実機で回す」体系なので、target 層に足す。
 *
 *  番号の選定（すべて一次資料。名前からの推測で決めていない）:
 *    使用中: 3=IPI(CLINT msip)・7=tick(CLINT mtime)  … esp32p4.h:97-98
 *           16=USB-Serial/JTAG(INTNO_SIO)           … target_syssvc.h:49
 *           17=INTNO1（上）                          … 本ファイル
 *           18=INTNO_UNOPTED(=INTNO1+1)              … fmp3_core/test/test_dcre5.h:23
 *                （テストが「有効範囲内だが未登録」として使う＝登録禁止）
 *           46=EMAC(Ethernet・段E)                   … esp/eth/os/fmp3_eth_pools.h:141
 *           47=SDMMC(慣行)・22=IDF の matrix 切断先・40..44=IDF vectors_clic 予約
 *                … esp/eth/os/fmp3_eth_pools.h:132-134, idf_riscv_intr_impl.md:123,157-158,263
 *    ⇒ 45（末尾側の空き。46/47 の隣）と 19（16/17/18 の直後の空き）を選ぶ。
 *    **この選定は毎ビルド機械照合する**（cmake/a1_p4_intno_audit.sh。
 *    configure 時 fail-closed。線が増えたら黙って壊れるのを止めるため）。
 *
 *  属性の根拠:
 *    - TA_EDGE: software から IP を立てて発火させる（ras_int）にはエッジ線であることが
 *      要る（fmp3_core/arch/riscv_gcc/common/clic_kernel_impl.c:104-108）。
 *    - TA_ENAINT: clic_config_int はこの属性のときだけ IE を立てる（同 :110-113）。
 *    - INTPRI は TMIN_INTPRI(-7)..TMAX_INTPRI(-1) の範囲（chip_kernel.h:55-56）。
 *      INTNO1 と同じ -4 に揃える（INTNO1 の値・属性・優先度は**変えない**＝AC-4f）。
 *    - intnoN_clear() は ISR から呼ばれる（test_dcre5.c:233,272,313,334）。
 *      INTNO1 と同じくレジスタ直叩きに揃える（clr_int() を使うと
 *      chip_kernel_impl.c:183-185 が注記する MODE ビットの扱いが別経路になる）。
 *
 *  割込みマトリクスのソースは割付けない（software IP のみで発生）＝INTNO1 と同じ。
 */
#define INTNO2          45
#define INTNO2_INTPRI   (-4)
#define INTNO2_INTATR   (TA_ENAINT | TA_EDGE)

#define intno2_clear() \
    (*(volatile uint32_t *)CLIC_INT_CTRL(INTNO2) &= ~CLIC_INT_IP_BIT)

#define INTNO3          19
#define INTNO3_INTPRI   (-4)
#define INTNO3_INTATR   (TA_ENAINT | TA_EDGE)

#define intno3_clear() \
    (*(volatile uint32_t *)CLIC_INT_CTRL(INTNO3) &= ~CLIC_INT_IP_BIT)

#endif /* TOPPERS_TARGET_TEST_H */
