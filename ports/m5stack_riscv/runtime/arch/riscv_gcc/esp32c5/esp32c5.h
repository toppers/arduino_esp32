/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  ESP32-C5 のハードウェア資源の定義（M5Stamp-C5 / FMP3）
 *
 *  出典（値の正本）:
 *   - asp3_esp_idf 6471444 asp3/arch/riscv_gcc/esp32c5/esp32c5.h
 *     （レジスタベース，WDT キー，systimer 16 MHz，割込みソース番号，
 *      SIL_DLY_TIM の実測/外挿値）
 *   - esp-idf v5.5.4（735507283d）components/soc/esp32c5/{include/soc/
 *     interrupts.h, include/soc/clic_reg.h, include/soc/soc.h, register/soc/
 *     reg_base.h, register/soc/pcr_reg.h, register/soc/intpri_reg.h}
 *     （本ファイルの数値は 2026-09-15 に IDF ヘッダを gcc でコンパイルして
 *      実値を採り asp3 の値と二重照合した。IMPORT_PROVENANCE.md の表参照）
 *  形（周波数分岐/#ifndef CORE_CLK_MHZ）は fmp3/arch/riscv_gcc/esp32c6/
 *  esp32c6.h，CLIC 関連マクロ名は fmp3/arch/riscv_gcc/esp32p4/esp32p4.h
 *  （fmp3_core 共通 clic_kernel_impl.h が要求する名前）に合わせた。
 *
 *  割込みコントローラ: 標準 RISC-V CLIC（SOC_INT_CLIC_SUPPORTED=1）。
 *  P4 と同じく mtvec.MODE=3 + mtvt（CSR 0x307），非ベクタ（SHV=0）+ソフト
 *  ディスパッチ。FMP3 の INTNO は CLIC 線番号そのもの（0..47。外部線は
 *  16..47 = 割込みマトリクス経由）。割込みマトリクスの MAP レジスタへ書く
 *  値も CLIC 線番号そのもの（P4 の chip_serial.c と同じ。asp3 が C5 実機で
 *  「MAP 値は CLIC 内部番号」と確定: docs/c5-bringup.md 実施02）。
 *
 *  閾値レジスタ: C5 は IDF が CSR mintthresh（0x347）を使う
 *  （INTTHRESH_STANDARD=1）。メモリマップド CLIC_INT_THRESH_REG（+0x8）も
 *  clic_reg.h に在る（asp3 は実測 0 で未使用）。**段3 Task 4（2026-09-16）で
 *  実機確定: 効くのは CSR**（test_ipmmask が CSR 版 PASS / MMIO 版 FAIL、
 *  .steering/20260915-c5-plan/stage3/README.md 4 節）。両経路を持つ
 *  （esp32c5_clic_kernel_impl.h / chip_support.S の
 *  TOPPERS_ESP32C5_CLIC_THRESH_MMIO 分岐）が既定 = CSR が正。
 */

#ifndef TOPPERS_ESP32C5_H
#define TOPPERS_ESP32C5_H

/*
 *  メモリマップ（soc/esp32c5/include/soc/soc.h）
 *
 *  SOC_DROM_LOW == SOC_IROM_LOW（0x42000000。soc.h:148-153 で確認済み。
 *  C6 と同じく IROM/DROM は分離していない）。SOC_IROM_HIGH は 0x44000000
 *  （32 MiB 固定窓）。HP SRAM は 0x40800000..0x40860000 = 384 KiB（C6 は
 *  512 KiB）。seam で静的に使えるのは bootloader の iram_loader_seg 手前
 *  0x4084E5A0 まで = 320,928 B（esp_system/ld/esp32c5/memory.ld.in の
 *  SRAM_SEG_END，bootloader/subproject/main/ld/esp32c5/bootloader.ld の
 *  ASSERT）。リンカスクリプト esp32c5_xip.ld と一致させること。
 */
#define ESP32C5_IROM_BASE       0x42000000
#define ESP32C5_DROM_BASE       0x42000000  /* IROM と同一（soc.h:152） */
#define ESP32C5_IROM_HIGH       0x44000000
#define ESP32C5_DRAM_BASE       0x40800000
#define ESP32C5_DRAM_SIZE       0x00060000  /* 384KB（soc.h:158-159） */
#define ESP32C5_SEAM_RAM_END    0x4084E5A0  /* bootloader iram_loader_seg start */

