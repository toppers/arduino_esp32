/*
 *  seam-P4: core1（HP core1 / hart1 / PRC2）を起こす
 *  ==========================================================================
 *  ESP32-P4 統合 段6（2026-08-15）。判定基準: .steering/20260815-p4-stage6/AC.md S6-1
 *
 *  --------------------------------------------------------------------------
 *  なぜ要るか
 *  --------------------------------------------------------------------------
 *  方式(a)（段2/段3）で core1 を起こしていたのは FMP3 ではなく **IDF アプリ側**で、
 *  `fmp_loader.c:38-41`（svn `target/m5stamp_esp32p4_gcc/tools/fmp_loader/main/`）が
 *
 *      REG_SET_BIT(HP_SYS_CLKRST_SOC_CLK_CTRL0_REG, HP_SYS_CLKRST_REG_CORE1_CPU_CLK_EN);
 *      REG_CLR_BIT(HP_SYS_CLKRST_HP_RST_EN0_REG,    HP_SYS_CLKRST_REG_RST_EN_CORE1_GLOBAL);
 *      ets_set_appcpu_boot_addr((uint32_t) &toppers_start);
 *      esp_cpu_unstall(1);
 *
 *  を実行していた。seam にはその IDF アプリが無い＝**core1 を起こす主体が存在しない**。
 *  段4/段5 の seam は PRC_NUM=1 固定で、CMake が PRC>=2 を fail-closed で拒否していた
 *  （`cmake/a1_p4_stage1.cmake`）。本ファイルがその主体である。
 *
 *  --------------------------------------------------------------------------
 *  どこから呼ばれるか（＝なぜ seam エントリではないのか）
 *  --------------------------------------------------------------------------
 *  `target_mprc_initialize()`（`fmp3/target/m5stamp_esp32p4_gcc/target_kernel_impl.c`）から。
 *  これは **ESP32-S3 の seam と同じ作法**である（`fmp3/target/esp32s3_devkitc_gcc/
 *  target_kernel_impl.c:328-360` が `target_mprc_initialize()` で APP_CPU を起こす）。
 *
 *  seam のエントリ（`esp/boot/seam_p4_entry.S`）から呼ばない理由は**構造的**である（事実）:
 *
 *    - `start.S` は core1 も `toppers_start` に入れる。core1 は `hardware_init_hook` の後
 *      `slave_wait` で `start_sync[]`（`arch/riscv_gcc/common/core_kernel_impl.c:53`、
 *      `volatile uint_t start_sync[TNUM_PRCID]` ＝ **.bss**）を読んで
 *      `MAGIC_START(0x87654321)` を待つ。
 *    - その `.bss` を**ゼロ埋めするのは master**であり、`toppers_start` の中である
 *      （`start.S:109-138`）。
 *    - 方式(a) では、FMP3 の `.bss` は IDF アプリの `.bss` の一部で、**IDF の起動処理が
 *      `app_main` より前に既にゼロ埋めしていた**。だから core1 を `app_main` で
 *      起こしてよかった。
 *    - seam にはその前段が無い。エントリから起こすと core1 は
 *      **ゼロ埋め前の `start_sync[1]` を読む**ことになり、たまたま `MAGIC_START` が
 *      入っていれば早すぎる時点で `sta_ker` へ進む。
 *
 *  `target_mprc_initialize()` は `start.S:174` から呼ばれる＝**.bss クリアと .data コピーの
 *  後・master のみ・`start_sync` へ MAGIC を書く直前**であり、上の穴が構造的に無い。
 *  （`_kernel_istkpt_table` は `STK_T *const`＝.rodata なので、core1 の `my_istkpt` は
 *   .data コピーの前でも正しい値を読む。こちらは元から問題にならない。）
 *
 *  --------------------------------------------------------------------------
 *  レジスタ番地・ビット位置の出典（**手で覚えた値は使っていない**）
 *  --------------------------------------------------------------------------
 *  すべて esp-idf v5.5.4 のヘッダを **bootloader サブビルドの実際の指定
 *  （`esp/boot/seam_p4/build/bootloader/compile_commands.json`）でプリプロセスして**
 *  得た（実測ログ: `build/p4-stage6/s6p/probe.out`）:
 *
 *    HP_SYS_CLKRST_SOC_CLK_CTRL0_REG      = (0x500C0000 + 0x26000) + 0x14 = 0x500E6014
 *    HP_SYS_CLKRST_REG_CORE1_CPU_CLK_EN   = BIT(4)
 *    HP_SYS_CLKRST_HP_RST_EN0_REG         = (0x500C0000 + 0x26000) + 0xc0 = 0x500E60C0
 *    HP_SYS_CLKRST_REG_RST_EN_CORE1_GLOBAL= BIT(8)
 *    PMU_CPU_SW_STALL_REG                 = (0x50110000 + 0x5000) + 0x200 = 0x50115200
 *    PMU_HPCORE1_SW_STALL_CODE            = bits [23:16]（_S=16 / _V=0xFF）
 *    HP_SYSTEM_CPU_CORESTALLED_ST_REG     = (0x500C0000 + 0x25000) + 0x64 = 0x500E5064
 *    HP_SYSTEM_REG_CORE1_CORESTALLED_ST   = BIT(1)
 *    ets_set_appcpu_boot_addr             = 0x4fc000a8
 *        （`esp-idf/components/esp_rom/esp32p4/ld/esp32p4.rom.ld:57`。seam は rom.ld を
 *          リンクしないので直値で呼ぶ。段5 の `ets_update_cpu_frequency`(0x4fc00044) と同じ作法）
 *
 *  前 2 つは `fmp3/arch/riscv_gcc/esp32p4/esp32p4.h:171-174`（`HP_CORE1_CLK_EN_REG` /
 *  `HP_CORE1_RST_REG`）と一致した。同ヘッダは当該定義に「要確認」と注記していたので、
 *  **本段でその注記の内容を裏取りしたことになる**（チップ層は pristine 監査対象なので
 *  ファイルは 1 バイトも触っていない。注記の解消は上流の仕事）。
 *
 *  stall/unstall のコード値 0x86 / 0xFF は
 *  `esp-idf/components/hal/esp32p4/include/hal/cpu_utility_ll.h:33-56`
 *  （`cpu_utility_ll_stall_cpu` / `cpu_utility_ll_unstall_cpu`）から。
 *
 *  --------------------------------------------------------------------------
 *  IDF 本体との順序の違い（記録しておく）
 *  --------------------------------------------------------------------------
 *  IDF の `start_other_core()`（`esp_system/port/cpu_start.c:318-326`）は
 *  **unstall → クロック/リセット → boot addr** の順で、`fmp_loader.c` は
 *  **クロック/リセット → boot addr → unstall** の順である。
 *  本ファイルは **`fmp_loader.c` の順**を採る（方式(a) の実績＝P4 実機で 52 件 PASS が
 *  その順で採られているため。PLAN の「実 IDF アプリと同じことをする」原則よりも
 *  「方式(a) の列を写す」という段6 の指示を優先した）。
 *
 *  なお `fmp_loader.c` の `REG_SET_BIT` / `REG_CLR_BIT` は無条件だが、IDF の
 *  `cpu_utility_ll_enable_clock_and_reset_app_cpu()` は `if (!bit)` / `if (bit)` で
 *  包んでいる。**この 2 つは意味が同じ**（既にセット済みのビットへの set は無変化）。
 *  ESP32-S3 側で同じ場所にガードが要ったのは、あちらが**リセットをパルス**
 *  （set→clear）するからで（`fmp3/target/esp32s3_devkitc_gcc/target_kernel_impl.c:340-352`）、
 *  P4 の「リセット許可ビットを落とす」操作は冪等である。よってガードは付けない。
 *
 *  --------------------------------------------------------------------------
 *  方式(a) への影響
 *  --------------------------------------------------------------------------
 *  無い。本ファイルは `A1_P4_SEAM=ON` かつ `A1_P4_SEAM_CORE1=ON` のときだけリンクされ、
 *  呼び出し側（`target_kernel_impl.c`）も `TOPPERS_SEAM_P4_CORE1` が定義されたときだけ
 *  コードを持つ。方式(a) では両方とも定義されない＝前処理で消える。
 */

