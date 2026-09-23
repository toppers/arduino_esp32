/*
 *  コンソール入力 — FMP3 のシリアルインタフェースドライバ経由
 *
 *  このポートに Arduino の `Serial` はありません（M5Stack core のランタイムを
 *  リンクしないため。README の「制約」）。**入力ができないわけではありません**
 *  ——FMP3 自身のシリアルインタフェースドライバ（`syssvc/serial.c`）が
 *  受信も持っており、stage には全構成・全チップで入っています
 *  （`serial.o` と、チップ側の `chip_serial_sio.o`。USB-JTAG では
 *  受信パケット割込みを使います）。本ヘッダはそこへの薄い入口です。
 *
 *  出力（ログ）と同じ 1 本のポートを読みます。TOPPERS/FMP3 本体の
 *  `sample1` がシリアルから打つコマンドで動くのと同じ作法です。
 *
 *  **実装はライブラリ側**（`ToppersFMP3_Console.cpp`）にあり、stage には
 *  入っていません。呼んだスケッチにだけリンクされるので、使わないスケッチの
 *  大きさは変わりません。`ToppersFMP3_Kernel.h`（stage 側のカーネル API
 *  ラッパ、Minimal 構成のみ）とは別物で、**どの `Tools > FMP3 Runtime` でも
 *  使えます。**
 */
#pragma once

#include <stdint.h>

//  ログと共用のコンソールポート。このポートの `TNUM_PORT` は 1 です
//  （`ports/*/runtime/target/*/target_serial.h`）。ポートは logtask が
//  起動時に開くので、スケッチ側で開く必要はありません。
#define TOPPERS_FMP3_CONSOLE_PORT 1

class ToppersFMP3ConsoleClass {
public:
    /*
     *  受信バッファに溜まっている文字数。エラーなら負。
     *  **read() の前に必ずこれを見ること**——FMP3 の `serial_rea_dat()` は
     *  要求した長さが揃うまで待つので、0 のときに読むと loop() が止まります。
     */
    int available() const;

    /*  1 文字取り出す。無ければ -1（待ちません）。  */
    int read() const;
};

extern ToppersFMP3ConsoleClass FMP3Console;