/*
 *  CPUクロック周波数（MHz。core_syssvc.h の性能カウンタ換算等で使用）
 *
 *  seam（IDF v5.5.4 bootloader @0x2000 -> FMP3）では bootloader が
 *  CPU_CLK_FREQ_MHZ_BTLD=80（soc.h:136。PLL_F240M/3、rev v1.0 の経路）で
 *  渡す（ソースからの推論。段2 の PCR 読出しで確定する = 未確認）。
 *  既定 80 は cmake/a1_c5_stage1.cmake が A1_C5_CPU_FREQ_MHZ で渡す
 *  CORE_CLK_MHZ と同値。240 は段4（分周 3->1 のみ、ソース切替なし。
 *  rev v1.0 の ICG erratum IDF-11064 回避）。asp3 は Direct Boot で BBPLL を
 *  自前で較正して 240 を実証した（esp32c5.h:111）。
 */
#ifndef CORE_CLK_MHZ
#define CORE_CLK_MHZ            80
#endif /* CORE_CLK_MHZ */

/*
 *  微少時間待ちのための定義（nsec単位）
 *
 *  sil_dly_nse の実装は core_support.S（回数較正方式、P4/C6 と同じ）:
 *    a0 -= TIM1; if (a0 <= 0) return;      ... k=0 経路（固定オーバヘッド a）
 *    do { a0 -= TIM2; } while (a0 > 0);    ... k 回のループ（1 回あたり b）
 *  TIM1/TIM2 を実機定数より小さくする方向は安全側（過剰に待つ）、大きくすると
 *  NG（要求より短く待つ）になる（C6 段3 の知見）。
 *
 *  【80 MHz: 較正済み 2026-09-16（段3 Task 5、M5Stamp-C5 実機、seam、
 *   CSR 閾値既定、.steering/20260915-c5-plan/stage3/README.md 5 節）】
 *  test_dlynse（CAPT=120）の実測 delay_time（ns、NO_LOOP=1e6 の平均）を
 *  ループ回数 k に対して並べると（暫定値 60/24 のビルド、
 *  logs/task5-baseline/test_dlynse.1.log。閾値 MMIO 版でも 1 ns 単位で同一）:
 *    k:      0   1   2   3   4   5   6   7   8   9  10   20    50
 *    delay: 62  87 149 149 224 224 299 299 374 374 449  837  1962
 *  C6（137+25k の完全な線形、b=2 サイクル）と違い、C5 では k>=2 で
 *  「2 ループごとに 75 ns（6 サイクル）」の階段になり、奇数 k は直前の偶数 k と
 *  同じ値（k=2m, 2m+1: 74+75m）。平均 b=37.5 ns/loop（3 サイクル）だが、
 *  奇数 k の下界は 36.5+37.5k、k=1 は 87、k=0 は 62（事実。原因はパイプライン/
 *  フェッチの都合と推測するが分解していない = 未確認。ループは 0x42001bca の
 *  c.addi 2 B + bgtz 4 B で、配置が変わると階段の形が変わる可能性がある = 未確認）。
 *
 *  選定: 全プローブで delay_time >= dlytim を保つ条件は
 *    TIM1 <= 62（k=0）、TIM1 + TIM2 <= 87（k=1）、TIM1 + k*TIM2 <= 36.5 + 37.5k（奇数 k>=3）
 *  で、TIM2 は c.addi の即値範囲（-32..31）に収まる 32 以下に保ち（超えると命令
 *  長が変わり、上の実測が適用できない）、**TIM1=40 / TIM2=32** とした
 *  （マージン: k=0 で 22 ns、k=1 で 15 ns、k=3 で 13 ns、以降単調増加）。暫定値
 *  60/24 は k=0/1 のマージンが 2-3 ns（0.2 サイクル）しか無く、階段の下界に対して
 *  実質ゼロだった。TIM1=40 は addi の即値範囲外（-40）で 4 B のまま = 命令配置は
 *  暫定値のビルドと同一（objdump で確認）。
 *
 *  positive control（AC-4a）: ESP32C5_SIL_DLY_OVER（cmake -DA1_C5_SIL_DLY_TIM1_OVER=ON、
 *  較正値の倍 = 80/64）で NG を再現する向き。ESP32C5_SIL_DLY_HALF（半分 = 20/16）は
 *  「小さくしても壊れない」確認用（C6 段3 8-3 節の知見どおり NG は出ない）。
 *  どちらも既定 OFF、同時指定は #error。240 MHz 分岐にも同名の 2 option が効く
 *  （段4 Task 1、倍 = 26/20、半分 = 6/5）。
 *
 *  【240 MHz: 較正済み 2026-09-16（段4 Task 1、M5Stamp-C5 実機、seam、分周 3->1
 *   のみ = esp/boot/seam_c5_clk.c、.steering/20260915-c5-plan/stage4/README.md 1 節）】
 *  asp3 の暫定値 17/17（asp3 esp32c5.h:126-127、192 MHz の 20/20 からの机上外挿）で
 *  建てた test_dlynse（logs/task1-dlynse-240-baseline/test_dlynse.1.log）は
 *  17 行中 NG 13（empty loop 33338 = 80 MHz の 100005 の 1/3.0 で CPU が 240 MHz で
 *  回っていることの証拠）。ループ回数 k に対する実測 delay_time（ns）:
 *    k:      0   1   2   3   4   5   6   7   8   9  10   20   50
 *    delay: 20  29  41  49  66  74  91  99 116 124 141  279  654
 *  80 MHz と同じ「2 ループごとの階段」で、1 段 = 25 ns（6 サイクル）、平均
 *  b = 12.5 ns/loop（3 サイクル）。k=2m（m>=1）は 16+25m、k=2m+1（m>=1）は 24+25m、
 *  k=1 は 29、k=0 は 20（ループは c.addi 2 B + bgtz 4 B、0x42001d98）。
 *
 *  選定: 全プローブで delay_time >= dlytim = TIM1 + TIM2*k を保つ条件は
 *    TIM1 <= 20（k=0）、TIM1 + TIM2 <= 29（k=1）、TIM1 + 3*TIM2 <= 49（k=3、最も厳しい）、
 *    TIM1 + 2m*TIM2 <= 16+25m、TIM1 + (2m+1)*TIM2 <= 24+25m
 *  で、TIM2 は c.addi の即値範囲に収め、**TIM1=13 / TIM2=10** とした（マージン:
 *  k=0 で 7 ns、k=1 で 6、k=2 で 8、k=3 で 6、k=5 で 11、以降 2 ループごとに +5 ns で
 *  単調増加。最小 6 ns = 240 MHz の 1.4 サイクル。TIM2=12 は k=3 でマージン 0、
 *  TIM2=11 は 3 ns（0.7 サイクル）なので採らなかった。大きい k では要求の 1.25 倍
 *  待つ = 安全側）。較正後の再実測は logs/task1-dlynse-240-calibrated/（17/17 OK、
 *  cost(k) は上の表と同一 = 命令配置が同じ）。positive control は
 *  ESP32C5_SIL_DLY_OVER（倍 = 26/20）で NG を再現（logs/task1-dlynse-240-over/）。
 */
