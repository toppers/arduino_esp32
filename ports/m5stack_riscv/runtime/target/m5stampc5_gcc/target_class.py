# -*- coding: utf-8 -*-
#
#		クラス定義（M5Stamp-C5 / ESP32-C5・単一コア）
#  fmp3/target/m5stamp_esp32p4_gcc/target_class.py の TNUM_PRCID==1 の枝と同一。
#
globalVars.append("clsData")

clsData = {
    1: {"clsid": NumStr(1, "CLS_PRC1"),
        "initPrc": 1, "affinityPrcList": [1]},
    2: {"clsid": NumStr(2, "CLS_ALL_PRC1"),
        "initPrc": 1, "affinityPrcList": [1]},
}
