/*
 *  TOPPERS Software
 *      Toyohashi Open Platform for Embedded Real-Time Systems
 *
 *  Copyright (C) 2006-2020 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 *
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 *
 */

/*
 * シリアルインタフェースドライバのチップ依存部（ESP32-S3用）
 * 対話的シリアルコンソール用の割込み駆動SIOドライバ本体（非TECS版専用）
 *
 * chip_serial.c（target_fput_logのみ、全ビルド常時
 * リンク）から分離。本ファイルはsyssvc/serial.cのsio_irdy_rcv/
 * sio_irdy_sndを参照するため、実際に対話コンソールを使うビルド
 * （sample1等、syssvc/serial.cfgをINCLUDEしCONFIG_OPTに
 * `-S chip_serial_sio.o`を追加したもの）でのみリンクする。
 *
 * sample1のserial_rea_dat/serial_wri_dat（syssvc/serial.c経由）が使う。
 * UART0のレベル割込みを割込みマトリクスでCPU割込みINT5（USART_INTNO、
 * EXTERN_LEVEL・レベル1）へ配線し、CFG_INT/CRE_ISR（chip_serial.cfg）で
 * sio_isrをディスパッチする。RP2350版chip_serial.cのSIO枠組みを踏襲。
 *
 * レジスタ配置はESP-IDFのSDKヘッダで確認（一次情報）：
 *   components/soc/esp32s3/register/soc/uart_reg.h / interrupt_core0_reg.h
 *   DR_REG_UART_BASE = 0x60000000 (UART0)
 *   FIFO +0x0（書=送信/読=受信）、INT_RAW +0x4、INT_ST +0x8、INT_ENA +0xc、
 *   INT_CLR +0x10、STATUS +0x1c（RXFIFO_CNT[9:0]/TXFIFO_CNT[25:16]）、
 *   CONF1 +0x24（RXFIFO_FULL_THRHD[9:0]）。割込みビット：RXFIFO_FULL=bit0、
 *   TXFIFO_EMPTY=bit1、RXFIFO_TOUT=bit8。
 *   割込みマトリクス CORE0 UART MAP = DR_REG_INTERRUPT_CORE0_BASE(0x600C2000)+0x6c。
 *
 * ボーレート・クロック設定は行わない（ROM/QEMUが既定で有効化したUART0の
 * FIFOを使う）。実機ブリングアップでボーレート初期化の要否を
 * 再検証する（BPS_SETTINGはtarget_serial.hに用意済み）。
 *
 * ビルド時オプション`TOPPERS_S3_CONSOLE_USJ`指定時は
 * 対話コンソールの実体をUART0からUSB-Serial-JTAG（USJ）へ切り替える。
 * 公開シンボル（sio_initialize等）は同一のまま、内部実装のみ`#ifdef`で
 * 分岐する。未定義時は本ファイルの動作は変更前と完全に同一（既定ビルド
 * の非回帰、詳細設計 
 * design-usj-console.md §3〜§5参照）。
 *
 * USJの割込みソース（ETS_USB_SERIAL_JTAG_INTR_SOURCE=96）はCORE0の
 * INTERRUPT_CORE0_USB_DEVICE_INT_MAP_REG（0x600C2180）でCPU割込み線へ
 * 配線する。CPU割込み線はINT17（USART_INTNO、target_serial.hで切替、
 * EXTERN_LEVEL・レベル1・blob動的選択範囲1〜15外・BT用INT23/27とも別）を
 * 使う。
 *
 * USJのISRは受信パケット到着（SERIAL_OUT_RECV_PKT）がパケット単位の
 * イベントであるため、UART0のsio_isr（要因クリア→処理）とは順序が逆で、
 * 「要因クリア→FIFOが空になるまでコールバックを繰り返す」必要がある
 * （C6参照実装踏襲、取りこぼし防止）。また、SERIAL_IN_EMPTY_INT_RAWは
 * リセット直後のデフォルト値が1のため、sio_opn_por（旧来のUART0では
 * 割込み禁止状態を作るだけの処理）でクリアしておかないと偽の送信可能
 * コールバックを生む（design-usj-console.md §5リスク表）。
 */

#include <kernel.h>
#include <sil.h>
#include <t_syslog.h>
#include "target_syssvc.h"
#include "target_serial.h"

#ifdef TOPPERS_S3_CONSOLE_USJ
#include "esp32s3_usbjtag.h"
#endif /* TOPPERS_S3_CONSOLE_USJ */

