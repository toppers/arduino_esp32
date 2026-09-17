/*
 *  seam-P4: core1（PRC2）の入口トランポリン（RAM 常駐）
 *  ==========================================================================
 *  ESP32-P4 統合 段6（2026-08-15）。設計の根拠は esp/boot/seam_p4_core1.c の
 *  `seam_p4_core1_entry` 宣言の上のコメント（実機 JTAG 実測にもとづく）。
 *
 *  役割は 2 つだけ:
 *    1. `SEAM_P4_CORE1_MARK` が定義されていれば USB-Serial/JTAG へ 1 文字出す。
 *       **カーネルを一切通らない**（syslog も sio ドライバも使わない）ので、
 *       「core1 が起きて実際に命令を実行した」ことの直接の証拠になる。
 *    2. `toppers_start` へ飛ぶ。
 *
 *  **なぜ RAM（.iram_text）なのか**:
 *    `ets_set_appcpu_boot_addr` に flash IROM の番地（`&toppers_start`
 *    ＝ 0x40000100）を渡すと、core1 は Instruction access fault で
 *    ROM のパニックハンドラへ落ちる（段6 の実機実測）。方式(a) は ld が
 *    全 text を L2MEM に置くので core1 の入口も RAM だった。ここで条件を揃える。
 *
 *  飛び先は `la`+`jr`（auipc+jalr, ±2GB）。素の `j` は ±1MB なので
 *  RAM(0x4ff0_0000) から flash(0x4000_0000) へは届かない。
 *
 *  レジスタは自由に使ってよい（core1 はここが最初の命令で、
 *  `toppers_start` の先頭が sp/gp を自分で作り直す）。
 *
 *  USJ の番地・手順は seam_p4_entry.S / seam_p4_clk.c と同じ
 *  （esp32p4.h:148-152 / chip_serial.c:165-168）。
 */

#define USJ_EP1                 0x500D2000
#define USJ_EP1_CONF            0x500D2004
#define USJ_WR_DONE             0x1
#define USJ_SERIAL_IN_DATA_FREE 0x2

/*  LP_SYSTEM_REG_BOOT_ADDR_HP_CORE1_REG（lp_system_reg.h:838）  */
#define LP_BOOT_ADDR_CORE1      0x50110164

	.section .iram_text,"ax",@progbits
	.align 4
	.global seam_p4_core1_entry
	.type   seam_p4_core1_entry, @function
seam_p4_core1_entry:
	/*
	 *  最初にすること: 入口アドレスレジスタを 0 に戻す。
	 *
	 *  IDF の core1 入口 call_start_cpu1 が先頭付近で
	 *  `ets_set_appcpu_boot_addr(0)` を呼んでいるのと同じ後始末である
	 *  （esp-idf/components/esp_system/port/cpu_start.c:250）。
	 *  このレジスタは **LP_AON＝常時給電ドメイン**にあり、CPU のリセットや
	 *  USB-UART リセットを跨いで値が残る。0 に戻しておかないと、次回の起動で
	 *  core1 が「bootloader が RAM を載せる前に」前回の番地へ飛び、
	 *  Instruction access fault で ROM のパニックへ落ちる
	 *  （段6 で実機に起きたのがまさにこれ。JTAG 実測 mcause=0x30000001）。
	 */
	li      t0, LP_BOOT_ADDR_CORE1
	sw      zero, 0(t0)
#ifdef SEAM_P4_CORE1_MARK
	li      t0, USJ_EP1_CONF
	li      t3, 0x00100000          /* 空きを待つ上限（永久ループにしない） */
1:	lw      t1, 0(t0)
	andi    t1, t1, USJ_SERIAL_IN_DATA_FREE
	bnez    t1, 2f
	addi    t3, t3, -1
	bnez    t3, 1b
	j       3f                      /* 待てなかった: 出力を諦めて先へ進む */
2:	li      t2, USJ_EP1
	li      t1, SEAM_P4_CORE1_MARK
	sw      t1, 0(t2)
	li      t1, USJ_WR_DONE
	sw      t1, 0(t0)
3:
#endif /* SEAM_P4_CORE1_MARK */
	la      t0, toppers_start
	jr      t0
	.size   seam_p4_core1_entry, . - seam_p4_core1_entry