#if CORE_CLK_MHZ == 80
#if defined(ESP32C5_SIL_DLY_HALF) && defined(ESP32C5_SIL_DLY_OVER)
#error "ESP32C5_SIL_DLY_HALF and ESP32C5_SIL_DLY_OVER cannot be combined"
#elif defined(ESP32C5_SIL_DLY_HALF)
#define SIL_DLY_TIM1    20      /* 較正値 40 の半分（安全側。NG は出ない） */
#define SIL_DLY_TIM2    16      /* 較正値 32 の半分（安全側。NG は出ない） */
#elif defined(ESP32C5_SIL_DLY_OVER)
#define SIL_DLY_TIM1    80      /* 較正値 40 の倍。実測 a=62 ns を超える -> NG 再現 */
#define SIL_DLY_TIM2    64      /* 較正値 32 の倍。実測 b=37.5 ns/loop を超える -> NG 再現（即値範囲外 = 命令長 4 B） */
#else
#define SIL_DLY_TIM1    40      /* 較正済み 2026-09-16（k=0 実測 62 ns、k=1 実測 87 ns に対しマージン 22/15 ns） */
#define SIL_DLY_TIM2    32      /* 較正済み 2026-09-16（奇数 k の下界 36.5+37.5k に対し k=3 で 13 ns、c.addi 範囲内） */
#endif
#elif CORE_CLK_MHZ == 240
#if defined(ESP32C5_SIL_DLY_HALF) && defined(ESP32C5_SIL_DLY_OVER)
#error "ESP32C5_SIL_DLY_HALF and ESP32C5_SIL_DLY_OVER cannot be combined"
#elif defined(ESP32C5_SIL_DLY_HALF)
#define SIL_DLY_TIM1    6       /* 較正値 13 の半分（安全側。NG は出ない） */
#define SIL_DLY_TIM2    5       /* 較正値 10 の半分（安全側。NG は出ない） */
#elif defined(ESP32C5_SIL_DLY_OVER)
#define SIL_DLY_TIM1    26      /* 較正値 13 の倍。実測 k=0 の 20 ns を超える -> NG 再現 */
#define SIL_DLY_TIM2    20      /* 較正値 10 の倍。実測 b=12.5 ns/loop を超える -> NG 再現 */
#else
#define SIL_DLY_TIM1    13      /* 較正済み 2026-09-16（k=0 実測 20 ns、k=1 実測 29 ns に対しマージン 7/6 ns） */
#define SIL_DLY_TIM2    10      /* 較正済み 2026-09-16（k=3 実測 49 ns に対し 6 ns、以降単調増加。c.addi 範囲内） */
#endif
#else
#error "CORE_CLK_MHZ must be 80 or 240 (SIL_DLY_TIM1/2 are provisioned only for these)"
#endif /* CORE_CLK_MHZ == 80 */

