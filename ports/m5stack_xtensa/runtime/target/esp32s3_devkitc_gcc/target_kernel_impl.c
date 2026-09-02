/*
 *  ターゲット依存モジュール（ESP32-S3-DevKitC-1 / FMP3 用）
 *
 *  本作業単位のスコープ：
 *  ・hardware_init_hook()：本作業単位では何もしない（QEMU/ROMが既定で
 *    有効化した状態のUART0をそのまま使う。chip_serial.c参照）。実機
 *    ブリングアップ時にクロック・GPIO初期化の要否を検証する。
 *  ・target_initialize()：core_initialize()の呼び出しのみ。SIOポート
 *    枠組み（sio_initialize/sio_opn_por）は本作業単位では未実装のため
 *    呼ばない（chip_serial.cのコメント参照）。
 *  ・target_mprc_initialize()：単一コアのみ対象のため空（SMP対応の
 *    別作業単位でcross-core interrupt起動シーケンスを実装する）。
 */
#include "kernel_impl.h"
#include "target_syssvc.h"
#include "chip_rom_libc.h"
#include "target_timer.h"		/* TOPPERS_S3_CPU_FREQ_MHZ */
#include <sil.h>

/*
 *  起動時のハードウェア初期化処理
 *
 *  start.S から，BSS/DATA の初期化に先立って呼び出される。
 */
void
hardware_init_hook(void)
{
}

