/*
 *  M5Unified profile の配布用 FMP3 アプリケーション
 *
 *  実行主体は Arduino がコンパイルする setup()／loop() で、この TU は
 *  FMP3 のアプリケーション名を固定するためだけに在る（他の profile と同じ形）。
 *  M5Unified を叩く実体は runtime/m5/adapter/m5_arduino_adapter.cpp にある。
 *
 *  ★自己診断（監視タスク）は phase5_m5_selftest.c へ分離した。
 */
const char toppers_phase5_m5_application[] = "Arduino M5Unified bridge";
