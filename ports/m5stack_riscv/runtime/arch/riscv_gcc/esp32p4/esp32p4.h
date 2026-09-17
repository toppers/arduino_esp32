/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  アを使用・複製・改変・再配布することを無償で許諾する．（条件本文は
 *  TOPPERS ライセンスに従う．polarfire_soc.h と同一．）
 *
 *  本ソフトウェアは，無保証で提供されているものである．
 */

/*
 *    ESP32-P4 のハードウェア資源の定義
 *
 *  出典: docs/research/esp32p4_hw_reference.md, idf_riscv_intr_impl.md
 *        （ESP32-P4 TRM v1.3 + ESP-IDF v5.5 ソース）および Step1 実機測定．
 *  注: 「要確認」コメントの値は TRM/実機で最終確認すること．
 */
#ifndef TOPPERS_ESP32P4_H
#define TOPPERS_ESP32P4_H

/*
 *  CPU / タイマ周波数
 *    Step1 実機測定で mtime は CPU コアクロック(=360MHz)で歩進することを確認．
 *    HRTCNT はマイクロ秒単位 (mtimer.h: count / MTIMER_FREQ_MHZ)．
 *    注意: CPU 周波数を変更(DVFS)すると mtime レートも変わる．
 */
#define MTIMER_FREQ_MHZ    UINT_C(360)

/*
 *  CLINT （Core Local Interruptor）
 *    自コア基準アドレス．他コアは +0x10000 のエイリアスでアクセスする想定．
 *    pid にはプロセッサID(=mhartid, 0オリジン)が入る．
 *    要確認: 他コア(+0x10000)エイリアスの実在と書込み可否(idf_riscv_intr_impl.md)．
 */
#define CLINT_BASE             ULONG_C(0x20000000)
#define CLINT_CORE_STRIDE      ULONG_C(0x00010000)
/*
 *  pid は FMP3 のプロセッサID(prcid, 1オリジン: PRC1=1)．ESP32-P4 の PE は
 *  hart0/hart1 = prcid 1/2 で，CLINT のコア別エイリアスは hart 番号(0オリジン)
 *  basis なので (pid-1) でオフセットする．
 *  (これを誤ると master=prcid1 が core1 の mtimecmp/msip を操作してしまい，
 *   自コアのタイマ割込みが arm されず発火しない)
 */
#define CLINT_CORE_BASE(pid)   (CLINT_BASE + ((pid) - 1) * CLINT_CORE_STRIDE)

/*  msip（マシンソフトウェア割込み = IPI 用, CLIC 内部線3）  */
#define CLINT_MSIP(pid)        (uint32_t *)(CLINT_CORE_BASE(pid) + 0x0000UL)

/*
 *  Machine Timer（mtime / mtimecmp）．RV32 のため 32bit×2 に分割．
 *  common/mtimer.h(RV32分岐)が要求するマクロ:
 *    MTIMER_MTIME_L / MTIMER_MTIME_U / MACHINE_TIMER_MTIME_U
 *    MTIMER_MTIMECMP_L(pid) / MTIMER_MTIMECMP_U(pid)
 */
#define MTIMER_MTIME_L         (uint32_t *)(CLINT_BASE + 0xBFF8UL)
#define MTIMER_MTIME_U         (uint32_t *)(CLINT_BASE + 0xBFFCUL)
#define MACHINE_TIMER_MTIME_U  MTIMER_MTIME_U
/*
 *  mtimecmp は **自コア基準(CLINT_BASE)** で常に自プロセッサのものを指す．
 *  ESP32-P4 の CLINT はコアローカルで，各コアが 0x2000_4000 を読み書きすると
 *  「自コアの」mtimecmp になる．FMP3 は TOPPERS_SUPPORT_CONTROL_OTHER_HRT 未定義で
 *  mtimecmp は必ず自プロセッサ(prcid=自分)に対してのみ操作するため，pid によらず
 *  自コア基準でよい．(pid)*stride のオフセットを付けると core1 が自分でない
 *  アドレス(他コアエイリアス)を arm してしまい，自コアのタイマが発火しない．
 */
