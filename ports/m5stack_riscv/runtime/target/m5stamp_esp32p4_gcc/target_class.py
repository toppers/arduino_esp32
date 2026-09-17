# -*- coding: utf-8 -*-
#
#    ターゲット依存のクラス定義（M5Stamp ESP32P4 用）
#
#  $Id: target_class.py (converted from target_class.trb) $
#
#  変換元 target_class.trb は polarfire_soc_kit_gcc のものと **バイト同一**である
#  （diff で確認済み。P4 の svn 側がそのまま流用しているため、ファイル冒頭の
#   コメントも "Plarfire SoC Kit用" のままになっている）。
#  したがって本ファイルの本体も fmp3_core/target/polarfire_soc_kit_gcc/target_class.py
#  と同一である。ヘッダのコメントだけがこのターゲットのものになっている。
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

elif TNUM_PRCID == 3:
    clsData = {
        1: {"clsid": NumStr(1, "CLS_PRC1"),
            "initPrc": 1, "affinityPrcList": [1]},
        2: {"clsid": NumStr(2, "CLS_PRC2"),
            "initPrc": 2, "affinityPrcList": [2]},
        3: {"clsid": NumStr(3, "CLS_PRC3"),
            "initPrc": 3, "affinityPrcList": [3]},
        4: {"clsid": NumStr(4, "CLS_ALL_PRC1"),
            "initPrc": 1, "affinityPrcList": [1, 2, 3]},
        5: {"clsid": NumStr(5, "CLS_ALL_PRC2"),
            "initPrc": 2, "affinityPrcList": [1, 2, 3]},
        6: {"clsid": NumStr(6, "CLS_ALL_PRC3"),
            "initPrc": 3, "affinityPrcList": [1, 2, 3]},
    }

elif TNUM_PRCID == 4:
    clsData = {
        1: {"clsid": NumStr(1, "CLS_PRC1"),
            "initPrc": 1, "affinityPrcList": [1]},
        2: {"clsid": NumStr(2, "CLS_PRC2"),
            "initPrc": 2, "affinityPrcList": [2]},
        3: {"clsid": NumStr(3, "CLS_PRC3"),
            "initPrc": 3, "affinityPrcList": [3]},
        4: {"clsid": NumStr(4, "CLS_PRC4"),
            "initPrc": 4, "affinityPrcList": [4]},
        5: {"clsid": NumStr(5, "CLS_ALL_PRC1"),
            "initPrc": 1, "affinityPrcList": [1, 2, 3, 4]},
        6: {"clsid": NumStr(6, "CLS_ALL_PRC2"),
            "initPrc": 2, "affinityPrcList": [1, 2, 3, 4]},
        7: {"clsid": NumStr(7, "CLS_ALL_PRC3"),
            "initPrc": 3, "affinityPrcList": [1, 2, 3, 4]},
        8: {"clsid": NumStr(8, "CLS_ALL_PRC4"),
            "initPrc": 4, "affinityPrcList": [1, 2, 3, 4]},
    }
