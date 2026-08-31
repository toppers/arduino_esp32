/*
 *  ESP32-S3 ROM newlib サポート（syscall stub table / per-task _reent）
 *
 *  詳細な設計・背景は chip_rom_libc.c の冒頭コメントを参照。
 *
 *  ★★本ヘッダに libc のヘッダ（<sys/reent.h>/<stdio.h>/<assert.h> 等）を
 *    includeしてはならない。<sys/reent.h> は newlib の <assert.h> を推移的に
 *    引き込み、TOPPERS の assert()（t_stddef.h:242）を newlib 版で上書き
 *    してしまう（カーネル全体が __assert_func→abort→_exit を参照し、
 *    本ポート（OSレス相当・newlib syscall未提供）ではリンクできなくなる）。
 *    libc のヘッダと struct syscall_stub_table の定義は chip_rom_libc.c の
 *    中だけに閉じ込めてある。
 */

#ifndef TOPPERS_CHIP_ROM_LIBC_H
#define TOPPERS_CHIP_ROM_LIBC_H

#ifndef TOPPERS_MACRO_ONLY

/*
 *  ROM newlib の初期化
 *
 *  syscall_table_ptr / _global_impure_ptr を張る。**コア0のみ**が、
 *  **コア1を解放する前**（target_mprc_initialize()より前）に、かつ
 *  **ROM libcの初回使用より前**に一度だけ呼ぶこと
 *  （target/esp32s3_devkitc_gcc/target_kernel_impl.c の
 *  software_init_hook() から呼び出している）。
 *  stub tableは両コア共有の1本（ESP32-S3のROMポインタは
 *  syscall_table_ptr 1本のみ。_pro/_app はS3のROMには存在しない）。
 */
extern void chip_rom_libc_init(void);

/*
 *  タスクのper-task _reent初期化（activate_contextから呼ばれる）
 *
 *  core_kernel_impl.h の activate_context() から、タスク起動（起動・
 *  再起動）の度に呼ばれる。ESP-IDF の esp_reent_init()
 *  （components/newlib/src/reent_init.c:22-30）相当。
 *  ここではTCBを不透明に扱う（本ヘッダはkernel_impl.hより前にも
 *  読まれうるため、struct task_control_blockの前方宣言で受ける）。
 */
struct task_control_block;
extern void chip_rom_libc_reent_init(struct task_control_block *p_tcb);

#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_CHIP_ROM_LIBC_H */
