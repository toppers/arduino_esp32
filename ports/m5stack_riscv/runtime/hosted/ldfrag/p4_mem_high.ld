/*
 *  seam-eth 構成の「追加メモリ領域」＝ sram_high（段 E-seam・2026-08-15）
 *  ============================================================================
 *  `fmp3/target/m5stamp_esp32p4_gcc/esp32p4_xip.ld` の MEMORY から
 *  `INCLUDE p4_mem_high.ld` で読まれる。**同名の既定ファイル**が
 *  `fmp3/target/m5stamp_esp32p4_gcc/p4_mem_high.ld`（コメントだけ）に在り、
 *  `-L` の探索順で**こちらが先**になる構成でだけ実体が効く
 *  （CMake の `A1_P4_SEAM_ETH` ブロックが `-L esp/p4shim` を先に積む）。
 *
 *  ---- 必要になった理由（実測）----
 *  seam へ lwIP を載せると `liblwip.a` の .bss が **97,664 バイト**あり
 *  （memp プール ＋ `MEM_SIZE`=16KB の ram_heap 等）、sram_low
 *  （0x2CBD0 = 183,248 B）に収まらない（`region RAM overflowed`）。
 *
 *  ---- 境界の根拠（一次資料）----
 *  IDF `esp_system/ld/esp32p4/memory.ld.in:30-32`:
 *      SRAM_HIGH_START = 0x4FF40000
 *      SRAM_HIGH_SIZE  = 0x80000 - CONFIG_CACHE_L2_CACHE_SIZE
 *  本 seam が使う bootloader の sdkconfig は L2 キャッシュ 0x20000 なので
 *      0x80000 - 0x20000 = 0x60000（384KB、0x4FF40000〜0x4FFA0000）。
 *  **上端 0x4FFA0000〜0x4FFC0000 は L2 キャッシュが占める**ので触らない。
 *
 *  0x4FF2_CBD0〜0x4FF4_0000（bootloader の iram_loader_seg と ROM 予約域）は
 *  **依然として触らない**——本 seam は ROM のキャッシュ関数
 *  （`Cache_WriteBack_Addr` 等）と ROM の regi2c を呼ぶので、ROM のデータ域を
 *  潰すと「動くが時々壊れる」形の事故になる。
 */
RAM_HIGH (rw) : ORIGIN = 0x4FF40000, LENGTH = 0x60000
