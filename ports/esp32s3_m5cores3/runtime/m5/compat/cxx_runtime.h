/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5ライブラリ対応 C++ ランタイム最小セット
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  C++ ランタイム最小セット §3）
 *
 *  提供するもの：
 *    (1) operator new / delete → esp_shim_malloc / esp_shim_free 写像
 *        （cxx_runtime.cpp。確保失敗は syslog + abort で顕在化）
 *    (2) esp_run_init_array() … .init_array（グローバル C++ コンストラクタ表）を
 *        順に呼ぶ ~10行の走査関数。
 *
 *  ★呼出し位置について（design.md §3(b) の通り。★本ポートの重要な設計判断）：
 *    esp_run_init_array() は **FMP3 の共有起動シーケンス（software_init_hook 等）
 *    からは呼ばない**。アプリ側（fmp3_m5）の main_task 先頭＝カーネル起動後・
 *    C++ グローバルオブジェクト初使用前に呼ぶこと。理由：
 *      - コンストラクタが operator new（→ esp_shim_malloc）やカーネル API に
 *        触れる可能性があり，start_dispatch 前（esp_shim 未初期化・タスク文脈外）
 *        で実行すると E_CTX やクラッシュになりうる。タスク文脈まで遅延させるのが
 *        安全（design.md §3(b)・§8）。
 *      - 共有起動シーケンスに本呼出しを埋めると esp32_s3 側に M5 専用ロジックが
 *        入り（design.md §7「M5専用ロジックは esp32_s3 に持ち込まない」に反する），
 *        かつ既存 5 seam 構成のバイナリ（golden）が変化してしまう。
 *    そのため本ヘッダ／実装は **再利用可能な部品** としてのみ提供し，呼出し場所は
 *    アプリが決める。
 */

#ifndef ESP_M5_CXX_RUNTIME_H
#define ESP_M5_CXX_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  グローバル／静的オブジェクトのコンストラクタを順次呼び出す。
 *  ★本プロジェクトのツールチェーン（xtensa-esp-elf esp-14.2.0）は
 *    コンストラクタを .init_array ではなく **.ctors** に出力する。
 *    本関数は .init_array（前方）と .ctors（GCC 逆順規約で後方）の両方を走査する。
 *
 *  __init_array_start/end・__ctors_start/end はリンカスクリプトで定義する
 *  （esp32s3_xip.ld の .flash_rodata 内，および QEMU テスト用 SRAM リンカ
 *   esp32s3_devkitc.ld の .rodata 内）。空（start==end）なら何もしない
 *  （C のみのビルドでの非回帰を保証）。
 *
 *  冪等ではない（コンストラクタは1度だけ呼ぶこと）。呼び出しは1コア・1回のみ。
 */
extern void esp_run_init_array(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_M5_CXX_RUNTIME_H */
