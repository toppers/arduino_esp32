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
 * シリアルインタフェースドライバのチップ依存部（無印ESP32(Xtensa LX6)用）
 * 対話的シリアルコンソール用の割込み駆動SIOドライバ本体（非TECS版専用）
 *
 * ESP32-S3版 chip_serial_sio.c のLX6派生。無印ESP32はUSB-Serial-JTAGを
 * 持たないためUSJ分岐は削除し、UART0のみ対応。chip_serial.cfg が
 * sio_initialize/sio_terminate/sio_isr を登録する（S3と同型）。
 *
 * レジスタ配置（一次情報：ESP-IDF v5.5 soc/esp32/register/soc/uart_reg.h,
 * dport_reg.h, reg_base.h）：
 *   DR_REG_UART_BASE = 0x3FF40000 (UART0、S3の0x60000000と異なる)
 *   FIFO +0x0（書=送信/読=受信）、INT_ST +0x8、INT_ENA +0xC、INT_CLR +0x10、
 *   STATUS +0x1C（TXFIFO_CNT[23:16]=0xFF / RXFIFO_CNT[7:0]=0xFF）、
 *   CONF1 +0x24（RXFIFO_FULL_THRHD[6:0]=0x7F）。割込みビット：RXFIFO_FULL=bit0、
 *   TXFIFO_EMPTY=bit1、RXFIFO_TOUT=bit8。
 *   割込みマトリクス：UART0 source を CPU割込み線(USART_INTNO=INT5)へ配線する
 *   PRO-CPU側MAPレジスタ DPORT_PRO_UART_INTR_MAP_REG = DR_REG_DPORT_BASE(0x3FF00000)+0x18C
 *   （S3の INTR_CORE0_UART_MAP_REG=0x600C206C と体系が異なる）。
 *
 * ボーレート・クロック設定は行わない（ROM/QEMUが既定で有効化したUART0の
 * FIFOを使う。BPS_SETTINGはtarget_serial.hに用意済み。実機ブリングアップで要否再検証）。
 */

#include <kernel.h>
#include <sil.h>
#include <t_syslog.h>
#include "target_syssvc.h"
#include "target_serial.h"

#define ESP32_UART0_BASE            0x3FF40000U   /* DR_REG_UART_BASE (無印ESP32 UART0) */
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
#define UART_TXFIFO_CNT_V           0xFFU     /* 無印ESP32 TXFIFO_CNT[23:16] */
#define UART_RXFIFO_CNT_V           0xFFU     /* 無印ESP32 RXFIFO_CNT[7:0] */
#define UART_RXFIFO_THRHD_V         0x7FU     /* CONF1 RXFIFO_FULL_THRHD[6:0] */
#define UART_TXFIFO_MAX             100U      /* FIFOあふれ防止の安全側マージン（実サイズ128） */
#define UART_RXFIFO_FULL_THRHD_1    1U        /* 1文字受信で割込み */

/* 割込みマトリクス：UART0 source を CPU割込み線へマップする（PRO-CPU）。
 * DPORT_PRO_UART_INTR_MAP_REG = 0x3FF00000 + 0x18C。 */
#define DPORT_PRO_UART_INTR_MAP_REG 0x3FF0018CU

#define UART_REG(base, off)         ((uint32_t *)(uintptr_t)((base) + (off)))

struct sio_port_control_block {
    intptr_t    exinf;
    uint32_t    base;
    bool_t      opened;
};

static SIOPCB   siopcb[TNUM_PORT];
static const uint32_t bases[TNUM_PORT] = {
    ESP32_UART0_BASE,
};

/*
 * SIOドライバの初期化（ATT_INIで登録。UART0割込みをUSART_INTNOへマトリクス配線）
 */
void
sio_initialize(intptr_t exinf)
{
    uint_t  i;

    for (i = 0; i < TNUM_PORT; i++) {
        siopcb[i].opened = false;
        siopcb[i].exinf  = 0;
        siopcb[i].base   = bases[i];    /* 下の直書きガードが使う（open 前でも要る） */
    }
    /*
     *  2026-07-30: S3 側（`esp32s3/chip_serial_sio.c`）で**実機のパニックとして
     *  再現した窓**を、同型なのでこちらにも塞ぐ。
     *
     *  【窓】**ROM と 2nd-stage bootloader は UART0 をコンソールに使う**ので、
     *  カーネル起動時に **UART0 の INT_ENA / INT_RAW が残っていることがある**。
     *  下の MAP 書込みで INT5 へ配線した瞬間、**INT5 は CFG_INT で既に許可済み**なので
     *  保留割込みが入り、`sio_opn_por` 前なので `exinf == 0` ⇒
     *  `sio_irdy_rcv/snd` が NULL 参照する（`syssvc/serial.c:559`）。
     *
     *  【対処】**配線する前に止めて要因をクリアする。**順序が本質。
     *  LX6 では実機でパニックを再現していない（S3 で再現した型の予防的な対処である）。
     */
    sil_wrw_mem(UART_REG(bases[0], UART_INT_ENA_OFF), 0U);
    sil_wrw_mem(UART_REG(bases[0], UART_INT_CLR_OFF), 0xFFFFFFFFU);
    /* UART0のレベル割込みをCPU割込みINT5（USART_INTNO）へマップする。
       CFG_INTで既にINT5は許可済み。実際のUART側割込み許可はsio_ena_cbrで行う。 */
    sil_wrw_mem((uint32_t *)(uintptr_t)DPORT_PRO_UART_INTR_MAP_REG, (uint32_t) USART_INTNO);
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

    /* UART側の全割込みを一旦禁止・クリアし、1文字受信で割込むよう閾値設定 */
    sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), 0U);
    sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_CLR_OFF), 0xFFFFFFFFU);
    {
        uint32_t conf1 = sil_rew_mem(UART_REG(p_siopcb->base, UART_CONF1_OFF));
        conf1 = (conf1 & ~UART_RXFIFO_THRHD_V) | UART_RXFIFO_FULL_THRHD_1;
        sil_wrw_mem(UART_REG(p_siopcb->base, UART_CONF1_OFF), conf1);
    }
    p_siopcb->opened = true;

    return p_siopcb;
}

