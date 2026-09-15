# `target/m5stampc5_gcc/`（M5Stamp-C5 target 層）の出典と改変

- 取込み 2026-09-15（計画 3 段1、`.steering/20260915-c5-plan/PLAN-stage1-impl.md` Task 3）。
- 出典: **C6** = `fmp3/target/m5nanoc6_gcc/`（C6 段1-4。asp3_esp_idf `6471444`
  `asp3/target/esp32c6_espidf/` 由来）。**P4** = `fmp3/target/m5stamp_esp32p4_gcc/`
  （target_test.h の方式）。**asp3** = `/home/honda/TOPPERS/asp3_esp_idf` `6471444`
  `asp3/target/esp32c5_espidf/`（値と罠のみ。コードは持ち込まない）。**IDF** = esp-idf
  v5.5.4 `735507283d`（`components/soc/esp32c5`、`bootloader/subproject/main/ld/esp32c5/
  bootloader.ld`、`esp_system/ld/esp32c5/memory.ld.in`）。
- 監査: 本段では pristine 監査を置かない（C6 段1 と同じ判断）。下表で改変を申告する。
- 照合コマンド: `diff -u <(sed 's/esp32c6/esp32c5/g;s/ESP32C6/ESP32C5/g;s/ESP32-C6/ESP32-C5/g;s/M5NanoC6/M5Stamp-C5/g;s/m5nanoc6_gcc/m5stampc5_gcc/g;s/M5NANOC6/M5STAMPC5/g;s/A1_C6_/A1_C5_/g' fmp3/target/m5nanoc6_gcc/<f>) fmp3/target/m5stampc5_gcc/<f>`
- 文字種: 新規/改変部分は Ambiguous-width 文字を使わない。C6 から写した既存コメントは対象外。

