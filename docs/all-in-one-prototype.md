# all-in-one（M5Unified + Dual Core + WiFi）の試作（2026-09-20）

`Tools > FMP3 Runtime` の `M5Unified + Dual Core` と `WiFi` を**1 つの構成**に
まとめられるか、という問いに対する試作の記録。**配布物には入れていない。**

## 到達点

| 何を | どこまで | 証拠 |
|---|---|---|
| ステージ | S3・LX6 とも建つ | 165 objects、重複記号監査 0 / 0 allowed |
| メニュー | 画面のある 3 板にだけ出る | `install_platform.BOARD_SKIP_ENTRIES` に `aio` を追加 |
| ビルド行列 | **9/9 PASS**（3 板 x 3 例題） | `verify_package.py --list-builds --profiles aio` の行列をそのまま |
| 実機 | **M5Stack Basic と M5CoreS3** で 120 秒ずつ、画面と Wi-Fi が同時に動く | 下記 |
| 動的セマフォ | **peak 7（LX6）/ 6（S3）・失敗 0**（未計測だった上限を測った） | 下記 |

### 実機（M5Stack Basic、ESP32-D0WDQ6-V3、2026-09-20）

例題 `M5UnifiedWiFi` に実 AP の資格情報を入れた像を焼いて 120 秒採取した
（`fmp3_esp_idf_dev/.steering/20260920-aio-probe/logs/lx6-capture-20260920-205533.log`）:

```
[AIO] display board=1 w=320 h=240
[AIO] scan=19
[WiFiConnect] connected authmode=6 channel=10
[WiFiConnect] DHCP address=... / DHCP completed
[AIO] alive=97 status=3 sem_live=7 sem_peak=7 sem_acre_fail=0 sem_del_fail=0
markers: banner=1 setup=1 unexpected=0
wifi:    scan=16 scanap=115 connected=1
```

**1 つの像で**: LCD が 320x240 で上がり、15 秒ごとのスキャンを 16 回（延べ 115 AP）
こなし、STA が繋がって DHCP まで通り、その間 97 秒ぶん画面を書き替え続けた。
`unexpected=0`。

### 実機（M5CoreS3、ESP32-S3、2026-09-20）

同じ例題を CoreS3 でも回した（`logs/s3-aio-noconnect.log`）:

```
[AIO] display board=10 w=320 h=240
[AIO] scan=14
[AIO] alive=101 status=6 sem_live=6 sem_peak=6 sem_acre_fail=0 sem_del_fail=0
markers: banner=1 setup=1 unexpected=0
wifi:    scan=12 scanap=89
```

**LCD 320x240 + スキャン 12 回（延べ 89 AP）を 120 秒。** セマフォ peak 6・失敗 0。

**STA 接続は、この AP では確認できない。** ESP32-S3 は WPA3 移行モードの AP に
`reason=17` で繋がらない（README「制約」、`docs/atoms3lite-port.md` F-3）。
手元の AP がそれなので、S3 側の「画面 + STA 接続」は未測定のままである。

さらに、**その切断のあとスケッチが止まる**ことがこの作業で見つかった。
**合成のせいではない**——同じ停止が出荷の `wifi-connect` 構成（M5Unified 無し）
でも再現し、STA を使わなければ all-in-one は 101 周回った。3 本の対照と
JTAG の判定は `docs/atoms3lite-port.md` の「F-3 の続き（2026-09-20）」。

### 未計測だった上限を測った

`fmp_app/allinone/allinone_app.cfg` は「M5GFX と Wi-Fi を同時に使うとき
セマフォ 24 本で足りるかは未計測」と書いてあった。測った結果:

- 生成 cfg の上限は **`TNUM_SEMID` = 36**（`wifi-connect` の 31 に m5 側の 5 が
  積み上がる。AID_SEM は**合算**される——構成ごとの生成物 `kernel_cfg.h` で確認）。
- 実機で**同時に生きていた本数の peak は 7**、`acre_sem` の失敗は **0**、
  シム自身の `esp_shim: acre_sem failed` も **0 行**。

