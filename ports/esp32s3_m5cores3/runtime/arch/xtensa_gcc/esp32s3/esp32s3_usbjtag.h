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
 * ESP32-S3 USB-Serial-JTAG（USJ）コンソール用 レジスタ定義＋プリミティブ
 * （非TECS版専用）
 *
 * コンソールをUART0の代わりにUSB-Serial-JTAG（ホストからはCDC-ACMの
 * シリアルポートとして見える）へ切り替えるビルド時オプション
 * （TOPPERS_S3_CONSOLE_USJ）用。1ポートしか持たない実機（M5Stack系ボード等）
 * でのコンソール確保を目的とする。
 *
 * 本ファイルはレジスタ定義とプリミティブ（putready/getready/putchar/
 * getchar）のみを提供する。SIOドライバとしての結線（sio_opn_por等）は
 * chip_serial_sio.c側、低レベルsyslog出力はchip_serial.c側で行う。
 *
 * レジスタ配置はESP-IDFのSDKヘッダで確認済み（一次情報）：
 *   components/soc/esp32s3/register/soc/usb_serial_jtag_reg.h
 *   DR_REG_USB_SERIAL_JTAG_BASE = 0x60038000
 *   EP1 = +0x00（送受信FIFO、1バイト単位）
 *   EP1_CONF = +0x04（bit0=WR_DONE書込みで送出、bit1=SERIAL_IN_EP_DATA_FREE
 *              読出しで送信可否、bit2=SERIAL_OUT_EP_DATA_AVAIL読出しで受信有無）
 *   INT_RAW/ST/ENA/CLR = +0x08/+0x0c/+0x10/+0x14
 *   （bit2=SERIAL_OUT_RECV_PKT、bit3=SERIAL_IN_EMPTY）
 * ESP32-S3のUSJレジスタはESP32-C6と完全同一レイアウト（ベースアドレス
 * のみ異なる）であることを確認済み。
 *
 * 初期化は不要（USJはリセット後デフォルトで動作、ボーレート概念なし）。
 *
 * ホスト未接続時の注意：ホスト（端末プログラム）がパケットを読み出さ
 * ないとSERIAL_IN_EP_DATA_FREEが0のまま戻らない。ポーリング送信側
 * （chip_serial.c/chip_serial_sio.c）はリトライ上限を設けて出力を捨てる
 * ことで、システムを停止させない。
 */

#ifndef TOPPERS_ESP32S3_USBJTAG_H
#define TOPPERS_ESP32S3_USBJTAG_H

#include <sil.h>

#define ESP32S3_USBJTAG_BASE            0x60038000U

#define ESP32S3_USBJTAG_EP1_REG          (ESP32S3_USBJTAG_BASE + 0x00U)
#define ESP32S3_USBJTAG_EP1_CONF_REG     (ESP32S3_USBJTAG_BASE + 0x04U)
#define ESP32S3_USBJTAG_INT_RAW_REG      (ESP32S3_USBJTAG_BASE + 0x08U)
#define ESP32S3_USBJTAG_INT_ST_REG       (ESP32S3_USBJTAG_BASE + 0x0cU)
#define ESP32S3_USBJTAG_INT_ENA_REG      (ESP32S3_USBJTAG_BASE + 0x10U)
#define ESP32S3_USBJTAG_INT_CLR_REG      (ESP32S3_USBJTAG_BASE + 0x14U)

/* EP1_CONFのフィールド */
#define ESP32S3_USBJTAG_WR_DONE                    (1U << 0)  /* WT: 書込みでパケット送出 */
#define ESP32S3_USBJTAG_SERIAL_IN_EP_DATA_FREE      (1U << 1)  /* RO: 送信FIFOに空きあり */
#define ESP32S3_USBJTAG_SERIAL_OUT_EP_DATA_AVAIL    (1U << 2)  /* RO: 受信データあり */

/* INT_{RAW,ST,ENA,CLR}共通のビット位置 */
#define ESP32S3_USBJTAG_INT_OUT_RECV_PKT   (1U << 2)  /* 受信パケット到着 */
#define ESP32S3_USBJTAG_INT_IN_EMPTY       (1U << 3)  /* 送信FIFOエンプティ（送信完了） */

/*
 * 送信バッファに空きがあるか（EP1_CONF.SERIAL_IN_EP_DATA_FREE）
 */
static inline bool_t
esp32s3_usbjtag_putready(void)
{
    return (sil_rew_mem((const uint32_t *) ESP32S3_USBJTAG_EP1_CONF_REG)
                & ESP32S3_USBJTAG_SERIAL_IN_EP_DATA_FREE) != 0U;
}

/*
 * 1文字送信（putready確認済み前提。EP1へ書込み後WR_DONEで即送出）
 */
static inline void
esp32s3_usbjtag_putchar(char c)
{
    sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_EP1_REG, (uint32_t) (uint8_t) c);
    sil_wrw_mem((uint32_t *) ESP32S3_USBJTAG_EP1_CONF_REG, ESP32S3_USBJTAG_WR_DONE);
}

/*
 * 受信バッファに文字があるか（EP1_CONF.SERIAL_OUT_EP_DATA_AVAIL）
 */
static inline bool_t
esp32s3_usbjtag_getready(void)
{
    return (sil_rew_mem((const uint32_t *) ESP32S3_USBJTAG_EP1_CONF_REG)
                & ESP32S3_USBJTAG_SERIAL_OUT_EP_DATA_AVAIL) != 0U;
}

/*
 * 受信した1文字の取出し（getready確認済み前提）
 */
static inline uint8_t
esp32s3_usbjtag_getchar(void)
{
    return (uint8_t) sil_rew_mem((const uint32_t *) ESP32S3_USBJTAG_EP1_REG);
}

#endif /* TOPPERS_ESP32S3_USBJTAG_H */
