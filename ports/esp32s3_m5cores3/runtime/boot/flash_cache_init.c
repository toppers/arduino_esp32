/*
 *  ESP32-S3 flash XIP：フラッシュMMUマッピング＋キャッシュ有効化（.iram_boot）
 *
 *  S3 ROMは直接ブートでDROM/IROMフラッシュマップを行わない（実機検証済み）。
 *  本コード（SRAM常駐、start.Sからフラッシュ.text呼出し前に実行）がROMの
 *  cache/MMU関数で、フラッシュ固定オフセットの.text/.rodataを
 *  IROM(0x42000000)/DROM(0x3C000000)へマップする。
 *
 *  【状態 2026-07-07】2部構成イメージはROMがSRAMセグメントをロードし _start(SRAM)
 *  まで起動、本flash_xip_map()も実行されることを実機確認。ただしROMのcache関数の
 *  正しい呼出し列（ROMがイメージ読込みに使った後のcache状態との整合）が未確定で、
 *  Cache_MMU_Init等が停止するケースあり。次段でreliableな診断（下のxputc）で
 *  cache列を確定する。ROMアドレスはesp32s3.rom.ld実測（Cache_Suspend_ICacheは
 *  rom.ldに無い＝使わない）。psize=64（KB単位・S3固定、ROM cache.h記載）。
 */
#include <stdint.h>

#define IRAM_BOOT __attribute__((section(".iram_boot")))

#if defined(TOPPERS_ESP32_LX6)
/* ============================================================
 *  無印ESP32(classic, Xtensa LX6) flash XIP：キャッシュMMUマッピング
 * ============================================================
 *  S3とは全く別のキャッシュモデル（旧世代の統合MMU）。ESP-IDF v5.5
 *  bootloader_utility.c / bootloader_flash.c の実装列を再現：
 *    Cache_Read_Disable(0) → Cache_Flush(0) → mmu_init(0)
 *    → cache_flash_mmu_set(0,0, DROM_VADDR, drom_paddr, 64, drom_pages)
 *    → cache_flash_mmu_set(0,0, IROM_VADDR, irom_paddr, 64, irom_pages)
 *    → Cache_Read_Enable(0)
 *  ROMアドレスは esp32.rom.ld 実測（_rom サフィックス版の直接エントリ）：
 *    cache_flash_mmu_set_rom=0x400095e0 / Cache_Read_Enable_rom=0x40009a84
 *    / Cache_Read_Disable_rom=0x40009ab8 / Cache_Flush_rom=0x40009a14
 *    / mmu_init=0x400095a4。
 *  無印はROM 1st-stageローダが app.bin(0x1000, elf2image)のSRAMセグメントを
 *  ロードして _start へ入る（S3のDirect Bootとは別。sram.binはSRAMのみ）。
 *  .flash_text/.flash_rodata は raw bin として別フラッシュオフセットに置き
 *  （merge_bin）、本関数が IROM(0x400D0000)/DROM(0x3F400000)へマップする。
 *  ★実機JTAG検証必須（S3のflash_cache_initも実機反復を要した）。
 * ============================================================ */
typedef int  (*rom_cache_mmu_set_t)(int, int, uint32_t, uint32_t, int, int);
typedef void (*rom_cache_cpu_t)(int);
#define ROM_cache_flash_mmu_set  ((rom_cache_mmu_set_t) 0x400095e0U)
#define ROM_Cache_Read_Enable    ((rom_cache_cpu_t)    0x40009a84U)
#define ROM_Cache_Read_Disable   ((rom_cache_cpu_t)    0x40009ab8U)
#define ROM_Cache_Flush          ((rom_cache_cpu_t)    0x40009a14U)
#define ROM_mmu_init             ((rom_cache_cpu_t)    0x400095a4U)

