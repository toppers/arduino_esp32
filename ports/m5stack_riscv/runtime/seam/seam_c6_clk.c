/*
 *  seam-C6: CPU クロックを 80MHz -> 160MHz へ昇圧する（A1_C6_CPU_FREQ_MHZ=160）
 *  ==========================================================================
 *  ESP32-C6 統合 段4 Task 1（2026-09-14）。判定基準:
 *  .steering/20260913-c6-stage4/AC.md AC-1。型は esp/boot/seam_p4_clk.c
 *  （P4 版の 90MHz->360MHz 昇圧）だが、レジスタもアプローチも別物 --
 *  C6 は分周器 1 本（PCR_CPU_FREQ_CONF の CPU_HS_DIV_NUM）を書き換える
 *  だけで済む（P4 のような 4 段の分周器、MEM_CLK 変化、RAM 常駐化は不要）。
 *
 *  --------------------------------------------------------------------------
 *  なぜ要るか
 *  --------------------------------------------------------------------------
 *  seam の CMake 既定（cmake/a1_c6_stage1.cmake の D1）は
 *  `-DCORE_CLK_MHZ=80` を渡す -- 段2 の実機診断で、seam が使う実 ESP-IDF
 *  2nd-stage bootloader が起動直後の PCR を
 *    PCR_SYSCLK_CONF   = 0x28010200（SOC_CLK_SEL=1=SPLL, HS_DIV_NUM=2=/3,
 *                                     XTAL_FREQ=40）
 *    PCR_CPU_FREQ_CONF = 0x00000100（CPU_HS_DIV_NUM=1=/2, HS_120M_FORCE=0）
 *  に設定して渡す（= 480/3/2 = 80MHz）ことを実測したため（esp32c6.h の
 *  CORE_CLK_MHZ コメント、段3 baseline ログ参照）。本ファイルは
 *  CPU_HS_DIV_NUM を 1 から 0 へ書き換えて 480/3/1 = 160MHz へ上げる。
 *
 *  --------------------------------------------------------------------------
 *  レジスタ、式の一次資料（手で覚えた値ではなく、ヘッダを読んで確かめた）
 *  --------------------------------------------------------------------------
 *  esp-idf/components/hal/esp32c6/include/hal/clk_tree_ll.h:377-397
 *  clk_ll_cpu_set_hs_divider(divider):
 *    "(PCR_HS_DIV_NUM + 1) * (PCR_CPU_HS_DIV_NUM + 1) = divider"
 *    HS_DIV_NUM は HRO（ハード読み出し専用、実質固定 2=/3）。
 *    CPU_HS_DIV_NUM は 480/3/(CPU_HS_DIV_NUM+1) となるよう選ぶ。
 *    divider=3 (=160MHz) -> cpu_hs_div_num = (3/3)-1 = 0、force_120m=0。
 *
 *  esp-idf/components/soc/esp32c6/register/soc/pcr_reg.h:
 *    PCR_SYSCLK_CONF_REG   (base+0x110)
 *      :1608-1614 PCR_HS_DIV_NUM     bitpos[15:8]  HRO（固定 2）
 *      :1615-1621 PCR_SOC_CLK_SEL    bitpos[17:16] 0=XTAL/1=SPLL/2=FOSC
 *    PCR_CPU_FREQ_CONF_REG (base+0x118)
 *      :1668-1676 PCR_CPU_LS_DIV_NUM   bitpos[7:0]
 *      :1677-1685 PCR_CPU_HS_DIV_NUM   bitpos[15:8]  <- 本ファイルが書く
 *      :1686-1693 PCR_CPU_HS_120M_FORCE bitpos[16]   <- 0 に保つ（160MHz
 *                  では 120MHz 強制は無効かつ無関係）
 *  esp32c6.h（本リポジトリ既存、ESP32C6_PCR_* マクロ）の
 *  「PCR_CPU_FREQ_CONF：bit[7:0] CPU_LS_DIV_NUM、bit[15:8] CPU_HS_DIV_NUM、
 *  bit16 CPU_HS_120M_FORCE」というコメントは上記 pcr_reg.h と一致することを
 *  本 Task で確認した（ビット位置は独自に導出し直した。ドキュメントの
 *  引用を鵜呑みにしていない）。
 *
 *  esp-idf/components/esp_hw_support/port/esp32c6/rtc_clk.c:217-227
 *  rtc_clk_cpu_freq_to_pll_mhz() の手順（80->160 と同じ PLL 内での遷移）:
 *    (1) clk_ll_cpu_set_hs_divider(divider)
 *    (2) clk_ll_cpu_set_src(SOC_CPU_CLK_SRC_PLL)   -- 既に PLL のため無変更
 *    (3) esp_rom_set_cpu_ticks_per_us(cpu_freq_mhz)
 *  rtc_clk.c:180-210（XTAL/LS 経路の rtc_clk_cpu_freq_to_xtal/_to_8m）は
 *  AHB の LS 分周器も書き換えるが、**PLL/HS 経路（上記 :217-227）は
 *  AHB/MSPI を一切触らない**（clk_ll_cpu_set_hs_divider と
 *  clk_ll_cpu_set_src のみ）。MSPI（flash）のクロックは
 *  PCR_MSPI_FAST_{LS,HS}_DIV_NUM という別レジスタで独立に持つ
 *  （pcr_reg.h:202-219）ため、CPU_HS_DIV_NUM の書き換えは cache/flash の
 *  タイミングに影響しない -- P4（HP_ROOT 分周器 1 本で CPU/MEM/SYS/APB
 *  全部が連動し、昇圧中に cache クロックが揺れる）と異なり、本ファイルは
 *  RAM 常駐にする必要が無い（flash 実行のままで安全）。
 *
 *  esp_rom_set_cpu_ticks_per_us() は ROM の ets_update_cpu_frequency の
 *  エイリアス（esp-idf/components/esp_rom/esp32c6/ld/esp32c6.rom.api.ld:55
 *  `PROVIDE ( esp_rom_set_cpu_ticks_per_us = ets_update_cpu_frequency )`、
 *  実体アドレスは esp32c6.rom.ld:33 `ets_update_cpu_frequency = 0x40000048`）。
 *  C6 の seam は P4 と異なり ROM linker script
 *  （esp-idf/components/esp_rom/esp32c6/ld/esp32c6.rom.ld）を**既に**
 *  target.cmake（fmp3/target/m5nanoc6_gcc/target.cmake:81）が全ビルドで
 *  リンクしている（fmp3/target/m5nanoc6_gcc/target_kernel_impl.c の
 *  hardware_init_hook が既に esp_rom_set_cpu_ticks_per_us(CORE_CLK_MHZ) を
 *  呼んでいる実績どおり）。そのため P4 のような関数ポインタ直接呼出しは
 *  不要で、esp_rom_sys.h を include して通常のシンボル呼出しでよい。
 *
 *  --------------------------------------------------------------------------
 *  結果の受け渡し先が .bss ではなく .data である理由（重要）
 *  --------------------------------------------------------------------------
 *  本関数は seam_c6_entry.S から toppers_start より前に呼ばれる。ところが
 *  fmp3_core/arch/riscv_gcc/common/start.S の順序は
 *    toppers_start -> hardware_init_hook（.bss 初期化より前）
 *                   -> .sbss/.bss クリア
 *                   -> .data コピー（本 ld は LMA==VMA なので自己コピー）
 *                   -> software_init_hook -> タスク開始
 *  であり、hardware_init_hook はまだ **.bss クリアより前**に呼ばれる
 *  （start.S:94-138、hardware_init_hook の呼出しが bss クリアブロックより
 *  前にある）。entry.S からの呼出しはさらにそれより前（toppers_start に
 *  入る前）なので、**.bss に結果を書いても直後の .bss クリアで消える**。
 *  一方 .data は fmp3/target/m5nanoc6_gcc/esp32c6_xip.ld が LMA==VMA
 *  （AT() 無し）で組んでおり、esp_image ローダ（bootloader）がイメージ内の
 *  .data の初期値バイト列を直接 RAM の .data 番地へ配置してから entry を
 *  呼ぶ。start.S の "data コピー" は __idata_start==__data_start の
 *  自己コピー（読んだ値をそのまま同じ番地へ書き戻すだけ）であり、entry.S
 *  が書き込んだ値を上書きしない。よって g_seam_c6_clk_result（本ファイル
 *  末尾、__attribute__((section(".data"))) で明示配置）は entry.S での
 *  書込みから fmp_app タスクでの読み出しまで生き残る。
 *  esp/boot/seam_c6_clk.h にも同じ理由を書いてある。
 */
