/*
 *  seam-P4: CPU クロックを 90MHz -> 360MHz へ上げる（FMP3 側で行う昇圧）
 *  ==========================================================================
 *  ESP32-P4 統合 段5（2026-08-15）。判定基準: .steering/20260815-p4-stage5/AC.md AC-1
 *  ユーザー決定 (a)＝「MTIMER_FREQ_MHZ を実クロックへ合わせる」のではなく
 *  「実クロックを 360MHz へ上げる」。
 *
 *  --------------------------------------------------------------------------
 *  なぜ要るか（段4 の実測）
 *  --------------------------------------------------------------------------
 *  seam では FMP3 が bootloader から直接制御を受け取るので、IDF アプリの
 *  esp_clk_init() に相当する昇圧が**誰も実行しない**。その結果 CPU は
 *  bootloader が残した 90MHz のままで、mtime が CPU コアクロックで歩進する
 *  P4 では `MTIMER_FREQ_MHZ=360`（esp32p4.h:31）との食い違いがそのまま
 *  「時間が 4 倍遅い」になる（段4 §5: 500ms 指定が実測 約1.94s）。
 *
 *  --------------------------------------------------------------------------
 *  何をするか（一次資料に基づく最小手順）
 *  --------------------------------------------------------------------------
 *  **90MHz は「CPLL=360MHz を cpu_div=4 したもの」である**——
 *  esp-idf/components/esp_hw_support/port/esp32p4/rtc_clk.c:339-343
 *  （CONFIG_ESP32P4_SELECTS_REV_LESS_V3 の枝。seam_p4/sdkconfig で有効）:
 *      case 90:  source = SOC_CPU_CLK_SRC_CPLL;
 *                source_freq_mhz = CLK_LL_PLL_360M_FREQ_MHZ;  (=360)
 *                divider.integer = 4;
 *  ⇒ **CPLL は既に enable 済み・360MHz へ校正済み**であり、昇圧に必要なのは
 *    **分周器 4 本の書き換えだけ**である（regi2c も PLL 再校正も要らない）。
 *
 *  分周器の目標値は rtc_clk.c:224-262 の表そのもの:
 *      CPLL 360 -div1-> CPU 360 -div2-> MEM 180 -div1-> SYS 180 -div2-> APB 90
 *  現在（90MHz）は cpu_div=4 / mem_div=1 / sys_div=1 / apb_div=1。
 *  書き換えの**順序**も rtc_clk.c:292-300 の昇圧経路と同一にする
 *  （APB -> SYS -> MEM -> CPU、各段で bus_update）。順序を誤ると中間状態で
 *  APB/MEM がタイミング要件を満たさない旨が同ファイル :285-291 に明記されている。
 *
 *  レジスタ（すべて esp-idf のヘッダから機械的に導いた。手で覚えた値ではない）:
 *    HP_SYS_CLKRST_ROOT_CLK_CTRL0_REG = DR_REG_HPPERIPH1_BASE(0x500C0000)
 *                                       + 0x26000 + 0x4 = 0x500E6004
 *      bit4      SOC_CLK_DIV_UPDATE            (hp_sys_clkrst_reg.h:15-18)
 *      [12:5]    CPU_CLK_DIV_NUM   (=divider-1) (:22-25)
 *      [20:13]   CPU_CLK_DIV_NUMERATOR          (:29-32)
 *      [28:21]   CPU_CLK_DIV_DENOMINATOR        (:36-39)
 *    HP_SYS_CLKRST_ROOT_CLK_CTRL1_REG = 0x500E6008
 *      [7:0]     MEM_CLK_DIV_NUM                (:48-51)
 *      [31:24]   SYS_CLK_DIV_NUM                (:69-72)
 *    HP_SYS_CLKRST_ROOT_CLK_CTRL2_REG = 0x500E600C
 *      [23:16]   APB_CLK_DIV_NUM                (:95-98)
 *    LP_CLKRST_HP_CLK_CTRL_REG = DR_REG_LPAON_BASE(0x50110000)+0x1000+0x40
 *                              = 0x50111040
 *      [1:0]     HP_ROOT_CLK_SRC_SEL  0=XTAL 1=CPLL 2=RC_FAST  (lp_clkrst_reg.h:663-669)
 *
 *  --------------------------------------------------------------------------
 *  なぜ RAM 常駐（.iram_text）なのか
 *  --------------------------------------------------------------------------
 *  昇圧の途中で MEM_CLK（＝キャッシュのクロック）が 90 -> 45 -> 180MHz と動く。
 *  この区間に flash から命令をフェッチする（＝キャッシュミス）状況を作らないため、
 *  切替を行うコードは **RAM 常駐**にする。ESP-IDF も既定で同じ扱いである
 *  （`CONFIG_RTC_CLK_FUNC_IN_IRAM=y`・esp_hw_support/linker.lf:17-18。
 *    実測: esp/boot/seam_p4/sdkconfig にも同 y が入っている）。
 *  **なお「XIP 実行中の分周器切替が安全である」という一次的な保証は
 *    esp-idf からは得られない**（IDF は常に IRAM 実行なので、その問いに
 *    答える記述が無い）。だから同じ条件（RAM 常駐・割込み禁止）に揃える。
 *
 *  --------------------------------------------------------------------------
 *  呼び出し位置
 *  --------------------------------------------------------------------------
 *  seam のエントリ（esp/boot/seam_p4_entry.S）から、`toppers_start` へ飛ぶ前に
 *  呼ぶ。理由:
 *    - カーネルが動き出す前に決着させれば、HRT/タイマの初期化以降はすべて
 *      360MHz 前提の値（MTIMER_FREQ_MHZ）と実クロックが一致する。
 *    - **svn から取り込んだ target 層（target_kernel_impl.c の
 *      hardware_init_hook 等）を 1 バイトも変えずに済む**。
 *  この時点では .data/.bss がまだ初期化されていない（start.S がこの後で行う）ので、
 *  **本ファイルはグローバル変数を一切使わない**。文字列リテラル（.rodata）は
 *  昇圧が終わった後にしか読まないので flash 上でよい。
 *
 *  --------------------------------------------------------------------------
 *  方式(a) への影響
 *  --------------------------------------------------------------------------
 *  無い。本ファイルは `A1_P4_SEAM=ON` のときだけリンクされる
 *  （cmake/a1_p4_stage1.cmake）。方式(a)（段2/段3）は IDF アプリが
 *  esp_clk_init() で 360MHz にした後に FMP3 を呼ぶので、そもそも昇圧は不要である。
 */