#define ESP32S3_UART0_BASE          0x60000000U
#define UART_FIFO_OFF               0x00U
#define UART_INT_ST_OFF             0x08U
#define UART_INT_ENA_OFF            0x0cU
#define UART_INT_CLR_OFF            0x10U
#define UART_STATUS_OFF             0x1cU
#define UART_CONF1_OFF              0x24U

#define UART_RXFIFO_FULL_INT        (1U << 0)
#define UART_TXFIFO_EMPTY_INT       (1U << 1)
#define UART_RXFIFO_TOUT_INT        (1U << 8)

#define UART_TXFIFO_CNT_S           16U
#define UART_TXFIFO_CNT_V           0x3FFU
#define UART_RXFIFO_CNT_V           0x3FFU
#define UART_TXFIFO_MAX             100U   /* FIFOあふれ防止の安全側マージン（実サイズ128） */
#define UART_RXFIFO_FULL_THRHD_1    1U     /* 1文字受信で割込み */

/* 割込みマトリクス：CORE0のUART0 sourceをCPU割込み線へマップするレジスタ */
#define INTR_CORE0_UART_MAP_REG     0x600C206CU

#ifdef TOPPERS_S3_CONSOLE_USJ
/* 割込みマトリクス：CORE0のUSB-Serial-JTAG(USB_DEVICE) sourceを
   CPU割込み線へマップするレジスタ（INTERRUPT_CORE0_USB_DEVICE_INT_MAP_REG） */
#define INTR_CORE0_USBJTAG_MAP_REG  0x600C2180U
#endif /* TOPPERS_S3_CONSOLE_USJ */

#define UART_REG(base, off)         ((uint32_t *)(uintptr_t)((base) + (off)))

struct sio_port_control_block {
    intptr_t    exinf;
    uint32_t    base;
    bool_t      opened;
};

static SIOPCB   siopcb[TNUM_PORT];
static const uint32_t bases[TNUM_PORT] = {
#ifdef TOPPERS_S3_CONSOLE_USJ
    ESP32S3_USBJTAG_BASE,
#else /* TOPPERS_S3_CONSOLE_USJ */
    ESP32S3_UART0_BASE,
#endif /* TOPPERS_S3_CONSOLE_USJ */
};

/*
 * SIOドライバの初期化（ATT_INIで登録。UART0/USJ割込みをUSART_INTNOへ
 * マトリクス配線）
 */
void
sio_initialize(intptr_t exinf)
{
    uint_t  i;

    for (i = 0; i < TNUM_PORT; i++) {
        siopcb[i].opened = false;
    }
#ifdef TOPPERS_S3_CONSOLE_USJ
    /*
     * A software reset can leave USB Serial/JTAG interrupts enabled by the
     * preceding application.  Disable and acknowledge them before routing
     * the source: serial_initialize() has not installed callback exinf yet.
     */
    sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG, 0U);
    sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_INT_CLR_REG,
                    ESP32S3_USBJTAG_INT_OUT_RECV_PKT
                    | ESP32S3_USBJTAG_INT_IN_EMPTY);
    /* USJのレベル割込みをCPU割込みINT17（USART_INTNO）へマップする。
       CFG_INTで既にINT17は許可済み。実際のUSJ側割込み許可はsio_ena_cbrで行う。 */
    sil_wrw_mem((uint32_t *)(uintptr_t)INTR_CORE0_USBJTAG_MAP_REG, (uint32_t) USART_INTNO);
#else /* TOPPERS_S3_CONSOLE_USJ */
    /* UART0のレベル割込みをCPU割込みINT5（USART_INTNO）へマップする。
       CFG_INTで既にINT5は許可済み。実際のUART側割込み許可はsio_ena_cbrで行う。 */
    sil_wrw_mem((uint32_t *)(uintptr_t)INTR_CORE0_UART_MAP_REG, (uint32_t) USART_INTNO);
#endif /* TOPPERS_S3_CONSOLE_USJ */
}

/*
 * SIOドライバの終了処理
 */
void
sio_terminate(intptr_t exinf)
{
}

/*
 * SIOポートのオープン
 */
