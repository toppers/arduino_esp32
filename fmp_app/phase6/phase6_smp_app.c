/*
 *  dual-core profile の配布用 FMP3 アプリケーション
 *
 *  実行主体は Arduino がコンパイルする setup()／loop() で、この TU は
 *  FMP3 のアプリケーション名を固定するためだけに在る（他の profile と同じ形）。
 *  カーネルは PRC_NUM=2 で建つが、配布構成では PRC1 に Arduino タスクを
 *  置くだけで PRC2 は空いている。
 *
 *  ★自己診断（監視タスクと PRC2 実演ワーカ）は phase6_smp_selftest.c へ
 *    分離した。
 */
const char toppers_phase6_smp_application[] = "Arduino SMP bridge";