/*
 *  ペリフェラルのベースアドレス（soc/esp32c5/register/soc/reg_base.h。
 *  2026-09-15 コンパイルで実値確認）
 */
#define ESP32C5_INTMTX_BASE     0x60010000  /* 割込みマトリクス（DR_REG_INTMTX_BASE。C6 と同一） */
#define ESP32C5_SYSTIMER_BASE   0x6000A000  /* システムタイマ（C6 と同一） */
#define ESP32C5_USBJTAG_BASE    0x6000F000  /* USB Serial/JTAG（C6 と同一。GPIO は 13/14） */
#define ESP32C5_UART0_BASE      0x60000000  /* UART0（C6 と同一。M5Stamp-C5 は USJ のみ） */
#define ESP32C5_TIMG0_BASE      0x60008000  /* タイマグループ0（MWDT） */
#define ESP32C5_TIMG1_BASE      0x60009000  /* タイマグループ1（MWDT） */
#define ESP32C5_LP_WDT_BASE     0x600B1C00  /* 低電力ドメイン WDT */
#define ESP32C5_PCR_BASE        0x60096000  /* Peripheral Clock and Reset */
#define ESP32C5_INTPRI_BASE     0x600C5000  /* ソフトウェア割込み（FROM_CPU_n）等 */
#define ESP32C5_EFUSE_BASE      0x600B4800  /* eFuse（C6 の 0x600B0800 から移動。MAC は +0x44/+0x48） */
#define ESP32C5_LPPERI_BASE     0x600B2800  /* LPPERI（RNG） */
#define ESP32C5_MODEM_SYSCON_BASE 0x600A9C00 /* MODEM_SYSCON（C6 は 0x600A9800。modem/reg_base.h） */
#define ESP32C5_MODEM_LPCON_BASE  0x600AF000 /* MODEM_LPCON（C6 と同一） */
#define ESP32C5_PMU_BASE        0x600B0000  /* PMU（C6 と同一） */

/*
 *  HW RNG（soc/esp32c5/include/soc/wdev_reg.h: WDEV_RND_REG =
 *  LPPERI_RNG_DATA_SYNC_REG = LPPERI + 0x28。C6 は +0x8。段4 の shim 用）
 */