/*
 *  CPUクロックのPLL昇圧（ESP32-S3 / Xtensa LX7、seam起動専用）
 *
 *  ■★なぜ esp_clk_init() をそのまま呼ばないか
 *    esp-idf/components/esp_system/port/soc/esp32s3/clk.c:78 の esp_clk_init() は
 *    CPU周波数設定の他に以下を行う「システム初期化一式」であり、本ポートから
 *    そのまま呼ぶことはできない：
 *     (1) select_rtc_slow_clk()：RTC低速クロックの選択と**校正**（rtc_clk_cal で
 *         タイマを回して実測する）。CONFIG_RTC_CLK_SRC_* に依存。
 *     (2) RTC WDT の再設定（CONFIG_BOOTLOADER_WDT_ENABLE / wdt_hal_* に依存）。
 *     (3) ★致命的：末尾(clk.c:139)で
 *           esp_cpu_set_cycle_count(esp_cpu_get_cycle_count() * new/old)
 *         と **CCOUNTを書き換える**。本ポートのHRTはCCOUNTの単調増加を前提に
 *         64bit累積器(_kernel_hrt_acc_cycles)を回しており、CCOUNTを飛ばされると
 *         時刻が壊れる（32bit差分が巨大値になり約2^32サイクル分ジャンプする）。
 *     (4) CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 等の sdkconfig 由来マクロに依存するが、
 *         本ポートは esp_hw_support の rtc_clk.c 等をビルド対象に含めていない
 *         （blobとヘッダのみを供給する構成）。
 *    ＝ esp_clk_init() は「FreeRTOS依存」ではないが、**CCOUNT書き換えとRTC低速
 *      クロック校正という余計な副作用**があり、必要なのはそのごく一部だけ。
 *    → **必要最小限のPLL設定を自前で行う**。本体は ESP-IDF v5.5 の
 *      esp-idf/components/esp_hw_support/port/esp32s3/rtc_clk.c の
 *      **rtc_clk_cpu_freq_to_pll_mhz()**（rtc_clk_cpu_freq_set_config() が
 *      source==PLL のときに呼ぶ実体）であり、それを静的レジスタアドレスと
 *      ROM関数で再現したのが本関数である。
 *
 *  ■なぜ seam では BBPLL の起動が要らないか（★これが実装を単純・安全にする）
 *    ESP32-S3では 80/160/240MHz は**すべて BBPLL 480M からの分周**（/6, /3, /2）で
 *    作られる（rtc_clk.c:250-292 rtc_clk_cpu_freq_mhz_to_config()）。seam起動では
 *    IDF 2nd-stage bootloader が既に CPU=80MHz(PLL 480M/6) にしているので、
 *    **BBPLLは既に480Mで回っており、CPUクロック源も既にPLL**である。
 *    実際 rtc_clk_cpu_freq_set_config()(rtc_clk.c:305-310) も
 *        if (old_cpu_clk_src != SOC_CPU_CLK_SRC_PLL) { bbpll_enable; bbpll_configure; }
 *    と、既にPLL上なら BBPLL の起動・再構成を**スキップ**する。
 *    → 本関数も rtc_clk_cpu_freq_to_pll_mhz() 相当だけを行い、BBPLL には触らない。
 *    ★これは安全性に直結する：esp/wifi/hal_src/periph_ctrl.c の esp_wifi_clock_init_pll()
 *      は先頭で esp_bbpll_enable_480m() を呼ぶが、**この早期コンテキスト
 *      （software_init_hook）からのその呼出しはクラッシュする**ことが判明しており
 *      （リブートループ、RTC_SLOW_MEMマーカー0x01で停止）。本関数は**BBPLLに触らない**
 *      ので、その地雷を踏まずに software_init_hook から実行できる。
 *      同じ理由で、LX6版(target/esp32_devkitc_gcc/target_kernel_impl.c)が必要とした
 *      「BBPLL再構成前にXTALへ退避する」処理も**S3 seamでは不要**である
 *      （LX6はDirect BootでBBPLL自体を起こす必要があり、320M→480Mの再構成を
 *        伴うためCPUをXTALへ逃がす必要があった。S3 seamは480Mのまま分周比だけ変える）。
 *
 *  ■APB / UART について
 *    PLL選択時のAPBは 80/160/240MHz のどれでも **80MHz固定**（rtc_clk.c:237
 *    rtc_clk_apb_freq_update(80*MHZ)）。seamでは bootloader が既にAPB=80MHzで
 *    UART0を設定済みであり、本昇圧でAPBは変化しない。
 *    → LX6版で必要だった UART0_CLKDIV の×2再設定・TXドレインは**S3 seamでは不要**
 *      （LX6のDirect BootはAPB 40→80MHzと倍増するため必要だった）。
 *
 *  ■★両コアに効くか
 *    効く。CPUクロックの選択レジスタ SYSTEM_CPU_PER_CONF / SYSTEM_SYSCLK_CONF は
 *    SYSTEMペリフェラル（0x600C0000系）の**チップ共通レジスタ**であり、コアごとの
 *    複製ではない。両コアは同一の CPU_CLK で駆動される（だからこそ
 *    target_timer.c の F1 同期が「両コアのCCOUNT差は定数でドリフトしない」と
 *    言える）。加えて本関数は software_init_hook＝**CPU1を解放する
 *    target_mprc_initialize() より前**に走るため、CPU1は最初から240MHzで動き出す。
 *
 *  ■呼び出し位置（★実ブートパスを追って決定）
 *    start.S → hardware_init_hook → BSS/DATA初期化 → **software_init_hook（本関数）**
 *    → target_mprc_initialize（CPU1解放）→ sta_ker → … → barrier_sync(4)
 *    → target_hrt_initialize（F1同期・tick開始）→ … → start_dispatch
 *    software_init_hook は start.S から**マスタコア（CPU0）のみ**が、BSS/DATA初期化後・
 *    sta_ker前に呼ぶ（スレーブはstart.Sのスレーブ分岐でスキップ）。すなわち
 *      ・「CPU0で1回だけ」という要件を自然に満たす
 *      ・**tick/HRTが走り出す前**である（★必須。tick開始後に切替えると、その瞬間
 *        までのCCOUNTは旧クロック(80MHz)で進んだのに TCYC_PER_HRT=240 で割ることに
 *        なり時刻がずれる。ESP-IDF自身も bootloader が app 実行前に昇圧する順序で
 *        あり整合する）
 *      ・CPU1解放前である（上記「両コアに効くか」）
 *    LX6版が同じ理由で software_init_hook を選んでいるのと同一の設計。
 */