⇒ **この作業負荷では余裕がある。** ただし測ったのは「LCD 描画 + scan + STA + DHCP」で、
TCP や BLE を重ねた場合は別の測定である。上限を上げるときは
プールを広げず `AID_SEM` の数値を上げること（cfg のコメントのとおり）。

## 使用量（M5CoreS3、同一スケッチで比較）

| 構成 | flash | 静的 RAM | 残り |
|---|---|---|---|
| `m5-unified`（例題 M5Unified） | 272,916 | 48,972 (14%) | 278,708 |
| `wifi-connect`（例題 WiFiScan） | 518,476 | 244,624 (74%) | 83,056 |
| **`all-in-one`（例題 M5UnifiedWiFi）** | **723,276** | **257,296 (78%)** | **70,384** |

**RAM を食っているのは Wi-Fi スタックで、M5Unified を足す増分は約 12.7 KB。**
`wifi-connect` が既に 74% を使っており、合成にしたから急に苦しくなる構図ではない。
ただし arduino-cli は 78% で "Low memory available" を出す。
LX6（M5Stack Basic）は flash 719,896（54%）/ RAM 264,456（80%）。

## 試作で入れたもの

- `examples/M5UnifiedWiFi/` — 画面 + scan + STA を 1 つのスケッチで行い、
  **シムのセマフォ計器を毎秒印字する**。15 秒ごとに自分で再スキャンするので、
  人が居なくても計器として成立する。`#error` ガードつき（他の構成では
  リンカではなくメニューの名前で止まる）。
- `scripts/install_platform.py` — `BOARD_SKIP_ENTRIES` に `aio` を追加。
  aio は m5-unified を**含む**ので、m5 を出せない板（M5AtomS3 Lite・
  M5Stack ATOM Lite）には出さない。
- `scripts/verify_package.py` — `PROFILES["aio"]` と
  `EXPERIMENTAL_BOARD_PROFILES`、`EXPERIMENTAL_PROFILES`。**既定の
  `--profiles` からは外してある**ので、出荷の本数（124）は動かない。
- `scripts/test_check_release_artifacts.py` — 実験構成にも不変条件を張った
  （「メニューに出る板 == verify する板」「aio はどの release 表にも無い」）。
  負対照で落ちることを確認済み（1 行消すと FAILED、戻すと OK）。

**出荷構成への影響が無いことの確認**: all-in-one を除いたステージ集合で
platform を組み立て、変更前後で比較した → `boards.txt` の sha256 一致、
ディレクトリ全体も `installedAt` のタイムスタンプ以外に差分なし。

## 出荷するなら残っている作業

1. `build_prebuilt_stages.SHIPPED_PROFILES` へ追加（ステージが常に建つ）。
2. `packaging/release-allowlist.json` の `prebuiltStages`、
   `install_platform.EXPECTED_PROFILES`、`xcheck_compare.profiles_for` を同時に更新。
   ドリフト検査がこの 3 つを 1 つの表として突き合わせるので、**同時でないと落ちる**
   （それが検査の目的）。
3. `verify_package` の `EXPERIMENTAL_*` から通常の表へ移す（本数が 124 -> 133 になる）。
4. パッケージが 1 チップあたり 5 MB 増える（S3 5.3 MB / LX6 5.2 MB）。
   配布物の大きさとダウンロード時間の判断が要る。
5. M5StickS3 の実機確認（この試作で実機を見たのは M5Stack Basic と M5CoreS3。
   StickS3 はこの機械に繋いだことが無い）。S3 の「画面 + STA 接続」は
   WPA2 専用の AP が要る（上記）。
6. `M5Unified + Dual Core` と `WiFi` を残すのか、aio に一本化するのかの判断。
   残す場合、利用者から見て 4 つ目の選択肢が増える。

## 判断の材料（私見）

技術的な障害は見当たらない。引っかかるのは **RAM 78-80% という表示**と
**パッケージ 5 MB 増**で、どちらも「動かない」ではなく「説明が要る」類である。
