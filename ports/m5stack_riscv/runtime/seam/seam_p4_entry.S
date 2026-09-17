/*
 *  seam-P4: イメージのエントリ（bootloader から最初に制御を受け取る場所）
 *
 *  段4（2026-08-14）。bootloader は set_cache_and_start_app の末尾で
 *  `(*entry)()` を呼ぶ。その entry を **FMP3 の toppers_start ではなく本スタブに
 *  向ける**（esp32p4_xip.ld の ENTRY(seam_p4_entry)）。やることは 2 つだけ:
 *
 *    1. 内蔵 USB-Serial/JTAG へ 1 文字出す（`-DSEAM_P4_ENTRY_MARK=<文字>` 指定時のみ）。
 *       ——「bootloader は jump したが FMP3 が黙っている」ときに、
 *       **どちらが黙っているのか**を切り分けるための最小の証拠。
 *       ここが出れば「制御は FMP3 のイメージへ渡っている」ことが確定する
 *       （syslog/serial ドライバ/カーネル初期化を一切通らない生の register 書込み）。
 *    2. toppers_start へ飛ぶ。
 *
 *  **RAM 常駐**（.iram_text）である。理由は 2 つ:
 *    - 「flash からの命令フェッチが成立しているか」という仮説を切り分けるとき、
 *      probe 自身が flash 上にあると何も切り分けられない。
 *    - toppers_start は flash IROM（0x4010_0000 帯）で本スタブは RAM（0x4FF0_0000 帯）
 *      なので、飛び先は **la+jr（auipc+jalr, ±2GB）**で書く。素の `j` は ±1MB で届かない。
 *
 *  USJ のレジスタ番地は fmp3/arch/riscv_gcc/esp32p4/esp32p4.h:148-152 と同じ:
 *    USJ_EP1      = 0x500D2000  （FIFO 書込み）
 *    USJ_EP1_CONF = 0x500D2004  （bit0=WR_DONE で flush / bit1=IN_EP_DATA_FREE）
 *  出力手順も chip_serial.c:165-168 と同じ（空きを待って 1 バイト書き、WR_DONE）。
 */

#define USJ_EP1                 0x500D2000
#define USJ_EP1_CONF            0x500D2004
#define USJ_WR_DONE             0x1
#define USJ_SERIAL_IN_DATA_FREE 0x2

	.section .iram_text,"ax",@progbits
	.align 4
	.global seam_p4_entry
	.type   seam_p4_entry, @function
seam_p4_entry:
#ifdef SEAM_P4_ENTRY_MARK
	li      t0, USJ_EP1_CONF
	li      t3, 0x00100000          /* 空きを待つ上限（永久ループにしない） */
1:	lw      t1, 0(t0)
	andi    t1, t1, USJ_SERIAL_IN_DATA_FREE
	bnez    t1, 2f
	addi    t3, t3, -1
	bnez    t3, 1b
	j       3f                      /* 待てなかった: 出力を諦めて先へ進む */
2:	li      t2, USJ_EP1
	li      t1, SEAM_P4_ENTRY_MARK
	sw      t1, 0(t2)
	li      t1, USJ_WR_DONE
	sw      t1, 0(t0)
3:
#endif /* SEAM_P4_ENTRY_MARK */
#ifdef SEAM_P4_CLK360
	/*
	 *  ---- CPU クロックの昇圧（90MHz -> 360MHz）。段5（2026-08-15）----
	 *  seam では esp_clk_init() 相当を実行する主体が居ないため、bootloader が
	 *  残した 90MHz のままカーネルが動く＝時間が 4 倍遅くなる（段4 §5 の実測）。
	 *  ここで上げる。実体と根拠は esp/boot/seam_p4_clk.c（RAM 常駐）。
	 *
	 *  スタブ用の一時スタックを自分で用意する:
	 *    ここに来た時点の sp は bootloader が使っていたもの（段4 の JTAG 実測で
	 *    0x4ff3cbb0）で、**本イメージの管轄外**である。使えている実績はあるが
	 *    bootloader の実装に依存するので当てにしない。.bss はこの後 start.S が
	 *    ゼロ埋めするので、ここで一時スタックとして踏んでも害が無い。
	 */
	la      sp, seam_p4_boot_stack_top
	call    seam_p4_clk_boost_360
	/*  a0 = 結果コード。そのまま seam_p4_clk_report(rc) の第 1 引数になる  */
	call    seam_p4_clk_report
#endif /* SEAM_P4_CLK360 */
#ifdef SEAM_P4_FLASH_MARK
	la      t0, seam_p4_flash_probe   /* flash XIP 側へ飛ぶ（そこから toppers_start） */
#else
	la      t0, toppers_start
#endif
	jr      t0
	.size   seam_p4_entry, . - seam_p4_entry

#ifdef SEAM_P4_CLK360
	/*
	 *  昇圧スタブ用の一時スタック（.bss・1KB）。start.S がこの後ゼロ埋めするので
	 *  中身は誰も引き継がない。**イメージには載らない**（NOLOAD）ので
	 *  app_xip.bin のサイズは増えない。
	 */
	.section .bss,"aw",@nobits
	.align 4
seam_p4_boot_stack:
	.space  1024
seam_p4_boot_stack_top:
#endif /* SEAM_P4_CLK360 */

#ifdef SEAM_P4_FLASH_MARK
/*
 *  ---- flash XIP 側の probe（診断用・既定 OFF）----
 *  `.text` に置く＝ld が flash IROM へ入れる。ここが出力できれば
 *  「flash からの命令フェッチが成立している」ことの直接証拠になる。
 *  出力後 toppers_start へ落ちる（同じ .text 内なので通常の j で届く）。
 */
	.section .text,"ax",@progbits
	.align 4
	.global seam_p4_flash_probe
	.type   seam_p4_flash_probe, @function
seam_p4_flash_probe:
	li      t0, USJ_EP1_CONF
	li      t3, 0x00100000
1:	lw      t1, 0(t0)
	andi    t1, t1, USJ_SERIAL_IN_DATA_FREE
	bnez    t1, 2f
	addi    t3, t3, -1
	bnez    t3, 1b
	j       3f
2:	li      t2, USJ_EP1
	li      t1, SEAM_P4_FLASH_MARK
	sw      t1, 0(t2)
	li      t1, USJ_WR_DONE
	sw      t1, 0(t0)
3:	la      t0, toppers_start
	jr      t0
	.size   seam_p4_flash_probe, . - seam_p4_flash_probe
#endif /* SEAM_P4_FLASH_MARK */