#if defined(TOPPERS_BOOT_SEAM) && TOPPERS_S3_CPU_FREQ_MHZ != 40

/* ESP32-S3 レジスタ（ESP-IDF soc/system_reg.h, soc/rtc_cntl_reg.h, reg_base.h） */
#define S3_SYSTEM_CPU_PER_CONF_REG  0x600C0010U  /* DR_REG_SYSTEM_BASE+0x10 */
#define S3_SYSTEM_SYSCLK_CONF_REG   0x600C0060U  /* DR_REG_SYSTEM_BASE+0x60 */
#define S3_RTC_CNTL_DATE_REG        0x600081FCU  /* DR_REG_RTCCNTL_BASE+0x1FC */
#define S3_SYSTEM_PLL_FREQ_SEL_M    (1U << 2)    /* SYSTEM_PLL_FREQ_SEL_S=2, 1=480M */
#define S3_SYSTEM_CPUPERIOD_SEL_M   0x3U         /* _S=0, _V=0x3 */
#define S3_SYSTEM_PRE_DIV_CNT_M     0x3FFU       /* _S=0, _V=0x3FF */
#define S3_SYSTEM_SOC_CLK_SEL_S     10           /* _V=0x3, 1=PLL */
#define S3_SYSTEM_SOC_CLK_SEL_M     (0x3U << S3_SYSTEM_SOC_CLK_SEL_S)
#define S3_RTC_CNTL_SLAVE_PD_S      13           /* _V=0x3F */
#define S3_RTC_CNTL_SLAVE_PD_M      (0x3FU << S3_RTC_CNTL_SLAVE_PD_S)

/* regi2c I2C_DIG_REG ブロック（soc/regi2c_dig_reg.h） */
#define S3_I2C_DIG_REG              0x6D
#define S3_I2C_DIG_REG_HOSTID       1
#define S3_I2C_DIG_REG_EXT_RTC_DREG 4   /* MSB=4, LSB=0 */
#define S3_I2C_DIG_REG_EXT_DIG_DREG 6   /* MSB=4, LSB=0 */

/* ROMシンボル（esp/ld/esp32s3.rom.ld で解決） */
extern void rom_i2c_writeReg_Mask(uint8_t block, uint8_t host_id, uint8_t reg_add,
                                  uint8_t msb, uint8_t lsb, uint8_t data);
extern void ets_update_cpu_frequency(uint32_t mhz);
extern void ets_delay_us(uint32_t us);

