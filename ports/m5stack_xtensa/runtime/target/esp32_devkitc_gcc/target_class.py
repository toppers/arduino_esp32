# -*- coding: utf-8 -*-
#
#		ターゲット依存のクラス定義（無印ESP32 / LX6用）
#
#  TNUM_PRCID に応じてクラスのリストを切り替える。
#
#  元の target_class.trb は target/rp2350_pico2_gcc/target_class.trb と
#    バイト同一であり、ヘッダコメントに RP2350 / Musca-B1 の記述がそのまま
#    残っていた（ARM-M 版からの派生の痕跡）。.py 化にあたりコメントのみ
#    実態に合わせた。生成内容は変わらない。
#

#
#  クラスのリスト
#
globalVars.append("clsData")

if TNUM_PRCID == 1:
    clsData = {
        1: {"clsid": NumStr(1, "CLS_PRC1"),
            "initPrc": 1, "affinityPrcList": [1]},
        2: {"clsid": NumStr(2, "CLS_ALL_PRC1"),
            "initPrc": 1, "affinityPrcList": [1]},
    }

elif TNUM_PRCID == 2:
    clsData = {
        1: {"clsid": NumStr(1, "CLS_PRC1"),
            "initPrc": 1, "affinityPrcList": [1]},
        2: {"clsid": NumStr(2, "CLS_PRC2"),
            "initPrc": 2, "affinityPrcList": [2]},
        3: {"clsid": NumStr(3, "CLS_ALL_PRC1"),
            "initPrc": 1, "affinityPrcList": [1, 2]},
        4: {"clsid": NumStr(4, "CLS_ALL_PRC2"),
            "initPrc": 2, "affinityPrcList": [1, 2]},
    }