#include <stdint.h>

/*  FMP3 の起動エントリ（start.S）。core1 もここへ入る（方式(a) と同じ飛び先）  */
extern void toppers_start(void);

/*
 *  ------------------------------------------------------------------
 *  core1 の飛び先（RAM 常駐トランポリン）
 *  ------------------------------------------------------------------
 *  段6 の実機切り分けで分かったこと（事実）:
 *    `ets_set_appcpu_boot_addr(&toppers_start)`（= flash IROM の 0x40000100）で
 *    core1 を起こすと、core1 は **Instruction access fault** で ROM のパニック
 *    ハンドラへ落ちる（JTAG 実測: mcause=0x30000001 / mepc=mtval=0xd9d3514a、
 *    mtvec は ROM のまま 0x4fc00b03 ＝ chip_initialize に到達していない）。
 *    このとき master は barrier_sync（startup.c:97）で待ち続ける。
 *  方式(a) との差は**飛び先のアドレス空間**である——方式(a) の ld
 *  （esp32p4_fmp.ld）は全 text を L2MEM(0x4ff00000 帯)に置くので、
 *  core1 の入口も RAM だった。seam は完全 XIP なので flash になる。
 *
 *  そこで **入口だけ RAM に置く**（方式(a) と同じ条件に揃える）。
 *  トランポリンは 2 命令＋任意の 1 文字出力で、`toppers_start` へ
 *  `la`+`jr`（auipc+jalr, ±2GB）で飛ぶ。素の `j` は ±1MB で届かない
 *  （RAM 0x4ff0_0000 と flash 0x4000_0000 は約 254MB 離れる。
 *   seam_p4_entry.S が core0 側で踏んだのと同じ制約）。
 *
 *  SEAM_P4_CORE1_MARK が定義されていれば、飛ぶ前に USB-Serial/JTAG へ
 *  1 文字出す（既定 'C'）。これが**「core1 が起きて RAM のコードを実行した」
 *  ことの、カーネルを一切通らない直接の証拠**になる。
 */