static void
esp32s3_cpu_clock_boost_pll(void)
{
	uint32_t v;

#if TOPPERS_S3_CPU_FREQ_MHZ == 240
	/*
	 * 240MHz時のみ：クロック切替の前にレギュレータ電圧を引き上げる
	 * （rtc_clk.c:221-231 rtc_clk_cpu_freq_to_pll_mhz() の
	 *  「cpu_freq_mhz > cur_config.freq_mhz かつ ==240」分岐の再現。
	 *  昇圧(80→240)なので必ずこの分岐に入る）。
	 *   dbias = pvt-dig + 3 相当。値28は rtc_init.c:41-42 の
	 *   g_dig_dbias_pvt_240m / g_rtc_dbias_pvt_240m の**初期値**である。
	 *   ★注記：stock ESP-IDF では rtc_init() が efuse から読んだPVT情報で
	 *     これらを較正し直す（rtc_init.c:407-409）が、本ポートは rtc_init() を
	 *     実行しない（bootloaderが自分のコピーに対して行うのみで、app側の
	 *     変数には伝わらない）。よって較正前の既定値28を使う。これは
	 *     ESP-IDF が efuse に PVT 情報を持たないチップで採る値と同じであり、
	 *     安全側（＝高め）の電圧である。
	 */
	rom_i2c_writeReg_Mask(S3_I2C_DIG_REG, S3_I2C_DIG_REG_HOSTID,
	                      S3_I2C_DIG_REG_EXT_RTC_DREG, 4, 0, 28);
	rom_i2c_writeReg_Mask(S3_I2C_DIG_REG, S3_I2C_DIG_REG_HOSTID,
	                      S3_I2C_DIG_REG_EXT_DIG_DREG, 4, 0, 28);
	ets_delay_us(40u);
#endif /* TOPPERS_S3_CPU_FREQ_MHZ == 240 */

	/*
	 * LDOスレーブ数の設定（rtc_clk.c:230
	 *   REG_SET_FIELD(RTC_CNTL_DATE_REG, RTC_CNTL_SLAVE_PD, DEFAULT_LDO_SLAVE >> pd_slave)）。
	 * DEFAULT_LDO_SLAVE=0x7、pd_slave = cpu_freq_mhz/80 なので
	 *   80MHz→0x7>>1=3 / 160MHz→0x7>>2=1 / 240MHz→0x7>>3=0。
	 * SLAVE_PDは「パワーダウンするスレーブ」の指定であり、0で6個全開＝
	 * 高クロックほど多くのスレーブを開ける（＝周波数上昇時の電圧降下を抑える）。
	 */
	v = sil_rew_mem((const uint32_t *) S3_RTC_CNTL_DATE_REG);
	v = (v & ~S3_RTC_CNTL_SLAVE_PD_M)
	    | ((0x7U >> (TOPPERS_S3_CPU_FREQ_MHZ / 80)) << S3_RTC_CNTL_SLAVE_PD_S);
	sil_wrw_mem((uint32_t *) S3_RTC_CNTL_DATE_REG, v);

	/*
	 * clk_ll_cpu_set_freq_mhz_from_pll()（hal/esp32s3/include/hal/clk_tree_ll.h:436）：
	 *   SYSTEM_CPUPERIOD_SEL = 0/1/2 → 80/160/240MHz。
	 * 併せて PLL_FREQ_SEL(bit2)=1（BBPLL 480M選択）を明示する（seamでは
	 * bootloaderが既に1にしているが、冪等に保つ）。
	 */
	v = sil_rew_mem((const uint32_t *) S3_SYSTEM_CPU_PER_CONF_REG);
	v = (v | S3_SYSTEM_PLL_FREQ_SEL_M) & ~S3_SYSTEM_CPUPERIOD_SEL_M;
	v |= (uint32_t)(TOPPERS_S3_CPU_FREQ_MHZ / 80 - 1);
	sil_wrw_mem((uint32_t *) S3_SYSTEM_CPU_PER_CONF_REG, v);

	/*
	 * clk_ll_cpu_set_divider(1)（PRE_DIV_CNT=0）＋
	 * clk_ll_cpu_set_src(SOC_CPU_CLK_SRC_PLL)（SOC_CLK_SEL=1）。
	 * この瞬間に CPU=TOPPERS_S3_CPU_FREQ_MHZ、APB=80MHz。
	 * PRE_DIV_CNTを0にしないとPLL分周が狂い不安定クロックでクラッシュする
	 * （periph_ctrl.c の同等処理のコメント参照。stock値0x000a8400に一致させる）。
	 */
	v = sil_rew_mem((const uint32_t *) S3_SYSTEM_SYSCLK_CONF_REG);
	v = (v & ~S3_SYSTEM_PRE_DIV_CNT_M & ~S3_SYSTEM_SOC_CLK_SEL_M)
	    | (1U << S3_SYSTEM_SOC_CLK_SEL_S);
	sil_wrw_mem((uint32_t *) S3_SYSTEM_SYSCLK_CONF_REG, v);

	/*
	 * ROMのg_ticks_per_usを新CPU周波数へ更新する（rtc_clk.c:238
	 * esp_rom_set_cpu_ticks_per_us() 相当）。以降の ets_delay_us() や
	 * ROM内部の遅延の精度がこれに依存する。
	 */
	ets_update_cpu_frequency((uint32_t) TOPPERS_S3_CPU_FREQ_MHZ);
}

#endif /* TOPPERS_BOOT_SEAM && TOPPERS_S3_CPU_FREQ_MHZ != 40 */

