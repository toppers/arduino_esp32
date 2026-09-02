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
 * （非TECS版専用）
 *
 * 本ファイルはsyslog/banner用の低レベル文字出力（target_fput_log、
 * ポーリング送信）のみを提供する。Makefile.chip（KERNEL_COBJS/
 * SYSSVC_COBJS）が全ビルドへ無条件でリンクするため、他のsyssvcモジュール
 * （syssvc/serial.c等）への依存を持たせてはならない。
 *
 * 対話的シリアルコンソール用の割込み駆動SIOドライバ
 * （sio_opn_por等、sample1が使う）は chip_serial_sio.c へ分離した。
 * 従来sio_isr等を本ファイルに同居させていたところ、sio_isrがsyssvc/
 * serial.cのsio_irdy_rcv/sio_irdy_sndを参照するため、chip_serial.o
 * （常時自動リンク）を含む通常のFMP3機能テスト（serial.cfgをINCLUDEせず
 * syslogのみで完結する設計）がリンクエラーになる回帰を引き起こしていた。
 * SIOドライバは実際に対話コンソールを使うビルド（sample1等）でのみ
 * `-S chip_serial_sio.o`を明示指定してリンクする（arch/xtensa_gcc/
 * esp32s3/chip_serial.cfg参照）。
 *
 * レジスタ配置はESP-IDFのSDKヘッダで確認済み（一次情報）：
 *   esp-idf/components/soc/esp32s3/register/soc/{reg_base,uart_reg}.h
 *   DR_REG_UART_BASE = 0x60000000 (UART0)
 *   UART_FIFO_REG(i) = REG_UART_BASE(i) + 0x0   （1バイト書き込みで送信）
 *   UART_STATUS_REG(i) = REG_UART_BASE(i) + 0x1c、bit[25:16] = UART_TXFIFO_CNT
 *
 * 本ファイルではUART0のボーレート・クロック設定は行わない（ROM/QEMUが
 * 既定で有効化した状態のFIFOへポーリング書き込みするのみ）。実機
 * ブリングアップ時にボーレート初期化の要否を再検証する。
 *
 * ビルド時オプション`TOPPERS_S3_CONSOLE_USJ`指定時は
 * target_fput_logの出力先をUART0からUSB-Serial-JTAG（USJ）へ切り替える。
 * 未定義時は本ファイルの動作は変更前と完全に同一（既定ビルドの非回帰、
 * 詳細設計 
 * §3参照）。USJはホストが読み出さないと送信FIFOが掃けないため、
 * リトライ上限（5000回）付きで打ち切り、システムは止めない。
 */

#include <sil.h>

#ifdef TOPPERS_S3_CONSOLE_USJ
#include "esp32s3_usbjtag.h"
#endif /* TOPPERS_S3_CONSOLE_USJ */

#define ESP32S3_UART0_BASE       0x60000000U
#define ESP32S3_UART_FIFO_REG    (ESP32S3_UART0_BASE + 0x0U)
#define ESP32S3_UART_STATUS_REG  (ESP32S3_UART0_BASE + 0x1cU)
#define ESP32S3_UART_TXFIFO_CNT_S   16U
#define ESP32S3_UART_TXFIFO_CNT_V   0x3FFU
#define ESP32S3_UART_TXFIFO_MAX     100U   /* FIFOあふれ防止のための安全側マージン（実サイズ128） */

static void esp32s3_uart0_putc(char c)
{
    uint32_t cnt;

    do {
        cnt = (sil_rew_mem((const uint32_t *)ESP32S3_UART_STATUS_REG) >> ESP32S3_UART_TXFIFO_CNT_S)
              & ESP32S3_UART_TXFIFO_CNT_V;
    } while (cnt >= ESP32S3_UART_TXFIFO_MAX);

    sil_wrw_mem((uint32_t *)ESP32S3_UART_FIFO_REG, (uint32_t) (uint8_t) c);
}

#ifdef TOPPERS_S3_CONSOLE_USJ

#define ESP32S3_USJ_PUTC_RETRY   5000U   /* C6実装の実測値を初期値に踏襲 */

/*
 * [実験1: USJ+BT/WiFi blob 早期ハングの H1/H2 切り分け]
 * TOPPERS_S3_USJ_CONSOLE_TO_UART0 を追加定義すると、USJコードは
 * 全てビルド/リンクされ（=失敗ビルドと同一のコードサイズ/配置）が、
 * target_fput_log の実際の出力先はUART0のまま（USJ putc は実行されない）。
 * esp32s3_usj_putc は未参照になるため、--gc-sections で捨てられないよう
 * used 属性で保持する（コードの「存在」だけを残す）。
 * 未定義時は現行と完全同一（非回帰）。詳細は investigation-usj-wifi-bt-hang.md §5。
 */
#ifdef TOPPERS_S3_USJ_CONSOLE_TO_UART0
__attribute__((used))
#endif
static void esp32s3_usj_putc(char c)
{
    uint_t retry;

#ifdef TOPPERS_S3_USJ_BOOT_WAIT
    /*
     * 初回出力前にホスト(USB-Serial-JTAG CDC)接続を bounded で待つ。
     * 単一USBポート実機(M5Stack CoreS3 等)は UART ブリッジが無く、リセット後の
     * USB 再enumerate に ~1-2s かかるため、起動直後の一発出力(test の TTSP_RESULT 等)が
     * ホスト接続前に流れて欠落する。初回だけ固定待ちを入れ、ホストの reopen-loop 採取が
     * 間に合うようにする。★bounded(ハングしない)。★opt-in: TOPPERS_S3_USJ_BOOT_WAIT を
     * 定義しない限り待たない(既定=無効＝高速起動・headless 無影響)。CMake は A1_USJ_BOOT_WAIT=ON。
     * USJ には接続検出プリミティブが無いため固定待ち(putready は FIFO 空きのみで接続を表さない)。
     */
#ifndef ESP32S3_USJ_BOOT_WAIT_MS
#define ESP32S3_USJ_BOOT_WAIT_MS   3000U   /* 実測: 再enumerate ~1-2s。余裕を見て 3s */
#endif
    static bool_t esp32s3_usj_boot_waited = false;
    if (!esp32s3_usj_boot_waited) {
        uint_t ms;
        esp32s3_usj_boot_waited = true;
        for (ms = 0U; ms < ESP32S3_USJ_BOOT_WAIT_MS; ms++) {
            sil_dly_nse(1000000U);   /* 1ms 相当のビジーウェイト */
        }
    }
#endif /* TOPPERS_S3_USJ_BOOT_WAIT */

    for (retry = ESP32S3_USJ_PUTC_RETRY; retry > 0U; retry--) {
        if (esp32s3_usbjtag_putready()) {
            esp32s3_usbjtag_putchar(c);
            return;
        }
        sil_dly_nse(100);
    }
    /* ホスト未接続：出力を捨てる。システムは止めない。 */
}

#endif /* TOPPERS_S3_CONSOLE_USJ */

/*
 * システムログ低レベル出力（target_syssvc.hで宣言）
 */
void target_fput_log(char c)
{
#if defined(TOPPERS_S3_CONSOLE_USJ) && !defined(TOPPERS_S3_USJ_CONSOLE_TO_UART0)
    if (c == '\n') {
        esp32s3_usj_putc('\r');
    }
    esp32s3_usj_putc(c);
#else /* USJ console (実験1では UART0 へ迂回) */
    if (c == '\n') {
        esp32s3_uart0_putc('\r');
    }
    esp32s3_uart0_putc(c);
#endif /* TOPPERS_S3_CONSOLE_USJ */
}
