# ソースからビルドする

利用者がスケッチを建てるのに CMake も Python も要りません。ここは
**配布物そのものを作り直す**手順です。

## 前提

| 必要なもの | 備考 |
| --- | --- |
| M5Stack Arduino core **3.3.8** | ツールチェーンと ESP-IDF v5.5.4 の成果物を供給する |
| CMake / Ninja | ステージ生成にのみ使う |
| Python 3.9 以上 | 同上 |
| `M5GFX` 0.2.27 / `M5Unified` 0.2.20 | `M5Unified` 構成のステージが必要とする |

submodule を先に取得してください。

```bash
git submodule update --init --recursive
```

## 二段構えになっている理由

このポートは**配布物を作るときのビルド**と**利用者がスケッチを建てるときの
ビルド**が分かれています。

```text
[配布物を作るとき]  CMake + Ninja + FMP3 の cfg
                    → 構成ごとの prebuilt stage
                      （objs/ ld/ link-manifest.json objects.rsp）

[スケッチを建てるとき]  M5Stack core 同梱のツールチェーンと esptool だけ
                        → stage の .o とスケッチの .o をリンク
```

stage はスケッチに依存しません（cfg は構成ごとに固定で、スケッチに依存するのは
最終リンクだけ）。これが「利用者の環境に CMake を要求しない」ことの根拠です。

## 1. ステージを作る

```bash
python scripts/build_prebuilt_stages.py --cmake <path/to/cmake> --ninja <path/to/ninja>
```

`minimal` / `m5-unified` / `wifi-connect` が
`build/prebuilt/esp32s3/<構成>/` に出ます。実験的な `all-in-one` を作るには
`--profiles all-in-one` を渡します（既定には入っていません）。

Arduino のデータディレクトリは OS ごとに解決します
（`%LOCALAPPDATA%\Arduino15` / `~/Library/Arduino15` / `~/.arduino15`）。
別の場所にある場合は `--arduino-data` で渡してください。

## 2. platform ディレクトリを組み立てる

```bash
python scripts/install_platform.py --prebuilt-stage-root build/prebuilt
```

`boards.txt` / `platform.txt` / stage / リンクドライバ / partition テーブルを
sketchbook の `hardware/toppers/esp32` へ置きます。Arduino IDE を再起動すると
ボードが選べます。

`build/prebuilt/<チップ>/` が並んでいる親を渡すと、**そこにある全チップの
ボードが 1 つの platform に入ります**（CoreS3 と M5Stack Basic が同居する）。
チップ 1 つ分のディレクトリを渡せばそのボードだけになり、`--chip` で
親から一部だけ選ぶこともできます。

## 3. パッケージと index を作る

```bash
python scripts/make_package_index.py --version <ver> \
    --platform-dir <platform> --owner <owner> --repo <repo> \
    --driver <host>=<fmp3-link-host.zip> ...
```

`--owner` と `--repo` に既定値はありません。これらは Boards Manager が
アーカイブを取りに行く URL を組み立てるので、**誤った値は利用者の手元で初めて
失敗します。**

リンクドライバはホストごとに凍結したものが要ります
（`.github/workflows/build-link-driver.yml` が `v*` タグで生成します）。

## 4. 検証する

```bash
python scripts/verify_package.py --platform-dir <platform>
python scripts/check_release_artifacts.py --release-dir <出力>
python scripts/check_host_paths.py <platform または zip>
```

- `verify_package.py` … Boards Manager 経由で入れ直し、3 構成 × 例題を建てる
- `check_release_artifacts.py` … index の checksum、ホストの網羅、ドライバの版
- `check_host_paths.py` … 配布物にビルド機の絶対パスが混入していないか

## 変更するときに守ること

- **cfg の `#ifdef` で構成を切り替えない。** cfg の pass1 はプリプロセッサを
  走らせず、`#ifdef` の中の `CRE_TSK` もそのまま静的 API として拾います。
  切り替えは「どの `.cfg` を渡すか」で行います。
- **リンクが通ることは動くことの証明になりません。** weak スタブは実装が無い
  構成で黙って失敗値を返します。
- **多重定義もリンクでは捕まりません。** リンクは常に
  `-Wl,--allow-multiple-definition` を付けるので、重複があっても通り、
  どちらが生き残るかはオブジェクト名の順序で決まります。
  `scripts/audit_duplicate_symbols.py` がステージ生成の最後に検査します。
- **リンク順序は ordinal**（バイト単位・大文字小文字を区別）です。ロケール依存や
  大文字小文字を無視するソートでは別のイメージになります。
- 構成を追加・変更するときは、`build_prebuilt_stages.py` の対応表、
  `ports/esp32s3_m5cores3/runtime/CMakeLists.txt` の分岐、
  `install_platform.py` のメニュー定義、`packaging/release-allowlist.json`、
  `scripts/verify_package.py` の `PROFILES` を揃えてください。