void seam_p4_core1_entry(void);

/*  レジスタ（出典は上のコメント）  */
#define HP_SOC_CLK_CTRL0        0x500E6014U
#define HP_CORE1_CPU_CLK_EN     (1U << 4)
#define HP_RST_EN0              0x500E60C0U
#define HP_RST_EN_CORE1_GLOBAL  (1U << 8)
#define PMU_CPU_SW_STALL        0x50115200U
#define PMU_HPCORE1_STALL_S     16U
#define PMU_HPCORE1_STALL_M     (0xFFU << PMU_HPCORE1_STALL_S)
#define PMU_STALL_CODE_STALL    0x86U
#define PMU_STALL_CODE_UNSTALL  0xFFU
#define HP_CORESTALLED_ST       0x500E5064U
#define HP_CORE1_CORESTALLED    (1U << 1)

/*  ROM API（seam は rom.ld をリンクしないので直値で呼ぶ）  */
#define ROM_ETS_SET_APPCPU_BOOT_ADDR    0x4FC000A8U

/*  USB-Serial/JTAG（seam_p4_entry.S / seam_p4_clk.c と同じ番地・同じ手順）  */
#define USJ_EP1             0x500D2000U
#define USJ_EP1_CONF        0x500D2004U
#define USJ_WR_DONE         0x1U
#define USJ_IN_DATA_FREE    0x2U

/*
 *  LP_AON 側（core1 の入口アドレスとリセット）
 *    LP_SYSTEM_REG_BOOT_ADDR_HP_CORE1_REG = DR_REG_LP_SYS_BASE(0x50110000) + 0x164
 *      （lp_system_reg.h:838。ROM の ets_set_appcpu_boot_addr は
 *        実測で `lui a5,0x50110 / sw a0,356(a5) / ret` ＝ **この 1 本を書くだけ**）
 *    LP_CLKRST_HPCPU_RESET_CTRL0_REG      = DR_REG_LP_CLKRST_BASE(0x50111000) + 0x14
 *      bit29 HPCORE1_SW_RESET（WT: 1 を書くとリセットが起きる。
 *      lp_clkrst_reg.h:370,467-473。IDF の cpu_utility_ll_reset_cpu(1) と同じ）
 */
#define LP_BOOT_ADDR_CORE1      0x50110164U
#define LP_HPCPU_RESET_CTRL0    0x50111014U
#define LP_HPCORE1_SW_RESET     (1U << 29)

/*  戻り値  */
#define P4CORE1_OK          0U
#define P4CORE1_NG_STALLED  1U   /* unstall を書いたのに CORESTALLED が下りない */

/*
 *  触る前のレジスタ値（診断用）。
 *  **ここでグローバルを使ってよい**——本関数は target_mprc_initialize() から
 *  呼ばれ、その時点で start.S が .bss クリアと .data コピーを済ませている
 *  （seam のエントリから呼ぶ seam_p4_clk.c とは条件が違う）。
 */
static uint32_t seam_p4_core1_pre_clk;
static uint32_t seam_p4_core1_pre_rst;
static uint32_t seam_p4_core1_pre_stall;
static uint32_t seam_p4_core1_pre_boot;
static uint32_t seam_p4_core1_pre_stst;

static uint32_t r32(uint32_t a)
{
    return *(volatile uint32_t *) a;
}

