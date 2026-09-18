# Fmp3Sample1

TOPPERS/FMP3 に付属する `sample1`（`fmp3_core/sample/sample1.c`）に相当する
**縮約版**です。`Tools > FMP3 Runtime > Minimal` 専用。

## 本家との差（なぜ縮約版なのか）

本家は 31 タスク・6 周期通知・6 アラーム通知・6 データキューを **cfg で静的に
宣言**し、**シリアルから打つコマンド**で動かします。このポートでは 2 つの前提が
成り立ちません。

1. **スケッチから cfg にオブジェクトを足せません。** stage の cfg は
   「スケッチ非依存で固定」がこのポートの設計で、だから stage が例題ごとに
   建て直されずに済んでいます。
   ⇒ タスクは実行時に作り（`toppers_fmp3_task_create` → `acre_tsk`）、
     周期通知とアラーム通知は minimal stage が持つ 1 個ずつを使います。
2. **Serial 入力がありません**（M5Stack core のランタイムをリンクしないため。
   README の「制約」）。⇒ コマンドの代わりに周期通知が進行を進めます。

### 周期通知とアラーム通知が「静的」な理由

動的生成（`acre_cyc` / `acre_alm`）にしたかったのですが、
**`AID_CYC(n>0)` / `AID_ALM(n>0)` は「同じ種の静的生成が 1 個以上あること」を
要求**し、minimal profile には元々 1 個もありませんでした。実測すると cfg が
こう止まります:

```
error: E_OBJ: AID_CYC requires at least one CRE_CYC in the system
```

⇒ ダミーを置くくらいなら、例題がそのまま使う 1 個ずつを置く、という判断です。
**周期は 1 秒固定**です（静的な周期通知の周期を後から変える API は TOPPERS に
ありません。`T_CCYC.cyctim` は生成時に決まります）。

## 残した要点（本家と同じ）

- 優先度の違う 3 つのタスクが走ること
- 起床待ち（`slp_tsk`）と起床（`wup_tsk`）でタスクが動くこと
- 周期通知が一定間隔で上がること
- アラーム通知が**一度だけ**上がること
- 優先度を変える（`chg_pri`）と走る順番が変わること

## 実機で見たこと（M5Stack ATOM Lite・2026-09-18・60 秒）

```
[Sample1] created task id=3 / 4 / 5
[Sample1] cyclic_start=0
[Sample1] alarm_start (3 s)=0
[Sample1] cyclic count=60          <- 1 秒周期 x 60 秒
[Sample1] alarm count=1            <- 3 秒後に 1 回だけ
[Sample1] run count = 61 / 61 / 61 <- act_tsk の 1 回 + 周期の 60 回
[Sample1] change_priority a=0 b=0
banner=1 setup=1 heartbeat=49 unexpected=0
```

## 使うときの注意

- **出力は FMP3 の syslog へ出ます**（`Serial` ではありません）。
- **タスクのスタックはスケッチが持ちます。** stage 側に置くと、この例題を
  使わないスケッチまで RAM を払うためです（Minimal は全 8 ボードの既定構成）。
- **ログ用のバッファが `static` なのは趣味ではありません。** M5Stack core は
  スケッチを `-fstack-protector` でコンパイルするので、ローカルの `char` 配列を
  持つ関数は `__stack_chk_fail` を参照し、Minimal では newlib の `_write` が
  未定義になってリンクに落ちます（実測）。同梱の `GpioInterrupt` が静的バッファを
  使っているのも同じ理由です。
- `Minimal` 以外を選ぶと `#error` で止まります。外すと
  `toppers_fmp3_task_create` ほか 5 記号が未定義になることを確認済みです。

このファイルはリポジトリの文書で、リリースパッケージには入りません。
