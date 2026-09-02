/* This file is generated from core_rename.def by genrename. */

#ifndef TOPPERS_CORE_RENAME_H
#define TOPPERS_CORE_RENAME_H

/*
 *  core_kernel_impl.h
 */
/*
 *  
 */
/*
 *  ★2026-07-24 追加。本ファイルは arm_m_gcc の core_rename.def と
 */
/*
 *  バイト同一のまま持ち込まれており、riscv_gcc / arm_gcc / arm64_gcc が
 */
/*
 *  持っている「# core_kernel_impl.h」節（lock_cpu / unlock_cpu / sense_lock）
 */
/*
 *  だけが**丸ごと欠けていた**。この非一貫性のせいで、cfg 生成器が
 */
/*
 *  多重 ISR 用に出力する `_kernel_sense_lock()` / `_kernel_unlock_cpu()`
 */
/*
 *  （doc/configurator.txt §4.7.1.2 の 1338-1339 行が条文で規定する形）が
 */
/*
 *  xtensa と arm_m でだけ未宣言になっていた。
 */
/*
 *  
 */
/*
 *  ★golden への影響は無い。
 */
/*
 *  当初「assert(!sense_lock()) の #exp が伸びて .rodata がずれる」と見積もったが
 */
/*
 *  **これは誤りだった**。C17 6.10.3.1p1 / 6.10.3.2p2 のとおり `#` の被演算子は
 */
/*
 *  マクロ展開されず**実引数の綴りがそのまま**文字列になるので、rename しても
 */
/*
 *  文字列は "!sense_lock()" のまま（gcc -E で同一行に
 */
/*
 *  `!_kernel_sense_lock()` と `"!sense_lock()"` が並ぶことを実測）。
 */
/*
 *  加えて 3 つとも Inline（static inline）で外部シンボルを作らず、app_xip.bin は
 */
/*
 *  symtab を持たないので、そもそも rename が像に現れる経路が無い。
 */
/*
 *  
 */
/*
 *  ★この追加が効いていることの検証は golden ではできない。生成器の該当ブロックは
 */
/*
 *  「同一 intno に ISR が 2 本以上」でしか出力されず、golden 5 構成にも QEMU
 */
/*
 *  テスト 51 件にも該当構成が無いため（54 件すべてで出現 0）。実際の検証は
 */
/*
 *  (1) 多重 ISR を持つ m5 構成（線 4 に CRE_ISR 2 本）の kernel_cfg.c が
 */
/*
 *  `_kernel_sense_lock()` を出力してコンパイル通過すること、
 */
/*
 *  (2) 素名版を接ぎ木すると `implicit declaration of function 'sense_lock'` で
 */
/*
 *  落ちること（両方向）、の 2 点で行った。
 */
#define lock_cpu					_kernel_lock_cpu
#define unlock_cpu					_kernel_unlock_cpu
#define sense_lock					_kernel_sense_lock

/*
 *  core_kernel_impl.c
 */
#define p_exc_tbl					_kernel_p_exc_tbl
#define p_vector_table				_kernel_p_vector_table
#define default_exc_handler			_kernel_default_exc_handler
#define default_int_handler			_kernel_default_int_handler
#define config_int					_kernel_config_int
#define core_initialize				_kernel_core_initialize
#define core_terminate				_kernel_core_terminate
#define idstkpt_table				_kernel_idstkpt_table
#define p_bitpat_cfgint				_kernel_p_bitpat_cfgint
#define set_exc_int_priority		_kernel_set_exc_int_priority
#define enable_exc					_kernel_enable_exc
#define disable_exc					_kernel_disable_exc
#define p_iipm_enable_irq_tbl		_kernel_p_iipm_enable_irq_tbl

/*
 *  core_support.S
 */
#define core_int_entry				_kernel_core_int_entry
#define core_exc_entry				_kernel_core_exc_entry
#define svc_handler					_kernel_svc_handler
#define pendsv_handler				_kernel_pendsv_handler
#define dispatch					_kernel_dispatch
#define do_dispatch					_kernel_do_dispatch
#define dispatcher_1				_kernel_dispatcher_1
#define start_r						_kernel_start_r
#define start_dispatch				_kernel_start_dispatch
#define exit_and_dispatch			_kernel_exit_and_dispatch
#define call_exit_kernel			_kernel_call_exit_kernel


#endif /* TOPPERS_CORE_RENAME_H */