static void w32(uint32_t a, uint32_t v)
{
    *(volatile uint32_t *) a = v;
}

static void usj_putc(char c)
{
    uint32_t n = 100000U;

    while (((r32(USJ_EP1_CONF) & USJ_IN_DATA_FREE) == 0U) && (n != 0U)) {
        n--;
    }
    if (n == 0U) {
        return;                      /* ホストが受けていない: 捨てて進む */
    }
    w32(USJ_EP1, (uint32_t) (unsigned char) c);
    w32(USJ_EP1_CONF, USJ_WR_DONE);
}

static void usj_puts(const char *s)
{
    while (*s != '\0') {
        usj_putc(*s++);
    }
}

static void usj_putx(uint32_t v)
{
    int i;

    for (i = 28; i >= 0; i -= 4) {
        uint32_t d = (v >> (uint32_t) i) & 0xFU;
        usj_putc((char) ((d < 10U) ? ('0' + d) : ('a' + (d - 10U))));
    }
}

/*
 *  core1 を起こす。
 *
 *  呼ばれる文脈（`target_mprc_initialize()` から）:
 *    - master（hart0）のみ。
 *    - `.bss` クリア済み・`.data` コピー済み・`start_sync[]` はゼロ。
 *    - 割込みは禁止（`start.S:66-68` が `mstatus.MIE` を落としたまま）。
 *    - カーネル未起動（`sta_ker` の前）。
 *
 *  戻り値は診断用で、失敗しても起動は続ける（master 側は `start_sync` に MAGIC を
 *  書いて `sta_ker` へ進み、`barrier_sync` で core1 を待つ形になる＝**そこで止まる**。
 *  黙って 1 コアで走り続けることはない）。
 */
