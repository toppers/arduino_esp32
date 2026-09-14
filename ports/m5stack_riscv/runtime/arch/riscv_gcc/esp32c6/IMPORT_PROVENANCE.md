# `arch/riscv_gcc/esp32c6/`（C6 chip 依存部）の出典と改変

- 出典: asp3_core `9904a444`（`~/TOPPERS/asp3_esp_idf/asp3/asp3_core`, branch feat/esp32c6）の
  `arch/riscv_gcc/esp32c6/`（22 ファイル）。取込み 2026-09-13。
- 監査: 本段では pristine 監査を置かない（PLAN.md 判断: dev repo 直下で育て、fmp3_core へ
  upstream した時点で P4 型の監査を後付けする）。代わりに下表で改変を申告する。
- 出典側との照合コマンド: `diff -u $ASP3/asp3/asp3_core/arch/riscv_gcc/esp32c6/<f> fmp3/arch/riscv_gcc/esp32c6/<f>`

| ファイル | 出典との差 | 理由 |
|---|---|---|
| chip_kernel_impl.h | FMP3 界面（get_my_prcidx / TNUM_INTNO=32 / VALID_INTNO(prcid,intno) / INTNO_MASK / p_intcfg_table / chip_mprc_initialize / chip_initialize(PCB*) / save_context,release_context） | polarfire_soc の ASP3->FMP3 diff と同型 |
| chip_kernel_impl.c | chip_mprc_initialize 追加・chip_initialize(PCB*)・initialize_interrupt(PCB*) が iprcid で絞る | 同上 |
| chip_support.S | irc_begin_int が inh_table ではなく my_pcb->PCB_p_inh_tbl を引く | FMP3 は inh_table がプロセッサ毎 |
| chip_asm.inc | my_pid / my_pidx を追加 | core_asm.inc の my_pcb / my_istkpt が使う |
| chip_sil.h | sil_get_pid を追加 | core_sil.h のスピンロックが使う |
| chip_rename.def / chip_rename.h / chip_unrename.h | chip_mprc_initialize を追加・genrename で再生成 | — |
| chip_kernel.py | 書き直し（INTNO_VALID を prcid 辞書・0..31、INHNO に prcid<<16、TargetCheckCfgInt） | FMP3 の core_kernel.py の契約 |
| chip.cmake | 書き直し（FMP3_* 変数・rv32imac・共通 PLIC/mtimer を積まない）。**2026-09-13 Task 5 で追記**: `include(${COREDIR}/arch.cmake)` の直後に `FMP3_START_FILES` の `common/start.S` を `chip_start.S`（新規）へ差し替える2行を追加 | 本文コメント参照。差替えの理由は chip_start.S 冒頭コメント参照（実装計画 Task 5 のビルドで発覚: fmp3_core 共通 start.S の FPU 初期化が `-march=rv32imac`＝F拡張無しで assembler error になるため） |
| chip_start.S | 新規（Task 5・2026-09-13）。fmp3_core `arch/riscv_gcc/common/start.S` の複製＋FPU初期化ブロックを `#ifdef __riscv_flen` で括っただけ | ESP32-C6 はハードウェア FPU を持たない（esp-idf soc_caps.h に SOC_CPU_HAS_FPU の定義が無い＝esp32p4 にはある）。fmp3_core は改変しないため chip 層で複製・差替え |
| intmtx_kernel_impl.h | コメント 2 箇所のみ | — |
| chip_kernel.h / chip_serial.c / chip_serial.h / chip_stddef.h / esp32c6_uart.c / esp32c6_uart.h / esp32c6_usbjtag.c / esp32c6_usbjtag.h（計8本） | バイト同一（`cmp` で確認済み） | — |
| esp32c6.h | **2026-09-13 段2 Task 3 で追記**: `CORE_CLK_MHZ` の定義を `#ifndef CORE_CLK_MHZ` で囲んだ（既定 160 は無改変）。`SIL_DLY_TIM1`/`SIL_DLY_TIM2` を `#if CORE_CLK_MHZ == 80` で分岐し、80MHz 用の仮置き値 15/6（160MHz実測 30/12 からの単純比例。「仮置き・段3 で較正」とコメント）を追加。`CORE_CLK_MHZ` が未定義（非 seam・既定パス）のときはどちらも従来どおり 160/30/12 のまま | seam（`cmake/a1_c6_stage1.cmake` の `A1_C6_SEAM=ON`）が `-DCORE_CLK_MHZ=80`（D1）を渡すための受け口。AC-4a（`build/c6-stage1` の sha256 `5dccfbdc…` 不変）で非 seam 経路が無改変であることを実測確認済み（task-3-report.md） |
| chip_serial.cfg | CLASS(CLS_SERIAL) 化のみ・exinf は 1 のまま（2026-09-13、Task 3 で上書き。レビュー fix round 1 で exinf を 0 から 1 へ戻した） | target 層 m5nanoc6_gcc（Task 3）の target_serial.cfg が `INCLUDE("chip_serial.cfg")` する前提を、esp32p4 の chip_serial.cfg と同じ CLASS で括る形へ合わせたが、CRE_ISR の exinf は P4 と同じ 0 にしてはいけない。C6 の sio_isr(exinf) は exinf をそのまま siopid として ESP32C6_SIO(isr) へ渡し、各ドライバが `siopcb_table[siopid-1]` を引くため（P4 の sio_isr は exinf を使わない）、exinf=0 だと最初のコンソール割込みで範囲外アクセスになる。asp3 版と同じ exinf=1 に戻した |
| （持ち込まない）chip_os_awareness.py | — | 本 repo に対応機構が無い |