/*
 *  ソフトウェア環境（ライブラリ等）の初期化処理
 *
 *  start.SからBSS/DATA初期化の後、sta_ker呼び出しの前に、マスタコア
 *  （コア0）のみ呼ばれる（スレーブはstart.Sのスレーブ分岐でスキップ）。
 *  ここでAPP_CPU（コア1）を起動し、両コアがsta_kerのbarrier_sync(1)で
 *  ランデブできるようにする（barrier_sync(1)はsta_ker冒頭にあり全コアの
 *  到達を待つため、それより前にコア1を起動する必要がある。design.md §5）。
 */
void
software_init_hook(void)
{
	/*
	 * ※クロックのPLL切替(esp_wifi_clock_init_pll)は当初ここ(software_init_hook,
	 *   カーネルtick前)で行おうとしたが、この早期コンテキストからの esp_bbpll_enable_480m
	 *   呼出しでクラッシュ(リブートループ, RTC_SLOW_MEMマーカー0x01で停止)。
	 *   esp_bbpll_enable_480m は wifi_scan.c(カーネル起動後)からは正常動作するため、
	 *   切替は wifi_scan.c 側へ移した。 追記14。
	 *
	 * ★2026-07-17：上の制約は「BBPLLを起こす」場合のもの。seam起動では
	 *   bootloaderが既にBBPLL 480Mを回してCPUをPLL上(80MHz)に置いているため、
	 *   BBPLLに触らず分周比だけを変えれば昇圧でき、この早期地点でも安全に
	 *   実行できる（詳細は esp32s3_cpu_clock_boost_pll のコメント）。
	 *   tick/HRTが走り出す前に一度だけ行う必要があるため、ここが正しい地点。
	 */
#if defined(TOPPERS_BOOT_SEAM) && TOPPERS_S3_CPU_FREQ_MHZ != 40
	esp32s3_cpu_clock_boost_pll();
#endif

	/*
	 *  ROM newlib（syscall stub table / _global_impure_ptr）の初期化
	 *
	 *  ★必ず target_mprc_initialize()（コア1の解放）より前に行うこと。
	 *    stub tableは両コア共有の1本であり、コア1がROM libc（rand等）を
	 *    使い始める前に張り終えている必要がある。ESP-IDFも同じ制約で
	 *    コア0のsystem_init_fn（system_init_fn.txt:35→43）で初期化する。
	 *  ★software_init_hook()はstart.Sからマスタコア（コア0）のみが
	 *    BSS/DATA初期化後・sta_ker前に呼ぶ（スレーブはstart.Sのスレーブ
	 *    分岐でスキップ）ので、「コア0で1回・ROM libc初使用前」という
	 *    要件をそのまま満たす。
	 *  ★この時点ではまだタスクは存在しない（sta_ker前）。各タスクの
	 *    _reentはactivate_context()からchip_rom_libc_reent_init()で
	 *    初期化される。
	 */
	chip_rom_libc_init();

	target_mprc_initialize();
}

/*
 *  マスタプロセッサ依存の初期化：APP_CPU（コア1）の起動
 *
 *  ESP32-S3のコア1はROMリセットベクタ(0x40000400)でリセットし、
 *  メッセージレジスタ(SYSTEM_CORE_1_CONTROL_1_REG)にブートアドレスが
 *  書かれるまでROMで待つ。以下でリセット解除・クロック有効化・
 *  runstall解除の後、ブートアドレスに_start（start.Sのスレーブ経路へ
 *  合流）を公開する。共有カーネルデータは内蔵SRAMでコヒーレント
 *  （design.md §0）。TNUM_PRCID==1では何もしない。
 */
#if TNUM_PRCID >= 2