#include "seam_c6_clk.h"
#include <sil.h>
#include "esp32c6.h"
#include "esp_rom_sys.h"

/*
 *  .bss ではなく .data に明示配置（理由は本ファイル冒頭コメント）。
 *  初期値はすべて 0 = SEAM_C6_CLK_RC_NOT_RUN（呼ばれなかった場合の既定）。
 */
seam_c6_clk_result_t g_seam_c6_clk_result __attribute__((section(".data"))) = {
	0U, 0U, 0U, 0U, SEAM_C6_CLK_RC_NOT_RUN
};

#define PCR_SYSCLK_CONF_SEL_MASK    ((uint32_t) (3U << 16))   /* SOC_CLK_SEL[17:16] */
#define PCR_SYSCLK_CONF_SEL_SPLL    ((uint32_t) (1U << 16))
#define PCR_CPU_FREQ_HS_DIV_MASK    ((uint32_t) (0xFFU << 8)) /* CPU_HS_DIV_NUM[15:8] */
#define PCR_CPU_FREQ_HS_FORCE_MASK  ((uint32_t) (1U << 16))   /* CPU_HS_120M_FORCE[16] */
#define PCR_CPU_FREQ_160MHZ_MASK    (PCR_CPU_FREQ_HS_DIV_MASK | PCR_CPU_FREQ_HS_FORCE_MASK)
#define PCR_CPU_FREQ_160MHZ_VALUE   0U   /* cpu_hs_div_num=0, force_120m=0 */

