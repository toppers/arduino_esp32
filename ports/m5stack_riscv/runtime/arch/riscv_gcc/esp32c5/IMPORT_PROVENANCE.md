# `arch/riscv_gcc/esp32c5/`（C5 chip 依存部）の出典と改変

- 取込み 2026-09-15（計画 3 段1、`.steering/20260915-c5-plan/PLAN-stage1-impl.md` Task 2）。
- 出典は 4 系統。表の「出典」列で区別する。
  - **P4**: `fmp3/arch/riscv_gcc/esp32p4/`（fmp3_core `685b36a9` pristine の複製。CLIC 層の骨格）
  - **C6**: `fmp3/arch/riscv_gcc/esp32c6/`（C6 段1、asp3_core `9904a444` 由来）
  - **asp3**: `/home/honda/TOPPERS/asp3_esp_idf` `6471444` の `asp3/arch/riscv_gcc/esp32c5/`（**値のみ**。コードは持ち込まない）
  - **IDF**: esp-idf submodule v5.5.4 `735507283d` の `components/soc/esp32c5/`、`components/riscv/include/esp_private/interrupt_clic.h`（値の照合先）
  - **新規**: 本段で書いたもの
- 監査: 本段では pristine 監査を置かない（C6 段1 と同じ判断。fmp3_core へ upstream した時点で P4 型の監査を後付けする）。代わりに下表で改変を申告する。
- 照合コマンド: `diff -u fmp3/arch/riscv_gcc/esp32p4/<f> fmp3/arch/riscv_gcc/esp32c5/<f>`、`diff -u fmp3/arch/riscv_gcc/esp32c6/<f> fmp3/arch/riscv_gcc/esp32c5/<f>`（C6 由来は `sed s/c6/c5/` 後に比べる）
- 文字種: 本ディレクトリの新規/改変部分は Ambiguous-width 文字（星印/矢印/波線/ダッシュ/中黒/三点）を使わない。P4/C6 から写した既存コメントは対象外。

## 1. ファイルごとの出典と改変