| ファイル | 出典 | 出典との差 | 理由 |
|---|---|---|---|
| target.cmake | C6 | esp32c6 -> esp32c5（IDF の hal/soc/esp_rom パス、ROM ld 2 本）。`esp/config/esp32c6/hal_stub_include` を積まない。既定 ld を `esp32c5_xip.ld`（C6 の段1 用 `esp32c6.ld` = Direct Boot 配置は作らない） | C5 の sdkconfig.h（asp3 手書きスタブ）は `nuttx/config.h` を include しない。seam 一本（PLAN.md D2） |
| target_kernel_impl.c | C6 | (1) PCR_TIMERGROUP0_CONF を直値 `0x6009603C` から `ESP32C5_PCR_TIMERGROUP0_CONF`（**+0x54**）のマクロ経由に（`sil_orw`/`sil_clrw`）。(2) WDT レジスタ/キーは esp32c5.h のマクロ（値は C6 と同一）。SWD の `(1U << 30)` 直値を `ESP32C5_RTC_CNTL_SWD_DISABLE` に。(3) 割込みソース番号は C5（`esp32c5_intmtx_route`: SYSTIMER_TARGET0=61 と FROM_CPU_0=23 を線 16、USJ=54 / UART0=47 を線 17）。(4) FROM_CPU_1..3 の route を**行わない**（target_test.h 参照）。(5) 冒頭コメントに asp3 の r32/r34/r35/r41/pmu_init/APM を持ち込まない理由 | C5 の `pcr_reg.h` は TIMERGROUP0_CONF が +0x54（C6 の直値 +0x3C を写すと別レジスタ）。IDF ヘッダのコンパイルで実値確認。seam の bootloader が BBPLL/CPU 80MHz/PMA を済ませる見込みは**机上**（段2 実測） |
| target_test.h | P4 の方式 + C6 の線番号 | 全面書き直し。INTNO1/2/3 = CLIC 線 18/20/21、`TA_ENAINT \| TA_EDGE`、`intnoN_clear()` は `CLIC_INT_CTRL` の IP を落とす。INTNO1_INTPRI = TMAX_INTPRI | **計画 Task 3 の「INTNO1/2/3 = FROM_CPU_0-2 の CLIC 線」から意図的に外れた（申告）**。理由: FROM_CPU_0 はタイマ強制（target_timer.h、C6/asp3 と同じ多重マップ）が使う。chip 層（P4 の型）の raise_int/clear_int は CLIC の IP を操作するので、割込みマトリクス経由のレベル線では ras_int が効かず、C6 型（INTPRI で raise/clear）へ chip 層を書き換える必要が出る。P4 方式は P4 実機で test_int1/test_ipmmask/test_dcre5 が PASS。**C5 実機で段3 Task 4（2026-09-16）に test_int1 / test_ipmmask（INTNO1 = 線 18）が PASS（S3-2 の FROM_CPU 経路は不要）。INTNO2/3（線 20/21）は test_dcre5 が CLS_PRC2 で建たないため未検証** |
| target_timer.h / target_timer.c / target_timer.cfg | C6 | 名前置換（ESP32C5_*、esp32c5_systimer_read）。INTNO_TIMER=16 / INHNO_TIMER_PRC1=(PRC1<<16)\|16 は C6 と同じ値（C5 では CLIC 外部線 16 = 最初の外部線）。コメントの「線1-15 は Wi-Fi shim 予約」を「0-15 は内部線」に | systimer は C6 と同一レイアウト/同一ベース（IDF `systimer_ll.h` の diff はコメント 2 行）。16 MHz = 48/3（asp3 実測。本 repo では未確認） |
| target_hrt64.c | C6 | 名前置換のみ。段1 ではリンクしない（C6 と同じく段4 の Wi-Fi ブロックだけ） | - |
| target_syssvc.h | C6 | TARGET_NAME `"M5Stamp-C5 (ESP32-C5)"`、INTNO_SIO=17 のコメントを C5 の線割付けに | - |
| target_kernel.h / target_kernel_impl.h / target_class.py / target_ipi.h / target_asm.inc / target_cfg1_out.h / target_check.py / target_kernel.cfg / target_kernel.py / target_serial.cfg / target_serial.h / target_sil.h / target_stddef.h / target_rename.def | C6 | 名前置換のみ（`TOPPERS_M5NANOC6_GCC` -> `TOPPERS_M5STAMPC5_GCC`、`#error` 文言等） | - |
| target_rename.h / target_unrename.h | 生成 | `ruby fmp3_core/utils/genrename.rb target` で再生成（C6 とバイト同一） | - |
| esp32c5_usbjtag_hal.c | C6（esp32c6_usbjtag_hal.c） | 名前置換のみ（`esp32c5_usbjtag_*`、`ESP32C5_USJ_PROBE/NOFIX`）。3 修正（バスリセット再武装 / IN_EMPTY writable / IN トークン監視）込み | USJ は C6 と同一ブロック（ベース 0x6000F000、EP1/EP1_CONF 同オフセット）。C5 の `hal/esp32c5/include/hal/usb_serial_jtag_ll.h` は C6 版と `ep1.val` vs `ep1.rdwr_byte`（LL 内部の union メンバ名）と mem_pd の有無しか違わず、本ドライバは LL 関数だけを呼ぶので影響しない。asp3 が C5 で記録した `rst:0x15`（CDC open）/ download latch は M5Stamp-C5 で出るか**未確認**（段2） |
| esp32c5_xip.ld | C6（esp32c6_xip.ld） | RAM LENGTH `0x4084E5A0 - 0x40800000`（320,928 B。C6 は 0x4086E610）。FLASH LENGTH `0x2000000 - 0x20`（32 MiB 固定窓。C6 は 16 MiB）。`INCLUDE esp32c5.peripherals.ld`。ENTRY は `toppers_start` のまま（**段2 で決めた**: seam は cmake が `-Wl,-e,seam_c5_entry` で上書きする。ld を分けず、非 seam ELF を 1 バイトも変えないため。冒頭 (4)）。他（.flash_rodata_dummy / .flash.appdesc / LMA==VMA / クラス別セクション / IRAM 群）は C6 と同一 | IDF `memory.ld.in` SRAM_SEG_END / `bootloader.ld` ASSERT / `soc.h` SOC_IROM_HIGH。LENGTH を切るので超過はリンク失敗（黙って bootloader を上書きしない） |
| diag_recorder.c / diag_recorder.h | C6 | 名前置換と syslog 文字列 `diag(c6)` -> `diag(c5)`。段1 ではリンクしない | 段4 の esp/shim が要求する API |
| app/fmp_app/fmp_app.c | C6 | seam_c6_clk の include と `A1_C6_SEAM` ガード付きの syslog 行を削除（段2/段4 で seam_c5_* を作るときに足す）。PCR 2 レジスタの読出し 1 行と tick の hrt 差分は残す | 段1 でも `-DA1_C5_APPDIR=app -DA1_C5_APP=fmp_app` がリンクできる |
| app/fmp_app/fmp_app.h / fmp_app.cfg | C6 | 名前置換のみ | - |
| （持ち込まない）esp32c6.ld（Direct Boot 配置） | - | - | seam 一本。RAM ORIGIN 0x40819000 等の asp3 診断由来の値も持ち込まない |
| （持ち込まない）app/usj_probe | - | - | C6 段3 の USJ 計測ツール。段3 で要れば写す |
| （持ち込まない）asp3 target_kernel_impl.c の r32/r34/r35/r41/pmu_init/APM 解除、Direct Boot ヘッダ生成用アセンブリ、run.cmake、target_os_awareness.py | - | - | PLAN.md D2/D3、C6 段1 と同じ判断。APM は段4（shim 側。C6 と同じ場所） |

