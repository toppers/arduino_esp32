/*
 *  TOPPERS/FMP Kernel
 *      Flexible MultiProcessor Kernel
 *
 *  Copyright (C) 2024-2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  利用条件は TOPPERS ライセンス．無保証．
 */

/*
 *    シリアルインタフェースドライバのチップ依存部（ESP32-P4 USB-Serial/JTAG, 非TECS版）
 *
 *  ポーリング送信のみの最小実装．受信は未対応(-1を返す)，割込みコールバックは
 *  スタブ（CLIC+割込みマトリクスによる割込み駆動化は後続作業）．
 *  出力先は内蔵 USB-Serial/JTAG(CDC, /dev/ttyACM*)．UART0(GPIO37/38)ではなく
 *  こちらを使うことで，追加配線なしにホストでログが見える．
 */

#include <kernel.h>
#include <sil.h>
#include <t_syslog.h>
#include "target_syssvc.h"
#include "target_serial.h"
#include "esp32p4.h"

/*
 *  SIOポート管理ブロック
 */
struct sio_port_control_block {
    bool_t  openflag;       /* オープン済みフラグ */
    bool_t  snd_cbr;        /* 送信コールバック許可フラグ */
    bool_t  rcv_cbr;        /* 受信コールバック許可フラグ */
    EXINF   exinf;          /* serial.c の SPCB(sio_irdy_snd の引数) */
};

static SIOPCB siopcb_table[TNUM_PORT];

/*
 *  USB-Serial/JTAG の送信 FIFO に空きがあるか
 *    EP1_CONF の SERIAL_IN_EP_DATA_FREE が 1 のとき書込み可．
 */
Inline bool_t
usj_putready(void)
{
    return((sil_rew_mem(USJ_EP1_CONF) & USJ_SERIAL_IN_EP_DATA_FREE) != 0U);
}

/*
 *  SIOドライバの初期化
 */
void
sio_initialize(EXINF exinf)
{
    uint_t i;

    (void) exinf;
    for (i = 0; i < TNUM_PORT; i++) {
        siopcb_table[i].openflag = false;
        siopcb_table[i].snd_cbr  = false;
        siopcb_table[i].rcv_cbr  = false;
        siopcb_table[i].exinf    = 0;
    }
}

/*
 *  SIOドライバの終了処理
 */
void
sio_terminate(EXINF exinf)
{
    (void) exinf;
}

/*
 *  SIOの割込みサービスルーチン
 *    USB-Serial/JTAG の TX(IN_EMPTY)割込み．割込みをクリアし，送信可能
 *    コールバック(sio_irdy_snd)を呼んで serial.c に次の文字を送らせる．
 *    割込みマトリクス→CLIC外部線(INTNO_SIO)→FMP3 ISR の経路で起動される．
 */
void
sio_isr(EXINF exinf)
{
    SIOPCB  *p_siopcb = &(siopcb_table[0]);
    uint32_t st;

    (void) exinf;
    st = sil_rew_mem(USJ_INT_ST_REG);
    if ((st & USJ_SERIAL_OUT_RECV_PKT_INT) != 0U) {
        /*
         *  受信(OUT パケット受信)割込み．要求をクリア(W1C)し，受信通知
         *  コールバック(sio_irdy_rcv)を呼ぶ → serial.c が sio_rcv_chr で
         *  FIFO を読み取る．低レート対話入力前提のため先にクリアでよい．
         */
        sil_wrw_mem(USJ_INT_CLR_REG, USJ_SERIAL_OUT_RECV_PKT_INT);
        if (p_siopcb->rcv_cbr) {
            sio_irdy_rcv(p_siopcb->exinf);
        }
    }
    if ((st & USJ_SERIAL_IN_EMPTY_INT) != 0U) {
        /*  送信(IN エンドポイント空)割込み．要求をクリア(W1C)  */
        sil_wrw_mem(USJ_INT_CLR_REG, USJ_SERIAL_IN_EMPTY_INT);
        if (p_siopcb->snd_cbr) {
            /*  送信可能コールバック(serial.c が次の1文字を送る)  */
            sio_irdy_snd(p_siopcb->exinf);
        }
    }
}

/*
 *  SIOポートのオープン
 */
