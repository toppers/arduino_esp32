# -*- coding: utf-8 -*-
#
#		パス2の生成スクリプトのコア依存部（Xtensa用）
#
#  core_kernel.trb の Python 版。arch/arm_m_gcc/common/core_kernel.py からは
#  generate_native_spn_defined のフラグ機構を削っている（Xtensa 版は持たない）。
#
def DefineVariableSection(genFile, defvar, secname):
    if secname != "":
        genFile.add(f'{defvar} __attribute__((section("{secname}"),nocommon));')
    else:
        genFile.add(f"{defvar};")


def SecnameKernelData(cls):
    return ""


def SecnameStack(cls):
    return ""


def GenerateNativeSpn(params):
    kernelCfgC.add(f"LOCK _kernel_lock_{params['spnid']};")
    return f"((intptr_t) &_kernel_lock_{params['spnid']})"


def GenerateTskinictxb(key, params):
    return (f"{{\t(void *)({params['tinib_stk']}), "
            f"\t((void *)((char *)({params['tinib_stk']}) + "
            f"({params['tinib_stksz']}))), }}")


def GenResVectVal(num):
    return 0


#
#  ターゲット非依存部のインクルード
#
IncludeTrb("kernel/kernel.py")
