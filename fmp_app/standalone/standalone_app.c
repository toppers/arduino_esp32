/*
 *  Arduino を一切参照しない FMP3 アプリケーション
 *
 *  ★これは**テスト用**であって配布物の一部ではない。Boards Manager
 *    パッケージに入るステージはこのアプリケーションを使わない。
 *
 *  Test-RecipeOverride.ps1 のためにある。あのテストが主張するのは
 *  「Arduino がリンクに一切関与せずとも、arduino-cli の recipe override
 *  経由で FMP3 イメージを publish できる」ことで、それを言うには
 *  スケッチのオブジェクトを繋がずに建つアプリケーションが要る。
 *  他のアプリケーションはどれも cfg で ARDUINO_TASK を作り、その実体
 *  `toppers_arduino_task` は Arduino builder がコンパイルする
 *  src/bridge/ArduinoSketchBridge.cpp にあるので、繋がなければ
 *  undefined reference になる。詳細は standalone_app.cfg のコメント。
 *
 *  タスクを 1 本持つのは、これが config の細工ではなく本当に起動する
 *  アプリケーションであることを、焼けば確かめられるようにするため。
 *  テスト自体は ELF と BIN を見るだけで、実機では走らせない。
 */

#include <kernel.h>
#include <t_syslog.h>
#include "standalone_app.h"

const char toppers_standalone_application[] = "no Arduino objects";

void
standalone_task(EXINF exinf)
{
	(void) exinf;

	syslog(LOG_NOTICE, "[Standalone] FMP3 is up with no Arduino objects linked.");
	ext_tsk();
}