#define XIP_DROM_VADDR   0x3F400000U   /* soc/esp32/soc.h SOC_DROM_LOW */
#define XIP_IROM_VADDR   0x400D0000U   /* soc/esp32/soc.h SOC_IROM_LOW */
#ifndef XIP_DROM_PADDR
#define XIP_DROM_PADDR   0x00030000U   /* .flash_rodata フラッシュオフセット（merge_bin、64KB整列。既定値。-Dで上書き可） */
#endif
#ifndef XIP_IROM_PADDR
#define XIP_IROM_PADDR   0x00100000U   /* .flash_text フラッシュオフセット（merge_bin、rodataと非重複。既定値。-Dで上書き可） */
#endif
#define MMU_PSIZE_64K    64            /* bootloaderが渡すpsize（64KBページ） */

extern char _flash_text_size[];
extern char _flash_rodata_size[];

/*
 * UART0(0x3FF40000)にFIFO空きを待って1文字（診断用・信頼できる出力）。
 * ★2026-07-17追記(F-10)：S3版と同様にフラグ化（既定は従来どおり出力、非回帰）。
 */
static void IRAM_BOOT
xputc(char c)
{
#ifndef TOPPERS_XIP_BOOT_DIAG_QUIET
	volatile uint32_t *fifo   = (volatile uint32_t *) 0x3FF40000U;         /* UART0 FIFO */
	volatile uint32_t *status = (volatile uint32_t *) 0x3FF4001CU;         /* UART0 STATUS */
	while (((*status >> 16) & 0xFFU) >= 120U) { }                          /* TXFIFO_CNT[23:16] */
	*fifo = (uint32_t) (uint8_t) c;
#else
	(void) c;
#endif
}

void IRAM_BOOT
flash_xip_map(void)
{
	uint32_t irom_pages = ((uint32_t)(uintptr_t)_flash_text_size   + 0xFFFFU) >> 16;
	uint32_t drom_pages = ((uint32_t)(uintptr_t)_flash_rodata_size + 0xFFFFU) >> 16;
	int rd, ri;

	if (irom_pages == 0U) { irom_pages = 1U; }
	if (drom_pages == 0U) { drom_pages = 1U; }

	xputc('[');
	ROM_Cache_Read_Disable(0);
	ROM_Cache_Flush(0);
	ROM_mmu_init(0);
	xputc('m');
	/* DROM(rodata)とIROM(text)をそれぞれ別vaddr窓へマップ（統合MMUだが
	 * vaddrからエントリ番号が決まるため個別呼出しで非重複）。 */
	rd = ROM_cache_flash_mmu_set(0, 0, XIP_DROM_VADDR, XIP_DROM_PADDR, MMU_PSIZE_64K, (int) drom_pages);
	ri = ROM_cache_flash_mmu_set(0, 0, XIP_IROM_VADDR, XIP_IROM_PADDR, MMU_PSIZE_64K, (int) irom_pages);
	xputc('D'); xputc((char)('0' + (rd & 7)));
	xputc('I'); xputc((char)('0' + (ri & 7)));
	/* ★キャッシュ領域マスク解除（S3のIBUS SHUTビット相当・実機で判明）：
	 * DPORT_PRO_CACHE_CTRL1_REG(0x3FF00044) の各領域MASKビットは既定1=キャッシュ禁止。
	 * DROM(0x3F400000)はROMのCache_Read_Enableで解け動作していたが、IROM命令フェッチ
	 * (0x400D0000〜、IRAM0/IROM0バス)はMASKが残り fault していた。.flash_text は
	 * 0x400D0000〜0x40139e0c で 0x40100000(IROM0領域)境界を跨ぐため IRAM0/IROM0 双方を
	 * クリアする。DROM0も明示クリア（冪等）。 */
	{
		volatile uint32_t *cc1 = (volatile uint32_t *) 0x3FF00044U; /* DPORT_PRO_CACHE_CTRL1_REG */
		*cc1 &= ~((1U << 0) | (1U << 1) | (1U << 2) | (1U << 4));    /* IRAM0/IRAM1/IROM0/DROM0 */
	}
	xputc('c');
	ROM_Cache_Read_Enable(0);
	__asm__ volatile ("isync");
	xputc(']');
	/* DROM(データ)読み出しでマップ成否を可視化（IROMは命令フェッチで検証）。 */
	{
		volatile uint8_t *drom = (volatile uint8_t *) XIP_DROM_VADDR;
		xputc('d'); xputc((char) drom[0]);
	}
}
#else /* defined(TOPPERS_ESP32_LX6) : 以下 ESP32-S3 既存実装 */

