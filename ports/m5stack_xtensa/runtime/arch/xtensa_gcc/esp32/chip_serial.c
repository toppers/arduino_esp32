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
 * シリアルインタフェースドライバのチップ依存部（無印ESP32(Xtensa LX6)
 * QEMU PoC用）
 * （非TECS版専用）
 *
 * [2026-07-13新規] esp32s3/chip_serial.cを雛形にコピーし、UART0ベース
 * アドレス・TXFIFO_CNTフィールド幅のみ差し替えた（オフセット構造自体は
 * S3と共通、design.md §6参照）。本PoCはUSJ（USB-Serial-JTAG）を持たず
 * QEMU専用のためUSJ分岐は移植していない。
 *
 * 本ファイルはsyslog/banner用の低レベル文字出力（target_fput_log、
 * ポーリング送信）のみを提供する。Makefile.chip（KERNEL_COBJS/
 * SYSSVC_COBJS）が全ビルドへ無条件でリンクするため、他のsyssvcモジュール
 * （syssvc/serial.c等）への依存を持たせてはならない。
 *
 * レジスタ配置はESP-IDFのSDKヘッダで確認済み（一次情報）：
 *   ~/tools/esp-idf/components/soc/esp32/register/soc/{reg_base,uart_reg}.h
 *   DR_REG_UART_BASE = 0x3ff40000 (UART0、旧世代APBペリフェラルマップ)
 *   UART_FIFO_REG(i) = REG_UART_BASE(i) + 0x0   （1バイト書き込みで送信）
 *   UART_STATUS_REG(i) = REG_UART_BASE(i) + 0x1c、bit[23:16] = UART_TXFIFO_CNT
 *   （ビット位置16はS3と共通、フィールド幅はLX6が8bit・S3が10bitで異なる）
 *
 * 本ファイルではUART0のボーレート・クロック設定は行わない（ROM/QEMUが
 * 既定で有効化した状態のFIFOへポーリング書き込みするのみ）。
 */

#include <sil.h>

#define ESP32_UART0_BASE       0x3ff40000U
#define ESP32_UART_FIFO_REG    (ESP32_UART0_BASE + 0x0U)
#define ESP32_UART_STATUS_REG  (ESP32_UART0_BASE + 0x1cU)
#define ESP32_UART_TXFIFO_CNT_S   16U
#define ESP32_UART_TXFIFO_CNT_V   0xFFU
#define ESP32_UART_TXFIFO_MAX     100U   /* FIFOあふれ防止のための安全側マージン（実サイズ128） */

static void esp32_uart0_putc(char c)
{
    uint32_t cnt;

    do {
        cnt = (sil_rew_mem((const uint32_t *)ESP32_UART_STATUS_REG) >> ESP32_UART_TXFIFO_CNT_S)
              & ESP32_UART_TXFIFO_CNT_V;
    } while (cnt >= ESP32_UART_TXFIFO_MAX);

    sil_wrw_mem((uint32_t *)ESP32_UART_FIFO_REG, (uint32_t) (uint8_t) c);
}

/*
 * システムログ低レベル出力（target_syssvc.hで宣言）
 */
void target_fput_log(char c)
{
    if (c == '\n') {
        esp32_uart0_putc('\r');
    }
    esp32_uart0_putc(c);
}