| ファイル | 出典 | 出典との差 | 理由 |
|---|---|---|---|
| chip_kernel_impl.h | P4 | esp32p4.h -> esp32c5.h。save_context/release_context の空マクロは P4 と同じく TOPPERS_MACRO_ONLY ガードの内側（fix round 1 / F7 で戻した）。`#include "clic_kernel_impl.h"`（共通）-> `"esp32c5_clic_kernel_impl.h"`（chip 側複製）。t_set_ipm の MSI/MTI の `riscv_set_mie/clear_mie` を削除。lazy PIE/HWLP の save_context/release_context 分岐を空マクロ固定に。`esp32c5_intmtx_route` の extern を追加。D1（`INT_IPM` の `\|0x1F`）は P4 のまま | 単一コア/CLINT 不使用/コプロセッサ無し。閾値 CSR/MMIO 2 経路 |
| chip_kernel_impl.c | P4 | irc_begin_ipi（storm breaker）/clic_storm_*/mtimer 有効化/CLIC 内部線 3/7 の設定/lazy PIE/HWLP を削除。`esp32c5_intmtx_route`（C6 の `esp32c6_intmtx_route` の型。srcmask/from_cpu 表は持たない）を追加。chip_initialize に割込みマトリクス全解除（84 ソースを 0 へ）と全 CLIC 線の ATTR バイトへ 0xC0（MODE=machine）書戻しを追加 | 単一コア。ATTR.MODE: 共通 clic_context_initialize が全ワード 0 で書くため MODE=0 になる。asp3 は C5 実機で ATTR=0xC0 を明示（asp3 clic_initialize）。MODE=0 で配信されるかは**未確認**なので安全側 |
| chip_support.S | P4 | do_msi/do_mti（CLINT 内部線）分岐を削除。lazy PIE ファストトラップ / pie_save_regs / pie_restore_regs / pie_fast_trap を削除。閾値 read/write を `TOPPERS_ESP32C5_CLIC_THRESH_MMIO` で CSR 0x347（既定、`csrrw`/`csrw`/`csrr` + `andi 0xff`）/ MMIO（P4 と同一）に分岐。出口正規化（`mcause.mpil=0`）と D3（`srli a1,a1,5`）はそのまま | C5 は IDF が CSR mintthresh を使う（INTTHRESH_STANDARD=1、[7:0]）。MMIO 版は段3 で「閾値として効かない」と実測（`test_ipmmask` FAIL）。既定 CSR が正 |
| esp32c5_clic_kernel_impl.h | 新規（fmp3_core 共通 `arch/riscv_gcc/common/clic_kernel_impl.h` の複製） | `clic_set_context_priority` を CSR 0x347 既定 / MMIO option に分岐。irc_begin_ipi のコメントを削除。include guard 名を変更。他の Inline 関数は同一（本ファイル末尾の diff 記録参照） | fmp3_core は改変しない（C6 の chip_start.S と同じ作法）。共通 clic_kernel_impl.c は無改変でリンク |
| chip_asm.inc | P4 | HWLP/PIE のマクロ群（hwlp_push/pop、pie_push/pop、pie_lazy_restore、CSR_PIE_STATE_REG）を削除し追加コンテキストのマクロを空に。my_pid/my_pidx/my_cidx、TLS、core_get_exccode_asm は同一 | コプロセッサ無し |
| chip_kernel.h | P4 | 名前のみ（TMIN_INTPRI -7 / TMAX_INTPRI -1、ena/dis/prb/clr/ras_int サポート） | - |
| chip_kernel.py | P4 | コメントのみ（INTNO_VALID 0..47、INHNO `(prcid<<16)\|intno`、pid2cidx、IncludeTrb clic_kernel.py） | - |
| clic_kernel.py | P4 | 冒頭コメントのみ（置き場所の説明を C5 用に） | common/ は pristine なので chip 層に置く |
| chip_rename.def / chip_rename.h / chip_unrename.h | P4 | def に `esp32c5_intmtx_route` を追加（C6 の `esp32c6_intmtx_route` と同じ扱い。fix round 1 / F2）。.h は `ruby fmp3_core/utils/genrename.rb chip` で再生成 | target_kernel_impl.c から呼ぶカーネル内部関数を `_kernel_` 名前空間へ |
| chip_sil.h | P4 | C6 の chip_sil.h に在るビット操作マクロ（sil_orw 等）と `#include "esp32c5.h"` を追加 | target 側の WDT 無効化（C6 の型）が sil_orw を使う |
| chip_stddef.h | P4 | `TOPPERS_ESP32P4` -> `TOPPERS_ESP32C5`、$Id 行削除 | - |
| chip.cmake | C6 + P4 | C6 の chip.cmake を骨格に、`USE_RISCV_DIRECT_TRAP` と `${COREDIR}/clic_kernel_impl.c` のリンクを P4 から足した。msi_ipi.c / mtimer.c は積まない。start.S 差替えの REMOVE_ITEM を fail-closed（list(FIND) で事前検査）にした | CLIC 非ベクタ。C6 段1 の申し送り 5「REMOVE_ITEM が fail-closed でない」を C5 では最初から塞ぐ |
| chip_start.S | C6 | c6 -> c5 の名前置換と冒頭コメントの書き直しのみ（レジスタ番地は含まない。`grep 0x` で確認） | fmp3_core 共通 start.S の無条件 FPU 初期化が rv32imac で落ちる（C6 段1 と同じ） |
| chip_serial.c / chip_serial.h / chip_serial.cfg | C6 | c6 -> c5 の名前置換のみ（ESP32C5_SIO ディスパッチ、exinf=1） | **計画（PLAN-stage1-impl.md Task 2）は「P4 から」と書いているが C6 から採った**。理由: 段1 で使う USJ ドライバは C6 の hal 版（`esp32c5_usbjtag_hal.c`、バスリセット再武装/IN_EMPTY writable/IN トークン監視の 3 修正込み。target 層）で、その公開 API（`esp32c5_usbjtag_*`）に合うのは C6 型の chip_serial.c である。P4 の chip_serial.c は P4 番地（0x500D2000）を直叩きする自己完結ドライバで、3 修正を持たない |
| esp32c5_uart.c / esp32c5_uart.h | C6（esp32c6_uart.*） | 名前置換のみ。UART0 ベースは target_syssvc.h の SIO_UART_BASE（0x60000000、C6 と同一）。レジスタオフセットは asp3 設計書 3 節で C6 と完全一致を確認済み | uart0 コンソール切替用（M5Stamp-C5 は USJ のみなので既定は usbjtag。段1 では uart0 経路はビルド確認のみ） |
| esp32c5_usbjtag.h | C6（esp32c6_usbjtag.h） | 名前置換のみ。ベース 0x6000F000 は C6 と同一（IDF reg_base.h で確認） | chip_serial.h と target の esp32c5_usbjtag_hal.c が include する（SIOPCB 型、API 宣言、SIO_RDY_*）。**esp32c5_usbjtag.c（レジスタ直叩き版、3 修正無し）は fix round 1（F8）で削除した**: どの cmake も積まない死にファイルで、生きたドライバ（target の hal 版）と取り違えるおそれがあるため。C6 側の esp32c6_usbjtag.c は無改変 |
| esp32c5.h | 新規（値は asp3 esp32c5.h + IDF ヘッダ） | 形は C6 esp32c6.h（`#ifndef CORE_CLK_MHZ`、周波数分岐）と P4 esp32p4.h（CLIC マクロ名）。値は 2 節の表。fix round 1: ATTR バイト値のマクロ名を `ESP32C5_CLIC_ATTR_MODE_BYTE_M` に（F1。IDF clic_reg.h の `CLIC_INT_ATTR_MODE_M`（3<<22）と同名衝突を避ける。`CLIC_INT_THRESH` は共通ヘッダが要求する名前で IDF `interrupt_clic.h` の関数形マクロと同名だが P4 と同じ露出なので据置き） | - |
| cmake/toolchain-riscv-esp32c5.cmake（repo 直下） | C6（toolchain-riscv-esp32c6.cmake） | c6 -> c5 置換。版検査（esp-14.2.0_20260121）は同じ | ディレクトリ外だが本層の一部（F9） |
| cmake/a1_c5_stage1.cmake（repo 直下） | C6（a1_c6_stage1.cmake）の段1 相当のみ | seam / Wi-Fi / USJ probe / SIL_DLY 対照の枝は持たない（空枝を置かない方を選択）。`A1_C5_CPU_FREQ_MHZ` 80/240、`A1_C5_CLIC_THRESH_MMIO`、ROM ld 11 本の絶対パス一覧（eco3 除外、実在検査のみ） | 段2/段4 で C6 の型から足す（F9） |
| CMakeLists.txt の esp32c5 分岐（repo 直下） | C6 分岐の写し | 直後に 10 行追加のみ | F9 |
| （持ち込まない）P4 chip_timer.h / clint_ipi.h / Makefile.chip / chip_kernel.trb / *.md | - | - | mtimer（CLINT）は使わない。.trb は cfg_py 一本化済み。文書は P4 固有 |
| （持ち込まない）asp3 chip_support.S / clic_kernel_impl.h / chip_os_awareness.py | - | - | asp3 の synthetic mret は持ち込まない（PLAN.md D3。FMP3 は P4 で出口正規化済み）。os_awareness は本 repo に機構が無い |