#include <stdint.h>

#define IRAM_TEXT   __attribute__((section(".iram_text"), noinline))

/*  レジスタ  */
#define HP_ROOT_CLK_CTRL0   0x500E6004U
#define HP_ROOT_CLK_CTRL1   0x500E6008U
#define HP_ROOT_CLK_CTRL2   0x500E600CU
#define LP_HP_CLK_CTRL      0x50111040U

#define SOC_CLK_DIV_UPDATE  (1U << 4)

/*  USB-Serial/JTAG（seam_p4_entry.S と同じ番地・同じ手順）  */
#define USJ_EP1             0x500D2000U
#define USJ_EP1_CONF        0x500D2004U
#define USJ_WR_DONE         0x1U
#define USJ_IN_DATA_FREE    0x2U

/*  戻り値（呼び手が印字するだけ。エラーでも起動は続ける）  */
#define P4CLK_OK            0U
#define P4CLK_SKIP_SRC      1U   /* HP_ROOT の源が CPLL でない */
#define P4CLK_SKIP_DIV      2U   /* CPU 分周が 4（＝90MHz）でない */
#define P4CLK_NG_VERIFY     3U   /* 書いたのに読み返しが一致しない */

static IRAM_TEXT uint32_t r32(uint32_t a)
{
    return *(volatile uint32_t *) a;
}

static IRAM_TEXT void w32(uint32_t a, uint32_t v)
{
    *(volatile uint32_t *) a = v;
}