#define MTIMER_MTIMECMP_L(pid) ((void)(pid), (uint32_t *)(CLINT_BASE + 0x4000UL))
#define MTIMER_MTIMECMP_U(pid) ((void)(pid), (uint32_t *)(CLINT_BASE + 0x4004UL))

/*
 *  CLINT mtime 制御レジスタ (mtimectl)
 *    ESP32-P4 の mtime は PolarFire と違い常時稼働ではなく，mtimectl の
 *    MTIME_EN で明示的に有効化する必要がある（Core0 の mtimectl のみ有効,
 *    hw_reference.md: MTIME_EN/OVF/SAM）．FMP3 common の mtimer.c は有効化
 *    しないため，チップ依存部(chip_initialize)で有効化する．
 *    要確認: MTIME_EN のビット位置（TRM 未明記のため bit0 と仮定）．
 */
#define MTIMER_MTIMECTL        (uint32_t *)(CLINT_BASE + 0x4010UL)
#define MTIMER_MTIME_EN        (1UL << 0)

/*
 *  CLIC （Core-Local Interrupt Controller）
 *    ESP32-P4 は CLIC モード固定(mtvec.MODE=3, 実機 mtvec=0x4ff00003 で確認)．
 *    mie/mip/mideleg は無効．割込み許可は mstatus.MIE + CLIC 個別 IE．
 *    NLBITS=3．内部線: msip=3, mtime=7．外部割込みは線16〜47(+16オフセット)．
 */
#define CLIC_BASE              ULONG_C(0x20800000)
#define CLIC_INT_THRESH        (uint32_t *)(CLIC_BASE + 0x0008UL)  /* [31:24], read-back要 */
#define CLIC_CTRL_BASE         (CLIC_BASE + 0x1000UL)
/*  割込み i の制御レジスタ．CTL[31:24]/TRIG[18:17]/SHV[16]/IE[8]/IP[0]  */
#define CLIC_INT_CTRL(i)       (uint32_t *)(CLIC_CTRL_BASE + (i) * 4UL)
#define CLIC_NLBITS            UINT_C(3)
#define CLIC_EXT_OFFSET        UINT_C(16)     /* 外部割込みの線番号オフセット */
#define CLIC_INTNO_MSIP        UINT_C(3)      /* CLINT msip 由来 */
#define CLIC_INTNO_MTIMER      UINT_C(7)      /* CLINT mtime 由来 */
#define CSR_MTVT               0x307          /* CLIC ベクタテーブルベース CSR */

/*  CLIC_INT_CTRL レジスタのフィールド  */
#define CLIC_INT_IP_BIT        (1UL << 0)     /* pending */
#define CLIC_INT_TRIG_EDGE     (1UL << 17)    /* TRIG[18:17]=01 立上りエッジ(software raise可) */
#define CLIC_INT_IE_BIT        (1UL << 8)     /* enable  */
#define CLIC_INT_SHV_BIT       (1UL << 16)    /* selective hw vectoring */
#define CLIC_INT_CTL_SHIFT     24             /* 優先度/レベル CTL[31:24] */
#define CLIC_INT_THRESH_SHIFT  24             /* THRESH[31:24] */

/*
 *  CLIC 割込み線の総数（内部0-15 + 外部16-47）．
 *  FMP3 の割込み番号(INTNO)は本ファイルで CLIC 線番号と同一に扱う．
 */
#define CLIC_TNUM_INTNO        UINT_C(48)

/*
 *  割込みマトリクス（Interrupt Matrix, PLIC 相当）
 *    ペリフェラル割込みソースをコアの CLIC 線へ割り付ける．
 *    REG(core_base + 4*source) に (割付先CLIC線) を書く．書いたコア側へ配線．
 *    要確認: 書込む値が (line+16) か CLIC線番号そのものか(idf_riscv_intr_impl.md)．
 */
#define INTMTX_CORE0_BASE      ULONG_C(0x500D6000)
#define INTMTX_CORE1_BASE      ULONG_C(0x500D6800)
#define INTMTX_CORE_BASE(pid)  ((pid) == 0 ? INTMTX_CORE0_BASE : INTMTX_CORE1_BASE)
#define INTMTX_MAP(pid, src)   (uint32_t *)(INTMTX_CORE_BASE(pid) + (src) * 4UL)