/* ESP32-S3 SYSTEM/RTC レジスタ（ESP-IDF soc/system_reg.h, rtc_cntl_reg.h） */
#define SYSTEM_CORE_1_CONTROL_0_REG   ((void *) 0x600C0000U)
#define SYSTEM_CORE_1_CONTROL_1_REG   ((void *) 0x600C0004U)
#define SYS_CORE_1_RUNSTALL           0x00000001U  /* bit0 */
#define SYS_CORE_1_CLKGATE_EN         0x00000002U  /* bit1 */
#define SYS_CORE_1_RESETING           0x00000004U  /* bit2（既定1=リセット保持） */
#define RTC_CNTL_OPTIONS0_REG         ((void *) 0x60008000U)
#define RTC_CNTL_SW_CPU_STALL_REG     ((void *) 0x600080BCU)
#define RTC_SW_STALL_APPCPU_C0_M      0x00000003U  /* OPTIONS0 [1:0] */
#define RTC_SW_STALL_APPCPU_C1_M      0x03F00000U  /* SW_CPU_STALL [25:20] */

void
target_mprc_initialize(void)
{
	extern void _start(void);

	/* 1. RTCソフトストール解除（APPCPUのC0/C1フィールドをクリア） */
	sil_andw(RTC_CNTL_OPTIONS0_REG, ~RTC_SW_STALL_APPCPU_C0_M);
	sil_andw(RTC_CNTL_SW_CPU_STALL_REG, ~RTC_SW_STALL_APPCPU_C1_M);

	/*
	 * 2. クロック有効化・runstall解除・リセットをパルス（set→clear）
	 *
	 * ［レビュー指摘#6］ESP-IDFのcpu_utility_ll_enable_clock_and_reset_app_cpu()
	 * （esp-idf/components/hal/esp32s3/include/hal/cpu_utility_ll.h）は
	 * このブロック全体を`if (!CLKGATE_EN)`でガードしている。cpu_start.cの
	 * コメントの通り、OpenOCDが先にAPP_CPUのクロック有効化・リセット解除
	 * 済み（デバッグ用にbreakpoint設定後の状態）の場合、ここで無条件に
	 * リセットパルスを打つとbreakpointが消えJTAGデバッグを阻害する。
	 * 本ポートはJTAGが中心的なデバッグ手法のため同じガードを追加する。
	 * 通常のコールドブート（CLKGATE_EN未設定）では従来通りブロック全体が
	 * 実行されるため、core1起動の非回帰は変わらない。
	 */
	if ((sil_rew_mem((const uint32_t *) SYSTEM_CORE_1_CONTROL_0_REG)
			& SYS_CORE_1_CLKGATE_EN) == 0U) {
		sil_orw(SYSTEM_CORE_1_CONTROL_0_REG, SYS_CORE_1_CLKGATE_EN);
		sil_andw(SYSTEM_CORE_1_CONTROL_0_REG, ~SYS_CORE_1_RUNSTALL);
		sil_orw(SYSTEM_CORE_1_CONTROL_0_REG, SYS_CORE_1_RESETING);
		sil_andw(SYSTEM_CORE_1_CONTROL_0_REG, ~SYS_CORE_1_RESETING);
	}

	/* 3. ブートアドレス公開：_start（start.Sスレーブ経路へ合流） */
	Asm("memw" ::: "memory");
	sil_wrw_mem(SYSTEM_CORE_1_CONTROL_1_REG, (uint32_t)(uintptr_t) &_start);
	Asm("memw" ::: "memory");
}

#else /* TNUM_PRCID >= 2 */

void
target_mprc_initialize(void)
{
}

#endif /* TNUM_PRCID >= 2 */

#ifdef TOPPERS_STACK_PROBE
/*
 *  タスクスタック実使用量の計測（-DTOPPERS_STACK_PROBE でオプトイン）
 *
 *  全タスクのスタック領域を既知パターンで塗り潰し（stack_probe_paint）、
 *  任意の時点で「底から連続してパターンが残っている長さ」＝未使用量を数える
 *  ことで high-water mark を求める（古典的手法）。ESP_SHIM_TSK_STKSZ 等の
 *  スタックサイズを実測に基づいて決めるための診断用であり、既定では
 *  コンパイルされない（本マクロ未定義時は関数自体が存在しない）。
 *
 *  塗り潰しは target_initialize() の中＝startup.c の barrier_sync(1) と
 *  barrier_sync(2) の間で行う。この区間はどのプロセッサもまだディスパッチを
 *  開始しておらず、全タスクスタックが未使用であることが保証される。
 *  TINIB は const で参照するだけなので、マスタプロセッサ 1 回で全タスク分を
 *  塗れる（PRC_NUM=2 でもスレーブは barrier_sync(2) で待つため競合しない）。
 */