typedef void     (*rom_void_t)(void);
typedef void     (*rom_u32arg_t)(uint32_t);
typedef int      (*rom_mmu_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

#define ROM_Cache_MMU_Init          ((rom_void_t)   0x40001998U)
#define ROM_Cache_Ibus_MMU_Set      ((rom_mmu_t)    0x400019a4U)
#define ROM_Cache_Dbus_MMU_Set      ((rom_mmu_t)    0x400019b0U)
#define ROM_Cache_Invalidate_All    ((rom_void_t)   0x400016d4U)
#define ROM_Cache_Enable_ICache     ((rom_u32arg_t) 0x40001878U)
#define ROM_Cache_Enable_DCache     ((rom_u32arg_t) 0x40001890U)
#define ROM_Cache_Invalidate_DCache ((rom_void_t)   0x400016e0U)
/* ICacheのハードウェアモード設定（cpu_start.cが命令キャッシュ有効化前に呼ぶ）。
 * これを欠くとIROM命令フェッチがハングする（DCacheはROM設定済みでDROMは動作）。 */
typedef void (*rom_cachemode_t)(uint32_t, uint8_t, uint8_t);
#define ROM_config_icache_mode      ((rom_cachemode_t) 0x40001a1cU)
typedef void (*rom_idrom_t)(uint32_t, uint32_t);
#define ROM_Cache_Set_IDROM_MMU_Size ((rom_idrom_t) 0x40001914U)
#define CACHE_DROM_MMU_MAX_END       0x400U
typedef void (*rom_idrominfo_t)(uint32_t, uint32_t, uint32_t, uint32_t, int, int);
#define ROM_Cache_Set_IDROM_MMU_Info ((rom_idrominfo_t) 0x40001950U)
/* 16KB(CACHE_SIZE_HALF=0), 8ways(=1), 32B line(=1) */
#define ICACHE_SIZE_16KB  0U
#define ICACHE_8WAYS      1U
#define ICACHE_LINE_32B   1U

#define XIP_DROM_VADDR   0x3C100000U   /* entry16 */
#define XIP_IROM_VADDR   0x42000000U
#ifndef XIP_DROM_PADDR
#define XIP_DROM_PADDR   0x00010000U   /* .flash_rodata フラッシュオフセット（Direct Boot merge_bin既定。seam-S3は-Dで上書き） */
#endif
#ifndef XIP_IROM_PADDR
#define XIP_IROM_PADDR   0x00100000U   /* .flash_text フラッシュオフセット（Direct Boot既定。seam-S3は-Dで上書き） */
#endif
#define MMU_PAGE_64K     64U           /* psizeはKB単位。S3は64固定 */

extern char _flash_text_size[];
extern char _flash_rodata_size[];

/*
 *  ============================================================================
 *  PADDRの取得（2パスリンクを使わない）
 *  ============================================================================
 *  【従来】ビルド時に2パス（link→elf2image→file_offs実測→PADDRを-Dで焼き込んで
 *  再link）でDROM/IROMの物理フラッシュオフセットを決めていた。
 *
 *  【現在】既定（TOPPERS_XIP_PADDR_RUNTIME）はMMUテーブルからの実行時取得である。
 *  seam経路ではESP-IDFの2nd-stage bootloaderが本関数より前にアプリの
 *  DROM/IROMをMMUへマップ済みで、そのエントリにページ番号が入っている。
 *  2026-08-21にCoreS3実機で、この復元値が2パスの焼き込み値と一致することを確認した
 *  （DROM=0x20000/IROM=0x30000、
 *  生エントリ=0x2/0x3、entry id=16/0）。
 *
 *  -DTOPPERS_XIP_PADDR_RUNTIMEを外すと従来の焼き込み値（XIP_*_PADDR）を使う
 *  （2パス構成へ戻すための退路。cmakeのA1_XIP_PADDR_RUNTIME=OFFで切替）。
 *
 *  MMUテーブルは周辺空間の直接読み出しで、キャッシュにもフラッシュにも触らない。
 *  ★Cache_MMU_Init()より前に読むこと（初期化でエントリが消える）。
 */
#define XIP_MMU_TABLE      0x600C5000U  /* soc/soc.h DR_REG_MMU_TABLE */
#define XIP_MMU_VADDR_MASK 0x01FFFFFFU  /* SOC_MMU_VADDR_MASK */
#define XIP_MMU_PAGE_MASK  0x00003FFFU  /* SOC_MMU_VALID_VAL_MASK */
#define XIP_MMU_INVALID    (1U << 14)   /* SOC_MMU_INVALID */
#define XIP_PADDR_INVALID  0xFFFFFFFFU

#if defined(TOPPERS_XIP_PADDR_RUNTIME) || defined(TOPPERS_XIP_PADDR_PROBE)
/*  2パス経路（RUNTIME無効・PROBE無効）では未使用なので、警告を出さないよう囲む */
static uint32_t IRAM_BOOT
xip_mmu_entry(uint32_t vaddr)
{
	uint32_t entry_id = (vaddr & XIP_MMU_VADDR_MASK) >> 16;

	return *(volatile uint32_t *)(XIP_MMU_TABLE + entry_id * 4U);
}

static uint32_t IRAM_BOOT
xip_mmu_paddr(uint32_t vaddr)
{
	uint32_t entry = xip_mmu_entry(vaddr);

	if ((entry & XIP_MMU_INVALID) != 0U) {
		return XIP_PADDR_INVALID;
	}
	return (entry & XIP_MMU_PAGE_MASK) << 16;
}
#endif /* TOPPERS_XIP_PADDR_RUNTIME || TOPPERS_XIP_PADDR_PROBE */

#if defined(TOPPERS_XIP_PADDR_PROBE)
/*
 *  取得したPADDRの観測（-DTOPPERS_XIP_PADDR_PROBEのみ有効）。
 *
 *  ここではUARTへ出さない。CoreS3のコンソールはUSB-Serial-JTAG
 *  （TOPPERS_S3_CONSOLE_USJ）で、本関数のxputc(UART0)はUSB-C側に出ないため、
 *  値をSRAMへ退避してカーネル起動後にアプリ側から出力する。
 *
 *  退避先は**非ゼロ初期化**にして`.data`へ置く（`.bss`だと後段のクリアで消える）。
 *  `.data`はLMA=VMAでbootloaderがSRAMへ直接ロードし、S3はOMIT_DATA_INITで
 *  start.Sのコピーも走らないため、本関数の時点で有効かつ以後上書きされない。
 */
volatile uint32_t toppers_xip_paddr_probe[8] = {
	0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU,
	0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU
};

static void IRAM_BOOT
probe_stash(uint32_t drom_used, uint32_t irom_used)
{
	/* [0][1] 実際にマッピングへ渡す値 */
	toppers_xip_paddr_probe[0] = drom_used;
	toppers_xip_paddr_probe[1] = irom_used;
	/* [2][3] MMUエントリの生値（bootloaderが張った状態） */
	toppers_xip_paddr_probe[2] = xip_mmu_entry(XIP_DROM_VADDR);
	toppers_xip_paddr_probe[3] = xip_mmu_entry(XIP_IROM_VADDR);
	/* [4][5] 生値から復元したPADDR */
	toppers_xip_paddr_probe[4] = xip_mmu_paddr(XIP_DROM_VADDR);
	toppers_xip_paddr_probe[5] = xip_mmu_paddr(XIP_IROM_VADDR);
	/* [6][7] 読んだエントリ番号（DROM=16 / IROM=0 のはず） */
	toppers_xip_paddr_probe[6] = (XIP_DROM_VADDR & XIP_MMU_VADDR_MASK) >> 16;
	toppers_xip_paddr_probe[7] = (XIP_IROM_VADDR & XIP_MMU_VADDR_MASK) >> 16;
}
#endif /* TOPPERS_XIP_PADDR_PROBE */

/*
 * UART0にFIFO空きを待って1文字（診断用・信頼できる出力）。
 *
 * ★2026-07-17追記(F-10)：本番seamビルドでも毎回UARTへ出る点をフラグ化。
 * 既定（TOPPERS_XIP_BOOT_DIAG_QUIET未定義）は従来どおり出力する（非回帰）。
 * 抑止したい場合は -DTOPPERS_XIP_BOOT_DIAG_QUIET を付けてビルドする。
 */
static void IRAM_BOOT
xputc(char c)
{
#ifndef TOPPERS_XIP_BOOT_DIAG_QUIET
	volatile uint32_t *fifo   = (volatile uint32_t *) 0x60000000U;
	volatile uint32_t *status = (volatile uint32_t *) 0x6000001CU; /* UART_STATUS_REG */
	/* TX FIFO count = STATUS[23:16]。128未満になるまで待つ */
	while (((*status >> 16) & 0xFFU) >= 120U) { }
	*fifo = (uint32_t) (uint8_t) c;
#else
	(void) c;
#endif
}

void IRAM_BOOT
flash_xip_map(void)
{
	uint32_t irom_pages = ((uint32_t)(uintptr_t)_flash_text_size   + 0xFFFFU) >> 16;
	uint32_t drom_pages = ((uint32_t)(uintptr_t)_flash_rodata_size + 0xFFFFU) >> 16;
	uint32_t drom_paddr;
	uint32_t irom_paddr;
	int rd, ri;

	if (irom_pages == 0U) { irom_pages = 1U; }
	if (drom_pages == 0U) { drom_pages = 1U; }

	/*  ★MMUの読み出しはCache_MMU_Init()より前に行う（初期化でエントリが消える） */
#if defined(TOPPERS_XIP_PADDR_RUNTIME)
	drom_paddr = xip_mmu_paddr(XIP_DROM_VADDR);
	irom_paddr = xip_mmu_paddr(XIP_IROM_VADDR);
#if defined(TOPPERS_XIP_POISON_PAGE)
	/*
	 *  negative control：実行時に得たページを意図的にずらす。
	 *  2パスを廃止して A1_POISON_PADDR が使えなくなった代わりに、
	 *  「算出値が本当に使われている」ことをこれで証明する（必ず起動失敗するはず）。
	 */
	if (drom_paddr != XIP_PADDR_INVALID) {
		drom_paddr += ((uint32_t)(TOPPERS_XIP_POISON_PAGE)) << 16;
	}
	if (irom_paddr != XIP_PADDR_INVALID) {
		irom_paddr += ((uint32_t)(TOPPERS_XIP_POISON_PAGE)) << 16;
	}
#endif
	if ((drom_paddr == XIP_PADDR_INVALID) || (irom_paddr == XIP_PADDR_INVALID)) {
		/*
		 *  bootloaderがDROM/IROMを張っていない構成。ここで既定値へ落ちると
		 *  原因不明のハングになるので、診断を出して止める。
		 *  （xputcはUART0。CoreS3のUSB-Cには出ないが、DevKitCとJTAGでは見える）
		 */
		xputc('!'); xputc('P'); xputc('A'); xputc('D'); xputc('D'); xputc('R');
		xputc('!'); xputc('\n');
		for (;;) { }
	}
#else
	/*  従来経路：2パスが焼き込んだビルド時定数を使う */
	drom_paddr = (uint32_t) XIP_DROM_PADDR;
	irom_paddr = (uint32_t) XIP_IROM_PADDR;
#endif

#if defined(TOPPERS_XIP_PADDR_PROBE)
	probe_stash(drom_paddr, irom_paddr);
#endif

	/*
	 * ★F-7：
	 * seam経路ではESP-IDFの2nd-stage bootloaderがICache/DCacheを有効化した状態で
	 * このコードへ制御を渡す。ESP-IDF自身（cpu_start.c）はMMU書換え前に必ず
	 * Cache_Disable/Suspend相当を挟むが、本関数はDisable/Suspendを挟まず
	 * config_icache_mode→Cache_MMU_Init→(Set_IDROM_MMU_Size→Dbus/Ibus_MMU_Set)→
	 * bus unshut→Invalidate→Enableをキャッシュ有効のまま実行する＝仕様外の順序。
	 * 許容している理由：(1) 実機で再現性よく成功することを確認済み
	 * （evidence多数、bringup各コミット参照）。(2) 書き換え対象はDROM/IROMの
	 * 読み取り専用フラッシュマッピングのみで、書き戻しが要るdirty cache line
	 * （ライトバック対象のRAMマッピング）は存在しないため、キャッシュ有効中に
	 * MMUエントリを差し替えても不整合なデータを書き戻すリスクが無い。
	 * (3) 直後にCache_Invalidate_All/Invalidate_DCacheで古いエントリを破棄し、
	 * Enable_ICache/DCacheの前にisyncを発行している。
	 * 恒久対応（Disable相当を先行させる、または本コメントの妥当性を再検証する）は
	 * 未実施。
	 */
	xputc('[');
	/* ICacheのモードを設定してから有効化する（これが命令フェッチの鍵） */
	ROM_config_icache_mode(ICACHE_SIZE_16KB, ICACHE_8WAYS, ICACHE_LINE_32B);
	xputc('M');
	ROM_Cache_MMU_Init();
	/* IROMを低エントリ、DROMを高エントリに分割（cpu_start.c相当）。
	 * irom_bytes = irom_pages * 4(entry size)。 */
	ROM_Cache_Set_IDROM_MMU_Size(16U * 4U, CACHE_DROM_MMU_MAX_END - 16U * 4U);  /* IROM=entry0..15 */
	xputc('m');
	rd = ROM_Cache_Dbus_MMU_Set(0U, XIP_DROM_VADDR, drom_paddr,
							   MMU_PAGE_64K, drom_pages, 0U);
	ri = ROM_Cache_Ibus_MMU_Set(0U, XIP_IROM_VADDR, irom_paddr,
							   MMU_PAGE_64K, irom_pages, 0U);
	xputc('D'); xputc((char)('0' + (rd & 7)));
	xputc('I'); xputc((char)('0' + (ri & 7)));
	/* ★真の欠落ステップ（codex指摘）：IBUSのcache busはSHUTビットで遮断されており、
	 * これをクリアしないとIROM命令フェッチが停止する（DBUS/DROMはSHUTされていないので
	 * 動いていた）。cache_ll_l1_enable_bus相当。EXTMEM_ICACHE_CTRL1_REG=0x600C4064、
	 * SHUT_CORE0_BUS=BIT0 / SHUT_CORE1_BUS=BIT1。MMU map→bus enable→cache enableの順。 */
	{
		volatile uint32_t *ic1 = (volatile uint32_t *) 0x600C4064U;
		*ic1 &= ~((1U << 0) | (1U << 1));   /* ICache IBUS遮断解除（core0/1） */
	}
	xputc('N');
	ROM_Cache_Invalidate_All();
	ROM_Cache_Invalidate_DCache();
	ROM_Cache_Enable_DCache(0U);
	ROM_Cache_Enable_ICache(0U);
	__asm__ volatile ("isync");
	xputc(']');
	/* DROM(データ)は読める。IROMはIBUS=命令フェッチ専用でデータ読みはfaultするため
	 * ここでは読まない（命令実行は_startのcallx4 hardware_init_hookで検証される）。 */
	{
		volatile uint8_t *drom = (volatile uint8_t *) XIP_DROM_VADDR;
		xputc('d'); xputc((char) drom[0]);
	}
}

#endif /* defined(TOPPERS_ESP32_LX6) */