/*
 *  コンソール UART0
 *    U0TXD=GPIO37 / U0RXD=GPIO38．ブートローダ初期化済み前提でポーリング送信に使う．
 *    STATUS の TXFIFO_CNT[23:16] が 0 でないことを確認して FIFO へ書く．
 */
#define UART0_BASE             ULONG_C(0x500CA000)
#define UART0_FIFO             (uint32_t *)(UART0_BASE + 0x00UL)
#define UART0_STATUS           (uint32_t *)(UART0_BASE + 0x1CUL)
#define UART0_CONF0            (uint32_t *)(UART0_BASE + 0x20UL)
#define UART0_CLKDIV           (uint32_t *)(UART0_BASE + 0x14UL)
#define UART0_CLK_CONF         (uint32_t *)(UART0_BASE + 0x88UL)
#define UART0_STATUS_TXFIFO_CNT_S  16
#define UART0_STATUS_TXFIFO_CNT_M  0xFFUL

/*
 *  USB Serial/JTAG コントローラ
 *    本ボードのコンソールは内蔵 USB-Serial/JTAG(303a:1001) の CDC．
 *    UART0(GPIO37/38)ではなくこちらに出力するとホスト(/dev/ttyACM*)で見える．
 *    base = HPPERIPH1(0x500C0000) + 0x12000 = 0x500D2000 (ESP-IDF reg_base.h)．
 *    EP1(+0x0): FIFO 書込(RDWR_BYTE[7:0])．
 *    EP1_CONF(+0x4): bit0 WR_DONE(1書込でパケット flush), bit1 SERIAL_IN_EP_DATA_FREE(RO,1=空き)．
 */
#define USJ_BASE                    ULONG_C(0x500D2000)
#define USJ_EP1                     (uint32_t *)(USJ_BASE + 0x00UL)
#define USJ_EP1_CONF                (uint32_t *)(USJ_BASE + 0x04UL)
#define USJ_WR_DONE                 (1UL << 0)
#define USJ_SERIAL_IN_EP_DATA_FREE  (1UL << 1)
/*  割込みレジスタ(usb_serial_jtag_reg.h): ST=+0xC, ENA=+0x10, CLR=+0x14．
 *  SERIAL_IN_EMPTY_INT(bit3)= IN(TX)エンドポイントが空=次データ書込み可．  */
#define USJ_INT_ST_REG              (uint32_t *)(USJ_BASE + 0x0CUL)
#define USJ_INT_ENA_REG            (uint32_t *)(USJ_BASE + 0x10UL)
#define USJ_INT_CLR_REG            (uint32_t *)(USJ_BASE + 0x14UL)
#define USJ_SERIAL_IN_EMPTY_INT     (1UL << 3)
/*  RX(OUT): SERIAL_OUT_EP_DATA_AVAIL(EP1_CONF bit2)=受信1バイト読込可，
 *  SERIAL_OUT_RECV_PKT_INT(INT bit2)=ホストから OUT パケット受信．  */
#define USJ_SERIAL_OUT_EP_DATA_AVAIL (1UL << 2)
#define USJ_SERIAL_OUT_RECV_PKT_INT  (1UL << 2)
/*  割込みマトリクスのソース番号(soc/interrupts.h の列挙序数, ETS_LP_UART=16 起算で 22)  */
#define USB_SERIAL_JTAG_INTR_SOURCE UINT_C(22)

/*
 *  2 コア目(HP core1)の起動用レジスタ（idf_riscv_intr_impl.md より, 要確認）
 *    手順: clk_en set → rst 解除(clr) → ROM API ets_set_appcpu_boot_addr() →
 *          PMU unstall(0x86→0xFF)
 */
#define HP_CORE1_CLK_EN_REG    (uint32_t *)ULONG_C(0x500E6014)  /* bit4 set */
#define HP_CORE1_CLK_EN_BIT    (1UL << 4)
#define HP_CORE1_RST_REG       (uint32_t *)ULONG_C(0x500E60C0)  /* bit8 clear で解除 */
#define HP_CORE1_RST_BIT       (1UL << 8)

#endif /* TOPPERS_ESP32P4_H */