#include "task.h"			/* tinib_table / tnum_tsk */

#define STACK_PROBE_PAT		0xA5A5A5A5U

void
stack_probe_paint(void)
{
	uint_t	i;

	for (i = 0; i < tnum_tsk; i++) {
		/*  stk_top=低位側（領域先頭）、stk_bottom=高位側（初期SP）  */
		uint32_t	*p = (uint32_t *) tinib_table[i].tskinictxb.stk_top;
		uint32_t	*e = (uint32_t *) tinib_table[i].tskinictxb.stk_bottom;

		while (p < e) {
			*p++ = STACK_PROBE_PAT;
		}
	}
}

void
stack_probe_report(const char *tag)
{
	uint_t	i;

	for (i = 0; i < tnum_tsk; i++) {
		uint32_t	*p = (uint32_t *) tinib_table[i].tskinictxb.stk_top;
		uint32_t	*e = (uint32_t *) tinib_table[i].tskinictxb.stk_bottom;
		uint32_t	size = (uint32_t)((char *) e - (char *) p);
		uint32_t	unused = 0U;

		while (p < e && *p == STACK_PROBE_PAT) {
			p++;
			unused += 4U;
		}
		syslog(LOG_NOTICE, "STKPROBE[%s] tsk%d size=%d used=%d unused=%d",
			   tag, (int_t)(i + TMIN_TSKID), (int_t) size,
			   (int_t)(size - unused), (int_t) unused);
	}
}
#endif /* TOPPERS_STACK_PROBE */

/*
 *  ターゲット依存部 初期化処理
 */
void
target_initialize(PCB *p_my_pcb)
{
	/*
	 *  コア依存部の初期化
	 */
	core_initialize(p_my_pcb);

#ifdef TOPPERS_STACK_PROBE
	if (p_my_pcb->prcid == TOPPERS_MASTER_PRCID) {
		stack_probe_paint();
	}
#endif /* TOPPERS_STACK_PROBE */
}

/*
 *  ターゲット依存部 終了処理
 */
void
target_exit(void)
{
	extern void	software_term_hook(void);
	void (*volatile fp)(void) = software_term_hook;

	if (fp != 0) {
		(*fp)();
	}

	/*
	 *  コア依存部の終了処理
	 */
	core_terminate();

#ifdef TOPPERS_QEMU
	/*
	 *  QEMU上ではXtensaセミホスティングのSYS_exit（simcall）でQEMU自体を
	 *  即座に終了させる（テスト高速化：タイムアウト待ちを不要にする）。
	 *  QEMUは -semihosting 付きで起動する。実機ではsimcallは非対応のため
	 *  この分岐はビルドフラグTOPPERS_QEMUでのみ有効化し、実機ビルドでは
	 *  下のループにフォールバックする。
	 *  Xtensa simcall ABI：a2=SYS_exit(1), a3=終了コード。
	 */
	{
		register uint32_t a2 __asm__("a2") = 1U;	/* SYS_exit */
		register uint32_t a3 __asm__("a3") = 0U;	/* 終了コード0 */
		__asm__ __volatile__("simcall" : "+r"(a2) : "r"(a3) : "memory");
	}
#endif /* TOPPERS_QEMU */

	while (1) {
	}
}

/*
 *  エラー発生時の処理
 */
void
Error_Handler(void)
{
	while (1) {
	}
}

/*
 *  デフォルトの software_term_hook（弱い定義）
 */
__attribute__((weak)) void
software_term_hook(void)
{
}