SIOPCB *
sio_opn_por(ID siopid, EXINF exinf)
{
    SIOPCB *p_siopcb = &(siopcb_table[siopid - 1]);

    p_siopcb->exinf    = exinf;       /* serial.c の SPCB を保存(sio_irdy_snd 用) */
    p_siopcb->openflag = true;

    /*
     *  USB-Serial/JTAG の割込みを一旦全クリア・全禁止．
     */
    sil_wrw_mem(USJ_INT_ENA_REG, 0U);
    sil_wrw_mem(USJ_INT_CLR_REG,
                USJ_SERIAL_IN_EMPTY_INT | USJ_SERIAL_OUT_RECV_PKT_INT);

    /*
     *  割込みマトリクス: USB-Serial/JTAG ソース(22) を CLIC 外部線 INTNO_SIO(16) へ
     *  割り付ける．書く値は割付先 CLIC 線番号そのもの(IDF interrupt_clic_ll_route と同形式)．
     *  シリアル(CLS_SERIAL=PRC1)は core0 で動くため CORE0 の割込みマトリクスに設定．
     *  CLIC 線 INTNO_SIO の有効化/優先度は CFG_INT(TA_ENAINT) 経由で設定済み．
     */
    sil_wrw_mem(INTMTX_MAP(0, USB_SERIAL_JTAG_INTR_SOURCE), (uint32_t) INTNO_SIO);

    return(p_siopcb);
}

/*
 *  SIOポートのクローズ
 */
void
sio_cls_por(SIOPCB *p_siopcb)
{
    p_siopcb->openflag = false;
}

/*
 *  SIOポートへの文字送信（ポーリング）
 *    送信できた場合 true，FIFO が一杯で送信できない場合 false．
 */
bool_t
sio_snd_chr(SIOPCB *p_siopcb, char c)
{
    /*
     *  TX FIFO に空きがあれば 1 文字書いて WR_DONE で flush し true．空きが
     *  無ければ false(送れない)を返す = 「試行1回」．serial.c は false を受けると
     *  送信可能コールバックを許可(sio_ena_cbr)し，USB の SERIAL_IN_EMPTY 割込み
     *  →sio_isr→sio_irdy_snd で続きを送る(割込み駆動)．低レベル出力
     *  (target_fput_log)は false の間ポーリングするのでそちらでも動く．
     *  WR_DONE を毎文字行うのは，空(SERIAL_IN_EMPTY)割込みを確実に発生させ
     *  割込み駆動を成立させるため．
     */
    (void) p_siopcb;
    if (usj_putready()) {
        sil_wrw_mem(USJ_EP1, (uint32_t)(uint8_t) c);
        sil_wrw_mem(USJ_EP1_CONF, USJ_WR_DONE);
        return(true);
    }
    return(false);
}

/*
 *  SIOポートからの文字受信（ポーリング）
 *    OUT エンドポイントに受信データがあれば EP1 から 1 バイト読んで返す．
 *    無ければ -1．割込み(sio_isr の RECV_PKT)で sio_irdy_rcv 通知後に
 *    serial.c がこれを呼んでバッファへ取り込む．
 */
int_t
sio_rcv_chr(SIOPCB *p_siopcb)
{
    (void) p_siopcb;
    if ((sil_rew_mem(USJ_EP1_CONF) & USJ_SERIAL_OUT_EP_DATA_AVAIL) != 0U) {
        return((int_t)(sil_rew_mem(USJ_EP1) & 0xFFU));
    }
    return(-1);
}

/*
 *  SIOポートからのコールバックの許可（スタブ）
 */
void
sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    if (cbrtn == SIO_RDY_SND) {
        p_siopcb->snd_cbr = true;
        /*  USB TX(IN_EMPTY)割込みを許可 → 空になると sio_isr が起動  */
        sil_wrw_mem(USJ_INT_ENA_REG,
                    sil_rew_mem(USJ_INT_ENA_REG) | USJ_SERIAL_IN_EMPTY_INT);
    }
    else if (cbrtn == SIO_RDY_RCV) {
        p_siopcb->rcv_cbr = true;
        /*  USB RX(OUT パケット受信)割込みを許可  */
        sil_wrw_mem(USJ_INT_ENA_REG,
                    sil_rew_mem(USJ_INT_ENA_REG) | USJ_SERIAL_OUT_RECV_PKT_INT);
    }
}

/*
 *  SIOポートからのコールバックの禁止（スタブ）
 */
void
sio_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
    if (cbrtn == SIO_RDY_SND) {
        p_siopcb->snd_cbr = false;
        /*  USB TX(IN_EMPTY)割込みを禁止  */
        sil_wrw_mem(USJ_INT_ENA_REG,
                    sil_rew_mem(USJ_INT_ENA_REG) & ~USJ_SERIAL_IN_EMPTY_INT);
    }
    else if (cbrtn == SIO_RDY_RCV) {
        p_siopcb->rcv_cbr = false;
        /*  USB RX(OUT パケット受信)割込みを禁止  */
        sil_wrw_mem(USJ_INT_ENA_REG,
                    sil_rew_mem(USJ_INT_ENA_REG) & ~USJ_SERIAL_OUT_RECV_PKT_INT);
    }
}
