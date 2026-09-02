/*
 *  LM-D-4（後半）: esp-idf/components/bt/common/osi/alarm.c を、別 basename
 *  としてビルドするための薄いラッパ。
 *
 *  a1_xip_build.cmake の XIP リンク段は全 .o を basename だけで単一ディレクトリへ
 *  ステージングする（classic の `ar x` フラット配置を再現するため）。btclassic
 *  構成は fmp3_core 側の TOPPERS アラームハンドラ機能（kernel/alarm.c）と、
 *  Bluedroid の OSI タイマホイール（common/osi/alarm.c）を両方リンクする必要が
 *  あり、両者とも basename が "alarm.c" で衝突する（実測: a1_stage_claim が
 *  fail-closed で検出）。
 *
 *  vendored（third_party/bluedroid・fmp3_core とも無改変方針）の
 *  ソースを改名・改変せず衝突を避けるため、本ファイルは実体を #include するだけの
 *  薄いラッパとして別 basename を与える。中身は無改変のまま。
 */
#include "../../../../../third_party/bluedroid/common/osi/alarm.c"