SIOPCB *
sio_opn_por(ID siopid, intptr_t exinf)
{
    const uint32_t  index = siopid - 1;
    SIOPCB         *p_siopcb = &siopcb[index];

    p_siopcb->exinf = exinf;
    p_siopcb->base = bases[index];

#ifdef TOPPERS_S3_CONSOLE_USJ
    /* USJ側の全割込みを一旦禁止・クリアする。SERIAL_IN_EMPTY_INT_RAWは
       リセット直後のデフォルト値が1のため、ここで明示的にクリアしないと
       ena_cbr(SIO_RDY_SND)直後に偽の送信可能コールバックが発生する。 */
    sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG, 0U);
    sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_INT_CLR_REG,
                    ESP32S3_USBJTAG_INT_OUT_RECV_PKT | ESP32S3_USBJTAG_INT_IN_EMPTY);
#else /* TOPPERS_S3_CONSOLE_USJ */
    /* UART側の全割込みを一旦禁止・クリアし、1文字受信で割込むよう閾値設定 */
    sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), 0U);
    sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_CLR_OFF), 0xFFFFFFFFU);
    {
        uint32_t conf1 = sil_rew_mem(UART_REG(p_siopcb->base, UART_CONF1_OFF));
        conf1 = (conf1 & ~UART_RXFIFO_CNT_V) | UART_RXFIFO_FULL_THRHD_1;
        sil_wrw_mem(UART_REG(p_siopcb->base, UART_CONF1_OFF), conf1);
    }
#endif /* TOPPERS_S3_CONSOLE_USJ */
    p_siopcb->opened = true;

    return p_siopcb;
}

/*
 * SIOポートのクローズ
 */
void
sio_cls_por(SIOPCB *p_siopcb)
{
#ifdef TOPPERS_S3_CONSOLE_USJ
    sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG, 0U);
#else /* TOPPERS_S3_CONSOLE_USJ */
    sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), 0U);
#endif /* TOPPERS_S3_CONSOLE_USJ */
    p_siopcb->opened = false;
}

/*
 * SIOポートへの文字送信（TX側に空きがあれば書いてtrue、非ブロッキング）
 */
bool_t
sio_snd_chr(SIOPCB *p_siopcb, char c)
{
#ifdef TOPPERS_S3_CONSOLE_USJ
    /* USJ既知の限界：ホストが読まない間はDATA_FREEが戻らずfalseが続く。
       呼出し元（syssvc/serial.cのserial_wri_dat）は待ちに入るが、システム
       停止はしない（design-usj-console.md §4(b)）。 */
    if (esp32s3_usbjtag_putready()) {
        esp32s3_usbjtag_putchar(c);
        return true;
    }
    return false;
#else /* TOPPERS_S3_CONSOLE_USJ */
    uint32_t cnt;

    cnt = (sil_rew_mem(UART_REG(p_siopcb->base, UART_STATUS_OFF)) >> UART_TXFIFO_CNT_S)
              & UART_TXFIFO_CNT_V;
    if (cnt < UART_TXFIFO_MAX) {
        sil_wrw_mem(UART_REG(p_siopcb->base, UART_FIFO_OFF), (uint32_t) (uint8_t) c);
        return true;
    }
    return false;
#endif /* TOPPERS_S3_CONSOLE_USJ */
}

/*
 * SIOポートからの文字受信（受信データがあれば返す、無ければ-1）
 */
int_t
sio_rcv_chr(SIOPCB *p_siopcb)
{
#ifdef TOPPERS_S3_CONSOLE_USJ
    if (esp32s3_usbjtag_getready()) {
        return (int_t) esp32s3_usbjtag_getchar();
    }
    return -1;
#else /* TOPPERS_S3_CONSOLE_USJ */
    uint32_t cnt;

    cnt = sil_rew_mem(UART_REG(p_siopcb->base, UART_STATUS_OFF)) & UART_RXFIFO_CNT_V;
    if (cnt > 0U) {
        return (int_t)(sil_rew_mem(UART_REG(p_siopcb->base, UART_FIFO_OFF)) & 0xFFU);
    }
    return -1;
#endif /* TOPPERS_S3_CONSOLE_USJ */
}

/*
 * SIOポートからのコールバックの許可（UART0/USJ側割込みENAを設定）
 */
void
sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    switch (cbrtn) {
        case SIO_RDY_SND:
#ifdef TOPPERS_S3_CONSOLE_USJ
            sil_orw((uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG, ESP32S3_USBJTAG_INT_IN_EMPTY);
#else /* TOPPERS_S3_CONSOLE_USJ */
            sil_orw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), UART_TXFIFO_EMPTY_INT);
#endif /* TOPPERS_S3_CONSOLE_USJ */
            break;
        case SIO_RDY_RCV:
