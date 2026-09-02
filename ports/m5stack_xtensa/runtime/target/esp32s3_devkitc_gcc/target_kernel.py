# -*- coding: utf-8 -*-
#
#		パス2の生成スクリプトのターゲット依存部（ESP32-S3用）
#
#  ARM-M版（rp2350_pico2_gcc）は静的ベクタテーブル・例外/割込みハンドラ
#  テーブル・CFG_INT用ビットパターンテーブルを生成していたが、Xtensa設計は
#  VECBASEを変更せずROM提供のXTOS APIで個別に割込みハンドラを登録する方式の
#  ため、それらの生成コードは
#  持たない。
#
#  一方、INTNO_VALID等の有効範囲定義はkernel/kernel.py（target非依存部）が
#  要求する必須のインタフェース契約のため、ここでは維持する。
#
#  ★Ruby の範囲は閉区間である。翻訳時の off-by-one に注意:
#      0.upto($TMAX_INTNO)             -> range(0, TMAX_INTNO + 1)
#      (0..63).to_a                    -> list(range(0, 64))
#      (-(1 << $TBITW_IPRI)..-1).to_a  -> list(range(-(1 << TBITW_IPRI), 0))
#

#
#  有効な割込み番号，割込みハンドラ番号
#
#  Xtensaの割込み番号はINTENABLE/INTERRUPTレジスタのビット位置（0〜31）に対応する。
#  ESP32-S3のデュアルコアは同一のintno空間を各コアがローカルに持つため prcid
#  符号化はせず、どのコアの割込みかはCFG_INT/CRE_ISRを囲むCLASSのアフィニティで決まる。
#
INHNO_VALID = {}
INTNO_VALID = {}
for prcid in range(1, TNUM_PRCID + 1):
    INHNO_VALID[prcid] = []
    INTNO_VALID[prcid] = []
    for intno in range(0, TMAX_INTNO + 1):
        INHNO_VALID[prcid].append(intno)
        INTNO_VALID[prcid].append(intno)

#
#  CPU例外番号(EXCNO)は (prcid<<16)|EXCCAUSE で符号化する
#  （core_exc_entry/define_exc が EXCCAUSE 部を & 0x3F で取り出す）
#
EXCNO_VALID = {}
excno_list = list(range(0, 64))
for prcid in range(1, TNUM_PRCID + 1):
    EXCNO_VALID[prcid] = []
    for excno in excno_list:
        EXCNO_VALID[prcid].append((prcid << 16) | excno)

#
#  CRE_ISRで使用できる割込み番号とそれに対応する割込みハンドラ番号
#
INTNO_CREISR_VALID = INTNO_VALID
INHNO_CREISR_VALID = INHNO_VALID

#
#  DEF_INH／DEF_EXCで使用できる割込みハンドラ番号／CPU例外ハンドラ番号
#
INHNO_DEFINH_VALID = INHNO_VALID
EXCNO_DEFEXC_VALID = EXCNO_VALID

#
#  CFG_INTで使用できる割込み番号と割込み優先度
#
INTNO_CFGINT_VALID = INTNO_VALID
INTPRI_CFGINT_VALID = list(range(-(1 << TBITW_IPRI), 0))

#
#  生成スクリプトのチップ依存部（チップ→コア→kernel.py）
#
IncludeTrb("chip_kernel.py")