#define ESP32C5_WDEV_RND_REG    (ESP32C5_LPPERI_BASE + 0x28)

/*
 *  eFuse の MAC（register/soc/efuse_reg.h: EFUSE_RD_MAC_SYS0/1_REG）
 */
#define ESP32C5_EFUSE_RD_MAC_SYS0   (ESP32C5_EFUSE_BASE + 0x44)
#define ESP32C5_EFUSE_RD_MAC_SYS1   (ESP32C5_EFUSE_BASE + 0x48)

/*
 *  CLIC（Core-Local Interrupt Controller。soc/esp32c5/include/soc/clic_reg.h）
 *    mtvec.MODE=3，mtvt（CSR 0x307）。NLBITS=3（CLIC_INT_INFO の CTLBITS
 *    既定 3。asp3 は冷間ブートで CLIC_INT_CONFIG=0x6（mnlbits=3）を実測し、
 *    書き替えない方針 = 本層も CLIC_INT_CONFIG には触らない）。
 *    内部線 0..15 は使わない（C5 に CLINT msip/mtimer は載せない: HRT は
 *    target 側 systimer、単一コアで IPI 無し）。外部線 16..47。
 *  以下のマクロ名は fmp3_core 共通 clic_kernel_impl.h（の chip 側複製
 *  esp32c5_clic_kernel_impl.h）と共通 clic_kernel_impl.c が要求する
 *  （P4 の esp32p4.h と同名）。
 */
#define CLIC_BASE              ULONG_C(0x20800000)
#define CLIC_INT_CONFIG        (uint32_t *)(CLIC_BASE + 0x0000UL)  /* NLBITS 等（触らない） */
#define CLIC_INT_INFO          (uint32_t *)(CLIC_BASE + 0x0004UL)  /* RO */
#define CLIC_INT_THRESH        (uint32_t *)(CLIC_BASE + 0x0008UL)  /* [31:24]。MMIO 経路のみ使用（段3 実測: 閾値として効かない） */
#define CLIC_CTRL_BASE         (CLIC_BASE + 0x1000UL)
/*  割込み i の制御レジスタ。CTL[31:24]/ATTR MODE[23:22]/TRIG[18:17]/SHV[16]/IE[8]/IP[0]  */
#define CLIC_INT_CTRL(i)       (uint32_t *)(CLIC_CTRL_BASE + (i) * 4UL)
#define CLIC_INT_CTRL_ATTR(i)  (uint8_t *)(CLIC_CTRL_BASE + (i) * 4UL + 2UL)  /* ATTR バイト（BYTE_CLIC_INT_ATTR_REG） */
#define CLIC_NLBITS            UINT_C(3)
#define CLIC_EXT_OFFSET        UINT_C(16)     /* 外部割込みの線番号オフセット（CLIC_EXT_INTR_NUM_OFFSET） */
#define CSR_MTVT               0x307          /* CLIC ベクタテーブルベース CSR */
#define CSR_MINTTHRESH         0x347          /* 標準 CLIC の閾値 CSR（IDF: MINTTHRESH_CSR。byte は [7:0]） */
#define CSR_MINTSTATUS         0xFB1          /* 標準 CLIC（IDF: MINTSTATUS_CSR。P4 の 0x346 とは違う） */

/*  CLIC_INT_CTRL レジスタのフィールド  */
#define CLIC_INT_IP_BIT        (1UL << 0)     /* pending */
#define CLIC_INT_IE_BIT        (1UL << 8)     /* enable  */
#define CLIC_INT_SHV_BIT       (1UL << 16)    /* selective hw vectoring */
#define CLIC_INT_TRIG_EDGE     (1UL << 17)    /* TRIG[18:17]=01 立上りエッジ(software raise 可) */
#define CLIC_INT_CTL_SHIFT     24             /* 優先度/レベル CTL[31:24] */
#define CLIC_INT_THRESH_SHIFT  24             /* MMIO THRESH[31:24] */
#define ESP32C5_CLIC_ATTR_MODE_BYTE_M   UINT_C(0xC0)   /* ATTR バイト(BYTE_CLIC_INT_ATTR_REG): MODE=3(machine)<<6, TRIG=level, SHV=0. IDF clic_reg.h の CLIC_INT_ATTR_MODE_M(ワードマスク 3<<22)とは別物なので衝突しない名前にした */

