# BlueDroid (vendored)

ESP-IDF の Bluetooth ホストスタック BlueDroid のソース。BT Classic (SPP) の
ランタイムをここからビルドする。

## 由来

- リポジトリ: https://github.com/espressif/esp-idf
- コミット: `735507283d5b2f9fb363a1901172dbd9e847945d` (**v5.5.4**)
- 取得元パス: `components/bt/`
- ライセンス: Apache-2.0（各ファイルの SPDX ヘッダのとおり）。
  `THIRD_PARTY_NOTICES.md` も参照。

**このコミットは M5Stack Arduino コア 3.3.8 がビルドされたものと同一**
（`esp32-libs/3.3.8/versions.txt` の `esp-idf: v5.5.4 735507283d`）。
同梱の `libbtdm_app.a`（BT コントローラ）と同世代であることが保証される。

## なぜアーカイブではなくソースなのか

M5Stack コアは BlueDroid を `libbt.a` としてコンパイル済みで同梱しており、
一度はそれをリンクした。しかしその archive は `CONFIG_BTDM_CTRL_HLI=y` で
ビルドされている。TOPPERS/FMP3 のベクタ表はレベル4割込みを持たないため、
2026-09-02 にレベル4ベクタを繋いで HLI を有効化して試したが、
コントローラは最後まで割込みを出さなかった。

一方、上流 `toppers/fmp3_esp_idf` は BlueDroid を**ソースからビルド**し、
HLI 無し（割込み線 5/7/8）で、**同じ M5Stack Core Basic 実機**で SPP の
RFCOMM 実データ往復まで実証している
（`.steering/20260813-btclassic-rfcomm-dynisr`、16/64/256/512/900B が
positive control つきで PASS）。実績のある構成へ合わせる。

## ファイル

- `sources.txt` — コンパイルするソースの一覧。上流
  `esp/boot/bluedroid_srcs_classic.txt` と同じ内容で、CMake がこれを読む
  （列挙と検証の真実源を 1 つにするため）。
- `common/`, `host/bluedroid/`, `include/` — 上記コミットからの無改変コピー。
  ヘッダは `.c` が要求する分をまとめて持ち込んでいる（M5Stack コアの
  include ツリーには BlueDroid の内部ヘッダが 218 本足りない）。

改変はしていない。この木を更新するときは、上のコミットを M5Stack コアの
`versions.txt` と突き合わせてからにすること。
