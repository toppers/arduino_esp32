/*
 *  既定の「追加メモリ領域」——**空**（コメントだけ）。
 *
 *  `esp32p4_xip.ld` の MEMORY から `INCLUDE p4_mem_high.ld` で読まれる。
 *  seam-eth 構成では `esp/p4shim/p4_mem_high.ld`（同名・実体つき）が
 *  `-L` の探索順で先に見つかり、こちらは使われない。
 *
 *  **ここに何も置かないことが、既定構成（seam-p4-smp を含む）の golden を
 *  不変に保つ条件である**——空の MEMORY 領域や空の出力セクションでも
 *  ELF のセクション表・シンボル表が変わり、esptool が
 *  `app_desc.app_elf_sha256` へ書く 32 バイトが動く（段E §6-1 と同じ経路）。
 */