static IRAM_TEXT void fld_w(uint32_t a, uint32_t lsb, uint32_t width, uint32_t v)
{
    uint32_t mask = ((1U << width) - 1U) << lsb;
    w32(a, (r32(a) & ~mask) | ((v << lsb) & mask));
}

static IRAM_TEXT uint32_t fld_r(uint32_t a, uint32_t lsb, uint32_t width)
{
    return (r32(a) >> lsb) & ((1U << width) - 1U);
}

/*
 *  分周器の更新を確定させる（clk_tree_ll.h:523-527 clk_ll_bus_update と同一）。
 *  上限つきで待つ（永久ループにしない）。
 */
static IRAM_TEXT void bus_update(void)
{
    uint32_t n = 100000U;

    w32(HP_ROOT_CLK_CTRL0, r32(HP_ROOT_CLK_CTRL0) | SOC_CLK_DIV_UPDATE);
    while (((r32(HP_ROOT_CLK_CTRL0) & SOC_CLK_DIV_UPDATE) != 0U) && (n != 0U)) {
        n--;
    }
}

/*
 *  昇圧本体。**割込みは禁止のまま呼ぶこと**（エントリからの呼出しは
 *  カーネル初期化前で、まだ mtvec すら設定されていない＝割込みは来ない）。
 */
IRAM_TEXT uint32_t seam_p4_clk_boost_360(void)
{
    uint32_t i;

    /*  (1) 前提の確認: 源が CPLL であること（＝CPLL は enable 済み・360MHz 校正済み）  */
    if ((r32(LP_HP_CLK_CTRL) & 0x3U) != 1U) {
        return P4CLK_SKIP_SRC;
    }
    /*  (2) 前提の確認: CPU 分周が 4（＝bootloader が残した 90MHz の状態）  */
    if (fld_r(HP_ROOT_CLK_CTRL0, 5U, 8U) != (4U - 1U)) {
        return P4CLK_SKIP_DIV;
    }

    /*
     *  (3) 直前の USJ 出力が USB へ抜けるのを待つ。APB は昇圧の途中で
     *      90 -> 45 -> 22.5MHz を通過するので、送信中だと化けうる
     *      （IDF も昇圧前に esp_rom_output_tx_wait_idle() を呼ぶ。clk.c:144-148）。
     *      ここでは USJ の空き待ち＋固定の空回しで代える（約 1ms @90MHz）。
     */
    for (i = 0U; i < 100000U; i++) {
        if ((r32(USJ_EP1_CONF) & USJ_IN_DATA_FREE) != 0U) {
            break;
        }
    }
    for (i = 0U; i < 30000U; i++) {
        __asm__ volatile ("nop" ::: "memory");
    }

    /*
     *  (4) 昇圧（rtc_clk.c:292-300 の Upscaling 経路と同じ順序・同じ値）
     *      目標: cpu_div=1 / mem_div=2 / sys_div=1 / apb_div=2
     */
    fld_w(HP_ROOT_CLK_CTRL2, 16U, 8U, 2U - 1U);   /* APB div 2 */
    bus_update();
    fld_w(HP_ROOT_CLK_CTRL1, 24U, 8U, 1U - 1U);   /* SYS div 1（不変だが順序を守る） */
    bus_update();
    fld_w(HP_ROOT_CLK_CTRL1,  0U, 8U, 2U - 1U);   /* MEM div 2 */
    bus_update();
    fld_w(HP_ROOT_CLK_CTRL0,  5U, 8U, 1U - 1U);   /* CPU div 1 */
    fld_w(HP_ROOT_CLK_CTRL0, 13U, 8U, 0U);        /* numerator   = 0 */
    fld_w(HP_ROOT_CLK_CTRL0, 21U, 8U, 0U);        /* denominator = 0 */
    bus_update();

    /*
     *  (5) 読み返して確かめる（書いたつもりで書けていない、を通さない）。
     *      **ハードウェアは達成できない分周を勝手に補正することがあり、
     *      それはレジスタに反映されない**（rtc_clk.c:255-259 の警告）。
     *      よってこの読み返しは「書込みが着いたこと」までしか言えない。
     *      実クロックの証拠は実測（tick 周期）で取る。
     */
    if ((fld_r(HP_ROOT_CLK_CTRL0, 5U, 8U) != 0U)
            || (fld_r(HP_ROOT_CLK_CTRL1, 0U, 8U) != 1U)
            || (fld_r(HP_ROOT_CLK_CTRL1, 24U, 8U) != 0U)
            || (fld_r(HP_ROOT_CLK_CTRL2, 16U, 8U) != 1U)) {
        return P4CLK_NG_VERIFY;
    }

    /*
     *  (6) ROM の g_ticks_per_us を更新する（ets_update_cpu_frequency）。
     *      FMP3 自身は ROM の遅延関数を使っていない（grep で 0 件）が、
     *      IDF が同じ場所で必ず行っている更新であり、ROM 側の時間が
     *      4 倍ずれたまま残るのを避ける。番地は esp-idf の
     *      components/esp_rom/esp32p4/ld/esp32p4.rom.ld:32
     *        ets_update_cpu_frequency = 0x4fc00044;
     *      から取った（seam は rom.ld をリンクしないため直値で呼ぶ）。
     *      書込み先は ROM 予約 RAM（0x4FF2CBD0 以降）で、本 ld はそこを
     *      1 バイトも使っていない（esp32p4_xip.ld の RAM 上限がまさにその値）。
     */
    ((void (*)(uint32_t)) 0x4FC00044U)(360U);

    return P4CLK_OK;
}