/*
 *  CLIC 割込み線の総数（内部 0-15 + 外部 16-47。CLIC_INT_INFO_NUM_INT 既定 48）。
 *  FMP3 の割込み番号(INTNO)は本ファイルで CLIC 線番号と同一に扱う。
 */
#define CLIC_TNUM_INTNO        UINT_C(48)

/*
 *  割込みマトリクス（Interrupt Matrix）
 *    ペリフェラル割込みソース src の MAP レジスタ = INTMTX_BASE + 4*src に
 *    割付先の CLIC 線番号（16..47）をそのまま書く。0 を書くと CLIC 線 0
 *    （内部線、IE=0）へ向くので実質切断（asp3/C6 の初期化と同じ扱い）。
 *    IDF の C 実装（riscv/interrupt_clic.c）は切断先に 22（INT_MUX_DISABLED_INTNO 6 + 16）
 *    を使う（intr_alloc.c が呼ぶ ROM 版の +16 処理は未確認 = 推測。線 22 を空けるのは
 *    安全側の配慮であって事実の主張ではない）。
 */
#define INTMTX_MAP(src)        (uint32_t *)(ESP32C5_INTMTX_BASE + (src) * 4UL)

/*
 *  ソフトウェア割込み（ras_int / タイマ強制用。INTPRI ペリフェラル。
 *  register/soc/intpri_reg.h INTPRI_CPU_INTR_FROM_CPU_n_REG = +0x90..+0x9C）
 */
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_0  (ESP32C5_INTPRI_BASE + 0x90)
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_1  (ESP32C5_INTPRI_BASE + 0x94)
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_2  (ESP32C5_INTPRI_BASE + 0x98)
#define ESP32C5_INTPRI_CPU_INTR_FROM_CPU_3  (ESP32C5_INTPRI_BASE + 0x9c)

/*
 *  PCR レジスタ（register/soc/pcr_reg.h。2026-09-15 コンパイルで実値確認）
 *
 *  PCR_SYSCLK_CONF (+0x110): SOC_CLK_SEL[17:16]（0=XTAL/1=RC_FAST/2=PLL_F160M/
 *  3=PLL_F240M。asp3 実施32 の stock 実測 soc_clk_sel=3），CLK_XTAL_FREQ[30:24]
 *  PCR_CPU_FREQ_CONF (+0x118): CPU_DIV_NUM[7:0]（bootloader は 2 = /3）
 *  PCR_AHB_FREQ_CONF (+0x11c): AHB_DIV_NUM[7:0]（bootloader は 5 = /6）
 *  PCR_TIMERGROUP0_CONF (+0x54): TG0_CLK_EN bit0 / TG0_RST_EN bit1
 *    **C6 は +0x3C**（fmp3/target/m5nanoc6_gcc/target_kernel_impl.c の直値
 *    0x6009603C）。C5 で同じ直値を書くと別のレジスタに当たるので注意。
 *  段1 では読まない（段2 の PCR 読出しで 80 MHz を確定する）。
 */
#define ESP32C5_PCR_SYSCLK_CONF         (ESP32C5_PCR_BASE + 0x110)
#define ESP32C5_PCR_CPU_FREQ_CONF       (ESP32C5_PCR_BASE + 0x118)
#define ESP32C5_PCR_AHB_FREQ_CONF       (ESP32C5_PCR_BASE + 0x11c)
#define ESP32C5_PCR_TIMERGROUP0_CONF    (ESP32C5_PCR_BASE + 0x54)
#define ESP32C5_PCR_SYSTIMER_CONF       (ESP32C5_PCR_BASE + 0x6c)
#define ESP32C5_PCR_SYSCLK_CONF_SEL_MASK  (3U << 16)
#define ESP32C5_PCR_TG0_CLK_EN          (1U << 0)
#define ESP32C5_PCR_TG0_RST_EN          (1U << 1)

