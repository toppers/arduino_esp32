/*
 *  テストプログラム用のターゲット依存定義（無印ESP32 / Xtensa LX6）
 *
 *  test_int1（割込み管理: dis/ena/clr/ras/prb_int）用にソフトウェア割込み
 *  INTNO1を定義する。ESP32-S3のXtensaコアのINT7はレベル1のソフトウェア
 *  割込み（XCHAL_INT7_TYPE=SOFTWARE, XCHAL_INT7_LEVEL=1）で、INTSET/
 *  INTCLEAR/INTENABLE/INTERRUPTレジスタで ras/clr/dis-ena/prb を実現できる。
 *  ras_intでの発火→ISRディスパッチは_kernel_l1int_dispatch（core_support.S
 *  経由でcore_kernel_impl.c）がINT7を判定してCRE_ISRのISRを呼ぶ。
 */
#ifndef TOPPERS_TARGET_TEST_H
#define TOPPERS_TARGET_TEST_H

#include <sil.h>

#ifndef STACK_SIZE
#define STACK_SIZE	(4096)	/* windowed ABIのウィンドウスピル分、余裕をもつ */
#endif /* STACK_SIZE */

/*
 *  テストで使用するソフトウェア割込み（INT7：レベル1ソフトウェア割込み）
 */
#define INTNO1			7U
#define INTNO1_INTATR	(TA_ENAINT | TA_EDGE)
#define INTNO1_INTPRI	TMAX_INTPRI		/* レベル1（XCHAL_INT7_LEVEL） */

/*
 *  ISRが割込み要求をクリアするための処理（INTCLEARでINT7要因をクリア）
 */
#define intno1_clear()	clr_int(INTNO1)

