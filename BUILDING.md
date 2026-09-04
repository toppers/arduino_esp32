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
curl -fL -o package_toppers_index.published.json \
    https://github.com/toppers/arduino_esp32/releases/latest/download/package_toppers_index.json
python scripts/make_package_index.py --version <ver> \
    --platform-dir <platform> --owner <owner> --repo <repo> \
    --driver <host>=<fmp3-link-host.zip> ... \
    --merge-into package_toppers_index.published.json --require-merge-target
```

`--merge-into` は公開済み index に載っている過去の版（platform と、リンク
ドライバの tool）をそのまま引き継ぎ、今回と同じ版だけを差し替えます。
**付けずに生成すると新しい index は今回の版だけになり、既存利用者の Boards
Manager から過去の版が消えます。** `--require-merge-target` は、取得に失敗して
ファイルが無いときに黙って新規 index を作らず止めるためのものです。初回
リリース以外では必ず両方付けてください。

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

## 5. リリースする — 実際に踏んだ落とし穴

いずれも「生成も検証も通るのに、失敗するのは利用者の手元」という形をとります。

- **出荷する platform アーカイブは、`verify-package` の `platform`
  アーティファクトから作ってください。** 手元でステージを建て直して組むと
  **検証していないバイト列**を配ることになります。3 ホストの一致は
  「`package` job が上げた同一のアーカイブ」に対する確認です。
- **pre-release で公開してはいけません。**
  `releases/latest/download/…` は pre-release を除外するので、
  README が案内している URL が**全員に 404 を返します。**
- **イメージの SHA-256 をリリースノートに載せないでください。**
  カーネルのバナー文字列が `third_party/fmp3_core/syssvc/banner.c` の
  `__DATE__` / `__TIME__` を含むため、**ステージを建て直すと必ず変わります**
  （サイズは固定長なので変わりません）。同じソース・同じタグの別 run で
  7 件とも別の値になった実測があります。配布物の同定は index の checksum で
  行ってください（Boards Manager が強制します）。
- **GitHub のアーティファクトをブラウザから取得すると二重 zip になります。**
  外側は GitHub の包装で、index に載せるのは内側です。外側のまま登録すると
  Boards Manager が展開に失敗します。

## 変更するときに守ること

- **cfg の `#ifdef` で構成を切り替えない。** cfg の pass1 はプリプロセッサを
  走らせず、`#ifdef` の中の `CRE_TSK` もそのまま静的 API として拾います。
  切り替えは「どの `.cfg` を渡すか」で行います。
- **リンクが通ることは動くことの証明になりません。** weak スタブは実装が無い
  構成で黙って失敗値を返します。
- **`requiredArduinoObjects` は「必ずリンクする物」の一覧であって、「リンクして
  よい物」の一覧ではありません。** ここに挙げたものと `build/sketch` の全
  オブジェクトは常にリンクされ、`build/libraries` の残りは `libarduino.a` に
  まとめて、参照されたときだけ引かれます。構成ごとに存在しないシンボルを呼ぶ
  オブジェクト（`ToppersFMP3_WiFi.cpp.o` など）を無条件にリンクしないための
  分け方です。**この一覧に足りないものがあっても、リンクエラーになるのは
  そのシンボルを実際に呼ぶスケッチだけ**なので、例題で気づけるとは限りません。
- **多重定義もリンクでは捕まりません。** リンクは常に
  `-Wl,--allow-multiple-definition` を付けるので、重複があっても通り、
  どちらが生き残るかはオブジェクト名の順序で決まります。
  `scripts/audit_duplicate_symbols.py` がステージ生成の最後に検査します。
- **リンク順序は ordinal**（バイト単位・大文字小文字を区別）です。ロケール依存や
  大文字小文字を無視するソートでは別のイメージになります。
- 構成を追加・変更するときは、`build_prebuilt_stages.py` の対応表、
  `ports/m5stack_xtensa/runtime/CMakeLists.txt` の分岐、
  `install_platform.py` のメニュー定義、`packaging/release-allowlist.json`、
  `scripts/verify_package.py` の `PROFILES` を揃えてください。

### どこに何を置くか

- **`src/` 配下に FMP3 の cfg や `kernel.h` に依存するソースを置かないでください。**
  Arduino builder は同梱ライブラリの `src/` を**再帰的にコンパイルします**。
  スケッチのビルドには FMP3 のヘッダも cfg ツールも無いので、置いた時点で
  利用者側のビルドが壊れます。FMP3 側のコードは `ports/` か `fmp_app/` へ。
  `src/` に置けるのは `Arduino.h` だけに依存するコードです。
  境界は `src/bridge/ArduinoSketchBridge.cpp` で、FMP3 API を `extern "C"`
  宣言で参照し、`kernel.h` を include しません。
- **Wi-Fi のコールバックテーブルを別ファイルへ複製しないでください。**
  `esp_wifi_init()`／`esp_wifi_start()`／auth backend の選択と、WPA
  コールバックテーブル（offset `0x1b4`・27 エントリ）は
  `runtime/wifi/adapter/toppers_wifi_core.c` だけが持ちます。かつて 3 箇所に
  あり、**private な ABI の記述が運で一致していました。** 複製すると、
  片方だけ直したときに壊れ方が Wi-Fi の失敗として現れます。

### 依存の固定

- **M5Stack Arduino core は 3.3.8 固定です。** 同梱 ESP-IDF v5.5.4 の
  **private な Wi-Fi ABI**、prebuilt archive、include 配置に依存しています。
  version を上げるには、この 3 つと board recipe を全面的に再検証する必要が
  あります。「ビルドが通った」では足りません。