uint32_t
seam_p4_core1_start(void)
{
    uint32_t v;
    uint32_t n;

    /*
     *  (0) 触る前の値を控える（診断用）。
     *      **なぜ要るか**: 「core1 が我々より前に既に走り出していたか」は、
     *      boot addr レジスタ（LP_SYSTEM_REG_BOOT_ADDR_HP_CORE1_REG）と
     *      クロック／リセット／ストールの**書く前の値**でしか分からない。
     *      段6 の切り分けで決定的だった。
     */
    seam_p4_core1_pre_clk   = r32(HP_SOC_CLK_CTRL0);
    seam_p4_core1_pre_rst   = r32(HP_RST_EN0);
    seam_p4_core1_pre_stall = r32(PMU_CPU_SW_STALL);
    seam_p4_core1_pre_boot  = r32(LP_BOOT_ADDR_CORE1);
    seam_p4_core1_pre_stst  = r32(HP_CORESTALLED_ST);

    /*
     *  (1) **まず core1 を止める**（PMU ストールコード 0x86。
     *      cpu_utility_ll_stall_cpu と同値）。
     *
     *      これは方式(a) の列には無い。足した理由は**実機実測**である（段6）:
     *      FMP3 が制御を受け取った時点で core1 は既に
     *        clk=有効 / global reset=解除 / stall code=0xFF（＝走行中）
     *      であり（P4CORE1pre の実測）、**方式(a) の 4 手はどれも no-op** だった。
     *      さらに boot addr レジスタ（0x50110164）は LP_AON＝常時給電ドメインに
     *      あるので**ソフトリセットを跨いで前回の値が残る**。
     *      ⇒ core1 は bootloader が RAM を載せるより前に「前回の番地」へ飛び、
     *        Instruction access fault で ROM のパニックへ落ちていた
     *        （mepc=mtval=前回値または残骸）。
     *      方式(a) でこれが問題にならなかったのは、IDF アプリの core1 入口
     *      call_start_cpu1 が先頭で ets_set_appcpu_boot_addr(0) を呼んで
     *      **レジスタを毎回ゼロに戻していた**ため（cpu_start.c:250）。
     *      FMP3 にはそれが無かった。
     *      ⇒ ここで「止める → ゼロにする → リセット → 番地を書く → 動かす」に
     *        順序を組み替える。トランポリン側でも 0 に戻す（IDF と同じ後始末）。
     */
    v = r32(PMU_CPU_SW_STALL);
    v = (v & ~PMU_HPCORE1_STALL_M) | (PMU_STALL_CODE_STALL << PMU_HPCORE1_STALL_S);
    w32(PMU_CPU_SW_STALL, v);

    /*
     *  (2) 入口アドレスをゼロに戻す。ROM の appcpu スタブは
     *      「このレジスタが 0 でない値になるまで待って、その番地へ飛ぶ」
     *      という作りである（IDF が core1 側で 0 を書き戻していることが根拠）。
     *      0 にしておけば、万一この先で core1 が走り出しても**待つ**。
     */
    w32(LP_BOOT_ADDR_CORE1, 0U);

    /*  (3) core1 のクロックを許可（fmp_loader.c:38）  */
    w32(HP_SOC_CLK_CTRL0, r32(HP_SOC_CLK_CTRL0) | HP_CORE1_CPU_CLK_EN);

    /*  (4) core1 のグローバルリセットを解除（fmp_loader.c:39）  */
    w32(HP_RST_EN0, r32(HP_RST_EN0) & ~HP_RST_EN_CORE1_GLOBAL);

    /*
     *  (5) core1 をソフトリセットして ROM のリセットベクタからやり直させる
     *      （cpu_utility_ll_reset_cpu(1) と同値。bit29 は WT＝1 を書くと発火）。
     *      (1) で止めた「走行中の core1」を、我々の番地を読む状態へ戻すため。
     */
    w32(LP_HPCPU_RESET_CTRL0, r32(LP_HPCPU_RESET_CTRL0) | LP_HPCORE1_SW_RESET);

    /*
     *  (6) core1 のブートアドレスを公開（fmp_loader.c:40）。
     *      飛び先は **RAM 常駐トランポリン**で、そこから `toppers_start` へ飛ぶ。
     *      seam のエントリ（seam_p4_entry）ではない——あちらは CPU クロックの
     *      昇圧と起動印字を行う「core0 用の前段」で、core1 が再実行してはならない。
     *      RAM に置く理由は seam_p4_core1_entry の宣言の上のコメント（実機実測）。
     */
    ((void (*)(uint32_t)) ROM_ETS_SET_APPCPU_BOOT_ADDR)((uint32_t)(uintptr_t) &seam_p4_core1_entry);

    /*
     *  (7) unstall（fmp_loader.c:41 の esp_cpu_unstall(1)）。
     *      実体は cpu_utility_ll_unstall_cpu(1)＝ストールコードへ 0xFF を書き、
     *      CORESTALLED 状態ビットが下りるのを待つ。
     *      IDF は無限に待つが、ここでは上限を付けて rc で返す
     *      （起動前の段で永久ループを作らない）。
     */
    v = r32(PMU_CPU_SW_STALL);
    v = (v & ~PMU_HPCORE1_STALL_M) | (PMU_STALL_CODE_UNSTALL << PMU_HPCORE1_STALL_S);
    w32(PMU_CPU_SW_STALL, v);

    n = 1000000U;
    while (((r32(HP_CORESTALLED_ST) & HP_CORE1_CORESTALLED) != 0U) && (n != 0U)) {
        n--;
    }

    return (n == 0U) ? P4CORE1_NG_STALLED : P4CORE1_OK;
}

/*
 *  診断出力（AC-S6-1c の証拠。`P4CLK …` と同じ体裁）。
 *
 *  **これは構成の事実であって「core1 がカーネルを走らせた」ことの証明ではない**
 *  ——その証明はバナーの `Processor 2 start.` と `tick prc2 #…` である。
 */
void
seam_p4_core1_report(uint32_t rc)
{
    usj_puts("\r\nP4CORE1pre clk=");
    usj_putx(seam_p4_core1_pre_clk);
    usj_puts(" rst=");
    usj_putx(seam_p4_core1_pre_rst);
    usj_puts(" stall=");
    usj_putx(seam_p4_core1_pre_stall);
    usj_puts(" bootaddr=");
    usj_putx(seam_p4_core1_pre_boot);
    usj_puts(" stalled_st=");
    usj_putx(seam_p4_core1_pre_stst);
    usj_puts("\r\nP4CORE1 rc=");
    usj_putx(rc);
    usj_puts(" clk_ctrl0=");
    usj_putx(r32(HP_SOC_CLK_CTRL0));
    usj_puts(" rst_en0=");
    usj_putx(r32(HP_RST_EN0));
    usj_puts(" sw_stall=");
    usj_putx(r32(PMU_CPU_SW_STALL));
    usj_puts(" stalled_st=");
    usj_putx(r32(HP_CORESTALLED_ST));
    usj_puts(" boot=");
    usj_putx((uint32_t)(uintptr_t) &seam_p4_core1_entry);
    usj_puts(" tstart=");
    usj_putx((uint32_t)(uintptr_t) &toppers_start);
    usj_puts("\r\n");
}
