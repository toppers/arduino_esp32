/*
 *  PSRAM 構成の「IRAM へ追加で置くもの」（段v-2・2026-08-16）
 *  ============================================================================
 *  `fmp3/target/m5stamp_esp32p4_gcc/esp32p4_xip.ld` の `.iram_text` から
 *  `INCLUDE p4_iram_extra.ld` で読まれる。既定（コメントだけ）は
 *  `fmp3/target/m5stamp_esp32p4_gcc/p4_iram_extra.ld`。
 *  どちらが読まれるかは **CMake 変数 `A1_P4_LDFRAG_DIR` 1 個**で決まる
 *  （`-L` の順序に頼らない。段 E-seam §4-3 罠(2) の教訓）。
 *
 *  ---- なぜ要るのか（**忘れると実機でだけ固まる**）----
 *  ESP-IDF は PSRAM 立ち上げ系を **noflash（＝内蔵 RAM 常駐）**に置いている:
 *    `esp_psram/linker.lf:16-17`                        … esp_psram_impl_ap_hex
 *    `esp_hw_support/mspi_timing_tuning/linker.lf:6-14` … mspi_timing_tuning /
 *                                                         mspi_timing_config /
 *                                                         mspi_timing_by_dqs /
 *                                                         mspi_timing_by_flash_delay
 *    `hal/linker.lf:5,9`                                … mmu_hal / cache_hal
 *  理由は、タイミング校正が **外部メモリのキャッシュを freeze する**からである
 *  （`mspi_timing_tuning.c` → `esp_cache_freeze_ext_mem_cache()`）。
 *  freeze 中に flash から命令を取りに行った瞬間に止まる。
 *  seam の ld にはこの配置が無いので、ここで同じことをする。
 *
 *  実 IDF ビルドの `.map` でも該当関数は内蔵 RAM に居る（実測）:
 *    mspi_timing_psram_tuning 0x4ff03ba2 / esp_psram_impl_enable 0x4ff062c6 /
 *    mmu_hal_map_region 0x4ff0ba9a
 *
 *  ---- 対象をアーカイブ名＋オブジェクト名で限定する ----
 *  `*(.text.foo)` のような**名前**で書くと他構成に同名が現れた瞬間に黙って
 *  移動する。`p4_bss_high.ld` と同じ規約でアーカイブ単位に切る。
 *
 *  ---- 自前シムも入れる ----
 *  `p4shim_psram.o` は `esp_cache_freeze_ext_mem_cache()` /
 *  `esp_ipc_isr_stall_other_cpu()` の実体を持つ＝**freeze 区間の中で走る**。
 *  flash に置いたままだと、まさにその瞬間に自分を読めなくなる。
 */
/*
 *  IDF の `IRAM_ATTR` は `.iram1.N` という**孤児セクション**を作る
 *  （例: `esp_rom/patches/esp_rom_regi2c_esp32p4.c` の regi2c 4 本）。
 *  seam の ld はこれを拾わないので、**flash 側へ勝手に流れる**。
 *  実測ではそれが `esptool elf2image` の
 *    「Segment loaded at 0x400076f6 lands in same 64 KB flash mapping as ...」
 *  で表面化した（＝焼く前に止まった。**黙って通らなかったのは幸運である**）。
 *  正しい置き場は IRAM なので、ここで拾う。
 */
*(.iram1 .iram1.*)

*libpsram.a:esp_psram_impl_ap_hex.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libpsram.a:esp_psram_mspi.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libpsram.a:mspi_timing_tuning.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libpsram.a:mspi_timing_config.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libpsram.a:mspi_timing_by_dqs.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libpsram.a:mspi_timing_by_flash_delay.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libpsram.a:mmu_hal.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*libpsram.a:cache_hal.o(.text .text.* .rodata .rodata.* .srodata .srodata.*)
*p4shim_psram.c.obj(.text .text.* .rodata .rodata.* .srodata .srodata.*)