- **ESP-IDF を複製しないでください。** Wi-Fi blob、PHY、lwIP、ツールチェーン、
  ヘッダはすべて M5Stack core から検出して使います。ツリーへ持ち込むと、
  利用者が入れた core との二重管理になります。
- **M5Unified 構成は大量の `-Wl,--wrap=` で mangled C++ シンボルを差し替えて
  います**（`ports/m5stack_xtensa/runtime/CMakeLists.txt`）。M5Unified／M5GFX の
  version を上げると mangled 名が変わり得るので、`--wrap` が空振りします。
  **空振りしてもリンクは通ります。**

### 壊れ方が静かなもの

- **Wi-Fi の OPEN／WPA 初期化分離を壊さないでください。**
  `-Wl,--wrap=esp_supplicant_init` で、`WiFi.begin()` が認証方式を知るまで
  supplicant の初期化を遅延させています。常時初期化すると**オープン AP が
  `AUTH_EXPIRE` で失敗**します。ここを触ったら Open／WPA2-PSK／WPA3-SAE の
  3 経路を実機で回帰してください。関連：`runtime/CMakeLists.txt`、
  `runtime/wifi/adapter/toppers_wifi_connect.c`、
  `runtime/wifi/adapter/toppers_wifi_core.c`、`runtime/wifi/prebuilt/wpa2/`。
- **未実装 API を無条件スタブで成功扱いにしないでください。** Arduino／FreeRTOS
  API は完全互換ではなく、各構成と例題で実際に使った範囲だけが対応済みです。
  `runtime/wifi/adapter/toppers_wifi_optional_stubs.c` の weak スタブは、
  実装が無い構成で**失敗値を返すだけで、リンクは通ります**。追加するときは
  未定義シンボル一覧を正としてください。
- **時間の単位に注意してください。** FreeRTOS API は tick、FMP3 の `dly_tsk` の
  RELTIM は**このポートではマイクロ秒**です。変換は一か所へ集約し、
  `configTICK_RATE_HZ` を暗黙に使わないでください。

### 配布物に入れる／入れない

- **`runtime/wifi/prebuilt/wpa2/` の `.a` は Git 管理対象です**
  （`esp32/` と `esp32s3/` に `libsupplicant.a`／`libmbedcrypto.a`）。
  **`.gitignore` に一般的な `*.a` 除外を追加しないでください。** 更新するときは
  由来 commit、build recipe、SHA-256、ライセンスを同時に更新します。
- **配布アーカイブにビルド出力を入れないでください。** `arduino-cli` は相対パスで
  スケッチをコンパイルすると `<sketch>/build` にも出力するため、同梱ライブラリの
  `examples/` をそのままコピーすると `.ino.bin` が配布物へ入ります。
  `make_package_index.py` は `build` を除外し、バイナリ成果物が残っていたら
  生成を止めます。
- **`gen_esp32part.exe` は同梱しません。** partition 生成はリンクドライバが行うので、
  `{tools.gen_esp32part.cmd}` を参照する recipe が残っていたらインストールを
  止めます。変換結果は `scripts/Test-PartitionTable.ps1` で
  `gen_esp32part.exe` とのバイト一致を確認します。
- **資格情報を残さないでください。** Wi-Fi の SSID／パスワードを commit せず、
  実機ログを文書化するときは SSID、BSSID、割当 IP を書かないでください。
  `examples/WiFiConnect/WiFiConnect.ino` は公開前に空であることを確認します。
- **ライセンスはリポジトリ単一ではありません。** 各ファイルのヘッダと
  `THIRD_PARTY_NOTICES.md` が正で、`LICENSE` はこのリポジトリ向けに書かれた
  部分に適用されます。取り込んだファイルはヘッダを保持してください。

## リリース経路の検証（`scripts/verify_package.py`）

パッケージを組み、Boards Manager 経由で入れ直し、両ボード×全構成を建て直す。

```sh
python3 -m venv ~/.venvs/toppers-verify
~/.venvs/toppers-verify/bin/pip install pyinstaller
~/.venvs/toppers-verify/bin/python scripts/verify_package.py \
    --platform-dir <プラットフォーム> --arduino-cli ~/bin/arduino-cli \
    --skip-core --skip-libraries
```

PyInstaller はリンクドライバの凍結に要る。多くのディストリの Python は
外部管理（PEP 668）なので venv で入れること。

**`--platform-dir` はスケッチブックの外を指すこと。** `<sketchbook>/hardware/
toppers/esp32` に置いたままだと、arduino-cli はそれをスケッチブック
プラットフォームとして扱い、Boards Manager 側の `toppers:esp32` を
「見つからない」と言う（install も uninstall も効かない）。検証するときは
別の場所へコピーして、スケッチブック側は一時的にどける。

同じ理由で、`~/.arduino15/packages/` に `toppers` の Boards Manager 版が
残っていると、その `installed.json` を arduino-cli が読み続けて古い index の
URL を掴む（実測: 消したはずのポートへ HEAD を投げ続けた）。検証中は
`~/.arduino15/packages/` の**外**へ出しておくこと。

## Windows 側テスト

`scripts/Test-*.ps1` は Windows でしか走らず、Linux/macOS の CI も
`verify_package.py` も一切呼ばない。手順・一覧・**既知の対象外**（15 本すべて
CoreS3 前提で、M5Core ボードと `bt-classic` は未検証）は
[`docs/windows-tests/README.md`](docs/windows-tests/README.md)。
実施記録は `docs/windows-tests/runs/` に置く。

