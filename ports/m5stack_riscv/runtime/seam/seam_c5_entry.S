/*
 *  seam-C5: イメージのエントリ（bootloader から最初に制御を受け取る場所）
 *
 *  計画 3 段2（2026-09-16）。esp/boot/seam_c6_entry.S（C6 段2）の写し。
 *  C6 版から SEAM_C6_CLK_BOOST（段4 の 160 MHz 昇圧）を落とし、USJ の到達マーク
 *  だけを残した（C5 の 240 MHz 化は段4 で別途）。bootloader は
 *  set_cache_and_start_app の末尾で `(*entry)()` を呼ぶ。その entry を FMP3 の
 *  toppers_start ではなく本スタブに向ける（cmake/a1_c5_stage1.cmake が seam の
 *  ときだけ -Wl,-e,seam_c5_entry を渡す。esp32c5_xip.ld の ENTRY(toppers_start)
 *  はコマンドラインの -e で上書きされる = 非 seam の ELF は 1 バイトも変わらない）。
 *  やることは 2 つだけ:
 *
 *    1. 内蔵 USB-Serial/JTAG へ 1 文字出す（`-DSEAM_C5_ENTRY_MARK=<値>` 指定時のみ）。
 *       「bootloader は jump したが FMP3 が黙っている」ときに、どちらが黙って
 *       いるのかを切り分けるための最小の証拠。ここが出れば「制御は FMP3 の
 *       イメージへ渡っている」ことが確定する（syslog/serial ドライバ/カーネル
 *       初期化を一切通らない生の register 書込み）。
 *    2. toppers_start へ飛ぶ。
 *
 *  .iram_text という名前は P4/C6 seam との命名規約（エントリ専用セクション）。
 *  C5 は DROM==IROM（soc.h:152）なので esp32c5_xip.ld の
 *  `.text : { KEEP(*(.iram_text)) ... } > FLASH` により flash（IROM）側に置かれる。
 *  toppers_start への飛び先は la+jr（auipc+jalr、+/-2GB）。
 *
 *  USJ のレジスタ番地は fmp3/arch/riscv_gcc/esp32c5/esp32c5.h と同じ
 *  （ベース 0x6000F000 は C6 と同一。一次資料は
 *  esp-idf/components/soc/esp32c5/register/soc/usb_serial_jtag_reg.h:17,33 と
 *  soc/esp32c5/register/soc/reg_base.h の DR_REG_USB_SERIAL_JTAG_BASE）。
 *    USJ_EP1      = 0x6000F000  （FIFO 書込み）
 *    USJ_EP1_CONF = 0x6000F004  （bit0=WR_DONE で flush / bit1=IN_EP_DATA_FREE）
 */

#define USJ_EP1                 0x6000F000
#define USJ_EP1_CONF             0x6000F004
#define USJ_WR_DONE              0x1
#define USJ_SERIAL_IN_DATA_FREE  0x2

	.section .iram_text,"ax",@progbits
	.align 4
	.global seam_c5_entry
	.type   seam_c5_entry, @function
seam_c5_entry:
#ifdef SEAM_C5_ENTRY_MARK
	/* USJ に 1 バイト出す（空き待ちは上限つき。出せなくても先へ進む） */
	li      t0, USJ_EP1_CONF
	li      t3, 0x00100000          /* 空きを待つ上限（永久ループにしない） */
1:	lw      t1, 0(t0)
	andi    t1, t1, USJ_SERIAL_IN_DATA_FREE
	bnez    t1, 2f
	addi    t3, t3, -1
	bnez    t3, 1b
	j       3f                      /* 待てなかった: 出力を諦めて先へ進む */
2:	li      t2, USJ_EP1
	li      t1, SEAM_C5_ENTRY_MARK
	sw      t1, 0(t2)
	li      t1, USJ_WR_DONE
	sw      t1, 0(t0)
3:
#endif /* SEAM_C5_ENTRY_MARK */
	la      t0, toppers_start
	jr      t0
	.size   seam_c5_entry, . - seam_c5_entry

#ifdef SEAM_C5_CLK_BOOST
/*
 *  ---- 240MHz 昇圧つきエントリ（計画 3 段4 Task 1、2026-09-16）----
 *  A1_C5_CPU_FREQ_MHZ=240 のとき cmake が -Wl,-e,seam_c5_entry_boost を渡す
 *  （80 は上の seam_c5_entry のまま）。**上の seam_c5_entry には 1 行も足さず、
 *  本ブロックをファイル末尾に置く**理由: 段3 の 80MHz 像（app_xip.bin）を
 *  1 バイトも変えないため。本 .S は -g で組まれ .debug_line を持ち、C5 の
 *  seam 像は app_desc に ELF 全体の sha256（esptool elf2image）を含むので、
 *  既存行の前に挿入すると 80 の像が動く（AC-1a/1e）。
 *
 *  やること: gp/sp を用意して seam_c5_clk_set()（esp/boot/seam_c5_clk.c、
 *  PCR の CPU_DIV_NUM 2->0 + bus_clk_update）を呼び、seam_c5_entry（USJ の
 *  到達マーク -> toppers_start）へ飛ぶ。昇圧がマークより先なので、240 の像で
 *  マーク 'S' が出ずに黙ったら昇圧側で止まっている（80 の像は出る）と読める。
 *
 *  gp の初期化が要る理由は C6 段4 Task 1 の実機記録どおり（esp/boot/
 *  seam_c6_entry.S: gp=0 のまま relax された gp 相対ロードを実行して
 *  Load access fault）。la sp も同じ norelax ブロックに置く（fix wave 1 A1）。
 *  ファイル先頭コメントの「C5 の 240 MHz 化は段4 で別途」は本ブロックのこと
 *  （先頭コメントを書き換えると 80 の像が動くので、そこは据え置く）。
 */
	.section .iram_text,"ax",@progbits
	.align 4
	.global seam_c5_entry_boost
	.type   seam_c5_entry_boost, @function
seam_c5_entry_boost:
	.option push
	.option norelax
	la      gp, __global_pointer$
	la      sp, seam_c5_boot_stack_top
	.option pop
	call    seam_c5_clk_set
	la      t0, seam_c5_entry
	jr      t0
	.size   seam_c5_entry_boost, . - seam_c5_entry_boost

	/*
	 *  昇圧スタブ用の一時スタック（.bss、512B。start.S がこの後ゼロ埋めする。
	 *  NOLOAD なので像には載らない）。
	 */
	.section .bss,"aw",@nobits
	.align 4
seam_c5_boot_stack:
	.space  512
seam_c5_boot_stack_top:
#endif /* SEAM_C5_CLK_BOOST */
