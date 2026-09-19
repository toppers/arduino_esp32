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

## 実機で見たこと（6 板・全ポート）

| ポート | 板 | 周期 | アラーム | `unexpected` | 日 |
|---|---|---|---|---|---|
| Xtensa LX6 | M5Stack ATOM Lite | 60 | 1 | 0 | 09-18 |
| Xtensa LX7 | M5CoreS3 | 60 | 1 | 0 | 09-19 |
| Xtensa LX7 | M5AtomS3 Lite | 61 | 1 | 0 | 09-19 |
| **RISC-V** | **M5Stamp-P4（SMP 2 コア）** | 60 | 1 | 0 | 09-19 |
| RISC-V | M5NanoC6 | 61 | 1 | 0 | 09-19 |
| RISC-V | M5Stamp-C5 | 61 | 1 | 0 | 09-19 |

周期が 60 と 61 に分かれるのは**採取を切った瞬間の差**です（1 秒周期なので、
60 秒の採取窓に 60 回入るか 61 回入るかは開始位相で決まる）。動作の差では
ありません。**アラームはどの板でも 1**——一度きりであることがそこで確かめられます。

**M5Stamp-P4 は SMP 2 コア**で、PRC2 の生存タスクと同居して動きます
（同じ採取で `core2_alive=60 core2_full=60 prc2_start=1`、つまり PRC1 側で
周期通知と 3 タスクが回っている間、PRC2 の 60 行が 1 行も壊れていない）。
`acre_tsk` が返す ID が他の板より 1 つ大きい（4/5/6）のは、PRC2 の生存タスクが
静的に先に居るためです。

### 最初の採取（M5Stack ATOM Lite・2026-09-18・60 秒）

**M5CoreS3（ESP32-S3）でも 2026-09-19 に同じ結果を確認しました**——周期 60・
アラーム 1・`change_priority=0`・`unexpected=0`。実行回数は 60/59/59 ですが、
これは採取を打ち切った瞬間の差です（周期通知が 3 タスクを順に起こす途中で
60 秒が来ると後ろの 2 本が 1 回ぶん少なく見える）。動作の差ではありません。


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