## 2. C5 で変えた値（asp3 と IDF の二重照合）

「IDF 実値」列は 2026-09-15 に `riscv32-esp-elf-gcc -S` で IDF ヘッダをコンパイルして
採った（`.steering/20260915-c5-plan/stage1/README.md` に手順）。「asp3」列は
`asp3/arch/riscv_gcc/esp32c5/esp32c5.h` と `docs/c5-port-design.md` 3/10 節。

| 項目 | C6（参考） | C5 の値 | IDF 実値（ヘッダ） | asp3 | 状態 |
|---|---|---|---|---|---|
| 割込みコントローラ | INTMTX + "PLIC"（0x20001000） | 標準 CLIC。base 0x20800000 / CTRL 0x20801000、NLBITS=3、外部線オフセット 16、線数 48 | `clic_reg.h` DR_REG_CLIC_BASE / DR_REG_CLIC_CTRL_BASE / NLBITS / CLIC_EXT_INTR_NUM_OFFSET | 同値 | 事実 |
| 閾値 | PLICMX_THRESH（MMIO） | 既定 CSR mintthresh 0x347（[7:0]）。option で MMIO 0x20800008（[31:24]） | `interrupt_clic.h` MINTTHRESH_CSR 0x347、INTTHRESH_STANDARD=1（C5） | CSR 0x347（実機で使用）。MMIO は実測 0/未使用 | **事実（段3 Task 4、2026-09-16）**: 効くのは CSR。`test_ipmmask` が CSR 版 PASS / MMIO 版 FAIL（`stage3/README.md` 4 節）。既定は CSR のまま |
| mintstatus | - | 0xFB1（記録のみ。コードでは未使用） | `interrupt_clic.h` MINTSTATUS_CSR 0xFB1（P4 は 0x346） | 同値 | 事実（未使用） |
| INTMTX | 0x60010000 | 0x60010000（同一）。MAP 値 = CLIC 線番号そのもの | `reg_base.h` DR_REG_INTMTX_BASE、`interrupt_matrix_reg.h` MAP_REG(0)=0x60010000 | 同値。「MAP 値は CLIC 内部番号」を実機で確定（実施02） | 事実（本 repo 実機では未確認） |
| 割込みソース番号 | UART0 43 / USJ 48 / SYSTIMER_T0 57 / FROM_CPU 22-25 / 77 本 | UART0 **47** / USJ **54** / SYSTIMER_TARGET0 **61** / FROM_CPU_0..3 **23..26** / ETS_MAX **84** | `interrupts.h`（コンパイルで 47/54/61/23/26/84） | 47/54/61/23-26/84（10 節） | 事実（二重照合一致） |
| INTPRI FROM_CPU_n | 0x600C5090.. | 0x600C5090 / 94 / 98 / 9C（同一） | `intpri_reg.h`（コンパイルで 0x600c5090 / 0x600c509c） | 同値 | 事実 |
| CLIC 線の割付（target 層） | 線 16 timer / 17 SIO / 18,20,21 INTNO1-3 / 1-15 Wi-Fi shim | 線 16 timer(SYSTIMER_T0 + FROM_CPU_0) / 17 SIO / 18 INTNO1 / 20 INTNO2 / 21 INTNO3。19 は INTNO_UNOPTED 用に空ける。22 は IDF の切断先。Wi-Fi shim（段4）は 23..47 から | - | asp3 は INTNO 16/17/18（= CLIC 32/33/34） | 設計（段1） |
| XTAL / systimer | 40 MHz / 16 MHz（40/2.5） | 48 MHz / **16 MHz（48/3）**、TICKS_PER_US=16 | `soc_caps.h` SOC_XTAL_SUPPORT_48M=1。段0 esptool 実読 48 MHz | 16.00 MHz を JTAG 二点法で実測（実施03） | 本 repo 実機では**未確認** |
| CPU クロック | 160（既定）/ seam 80 | 既定 80（seam bootloader の CPU_CLK_FREQ_MHZ_BTLD=80）。240 は段4 | `soc.h:136` CPU_CLK_FREQ_MHZ_BTLD 80 | asp3 は Direct Boot で BBPLL 較正後 240 を実証 | **未確認**（段2 PCR 読出し） |
| SIL_DLY_TIM1/2 | 80 MHz 60/24、160 MHz 30/12（実測） | 80 MHz **60/24（C6 の値を暫定転記）**、240 MHz **17/17（asp3 机上外挿）** | - | 192 MHz 20/20 実測、240 は外挿 | **未較正**（段3） |
| PCR | TG0_CONF +0x3C | SYSCLK_CONF +0x110 / CPU_FREQ_CONF +0x118 / AHB +0x11c（C6 と同じ）、**TIMERGROUP0_CONF +0x54（C6 は +0x3C）** | `pcr_reg.h`（コンパイルで 0x60096054） | - | 事実。C6 の直値 0x6009603C を写すと別レジスタに当たる |
| RAM | 512 KiB、seam 上限 0x4086E610（452,112） | 384 KiB（0x40800000..0x40860000）、seam 上限 **0x4084E5A0（320,928）** | `soc.h:158-159`、`memory.ld.in` SRAM_SEG_END、`bootloader.ld` ASSERT | 384 KiB | 事実 |
| FLASH 窓 | 0x42000000、16 MiB（MMU 256 entry） | 0x42000000（DROM==IROM）、**32 MiB 固定（SOC_IROM_HIGH 0x44000000）** | `soc.h:148-153` | 同値 | 事実 |
| eFuse | 0x600B0800 | **0x600B4800**（MAC +0x44/+0x48） | `reg_base.h`、`efuse_reg.h`（コンパイルで 0x600b4844/48） | 同値 | 事実（段4 用） |
| RNG | LPPERI+0x8 | **LPPERI+0x28**（0x600B2828） | `wdev_reg.h`（コンパイルで 0x600b2828） | 同値 | 事実（段4 用） |
| MODEM_SYSCON | 0x600A9800 | **0x600A9C00** | `modem/reg_base.h`（コンパイルで 0x600a9c00） | 同値 | 事実（段4 用） |
| WDT キー | 0x50D83AA1 | 0x50D83AA1（TIMG / LP_WDT / SWD 共通） | `lpwdt_ll.h:30` LP_WDT_SWD_WKEY_VALUE、`timer_group_reg.h` default | 同値（実施33 で 0x8F1D312A の誤記を訂正） | 事実 |
| USB Serial/JTAG | 0x6000F000、GPIO 12/13 | 0x6000F000（同一）、GPIO **13/14** | `reg_base.h`、`io_mux_reg.h` USB_INT_PHY0_DM/DP_GPIO_NUM（コンパイルで 13/14） | 同値 | 事実 |
| FPU | 無し | 無し（chip_start.S の `__riscv_flen` ガード必須） | `soc_caps.h` に SOC_CPU_HAS_FPU 無し、`esp32c5.rom.rvfp.ld` 在り | - | 事実 |
| ROM ld | 13 本 | 同名 13 本。**eco3 は使わない**（rev v1.0 = 100 < 102） | `esp_rom/esp32c5/ld/`、`esp_rom/CMakeLists.txt:328` | eco3 で store fault（実施08） | 事実（段1 は rom.ld + rom.api.ld の 2 本のみ） |

## 3. esp32c5_clic_kernel_impl.h と共通 clic_kernel_impl.h の差分（記録）

`diff <(sed -n '/^#ifndef TOPPERS_MACRO_ONLY/,$p' fmp3/fmp3_core/arch/riscv_gcc/common/clic_kernel_impl.h) <(同 esp32c5_clic_kernel_impl.h)` の実質差分は
`clic_set_context_priority` の本体のみ:

```
+#ifdef TOPPERS_ESP32C5_CLIC_THRESH_MMIO
     sil_wrw_mem(CLIC_INT_THRESH, (pri & 0xFFU) << CLIC_INT_THRESH_SHIFT);
     (void) sil_rew_mem(CLIC_INT_THRESH);   /* read-back */
+#else /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
+    Asm("csrw 0x347, %0" : : "r"(pri & 0xFFU) : "memory");   /* mintthresh */
+#endif /* TOPPERS_ESP32C5_CLIC_THRESH_MMIO */
```

（他はコメントと include guard 名。irc_begin_ipi の説明コメントを削除。）
共通 `clic_kernel_impl.c` が変わったら本複製を見直すこと（`clic_ipm_shadow` の
宣言と 3 つの extern の契約）。
