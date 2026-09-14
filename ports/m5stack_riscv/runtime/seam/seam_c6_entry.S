/*
 *  seam-C6: イメージのエントリ（bootloader から最初に制御を受け取る場所）
 *
 *  段2（2026-09-13）。esp/boot/seam_p4_entry.S（P4 版）と同じ役割 / 同じ
 *  骨格の C6 版。bootloader は set_cache_and_start_app の末尾で `(*entry)()`
 *  を呼ぶ。その entry を FMP3 の toppers_start ではなく本スタブに向ける
 *  （esp32c6_xip.ld の ENTRY(seam_c6_entry)）。やることは 2 つだけ:
 *
 *    1. 内蔵 USB-Serial/JTAG へ 1 文字出す（`-DSEAM_C6_ENTRY_MARK=<値>` 指定時のみ）。
 *       -- 「bootloader は jump したが FMP3 が黙っている」ときに、
 *       どちらが黙っているのかを切り分けるための最小の証拠。
 *       ここが出れば「制御は FMP3 のイメージへ渡っている」ことが確定する
 *       （syslog/serial ドライバ/カーネル初期化を一切通らない生の register 書込み）。
 *    2. toppers_start へ飛ぶ。
 *
 *  ---- .iram_text という名前について（P4 との違い）----
 *  P4 の同名セクション .iram_text は「RAM 常駐」の意味で使われていた
 *  （P4 は DROM/IROM と RAM が別アドレス空間で遠いため、entry を RAM に置いて
 *  auipc+jalr で toppers_start（flash）へ届かせる設計だった）。
 *  **C6 は事情が異なる**：C6 は SOC_DROM_LOW==SOC_IROM_LOW（IROM/DROM が
 *  同一アドレス空間 0x42000000）なので、本スタブは esp32c6_xip.ld の
 *  `.text : { KEEP(*(.iram_text)) ... } > FLASH` により **flash（IROM）側に
 *  配置される** -- 実体は RAM 常駐ではない。名前だけ P4 から引き継いでいる
 *  （P4/S3 seam との命名規約を揃えて「エントリ専用セクション」であることを
 *  示す目的。実体が flash か RAM かは ld 側の配置で決まる）。
 *  toppers_start への飛び先は la+jr（auipc+jalr、+/-2GB）で書く -- 同一 flash
 *  領域内で通常は `j`（+/-1MB）でも届くはずだが、P4 と同じ安全側の書き方に
 *  揃えておく（距離超過によるリンクエラーを避ける）。
 *
 *  USJ のレジスタ番地は fmp3/arch/riscv_gcc/esp32c6/esp32c6.h:209-212 と
 *  同じ（ESP32C6_USBJTAG_EP1/EP1_CONF、ベース 0x6000F000）。一次資料は
 *  esp-idf/components/soc/esp32c6/register/soc/usb_serial_jtag_reg.h。
 *    USJ_EP1      = 0x6000F000  （FIFO 書込み）
 *    USJ_EP1_CONF = 0x6000F004  （bit0=WR_DONE で flush / bit1=IN_EP_DATA_FREE）
 *  出力手順は esp32c6_usbjtag.c の書込みシーケンスと同じ（空きを待って
 *  1 バイト書き、WR_DONE）。
 *
 *  ---- SEAM_C6_CLK_BOOST（段4 Task 1、2026-09-14）----
 *  A1_C6_CPU_FREQ_MHZ=160 のときだけ定義される。toppers_start へ飛ぶ前に
 *  seam_c6_clk_set()（esp/boot/seam_c6_clk.c）を呼んで CPU を 80->160MHz へ
 *  昇圧する。P4 の SEAM_P4_CLK360 と同じ型：C 関数を呼ぶために一時スタックを
 *  用意する（.bss、この後 start.S がゼロ埋めするので踏んでも無害。
 *  P4 は 1KB、C6 は呼ぶ関数がレジスタ read-modify-write のみで再帰も無い
 *  ため 512B で足りる）。
 */

#define USJ_EP1                 0x6000F000
#define USJ_EP1_CONF             0x6000F004
#define USJ_WR_DONE              0x1
#define USJ_SERIAL_IN_DATA_FREE  0x2

	.section .iram_text,"ax",@progbits
	.align 4
	.global seam_c6_entry
	.type   seam_c6_entry, @function
seam_c6_entry:
#ifdef SEAM_C6_ENTRY_MARK
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
	li      t1, SEAM_C6_ENTRY_MARK
	sw      t1, 0(t2)
	li      t1, USJ_WR_DONE
	sw      t1, 0(t0)
3:
#endif /* SEAM_C6_ENTRY_MARK */
#ifdef SEAM_C6_CLK_BOOST
	/*
	 *  gp（globalポインタ）の初期化。fmp3_core/arch/riscv_gcc/common/
	 *  start.S の toppers_start と同じ手順（AO_NORELAX 相当を素の
	 *  `.option norelax` で代用。core_asm.inc を include せずに済ませる
	 *  ため）。
	 *
	 *  **これが無いと実機で Load access fault で落ちる**（段4 Task 1、
	 *  2026-09-14 実機で確認）。理由: gp はブートローダから渡された時点で
	 *  0（実機レジスタダンプで GP=0x00000000 を確認）。seam_c6_clk_set() は
	 *  ここでは普通の C 関数としてコンパイルされ、その中で参照する
	 *  グローバル（g_seam_c6_clk_result、__attribute__((section(".data")))
	 *  で明示配置）はリンカの relax パスにより、実際の番地が
	 *  __global_pointer$ の +/-2KiB 以内（.data 全体が 416 バイトしか無く、
	 *  実測で収まる）なら auipc+addi の2命令ロードが gp 相対の1命令
	 *  ロードへ自動的に書き換えられる（-mrelax は既定 ON、target.cmake で
	 *  無効化していない）。gp=0 のままこの relax されたコードを実行すると、
	 *  番地 0 近傍への誤アクセスになり Load access fault（実測: MCAUSE=5、
	 *  PC は ROM 内、RA=0x40000000 という不審な値）。toppers_start 自身は
	 *  gp を設定してから .data/.bss を触るので通常は問題にならないが、
	 *  本呼出しは toppers_start より前なので明示的に設定する必要がある。
	 */
	.option push
	.option norelax
	la      gp, __global_pointer$
	/*
	 *  la sp も同じ norelax ブロック内に置く -- ここを抜けた後だと
	 *  seam_c6_boot_stack_top への la が gp 相対の1命令（addi sp,gp,...）へ
	 *  relax されてしまう（実測: objdump で確認）。直前で gp を設定して
	 *  いても、relax 判定はリンク時のシンボル配置に依存する暗黙の前提と
	 *  なり壊れやすいため、sp のロードは明示的に gp から独立させる。
	 */
	la      sp, seam_c6_boot_stack_top
	.option pop
	call    seam_c6_clk_set
#endif /* SEAM_C6_CLK_BOOST */
	la      t0, toppers_start
	jr      t0
	.size   seam_c6_entry, . - seam_c6_entry

#ifdef SEAM_C6_CLK_BOOST
	/*
	 *  昇圧スタブ用の一時スタック（.bss、512B）。start.S がこの後ゼロ埋め
	 *  するので中身は誰も引き継がない。**イメージには載らない**（NOLOAD）
	 *  ので app_xip.bin のサイズは増えない（P4 と同じ扱い）。
	 */
	.section .bss,"aw",@nobits
	.align 4
seam_c6_boot_stack:
	.space  512
seam_c6_boot_stack_top:
#endif /* SEAM_C6_CLK_BOOST */
