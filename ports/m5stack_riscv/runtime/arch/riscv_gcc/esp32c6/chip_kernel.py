# -*- coding: utf-8 -*-
#
#		パス2の生成スクリプトのチップ依存部（ESP32-C6用・FMP3）
#
#  asp3_core arch/riscv_gcc/esp32c6/chip_kernel.py（INTNO_VALID=1..31）を
#  FMP3 の型（fmp3/arch/riscv_gcc/esp32p4/chip_kernel.py）へ写したもの。
#  INTNO_VALID[prcid] は CPU割込み線番号（0..31。線 0 は使わないが，
#  kernel_cfg.c の _kernel_intcfg_table_prc1[] の添字と線番号を一致させる
#  ために 0 から並べる＝chip_kernel_impl.h の check_intno_cfg が
#  INTNO_MASK(intno) で直接添字にする）。INHNO は (prcid << 16) | 線番号。
#
#  段4 fix wave 1 (C, Codex #2 / Fable Low-2): INTNO_VALID から単純に線 0 を
#  抜く修正（range(1, 32)）は**やってはいけない**-- fmp3_core の
#  arch/riscv_gcc/common/core_kernel.py（読み取り専用）はテーブルを
#  `enumerate(INTNO_VALID[prcid])` の**位置**（0起点の連番）で生成する一方、
#  実行時は `p_intcfg_table[prcidx][INTNO_MASK(intno)]` で**値そのもの**を
#  添字にする（chip_kernel_impl.h:138）。INTNO_VALID が 0 から連番でなければ
#  「生成順の位置」と「実行時の添字」がずれ、CFG_INT で設定した割込みが
#  別の線のフラグとして誤って書かれる（実測はしていないが、生成コードを
#  読めば構造的に導ける。上のコメントが最初からそう書いていた理由）。
#  よって INTNO_VALID の形（テーブルの箱の大きさと並び）は変えず、
#  「CFG_INT に書ける値」だけを TargetCheckCfgInt で線 0 を拒否する形にする
#  （chip_kernel_impl.h の TMIN_INTNO=1 と同じ境界を cfg 時点でも効かせる）。
#

INTNO_VALID = {}
INHNO_VALID = {}
intmtx_intno_list = list(range(0, 32))
for prcid in range(1, TNUM_PRCID + 1):
    INTNO_VALID[prcid] = []
    INHNO_VALID[prcid] = []
    for intno in intmtx_intno_list:
        INTNO_VALID[prcid].append(intno)
        INHNO_VALID[prcid].append((prcid << 16) | intno)

#
#  CFG_INTのターゲット依存のチェック（単一コアなので割付け可能プロセッサは
#  常に初期割付けプロセッサと一致する。P4/polarfire と同じ検査を残す）
#
#  段4 fix wave 1 (C): 線 0 は intmtx 上に実体が無い（chip_kernel_impl.h の
#  TMIN_INTNO=1、check_intno_cfg の assert 参照）。INTNO_VALID 自体は上記の
#  理由で 0 を含めたまま（テーブル生成の都合）なので、ここで別途 E_PAR に
#  する。intno は (prcid << 16) | 線番号 の形を取りうる（上の affinity 検査と
#  同じ形）ため、下位16ビット（線番号）だけを見る。
#
def TargetCheckCfgInt(params):
    if (params["intno"] & 0xffff) == 0:
        error_ercd("E_PAR", params, "%%intno line 0 does not exist on "
                   "ESP32-C6 (TMIN_INTNO=1, chip_kernel_impl.h).")
    if ((params["intno"] >> 16) == 0) \
            and (clsData[params["class"]]["affinityPrcBitmap"]
                 != (1 << (clsData[params["class"]]["initPrc"] - 1))):
        error_ercd("E_RSATR", params, "%%intno is configured "
                   "to be accepted by more than one processors, "
                   "which is not supported on this target.")

#
#  生成スクリプトのコア依存部
#
IncludeTrb("core_kernel.py")