/*
 * SIOポートのクローズ
 */
void
sio_cls_por(SIOPCB *p_siopcb)
{
    sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), 0U);
    p_siopcb->opened = false;
    /*
     *  2026-07-31 追加（統合レビュー Part B の低重大度・S3 側と対称）:
     *  **close でも `exinf` を 0 へ戻す。**
     *  `sio_isr` の番人は `exinf == 0` を「まだ open していない」と読む。
     *  open 時にしか触らないと、**close 後に入った割込みが番人をすり抜けて
     *  もう有効でない SPCB を持って先へ進む**。open 前の窓と対称の窓なので、
     *  塞ぎ方も対称にする。
     */
    p_siopcb->exinf = 0;
}

/*
 * SIOポートへの文字送信（TX側に空きがあれば書いてtrue、非ブロッキング）
 */
bool_t
sio_snd_chr(SIOPCB *p_siopcb, char c)
{
    uint32_t cnt;

    cnt = (sil_rew_mem(UART_REG(p_siopcb->base, UART_STATUS_OFF)) >> UART_TXFIFO_CNT_S)
              & UART_TXFIFO_CNT_V;
    if (cnt < UART_TXFIFO_MAX) {
        sil_wrw_mem(UART_REG(p_siopcb->base, UART_FIFO_OFF), (uint32_t) (uint8_t) c);
        return true;
    }
    return false;
}

/*
 * SIOポートからの文字受信（受信データがあれば返す、無ければ-1）
 */
int_t
sio_rcv_chr(SIOPCB *p_siopcb)
{
    uint32_t cnt;

    cnt = sil_rew_mem(UART_REG(p_siopcb->base, UART_STATUS_OFF)) & UART_RXFIFO_CNT_V;
    if (cnt > 0U) {
        return (int_t)(sil_rew_mem(UART_REG(p_siopcb->base, UART_FIFO_OFF)) & 0xFFU);
    }
    return -1;
}

/*
 * SIOポートからのコールバックの許可（UART0側割込みENAを設定）
 */
void
sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    switch (cbrtn) {
        case SIO_RDY_SND:
            sil_orw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), UART_TXFIFO_EMPTY_INT);
            break;
        case SIO_RDY_RCV:
            sil_orw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF),
                        UART_RXFIFO_FULL_INT | UART_RXFIFO_TOUT_INT);
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
            sil_clrw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), UART_TXFIFO_EMPTY_INT);
            break;
        case SIO_RDY_RCV:
            sil_clrw(UART_REG(p_siopcb->base, UART_INT_ENA_OFF),
                        UART_RXFIFO_FULL_INT | UART_RXFIFO_TOUT_INT);
            break;
        default:
            break;
    }
}

/*
 * SIOの割込みサービスルーチン（CRE_ISRでUSART_INTNOに登録、demuxから呼ばれる）
 *
 * exinf = SIOPID_FPUT-1（ポートindex）。INT_STのうちENAされたビットを見て
 * 受信/送信可能コールバックを呼び、処理したビットをINT_CLRでクリアする
 * （レベル割込みのため要因クリアで解除）。
 */
/*
 *  開く前に割込みが入った回数（観測量）。**0 でないこと自体が異常**である。
 */
volatile uint32_t   sio_isr_unopened_count = 0U;

void
sio_isr(intptr_t exinf)
{
    SIOPCB   *p_siopcb = &siopcb[exinf];
    uint32_t  st, ena, active;

    /*
     *  2026-07-30 追加の番人（根治は `sio_initialize` 側・ここは二重の防御）。
     *  **黙って return しない**——数える／コンソールへ 1 回だけ直書きする／要因を止める。
     *  （`syslog` は使わない。ログタスク経由なのでこの時点では届かない。）
     */
    if (p_siopcb->exinf == 0) {
        static const char msg[] =
            "\r\n[SIO] open 前に割込みが入った（要因を止めた。sio_isr_unopened_count 参照）\r\n";
        uint_t  k;

        sio_isr_unopened_count++;
        sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_ENA_OFF), 0U);
        sil_wrw_mem(UART_REG(p_siopcb->base, UART_INT_CLR_OFF), 0xFFFFFFFFU);
        if (sio_isr_unopened_count == 1U) {
            for (k = 0; k < (sizeof(msg) - 1U); k++) {
                if (!sio_snd_chr(p_siopcb, msg[k])) {
                    break;              /* 詰まったら諦める（絶対に待たない） */
                }
            }
        }
        return;
    }

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
}
