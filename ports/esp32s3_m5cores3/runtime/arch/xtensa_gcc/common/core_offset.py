# -*- coding: utf-8 -*-
#
#		オフセットファイル生成スクリプトのコア依存部（Xtensa用）
#
#  core_offset.trb の Python 版。arch/arm_m_gcc/common/core_offset.py との差は
#  TCB_fpu_area の 1 行だけである（Xtensa は FPU コンテキストを eager 方式で
#  常時保存するため TCB に fpu_area を持つ）。
#
#  ★.trb と同一ディレクトリ・同一 basename で並べること。
#    tools/cfg_equivalence.sh は build.ninja から Python の呼び出し行を抜き、
#    -T/-C 引数の .py を .trb へ sed 置換して Ruby オラクルを走らせる。
#
IncludeTrb("kernel/genoffset.py")
offsetH.append(f"""\
#define TCB_p_tinib\t\t{offsetof_TCB_p_tinib}
#define TCB_pc\t\t\t{offsetof_TCB_pc}
#define TCB_sp\t\t\t{offsetof_TCB_sp}
#define TCB_stk_top\t\t{offsetof_TCB_stk_top}
#define TCB_fpu_flag\t\t{offsetof_TCB_fpu_flag}
#define TCB_fpu_area\t\t{offsetof_TCB_fpu_area}
#define TINIB_exinf\t\t{offsetof_TINIB_exinf}
#define TINIB_task\t\t{offsetof_TINIB_task}
#define TINIB_stk_bottom\t{offsetof_TINIB_TSKINICTXB_stk_bottom}
#define PCB_p_runtsk\t\t{offsetof_PCB_p_runtsk}
#define PCB_p_schedtsk\t\t{offsetof_PCB_p_schedtsk}
#define PCB_idstkpt\t\t{offsetof_PCB_idstkpt}
#define PCB_idstktop\t\t{offsetof_PCB_idstktop}
#define PCB_lock_flag\t\t{offsetof_PCB_lock_flag}
#define PCB_target_pcb\t\t{offsetof_PCB_target_pcb}
""")