/*
 *  ------------------------------------------------------------------
 *  診断出力（昇圧の**後**に呼ぶ。flash 上の .rodata を読んでよい）
 *  ------------------------------------------------------------------
 *  AC-1d: 「昇圧が実際に起きた根拠をレジスタから読む」。
 *  ただしこれは**構成の事実**であって、実クロックの測定ではない
 *  （測定は scripts/p4_tick_measure.py が壁時計で行う）。
 */
static IRAM_TEXT void usj_putc(char c)
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

static IRAM_TEXT void usj_puts(const char *s)
{
    while (*s != '\0') {
        usj_putc(*s++);
    }
}

static IRAM_TEXT void usj_putu(uint32_t v)
{
    char buf[12];
    int  i = 0;

    if (v == 0U) {
        usj_putc('0');
        return;
    }
    while ((v != 0U) && (i < 11)) {
        buf[i++] = (char) ('0' + (v % 10U));
        v /= 10U;
    }
    while (i > 0) {
        usj_putc(buf[--i]);
    }
}

void seam_p4_clk_report(uint32_t rc)
{
    uint32_t src  = r32(LP_HP_CLK_CTRL) & 0x3U;
    uint32_t cpud = fld_r(HP_ROOT_CLK_CTRL0, 5U, 8U) + 1U;
    uint32_t memd = fld_r(HP_ROOT_CLK_CTRL1, 0U, 8U) + 1U;
    uint32_t sysd = fld_r(HP_ROOT_CLK_CTRL1, 24U, 8U) + 1U;
    uint32_t apbd = fld_r(HP_ROOT_CLK_CTRL2, 16U, 8U) + 1U;
    /*  src=CPLL(1) のとき HP_ROOT は 360MHz（rtc_clk.c:339-343）  */
    uint32_t cpu  = (src == 1U) ? (360U / cpud) : 0U;

    usj_puts("\r\nP4CLK rc=");
    usj_putu(rc);
    usj_puts(" src=");
    usj_putu(src);
    usj_puts(" cpu_div=");
    usj_putu(cpud);
    usj_puts(" mem_div=");
    usj_putu(memd);
    usj_puts(" sys_div=");
    usj_putu(sysd);
    usj_puts(" apb_div=");
    usj_putu(apbd);
    usj_puts(" cpu=");
    usj_putu(cpu);
    usj_puts("MHz mem=");
    usj_putu((src == 1U) ? (cpu / memd) : 0U);
    usj_puts("MHz apb=");
    usj_putu((src == 1U) ? (cpu / memd / sysd / apbd) : 0U);
    usj_puts("MHz\r\n");
}
