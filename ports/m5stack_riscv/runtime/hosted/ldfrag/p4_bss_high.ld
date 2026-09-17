/*
 *  段7d（esp-hosted データパス）の「追加出力セクション」・2026-08-16
 *  ============================================================================
 *  `esp32p4_xip.ld` の SECTIONS から `INCLUDE p4_bss_high.ld` で読まれる。
 *  どの実体が読まれるかは `-L` の探索順で決まる（CMake の
 *  `A1_P4_LDFRAG_DIR`）。本ファイルは **`A1_P4_HOSTED_NET=ON` のときだけ**
 *  読まれる。
 *
 *  ---- なぜ `esp/p4shim/p4_bss_high.ld` を使い回さないのか（記録）----
 *  必要な規則が段E-seam と 1 行違う（下の `target_heap`）。あちらへ 1 行足すと
 *  **段E-seam の最終バイナリが動く**（ヒープの番地が変わる）。段E の対象板は
 *  Unit PoE-P4 であり、本段の対象は **Tab5 のみ**と決まっているので、
 *  **確かめられない構成を変えない**。⇒ 別ファイルにして段E を 1 バイトも
 *  触らない。4 本の断片のうち 3 本（mem_high / iram_extra / dram_extra）は
 *  `esp/p4shim/` からの写しである（`INCLUDE` は同名を自分自身で解決して
 *  無限再帰になるため、共有できない）。
 *
 *  ---- なぜ必要か（実測 2026-08-16）----
 *  段 7c の構成は sram_low（183,248 B）を **183,104 B** 使っており、
 *  **空きは 144 バイトしかなかった**。そこへ lwIP を載せると
 *    - `liblwip.a` の .bss                      97,664 B （下の規則で RAM_HIGH へ）
 *    - lwIP プールのタスクスタック 8 x 4096     32,768 B （kernel_cfg.c 側）
 *    - sys_arch / netif / 判定器                 約 3 KB
 *  で **38,784 B 超過**した（`ld: region RAM overflowed by 38640 bytes`）。
 *
 *  ⇒ `target_heap`（newlib のヒープ・**64KB 固定**）を RAM_HIGH へ移す。
 *    - 容量を**減らさない**のが要点である。スタックやヒープを縮めて凌ぐと、
 *      不足は「不定なタイミング・箇所でのクラッシュ」として後から出る
 *      （7b の `A1_P4_HOSTED_POOL_TASK_STACK` の警告と同じ理由）。
 *    - RAM_HIGH は素の SRAM である。段E-seam は既に `liblwip.a` の
 *      PBUF_POOL をここへ置いて **EMAC の DMA を当てて動かしている**ので、
 *      DMA 可否についても実績がある。
 *    - ヒープはゼロ初期化を必要としないが、`.bss_high` は
 *      `software_init_hook`（`esp/p4shim/p4shim_misc.c`）がゼロクリアするので
 *      いずれにせよ従来と同じ状態で始まる。
 *
 *  **入れるものをアーカイブ名／オブジェクト名で限定している**のが要点である
 *  （段E-seam から引き継いだ規律）。`*(.bss.foo)` のような**名前**で書くと、
 *  他構成に同名が現れた瞬間に黙って移動する。
 */
.bss_high (NOLOAD) : ALIGN(16)
{
    __bss_high_start = ABSOLUTE(.);
    *liblwip.a:*(.bss .bss.* .sbss .sbss.* .scommon COMMON)
    *libeth.a:*(.bss .bss.* .sbss .sbss.* .scommon COMMON)
    /*  段7d: newlib のヒープ（64KB）。**オブジェクト名まで指定して限定する**  */
    *libfmp3.a:target_kernel_impl.c.obj(.bss.target_heap)
    /*
     *  ---- 本リポジトリで足した 1 行（段 B2b・2026-09-18）----
     *  こちらのスケッチリンクは `libfmp3.a` を通さない。段（stage）は
     *  カーネルの .o を**そのまま**並べて渡すので（objs/ の 72 本、
     *  link-manifest.json の objectOrder）、上の
     *  `*libfmp3.a:target_kernel_impl.c.obj` はどれにも当たらず、
     *  64KB のヒープが low RAM に残って `.bss` が 52,328 B 溢れた（実測）。
     *  上の規律（アーカイブ名／オブジェクト名で限定する。名前だけで書かない）
     *  はそのままに、段の綴りでもう 1 本書く。dev 側の構成では
     *  `target_kernel_impl.o` という綴りの入力が無いので、あちらの
     *  バイト列には影響しない。
     */
    *target_kernel_impl.o(.bss.target_heap)
    . = ALIGN(16);
    __bss_high_end = ABSOLUTE(.);
} > RAM_HIGH
