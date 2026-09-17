/*
 *  PSRAM 構成の「.data へ追加で置くもの」（段v-2・2026-08-16）
 *  ============================================================================
 *  `esp32p4_xip.ld` の `.data` から `INCLUDE p4_dram_extra.ld` で読まれる。
 *  既定（コメントだけ）は `fmp3/target/m5stamp_esp32p4_gcc/p4_dram_extra.ld`。
 *
 *  IDF の `DRAM_ATTR` / `ESP_DRAM_LOGx` は `.dram1.N` という**孤児セクション**を
 *  作る（例: `esp_rom_regi2c_esp32p4.c` の文字列定数）。
 *  意図は「キャッシュが止まっている間でも読める場所に置く」ことなので、
 *  **flash（XIP）へ流してはいけない**。RAM 側（.data）で受ける。
 */
*(.dram1 .dram1.*)
