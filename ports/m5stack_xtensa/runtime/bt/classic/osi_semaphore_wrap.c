/*
 *  LM-D-4（後半）: esp-idf/components/bt/common/osi/semaphore.c を、別 basename
 *  としてビルドするための薄いラッパ。詳細は同ディレクトリの
 *  osi_alarm_wrap.c 冒頭コメント参照（fmp3_core/kernel/semaphore.c との
 *  basename衝突対策、実測）。
 */
#include "../../../../../third_party/bluedroid/common/osi/semaphore.c"