#ifdef TOPPERS_S3_CONSOLE_USJ
            sil_orw((uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG, ESP32S3_USBJTAG_INT_OUT_RECV_PKT);
#else /* TOPPERS_S3_CONSOLE_USJ */
            sil_orw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF),
                        UART_RXFIFO_FULL_INT | UART_RXFIFO_TOUT_INT);
#endif /* TOPPERS_S3_CONSOLE_USJ */
            break;
        default:
            break;
    }
}

/*
 * SIOポートからのコールバックの禁止
 */
void
sio_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    switch (cbrtn) {
        case SIO_RDY_SND:
#ifdef TOPPERS_S3_CONSOLE_USJ
            sil_clrw((uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG, ESP32S3_USBJTAG_INT_IN_EMPTY);
#else /* TOPPERS_S3_CONSOLE_USJ */
            sil_clrw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), UART_TXFIFO_EMPTY_INT);
#endif /* TOPPERS_S3_CONSOLE_USJ */
            break;
        case SIO_RDY_RCV:
#ifdef TOPPERS_S3_CONSOLE_USJ
            sil_clrw((uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG, ESP32S3_USBJTAG_INT_OUT_RECV_PKT);
#else /* TOPPERS_S3_CONSOLE_USJ */
            sil_clrw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF),
                        UART_RXFIFO_FULL_INT | UART_RXFIFO_TOUT_INT);
#endif /* TOPPERS_S3_CONSOLE_USJ */
            break;
        default:
            break;
    }
}

/*
 * SIOの割込みサービスルーチン（CRE_ISRでUSART_INTNOに登録、demuxから呼ばれる）
 *
 * exinf = SIOPID_FPUT-1（ポートindex）。
 *
 * UART0：INT_STのうちENAされたビットを見て受信/送信可能コールバックを呼び、
 * 処理したビットをINT_CLRでクリアする（レベル割込みのため要因クリアで解除）。
 *
 * USJ：UART0とは順序が逆で「要因クリア→コールバック」にする必要がある
 * （SERIAL_OUT_RECV_PKTはパケット到着ごとのイベント型で、クリアせず処理を
 * 続けると新規パケット到着を取りこぼす）。OUT_RECV_PKTは1パケットに複数
 * 文字を含み得るため、クリア後にFIFOが空になるまでコールバックを繰り返す
 * （C6参照実装踏襲、design-usj-console.md §5参照）。
 */
void
sio_isr(intptr_t exinf)
{
    SIOPCB   *p_siopcb = &siopcb[exinf];
#ifdef TOPPERS_S3_CONSOLE_USJ
    uint32_t  st, ena, active;

    st  = sil_rew_mem((const uint32_t *) ESP32S3_USBJTAG_INT_ST_REG);
    ena = sil_rew_mem((const uint32_t *) ESP32S3_USBJTAG_INT_ENA_REG);
    active = st & ena;

    if ((active & ESP32S3_USBJTAG_INT_IN_EMPTY) != 0U) {
        sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_INT_CLR_REG, ESP32S3_USBJTAG_INT_IN_EMPTY);
        sio_irdy_snd(p_siopcb->exinf);
    }
    if ((active & ESP32S3_USBJTAG_INT_OUT_RECV_PKT) != 0U) {
        sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_INT_CLR_REG, ESP32S3_USBJTAG_INT_OUT_RECV_PKT);
        while (esp32s3_usbjtag_getready()) {
            sio_irdy_rcv(p_siopcb->exinf);
        }
    }
#else /* TOPPERS_S3_CONSOLE_USJ */
    uint32_t  st, ena, active;

    st  = sil_rew_mem(UART_REG(p_siopcb->base, UART_INT_ST_OFF));
    ena = sil_rew_mem(UART_REG(p_siopcb->base, UART_INT_ENA_OFF));
    active = st & ena;

    if ((active & (UART_RXFIFO_FULL_INT | UART_RXFIFO_TOUT_INT)) != 0U) {
        sio_irdy_rcv(p_siopcb->exinf);
    }
    if ((active & UART_TXFIFO_EMPTY_INT) != 0U) {
        sio_irdy_snd(p_siopcb->exinf);
    }
    /* 処理した要因をクリア（レベル割込みの解除） */
    sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_CLR_OFF),
                    active & (UART_RXFIFO_FULL_INT | UART_RXFIFO_TOUT_INT | UART_TXFIFO_EMPTY_INT));
#endif /* TOPPERS_S3_CONSOLE_USJ */
}