/*
 *  割込みソース番号（割込みマトリクスへの入力。soc/esp32c5/include/soc/
 *  interrupts.h の periph_interrupt_t enum 実値。2026-09-15 に IDF ヘッダを
 *  コンパイルして採り、asp3 docs/c5-port-design.md 10 節（UART0=47、
 *  USBJTAG=54、SYSTIMER_TARGET0=61、FROM_CPU_0-3=23-26、全 84 本）と
 *  二重照合済み。C6（43/48/57/22-25、77 本）とは値が違う）
 */
#define ESP32C5_INTSRC_UART0             47
#define ESP32C5_INTSRC_USB_SERIAL_JTAG   54
#define ESP32C5_INTSRC_SYSTIMER_TARGET0  61
#define ESP32C5_INTSRC_FROM_CPU_0        23
#define ESP32C5_INTSRC_FROM_CPU_1        24
#define ESP32C5_INTSRC_FROM_CPU_2        25
#define ESP32C5_INTSRC_FROM_CPU_3        26
#define ESP32C5_TNUM_INTSRC              84  /* ETS_MAX_INTR_SOURCE */

/*
 *  SYSTIMER レジスタ（unit0 + target0 のみ使用。C6 と同一レイアウト＝
 *  ベースも同一。hal/esp32c5/include/hal/systimer_ll.h は C6 版と
 *  コメント 2 行しか違わない）
 *
 *  クロックは 16 MHz。IDF が「XTAL が 40/48 MHz のどちらでも systimer の分解能は
 *  常に 16 MHz」と明記（esp_hw_support/port/esp32c5/systimer.c:12-13）し、asp3 が
 *  JTAG 二点法で 16.00 MHz を実測（docs/c5-bringup.md 実施03。48 MHz / 3。C6 は
 *  40 MHz / 2.5）。M5Stamp-C5 の XTAL は 48 MHz（段0 esptool 実読）。本 repo の
 *  個体での実測は段2/段3 の tick 突き合わせで行う（IDF 文書 + asp3 実測済み、
 *  本 repo では未実測）。
 */
#define ESP32C5_SYSTIMER_CONF           (ESP32C5_SYSTIMER_BASE + 0x00)
#define ESP32C5_SYSTIMER_UNIT0_OP       (ESP32C5_SYSTIMER_BASE + 0x04)
#define ESP32C5_SYSTIMER_TARGET0_HI     (ESP32C5_SYSTIMER_BASE + 0x1C)
#define ESP32C5_SYSTIMER_TARGET0_LO     (ESP32C5_SYSTIMER_BASE + 0x20)
#define ESP32C5_SYSTIMER_TARGET0_CONF   (ESP32C5_SYSTIMER_BASE + 0x34)
#define ESP32C5_SYSTIMER_UNIT0_VALUE_HI (ESP32C5_SYSTIMER_BASE + 0x40)
#define ESP32C5_SYSTIMER_UNIT0_VALUE_LO (ESP32C5_SYSTIMER_BASE + 0x44)
#define ESP32C5_SYSTIMER_COMP0_LOAD     (ESP32C5_SYSTIMER_BASE + 0x50)
#define ESP32C5_SYSTIMER_INT_ENA        (ESP32C5_SYSTIMER_BASE + 0x64)
#define ESP32C5_SYSTIMER_INT_RAW        (ESP32C5_SYSTIMER_BASE + 0x68)
#define ESP32C5_SYSTIMER_INT_CLR        (ESP32C5_SYSTIMER_BASE + 0x6C)
#define ESP32C5_SYSTIMER_INT_ST         (ESP32C5_SYSTIMER_BASE + 0x70)

#define ESP32C5_SYSTIMER_CONF_UNIT0_WORK_EN    (1U << 30)
#define ESP32C5_SYSTIMER_CONF_TARGET0_WORK_EN  (1U << 24)
#define ESP32C5_SYSTIMER_OP_UPDATE             (1U << 30)
#define ESP32C5_SYSTIMER_OP_VALUE_VALID        (1U << 29)
#define ESP32C5_SYSTIMER_TARGET0_PERIOD_MODE   (1U << 30)
#define ESP32C5_SYSTIMER_INT_TARGET0           (1U << 0)
#define ESP32C5_SYSTIMER_TICKS_PER_US   16U  /* IDF 文書 + asp3 実測 16.00MHz。本 repo では未実測 */