## 段1 で確定した数値（C5 固有。ld と esp32c5.h に写した）

| 項目 | 値 | 出典 |
|---|---|---|
| seam 静的 RAM 上限 | 0x4084E5A0（320,928 B） | IDF `esp_system/ld/esp32c5/memory.ld.in:21` SRAM_SEG_END、`bootloader.ld:48` ASSERT |
| HP SRAM | 0x40800000..0x40860000（384 KiB） | IDF `soc.h:158-159` |
| FLASH 窓 | 0x42000000..0x44000000（DROM==IROM） | IDF `soc.h:148-153` |
| MMU ページ | 64 KiB | IDF `ext_mem_defs.h:19`、M5Stack sdkconfig |
| 段1 ELF の RAM 使用（test_int1、CSR 版） | .data 0x160 (352 B) @0x40800000、.bss 0x3000 (12,288 B) @0x40800180、終端 0x40803180 | `readelf -S`（`size` の bss 63,600 は NOBITS の .flash_rodata_dummy 51,312 B を含む） |

## 段2（2026-09-16）で足したもの（本ディレクトリ外。C6 段2 の写し）

| ファイル | 出典 | 差分 |
|---|---|---|
| `esp/boot/seam_c5_entry.S` | `esp/boot/seam_c6_entry.S` | 名前置換 + `SEAM_C6_CLK_BOOST`（C6 段4 の 160 MHz 昇圧）を落とした。USJ 到達マーク（`SEAM_C5_ENTRY_MARK`、既定 0x53）と `la/jr toppers_start` のみ |
| `esp/boot/seam_c5_appdesc.c` | `esp/boot/seam_c6_appdesc.c` | 名前置換（`seam_c5_app_desc`、`"FMP3-seam-C5"`、`"fmp3_esp32c5"`）。`mmu_page_size=0` は C5 では読まれない（`SOC_MMU_PAGE_SIZE_CONFIGURABLE` 無し）ことをコメントに |
| `cmake/a1_c5_seam_image.sh` | `cmake/a1_c6_seam_image.sh` | C5 の窓/SRAM/LOADER_SEG（0x4084E5A0）、`esp/boot/seam_c5/partitions.csv`、**C-9**（chip_id 0x17 と rev 100 の範囲）を追加 |
| `cmake/a1_c5_stage1.cmake` の `A1_C5_SEAM` | `cmake/a1_c6_stage1.cmake` の `A1_C6_SEAM`（段2 部分） | `seam_c6_clk` を持ち込まない。`-Wl,-e,seam_c5_entry` を `target_link_options(fmp)` で渡す |
| `esp/boot/seam_c5/`、`esp/boot/build_seam_c5_bootloader_esp32c5.sh` | `esp/boot/seam_c6/`、`build_seam_c6_bootloader_esp32c6.sh` | target esp32c5。台本末尾で offset 0x2000 / MMU 0x10000 / USJ / WDT / REV_MIN,MAX を表示 |
