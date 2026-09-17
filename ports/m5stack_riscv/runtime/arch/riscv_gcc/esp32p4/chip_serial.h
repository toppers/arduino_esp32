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
 *    シリアルインタフェースドライバのチップ依存部（ESP32-P4 UART0）
 *
 *  Step3 初手はポーリング送信のみ（受信・割込みコールバックはスタブ）．
 *  ブートローダが UART0 のクロック/ピン/ボーレートを設定済みである前提．
 */

#ifndef TOPPERS_CHIP_SERIAL_H
#define TOPPERS_CHIP_SERIAL_H

/*
 *  コールバックの種類（sio_ena_cbr/sio_dis_cbr の cbrtn）
 */
#define SIO_RDY_SND  1U     /* 送信可能コールバック */
#define SIO_RDY_RCV  2U     /* 受信通知コールバック */

#ifndef TOPPERS_MACRO_ONLY

/*
 *  SIOポート管理ブロックの前方宣言（実体は chip_serial.c）
 */
typedef struct sio_port_control_block SIOPCB;

extern void    sio_initialize(EXINF exinf);
extern void    sio_terminate(EXINF exinf);
extern void    sio_isr(EXINF exinf);
extern SIOPCB *sio_opn_por(ID siopid, EXINF exinf);
extern void    sio_cls_por(SIOPCB *p_siopcb);
extern bool_t  sio_snd_chr(SIOPCB *p_siopcb, char c);
extern int_t   sio_rcv_chr(SIOPCB *p_siopcb);
extern void    sio_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn);
extern void    sio_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn);
extern void    sio_irdy_snd(EXINF exinf);
extern void    sio_irdy_rcv(EXINF exinf);

#endif /* TOPPERS_MACRO_ONLY */
#endif /* TOPPERS_CHIP_SERIAL_H */