/*
 *  USB Serial/JTAG レジスタ（C6 と同一レイアウト/同一ベース。
 *  register/soc/usb_serial_jtag_reg.h: EP1=+0x0 / EP1_CONF=+0x4）
 */
#define ESP32C5_USBJTAG_EP1(base)		((uint32_t *)((base) + 0x00U))
#define ESP32C5_USBJTAG_EP1_CONF(base)	((uint32_t *)((base) + 0x04U))
#define ESP32C5_USBJTAG_EP1_CONF_WR_DONE		UINT_C(0x00000001)
#define ESP32C5_USBJTAG_EP1_CONF_IN_DATA_FREE	UINT_C(0x00000002)
#define ESP32C5_USBJTAG_EP1_CONF_OUT_DATA_AVAIL	UINT_C(0x00000004)

/*
 *  ウォッチドッグタイマ
 *
 *  TIMG_WDT_WKEY: register/soc/timer_group_reg.h の default 0x50D83AA1
 *  （C6 と同一）。LP_WDT / SWD の解錠キーも同値
 *  （hal/esp32c5/include/hal/lpwdt_ll.h:30 LP_WDT_SWD_WKEY_VALUE 0x50D83AA1。
 *  asp3 も C5 で 0x8F1D312A の誤記を踏んで実施33 で訂正、C6 も段4 fix wave 1
 *  で同じ訂正をした）。
 *  レジスタオフセット（WDTCONFIG0 +0x48 / WDTWPROTECT +0x64、LP_WDT CONFIG0
 *  +0x00 / WPROTECT +0x18 / SWD_CONFIG +0x1c / SWD_WPROTECT +0x20、
 *  SWD_AUTO_FEED_EN bit18、SWD_DISABLE bit30）は 2026-09-15 に IDF ヘッダを
 *  コンパイルして確認（C6 と同一）。
 */
#define ESP32C5_TIMG_WDTCONFIG0(base)   ((base) + 0x48)
#define ESP32C5_TIMG_WDTWPROTECT(base)  ((base) + 0x64)
#define ESP32C5_TIMG_WDT_WKEY           0x50D83AA1U

#define ESP32C5_RTC_CNTL_WDTCONFIG0     ESP32C5_LP_WDT_CONFIG0
#define ESP32C5_RTC_CNTL_WDTWPROTECT    ESP32C5_LP_WDT_WPROTECT
#define ESP32C5_RTC_CNTL_WDT_WKEY       ESP32C5_LP_WDT_WDT_WKEY
#define ESP32C5_RTC_CNTL_SWD_CONF       ESP32C5_LP_WDT_SWD_CONFIG
#define ESP32C5_RTC_CNTL_SWD_WPROTECT   ESP32C5_LP_WDT_SWD_WPROTECT
#define ESP32C5_RTC_CNTL_SWD_WKEY       ESP32C5_LP_WDT_SWD_WKEY
#define ESP32C5_RTC_CNTL_SWD_AUTO_FEED_EN  (1U << 18)
#define ESP32C5_RTC_CNTL_SWD_DISABLE       (1U << 30)

#define ESP32C5_LP_WDT_CONFIG0          (ESP32C5_LP_WDT_BASE + 0x00)
#define ESP32C5_LP_WDT_WPROTECT         (ESP32C5_LP_WDT_BASE + 0x18)
#define ESP32C5_LP_WDT_WDT_WKEY         0x50D83AA1U
#define ESP32C5_LP_WDT_SWD_CONFIG       (ESP32C5_LP_WDT_BASE + 0x1c)
#define ESP32C5_LP_WDT_SWD_WPROTECT     (ESP32C5_LP_WDT_BASE + 0x20)
#define ESP32C5_LP_WDT_SWD_WKEY         0x50D83AA1U
#define ESP32C5_LP_WDT_SWD_AUTO_FEED_EN (1U << 18)

#endif /* TOPPERS_ESP32C5_H */