/*
 *  test_dcre5（動的ISR生成: acre_isr/del_isr）が使う2本目・3本目の割込み
 *
 *  2026-08-04（段5）に **INTNO2 を INT10 から INT29 へ変更した。**
 *  旧 INTNO2=INT10 は **ras_int で発火しない**＝壊れていた。以下に経緯を残す。
 *
 *  ──────────────────────────────────────────────────────────────────────
 *  【何が間違っていたか】ras_int できるのは SOFTWARE 型だけである
 *  ──────────────────────────────────────────────────────────────────────
 *  本ヘッダは 2026-08-04 の INTNO2 導入時（commit 1fe54b0）に、条件(1)を
 *
 *      「INTSET/INTCLEARで要求をセット／クリアできること
 *        ＝ SOFTWARE **または EXTERN_EDGE** 型であること」
 *
 *  と書いていた。**この「または EXTERN_EDGE」が誤りである。**
 *  WSR.INTSET が立てられるのは **SOFTWARE 型のビットだけ**で、EXTERN_EDGE 型の
 *  線は外部エッジでしか要求が立たない。
 *
 *  一次証拠（推測ではない。2つある）:
 *   (a) QEMU 実装（qemu/target/xtensa/exc_helper.c の HELPER(intset)）:
 *          qatomic_or(&env->sregs[INTSET],
 *                     v & env->config->inttype_mask[INTTYPE_SOFTWARE]);
 *       ⇒ **SOFTWARE 型でマスクされる。**INT10(EXTERN_EDGE) は素通りで消える。
 *   (b) 実測 A/B（判定文字列ではなく実際の出力で比べた）:
 *          INTNO2=10 … test_dcre5 は「Check point 5 passed」の直後、
 *                       手順5（quiesce 実証）の `check_assert(long_started)` で
 *                       **FAIL**（PRC2 の long_isr が起動しない）。
 *          INTNO2=29 … **PASS**。
 *       生ログ: 非公開作業記録/20260804-dcre-stage5-pin/logs/dcre5-intno2-ab.txt
 *
 *  なぜ 5 段のあいだ気づかなかったか: test_dcre5 は INTNO2 未定義で
 *    **BUILD_ERROR のまま**だった（段0〜段4）。**ビルドが通らないテストは、
 *    定義の誤りを 1 ミリも検査しない。**pin 更新でビルドが通って初めて露見した。
 *
 *  ──────────────────────────────────────────────────────────────────────
 *  【要求の違い】INTNO2 は発火する／INTNO3 は発火しない
 *  ──────────────────────────────────────────────────────────────────────
 *  INTNO2（PRC2）… test_dcre5.c の task3 が `ras_int(INTNO2)` で**発火させる**。
 *      long_isr / drain_isr_a / drain_isr_b が走り `intno2_clear()` で落とす。
 *      静的 ISR を持たない「動的専用」の線として ENA_DYNISR(INTNO2) される。
 *      ⇒ 次の3つを満たすこと:
 *        (1) **SOFTWARE 型であること**（上記。ras_int/clr_int が効く唯一の型）。
 *        (2) その線のレベルに対応するディスパッチ経路が在ること。
 *        (3) 他用途と衝突せず、INTNO1 でも INTNO1+1 でもないこと
 *            （INTNO1+1 は test_dcre5.h の INTNO_UNOPTED が使う）。
 *
 *  INTNO3（PRC1）… `CFG_INT` ＋ `CRE_ISR(ISR_SELF, …)` で**登録されるだけ**で
 *      **決して発火しない**（cfg・c 両方のコメントが明示。ras_int されない）。
 *      fmp3_core commit 41656ea が、syssvc/serial.cfg 由来の ISR_SIO への依存を
 *      解消して test_dcre5 を自立化したときに導入した。本ポートは
 *      test_ovr/test_common1.cfg で serial.cfg を外すため ISR_SIO が存在せず、
 *      これが無いと test_dcre5 はビルドできない。
 *      用途は (a) 2レンジID検証（erid > ISR_SELF）と
 *              (b) del_isr(ISR_SELF) が E_OBJ を返すこと、の2点のみ。
 *      ⇒ 要求は「CFG_INT/CRE_ISR の有効範囲内で、他用途と衝突しない」だけ。
 *        (1)(2) は要らない。発火させないからである。
 *
 *  ──────────────────────────────────────────────────────────────────────
 *  【番号の実測】推測せず core-isa.h を gcc -E -dM で読んだ
 *  ──────────────────────────────────────────────────────────────────────
 *  生ログ: 非公開作業記録/20260804-dcre-stage5-pin/logs/coreisa.txt
 *
 *      XCHAL_INTTYPE_MASK_SOFTWARE    = 0x20000080 → INT7, INT29
 *      XCHAL_INTTYPE_MASK_EXTERN_EDGE = 0x50400400 → INT10, INT22, INT28, INT30
 *      XCHAL_INTLEVEL1_MASK           = 0x000637FF → INT0-10,12,13,17,18
 *      XCHAL_INTLEVEL2_MASK           = 0x00380000 → INT19,20,21
 *      XCHAL_INTLEVEL3_MASK           = 0x28C08800 → INT11,15,22,23,27,29
 *      XCHAL_EXCM_LEVEL               = 3
 *  ESP32-S3 と ESP32(LX6) で上記はすべて同値（実測・差分ゼロ）。
 *    ⇒ 両 target_test.h をバイト同一に保つ規約と衝突しない。
 *
 *  【INTNO2 = INT29 を選んだ理由】選択肢は1つしか無い
 *    SOFTWARE 型は **INT7 と INT29 の 2 本だけ**。INT7 は INTNO1 が使う。
 *    ⇒ **INTNO2 の候補は INT29 以外に存在しない。**（INT8 = INTNO_UNOPTED
 *      なので、そもそも SOFTWARE でない上に使ってはならない。）
 *    INT29 は Level-3 だが、本ポートは Level-3 経路を実装済みである:
 *      _kernel_l3int_entry（core_support.S）→ _kernel_l3int_dispatch
 *      （target_timer.c）→ _kernel_inh_handler_tbl[]
 *    このフックは EXCSAVE3 経由で **CLS_PRC1/CLS_PRC2 の両方**から
 *      初期化される（target_timer.cfg。EXCSAVE3 はコア毎レジスタ）ので、
 *      PRC2 に割り付ける INTNO2 でも成立する。
 *    同一リポジトリに前例がある: fmp3/app/l3int/l3int.h:55 の
 *      INTNO_L3SW = 29U（(TA_ENAINT | TA_EDGE) / intpri -2）が
 *      CFG_INT ＋ CRE_ISR で Level-3 経路の検証に使われている。
 *
 *  【INTNO3 = INT10 を選んだ理由／却下した候補】
 *    INTNO3 は発火しないので、空いていて衝突しなければ何でもよい。
 *    INT29 が INTNO2 へ移ったので、旧 INTNO2 の INT10 をそのまま INTNO3 が
 *      引き継ぐのが**新たな線を1本も消費しない**唯一の選び方である。
 *      （INT10 は EXTERN_EDGE・Level-1 で、本ポートのどの用途にも現れない。）
 *    却下: INT11(PROFILING)/INT15(TIMER=CCOMPARE1)/INT22(EXTERN_EDGE,L3)/
 *          INT19-21(L2, EXTERN_LEVEL) … いずれも「線を新たに1本消費する」上に、
 *          L2/L3 の空きは ENA_DYNISR（将来の Arduino 対応。
 *          非公開作業記録/20260804-arduino-support/）で使える数少ない候補なので、
 *          発火しないだけの用途に充てるのは惜しい。
 *
 *  (3)の確認（割込み線の棚卸し。INT10 と INT29 はどちらも他用途に現れない）:
 *      INT0-3 = Wi-Fi/BT blob   INT4/8/9/12 = ESP-IDF割込み確保シムのスロット
 *      INT5   = UART0コンソール INT6  = tick(XT_TIMER_INTNUM)
 *      INT7   = INTNO1          INT10 = INTNO3（旧 INTNO2）
 *      INT13  = IPI(XT_IPI_INTNUM。LX6はシムのスロット1)
 *      INT17  = USJコンソール   INT18 = LX6のUSJコンソール
 *      INT23/27 = BTコントローラ(Level-3)  INT29 = INTNO2
 *      INT8 = INTNO_UNOPTED（= INTNO1 + 1。test_dcre5.h が「有効範囲内だが
 *        CFG_INT も ENA_DYNISR もされていない番号」として使う）。テストビルドは
 *        esp/shim をリンクせず esp_shim.cfg も INCLUDE しないので、線8 は
 *        テストビルドでは未登録のままであり要求を満たす。
 *
 *  fmp3/app/l3int の INTNO_L3SW と INTNO2 の値が同じ(29)である点について:
 *    両者は別マクロで、同一の cfg に同居しない（l3int.cfg と test_dcre5.cfg は
 *    別アプリ）。万一同居させると cfg が「intno 29 is duplicated」で
 *    **ビルドエラーになる**＝fail-closed であり、黙って壊れることはない。
 *
 *  intpri について: config_int（core_kernel_impl.c）は intpri を読まず
 *    TA_ENAINT の有無しか見ない＝本ポートの intpri はハードのレベルと
 *    結びついていない（IPI が INTPRI_IPI=-2 でレベル1の線13 を使うのが実例）。
 *    Level-3 の線には l3int と同じ -2 を書く。これは「決めた」のではなく
 *    既存の流儀を写した。
 *
 *  「本ヘッダはテストビルド専用」ではない（2026-08-04 実測。推測しかけて外した）。
 *  seam の golden 10 構成は既定アプリが `test_int1`（CMakeLists.txt の
 *  A1_APP 既定値）で、test_svc.h 経由で本ヘッダを include する
 *  （`ninja -C build/seam-s3-smp -t deps` に target_test.h が出る）。
 *  ただし INTNO2/INTNO3 は**どのアプリからも参照されない未使用マクロ**なので、
 *  プリプロセス結果は1トークンも変わらず、golden の app_xip.bin は不変である
 *  （段5 で PRESETS=ALL の 10 構成を**追加前後で建てて実測**した。
 *    非公開作業記録/20260804-dcre-stage5-pin/RESULT.md）。
 *  将来ここへ「使われる定義」を足すときは、golden の再測が要る。
 *
 *  esp/shim の ISR 確保（esp_shim_intr_lines.h）とは、テストビルドでは
 *  esp/shim がリンクされないため競合しない。seam 構成では CFG_INT 自体を
 *  test_int1.cfg / test_dcre5.cfg 側が持つので、本ヘッダの定義だけでは
 *  線を占有しない。
 */
#define INTNO2			29U
#define INTNO2_INTATR	(TA_ENAINT | TA_EDGE)
#define INTNO2_INTPRI	(-2)			/* 読み捨てられる。レベルはHW固定で3 */

/*
 *  ISRが割込み要求をクリアするための処理（INTCLEARでINT29要因をクリア）
 */
#define intno2_clear()	clr_int(INTNO2)

#define INTNO3			10U
#define INTNO3_INTATR	(TA_ENAINT | TA_EDGE)
#define INTNO3_INTPRI	TMAX_INTPRI		/* レベル1（XCHAL_INT10_LEVEL） */

/*
 *  ISRが割込み要求をクリアするための処理（INTCLEARでINT10要因をクリア）
 *
 *  呼ばれない（static_isr_self は発火しない）。コンパイルが通ることだけが要件。
 */
#define intno3_clear()	clr_int(INTNO3)

#include "core_test.h"

#endif /* TOPPERS_TARGET_TEST_H */
