# -*- coding: utf-8 -*-
#
#		チェックパスの生成スクリプトのコア依存部（Xtensa用）
#
#  core_check.trb の Python 版。ARM-M 版は静的な例外/割込みハンドラテーブル
#  （_kernel_p_exc_tbl）の各エントリのアライン・非NULLをチェックしているが、
#  Xtensa 設計はそのようなテーブルを生成しない（VECBASE 非変更、ROM 提供の
#  XTOS API で個別登録する方式）ため、そのチェックを持たない。
#


#
#  TSKINICTXB からスタック領域の先頭番地を取り出す
#  （kernel_check.py がスタックのアライン・非NULLチェックのために呼ぶ）
#
def GetStackTskinictxb(key, params, tinib):
    return PEEK(tinib + offsetof_TINIB_TSKINICTXB_stk_top, sizeof_void_ptr)


#
#  ターゲット非依存部のインクルード
#
IncludeTrb("kernel/kernel_check.py")
