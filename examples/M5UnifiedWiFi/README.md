# M5UnifiedWiFi（試作・`All-in-one (experimental)` 専用）

画面（M5Unified / M5GFX）と Wi-Fi を **1 つのスケッチ**で使う例題です。
`Tools > FMP3 Runtime > All-in-one (experimental)` を選んでください。他の構成を
選ぶとコンパイル時に `#error` で止まります（リンカのエラーではなく、選ぶべき
メニュー項目の名前で止まります）。

**このメニュー項目は既定のパッケージには出ません。** all-in-one は合成の
試作であり、配布物のステージ集合に入っていないためです。自分で作る手順は
[`BUILDING.md`](../../BUILDING.md) の `--profiles all-in-one`、到達点と使用量は
[`docs/all-in-one-prototype.md`](../../docs/all-in-one-prototype.md)。

## 何をするか

- LCD を上げて、Wi-Fi のスキャン結果（SSID・RSSI・チャネル）を並べます
- `WIFI_SSID` を書いておくと STA 接続し、IP を画面の下端に出します
- **1 秒ごと**に画面を書き替え、**15 秒ごと**に再スキャンしながら、
  シムの動的セマフォ計器をシリアルへ出します:

```
[AIO] alive=97 status=3 sem_live=7 sem_peak=7 sem_acre_fail=0 sem_del_fail=0
```

最後の 2 つがこの例題の主目的です。画面と Wi-Fi を同時に使うと動的セマフォが
いくつ要るのかは、この構成を作った時点で測られていませんでした
（`fmp_app/allinone/allinone_app.cfg` の `AID_SEM` のコメント）。足りなくなると
`acre_sem` が失敗して `sem_acre_fail` が増えるので、**足りているかどうかが
画面ではなくログで分かります**。

M5Stack Basic の実測では peak 7 / 失敗 0 でした（上限は 36）。

## 使える板

画面のある 3 板（M5Stack CoreS3・M5StickS3・M5Stack Basic）。all-in-one は
`M5Unified + Dual Core` を含むので、その構成を出していない板には出ません。