void
seam_c6_clk_set(void)
{
	uint32_t sysclk_conf, cpu_freq_conf;
#ifdef SEAM_C6_CLK_BOOST
	uint32_t written, readback;
#endif

	sysclk_conf   = sil_rew_mem((const uint32_t *) ESP32C6_PCR_SYSCLK_CONF);
	cpu_freq_conf = sil_rew_mem((const uint32_t *) ESP32C6_PCR_CPU_FREQ_CONF);
	g_seam_c6_clk_result.sysclk_conf_before   = sysclk_conf;
	g_seam_c6_clk_result.cpu_freq_conf_before = cpu_freq_conf;

#ifdef SEAM_C6_CLK_BOOST
	if ((sysclk_conf & PCR_SYSCLK_CONF_SEL_MASK) != PCR_SYSCLK_CONF_SEL_SPLL) {
		/*
		 *  前提（ブートローダが SPLL を残す）が崩れている。書かずに戻る --
		 *  ソースが XTAL/FOSC のまま HS 分周器だけ書き換えても意味がない
		 *  （分周式は HS=SPLL 前提。rtc_clk.c:220 のコメント「PLL must
		 *  already be enabled」を参照）。
		 */
		g_seam_c6_clk_result.sysclk_conf_after   = sysclk_conf;
		g_seam_c6_clk_result.cpu_freq_conf_after = cpu_freq_conf;
		g_seam_c6_clk_result.rc = SEAM_C6_CLK_RC_SKIP_NOT_PLL;
		return;
	}

	written = (cpu_freq_conf & ~PCR_CPU_FREQ_160MHZ_MASK) | PCR_CPU_FREQ_160MHZ_VALUE;
	sil_wrw_mem((uint32_t *) ESP32C6_PCR_CPU_FREQ_CONF, written);

	/*
	 *  読み戻して確かめる（書いたつもりで書けていない、を通さない。
	 *  P4 版 seam_p4_clk.c と同じ作法）。
	 */
	readback = sil_rew_mem((const uint32_t *) ESP32C6_PCR_CPU_FREQ_CONF);
	g_seam_c6_clk_result.sysclk_conf_after   =
		sil_rew_mem((const uint32_t *) ESP32C6_PCR_SYSCLK_CONF);
	g_seam_c6_clk_result.cpu_freq_conf_after = readback;

	if ((readback & PCR_CPU_FREQ_160MHZ_MASK) != PCR_CPU_FREQ_160MHZ_VALUE) {
		g_seam_c6_clk_result.rc = SEAM_C6_CLK_RC_VERIFY_NG;
		return;
	}

	/*
	 *  ROM の g_ticks_per_us 相当を実クロックへ合わせる（rtc_clk.c:227 と
	 *  同じ手順）。FMP3 自身は ROM 遅延関数を使っていないが（esp32c6.h の
	 *  CORE_CLK_MHZ コメント参照）、target_kernel_impl.c の
	 *  hardware_init_hook が本関数の直後（toppers_start 経由）で同じ呼出しを
	 *  CORE_CLK_MHZ 使って行うため、実質的には二重呼出しになる（無害 --
	 *  ROM 側は単に格納するだけの関数）。ここで呼ぶのは「レジスタを書いた
	 *  直後に対応する ROM 状態も合わせる」という IDF と同じ規律を保つため。
	 */
	esp_rom_set_cpu_ticks_per_us(160U);
	g_seam_c6_clk_result.rc = SEAM_C6_CLK_RC_OK;
#else
	/*
	 *  SEAM_C6_CLK_BOOST 未定義（80MHz ビルド）: 昇圧しない。before/after は
	 *  同じ値のまま、rc は初期値 SEAM_C6_CLK_RC_NOT_RUN（本ファイル冒頭の
	 *  初期化子）のまま変更しない -- entry.S は SEAM_C6_CLK_BOOST 未定義時に
	 *  そもそも本関数を呼ばないので、実際にはこの #else 分岐へ到達すること
	 *  自体が無い（cmake/a1_c6_stage1.cmake が CORE_CLK_MHZ=160 と
	 *  SEAM_C6_CLK_BOOST を常に同時に立てるため、両者が食い違う組み合わせは
	 *  存在しない）。ここに来た場合でも rc は NOT_RUN のままにしておく、
	 *  というのが本分岐の意図であり、「呼ばれたことを示す専用の SKIP を
	 *  設ける」計画は無い。
	 */
	g_seam_c6_clk_result.sysclk_conf_after   = sysclk_conf;
	g_seam_c6_clk_result.cpu_freq_conf_after = cpu_freq_conf;
#endif
}
