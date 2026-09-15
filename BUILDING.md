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

### M5NanoC6（ESP32-C6）のステージ

```bash
python scripts/build_prebuilt_stages.py --chip esp32c6
```

`--chip esp32c6` の既定 profile は `minimal` / `wifi-connect` の 2 つだけです
（`m5-unified` / `bt-classic` は選べません。D11）。トゥールチェーンは
M5Stack Arduino core 3.3.8 が同梱する `esp-rv32` 2601 と `esp32c6-libs`
3.3.8 で、他の 3 ボードと同じ core から取れます（別途取得は不要）。

診断・対照用の CMake オプションは `--cmake-define` でそのままステージの
CMake 呼び出しへ渡せます。

| オプション | 既定 | 用途 |
| --- | --- | --- |
| `TOPPERS_C6_APM_UNBLOCK` | ON | LP/HP APM のロック解除。OFF にすると Wi-Fi scan/接続が意図的に 0 件になる対照ステージが作れます（`docs/c6-port.md` 段4「APM 対照」） |
| `TOPPERS_C6_WIFI_DIAG` | OFF | Wi-Fi 初期化の段階マーカー |
| `TOPPERS_C6_NET_DIAG` | OFF | port 7 の TCP/UDP echo サーバ、DHCP 直後の gateway ping、`ip=`/`gw=` 付きログ行。配布する stage では既定 OFF（段5 判断 S5-3。下記「変更するときに守ること」参照） |

対照ステージを既定の `build/prebuilt/esp32c6/<profile>/` に**上書きしない**
ように、`--output-directory <別の場所>` を必ず付けてください。既定のまま
`ON`/`OFF` を切り替えて作り直すと、直前に作った配布用ステージが対照用の
ものに置き換わります。

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

### M5NanoC6（`m5nanoc6_fmp3`）のイメージ形式

Xtensa 3 ボードは`paddrMode="runtime-mmu"`（ブート時に自分で MMU を
設定する形式）ですが、M5NanoC6 は RISC-V の `paddrMode="fixed-vma"`
（ELF が最初から仮想アドレスへリンクされ、bootloader が flash から
そのまま mmap する形式）です。manifest schema は 2 に上がっており
（`DRIVER_VERSION` は現在 4。段5 で 3 から上げた経緯は下記「M5NanoC6
イメージのビルドパス非依存化（S5-8）」）、`fmp3-link` は fixed-vma の
イメージを bootloader が受理する条件 C-1..C-8（`scripts/fmp3_link.py`
の `check_fixed_vma_image`）で検査してからリンクします。

| # | 検査内容 |
| --- | --- |
| C-1 | flash から map するセグメントがちょうど 2 本 |
| C-2 | セグメント #0 の先頭が `ESP_APP_DESC_MAGIC_WORD`（`0xABCD5432`） |
| C-3 | 各セグメントの `file_offset % page == vaddr % page` |
| C-4 | エントリポイントがイメージに含まれるセグメント内にある |
| C-5 | RAM セグメントが bootloader 自身の `iram_loader_seg` に重ならない |
| C-6 | 各 RAM セグメントのバイト列が ELF の `.data` と一致する |
| C-7 | 2 本の flash map セグメントの MMU ページ範囲が重ならない |
| C-8 | イメージが書込み先の app パーティションに収まる |

C-1..C-8 はすべて `fmp3-link` 実行時に検査され、満たさなければリンクは
失敗します（Xtensa の runtime-mmu 側にこの検査はありません）。

### この platform はライブラリを同梱しません

**同梱するのは `make_package_index.py`（配布パッケージ）だけです。** そのため
この platform でスケッチを建てるときは、ライブラリの供給元を自分で指定します。
リポジトリそのものがライブラリ（`library.properties` ＋ `src/`）なので、
そのまま渡せます。

```bash
arduino-cli compile --fqbn toppers:esp32:m5cores3_fmp3:FMP3Runtime=m5 \
    --library . examples/StackChanBasic
```

**`--library` を付けないと、スケッチブックの `libraries/` にある古いコピーが
使われます。** そこに以前の版が残っていると、

```text
fatal error: ToppersFMP3_M5Unified.h: No such file or directory
```

のように**そのファイルが無いという形**で失敗します。例題の不備にも platform の
不備にも見えますが、原因はライブラリの供給元です。`verify_package.py` が同じ
例題を通すのは、あちらが配布パッケージを入れており、そこには最新のライブラリが
同梱されているからです。

> **スケッチブック側と Boards Manager 側は、両方向で邪魔をします。**
>
> スケッチブックに platform があるあいだ、`toppers:esp32` は Boards Manager から
> 管理できません（`install` / `uninstall` / `search` が「無い」ように振る舞い、
> `Platform 'toppers:esp32@x' not found` という何も指さないエラーになります）。
> 公開パッケージを試すときは、この platform を先に消します。
>
> 逆に、**Boards Manager のコピーがあると、そちらがビルドに使われます。**
> 組み立てたばかりの platform が黙って使われず、直したはずの箇所が反映されない
> という形で出ます。どちらが使われているかは次で確かめられます。
>
> ```sh
> arduino-cli board details -b toppers:esp32:m5cores3_fmp3
> ```

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

- `verify_package.py` … Boards Manager 経由で入れ直し、**既定で全ボードx
  対応する構成x例題を建てる**（下記「`verify_package.py` の板と構成」）
- `check_release_artifacts.py` … index の checksum、ホストの網羅、ドライバの版、
  各チップの stage・ツール依存（C6 なら `esp-rv32`/`esp32c6-libs`）
- `check_host_paths.py` … 配布物にビルド機の絶対パスが混入していないか

### `verify_package.py` の板と構成

対象は `scripts/verify_package.py` の `BOARD_PROFILES`（板 -> 選べる構成の集合）
と `PROFILES`（構成 -> 例題）から機械的に決まります。固定の本数をこの文書に
書き写さないでください（ドリフトします）。導出は次のコマンドで確認できます。

```bash
python3 scripts/verify_package.py --list-builds   # 実行せず、計画だけを表示
```

`--list-builds` はパッケージも Boards Manager への出入れもせず、板x構成x例題の
表と合計だけを表示します（2026-09-15 実測: CoreS3 13・M5StickS3 13・M5Core 17・
M5NanoC6 8 = 計 51。導出の正本はコマンドそのもので、この数字は実測の一例です）。

- **既定は 4 板すべて**です。`--boards`/`--profiles` で絞り込めます。
- **`verify_package.py` はローカルの package index を作って Boards Manager の
  設定を一時的に書き換えます。** これは開発機の `~/.arduino15/` にキャッシュ
  されている**公開 index（`package_toppers_index.json`）を同じファイル名で
  上書きします**（`cache clean` で `staging/packages/` も空にします）。
  実行後、そのまま Boards Manager で通常の導入作業をすると `0.4.2 @
  127.0.0.1` のようなローカル URL しか見えません。**検証が終わったら
  `arduino-cli core update-index` を実行して公開 index を復旧してください。**
- **Windows／Apple Silicon macOS 向けのリンクドライバ zip は、Linux 機では
  凍結できません。** `verify_package.py` はこの 2 つを stub zip として扱い、
  実行できるのはビルド機自身のホスト分だけです。3 ホストぶんの本物のドライバは
  CI（`.github/workflows/build-link-driver.yml`）が `v*` タグで生成します。

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
- **M5NanoC6 イメージのビルドパス非依存化（S5-8）。** 段5 の 4 板 verify で、
  同じソース・同じステージから建てた M5NanoC6 の 7 成果物が、ビルドパスの
  綴りが違うだけで 64-65 B（esptool が書く app descriptor の ELF sha256・32 B
  とイメージ末尾ハッシュ・32 B の 2 本。実測では 64 バイトの差になることも
  65 バイトの差になることもあります--2 本のハッシュのどちらかで 1 バイトが
  たまたま一致する場合があるため）だけ異なることが分かりました。原因は
  arduino-cli がコンパイルするスケッチ／コアのオブジェクトの `.debug_str` にビルドパスが
  残ること（stage 側は `-ffile-prefix-map` で対策済みだが、スケッチ側の
  コンパイルはその外）です。**stage-5 の fix wave（driver 4、commit `aa62fde`）**で
  `fmp3-link` の fixed-vma 経路に、`elf2image` の前に ELF のコピーを
  `--strip-debug` する処理を追加し（読み込む内容は不変）、`app_elf_sha256`
  が DWARF に依存しないようにしました。実測（positive control）: 同じスケッチを
  2 つの異なる build path で建てて `.bin` の sha256 が一致することを確認済みです
  （strip を外すと 64-65 バイトが再び異なります）。Xtensa（schema 1／runtime-mmu
  経路）はこの変更の対象外（driver 3/4 の再リンクで app bin が byte 同一なことを
  確認済み）。**ただしこの実測は 1 ホスト内の build path 差のみで、3 ホストの
  バイト一致は Xtensa 側についての記述です。** M5NanoC6 の成果物についてホスト間
  バイト一致まで主張できるかどうかは、まだ確認していません。
- **GitHub のアーティファクトをブラウザから取得すると二重 zip になります。**
  外側は GitHub の包装で、index に載せるのは内側です。外側のまま登録すると
  Boards Manager が展開に失敗します。
- **公開後の確認は、浅いパスの隔離環境で行ってください。** Windows で
  `directories.data` を深い場所に置くと、M5Stack core の SDK ヘッダへの
  パスが 260 文字を超え、GCC が `deprecated_definitions.h: No such file`
  で止まります（長いパスの許可が既定で無効なため）。ファイルは展開されて
  いて、パッケージも正しいのに、全ボードのビルドが数秒で失敗する形で
  現れます。0.4.0 の確認で一度これを配布物の欠陥と疑いました。
  `C:\Users\<name>\AppData\Local\Temp\<短い名前>` のような場所に隔離環境を
  作れば、公開 index からの導入と全ボードのビルドがそのまま通ります。
- **リリースを作ったら、ファイルを添付したか確かめてください。** 添付を忘れても
  リリースは正常に見えます——タグも本文も正しく、`draft` でも `pre-release` でも
  なく、**手元の検査はすべて通っています。** それでも `latest` が
  アセットの無いリリースを指した瞬間に、README が案内している URL は
  **全員に 404 を返します。** 0.4.1 で実際にこれをやりました。
  影響は新規の利用者だけではありません。**既存の利用者も index を取得できなく
  なる**ので、0.3.0 や 0.4.0 を使っている人まで更新できなくなります。
  pre-release にしたときと**同じ URL が同じ形で壊れます。**

### 公開したら必ずこれを実行する

30 秒で終わり、上の 2 つ（pre-release・添付漏れ）をまとめて捕まえます。

```bash
curl -fL -o published.json \
    https://github.com/toppers/arduino_esp32/releases/latest/download/package_toppers_index.json
cmp published.json <出力>/package_toppers_index.json
```

**`curl` が 404 で落ちたら、利用者から見て公開は失敗しています。** 成功して
バイト一致すれば、`latest` が今回のリリースを指し、index が検査を通したものと
同一だと確認できたことになります。

続けて、index が名指しするアーカイブが実際に取得できるかも見ておくと確実です。
**過去の版も含めて**確認してください。`--merge-into` で引き継いだ古い版は、その
リリースのアセットが消えていると Boards Manager に「入れられない版」として
並びます。

```bash
python - <<'PY'
import json, hashlib, urllib.request
idx = json.load(open("published.json", encoding="utf-8"))
pkg = idx["packages"][0]
entries = [(p["version"], p["url"], p["checksum"]) for p in pkg["platforms"]]
entries += [(t["version"], s["url"], s["checksum"])
            for t in pkg["tools"] for s in t["systems"]]
for version, url, checksum in entries:
    with urllib.request.urlopen(url, timeout=60) as response:
        digest = hashlib.sha256(response.read()).hexdigest()
    want = checksum.split(":", 1)[1].lower()
    print(f"{version:<8} {'OK ' if digest == want else 'MISMATCH'} {url}")
PY
```

> プロキシの内側では `HTTP_PROXY` / `HTTPS_PROXY` を設定してください。
> `urllib` はこれを見ます（`arduino-cli` は見ません）。同じ理由で、
> `check_release_artifacts.py` が引き継いだ版の URL を叩く検査も、
> プロキシ未設定だと接続タイムアウトで全件失敗します。

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
- **ランタイムを要求する例題には、選択中の構成を見るガードを付けてください。**
  `install_platform.py` が各メニュー項目に
  `-DTOPPERS_FMP3_RUNTIME_<構成>`（`BT_CLASSIC` / `WIFI_CONNECT` /
  `M5_UNIFIED` / `MINIMAL` / `ALL_IN_ONE`）と
  `-DTOPPERS_FMP3_RUNTIME_SELECTED=1` を渡します。**これが無いと、構成の
  選び間違いはリンカまで到達して、メニューではなく未定義シンボルの一覧として
  出ます。** `ToppersFMP3_BT.cpp` や `ToppersFMP3_WiFi.cpp` はどの構成でも
  コンパイルが通り、その先のシンボルが 1 つのステージにしか無いためです。
  `SELECTED` を条件に入れるのは、この define を持たない古い platform に対して
  ガードが誤って発火しないようにするためです。例題と構成の対応は
  `scripts/verify_package.py` の `PROFILES` が正本です。
- **多重定義もリンクでは捕まりません。** リンクは常に
  `-Wl,--allow-multiple-definition` を付けるので、重複があっても通り、
  どちらが生き残るかはオブジェクト名の順序で決まります。
  `scripts/audit_duplicate_symbols.py` がステージ生成の最後に検査します。
- **リンク順序は ordinal**（バイト単位・大文字小文字を区別）です。ロケール依存や
  大文字小文字を無視するソートでは別のイメージになります。
- **板や構成を追加・変更するときは、表駆動の各テーブルを揃えてください**
  （段5 で「3 構成 x 例題」「両ボード」という書き方から、板ごとに選べる
  構成が違う 4 板の表駆動へ変わりました）。触る箇所は次の名前で引けます。
  - `scripts/verify_package.py` の `BOARD_PROFILES`（板 -> 選べる構成の集合）
    と `PROFILES`（構成 -> 例題）。本数の導出はこの 2 つの直積です。
  - `scripts/build_prebuilt_stages.py` の chip -> profile の対応表（`CHIPS`）。
  - `ports/<xtensa|riscv>/runtime/CMakeLists.txt` の構成分岐。
  - `scripts/install_platform.py` の `BOARDS`／メニュー定義と
    `UPLOAD_SIZE_OVERRIDES`（size 表示の分母を上書きする板だけの表）。
  - `packaging/release-allowlist.json` の `releaseArtifacts.platformArchive`
    配下、`prebuiltStages`（チップ -> 必須 profile）と
    `chipToolDependencies`（チップ -> 必須ツール）の 2 つの表。
  - `.github/workflows/verify-package.yml` の chip ループ・板名検査・
    stage 存在検査の spec 文字列。

  **このうち機械的に一致を強制されている（ドリフト検査がある）のは次だけです。**
  `scripts/test_check_release_artifacts.py:352-380` が、`packaging/
  release-allowlist.json` の `prebuiltStages`／`chipToolDependencies` と、
  `make_package_index.CHIP_TOOL_DEPENDENCIES`、`install_platform.
  EXPECTED_PROFILES`＋`CHIP_ONLY_ENTRIES`、`xcheck_compare.profiles_for()`
  （`build_prebuilt_stages.CHIPS` から導出）の 4 つが同じ内容であることを
  比較します。**それ以外**--`verify_package.py` の `BOARD_PROFILES`／
  `PROFILES`、CI yml の spec 文字列と板名リスト、`ports/*/runtime/
  CMakeLists.txt` の構成分岐、`install_platform.py` の `BOARDS`／
  メニュー定義／`UPLOAD_SIZE_OVERRIDES`--は**どのテストにも縫い付けられて
  いません**。ずれても `test_check_release_artifacts.py` は落ちず、実際に
  `verify_package.py` を走らせるか CI を回さない限り気づけません
  （レビューで `BOARD_PROFILES` に架空の `"m5"` を M5NanoC6 行へ足しても
  テストは green のままであることを実演済み）。

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

  > **例外（M5NanoC6、D8、段5 判断 S5-2 で維持）。** `ports/m5stack_riscv/
  > runtime/wifi/` は、この原則から逸脱する ESP-IDF 原本ファイルを計 9 本
  > vendoring しています。
  >
  > - **esp-idf 原本 6 本**（Apache-2.0）: `periph_ctrl.c` `modem_clock.c`
  >   `modem_clock_hal.c` `efuse_hal.c` `efuse_hal_esp32c6.c`
  >   `phy_init_data.c`。**理由**: M5Stack core の `esp32c6-libs` が持つ
  >   `.a`（`libesp_hw_support.a`／`libhal.a`）のこれらに対応するメンバは、
  >   `vPortEnterCritical`／`vPortExitCritical`／`xPortInIsrContext`
  >   （FreeRTOS のクリティカルセクション API）を未解決参照として要求します
  >   が、FMP3 はこれらを提供しません。そのため `.a` のメンバをそのまま
  >   リンクする経路は使えず、開発リポジトリはこの 6 本を esp-idf の
  >   ソースからコンパイルし、FreeRTOS スタブ（`esp/bt/stub/include`）に
  >   対してリンクしています（出典: 開発リポジトリ
  >   `.steering/20260915-c6-arduino-plan/INVESTIGATION.md` 2-3 節）。
  > - **lwIP contrib ヘッダ 3 本**（BSD-3-Clause）: `ping.h` `tcpecho_raw.h`
  >   `udpecho_raw.h`。`netif_esp32s3.c` が include するが、M5Stack core の
  >   SDK は lwIP contrib apps のヘッダを含まない（実体は `liblwip.a` の
  >   中にある）ため、ヘッダだけを補っている。
  >
  > **出自**は開発リポジトリ（`https://github.com/exshonda/
  > fmp3_esp_idf_dev.git`）で、内容は無改変・原ライセンスヘッダ保持。
  > 1 本ごとの正確な出自と改変境界は
  > [`ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md`](ports/m5stack_riscv/runtime/IMPORT_PROVENANCE.md)
  > （このファイルでは再掲しない）。**再評価の条件**は、(1) 「core の
  > `.a` メンバ + `vPort*` シム」経路を実際に検証する、または (2) 固定中の
  > M5Stack core バージョンが動く、のどちらかが起きたときです。いずれも
  > 段5 時点ではまだ起きていません。それまではこの例外のまま維持します
  > （段5 判断 S5-2）。
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
- **`scripts/capture_c6_usj.sh` は開発者向けの採取台本で、配布しません。**
  M5NanoC6 の実機 USB Serial/JTAG からログを採り、`C6_REDACT_ONLY` モードで
  SSID／パスワード／割当 IP などを機械的に伏字化するためのものです
  （`git ls-files` で追跡ファイルを拒否し、`LOG_DIR` の外のファイルも
  `C6_REDACT_ANYWHERE=1` を明示しない限り拒否する guard 付き）。
  `packaging/release-allowlist.json` にはこの台本のエントリを置いていません。
- **ライセンスはリポジトリ単一ではありません。** 各ファイルのヘッダと
  `THIRD_PARTY_NOTICES.md` が正で、`LICENSE` はこのリポジトリ向けに書かれた
  部分に適用されます。取り込んだファイルはヘッダを保持してください。

## X-check -- 共有スクリプトが Xtensa 3 板の配布物を変えていないことの機械判定

M5NanoC6 の追加は `build_prebuilt_stages.py` / `install_platform.py` /
`fmp3_link.py` など、Xtensa 3 板も使う共有スクリプトを触ります。「Xtensa は
変えていない」をバイト列で示すのが X-check です。

```bash
# 作業前（作業ツリーが clean な段の起点で 1 回）
python scripts/xcheck_baseline.py            # 既定は Xtensa 7 stage を退避
# 作業後、対象 chip を建て直してから
python scripts/xcheck_compare.py             # 7/7 MATCH で rc=0
python scripts/xcheck_compare.py --strict    # banner.o（__DATE__/__TIME__）も比べる
python scripts/test_xcheck.py                # 判定器自身の自己テスト
```

- **既定の比較対象は Xtensa（`esp32s3`／`esp32`）の 7 stage のままです。**
  `xcheck_baseline.py --chips esp32c6` を既定の baseline ディレクトリに対して
  実行すると、既存の baseline がある限り **`--force` 無しでは拒否されます**
  （rc=1、「先に取り直す理由がない限り上書きしない」設計）。**`--force` を
  付けると、指定した `--chips` に関わらず既存の baseline セット全体
  （Xtensa の分も含む）を消して置き換えます。** C6 だけの baseline が欲しい
  ときは、既定のディレクトリへ `--force` するのではなく、
  `--baseline-directory <別の場所>` で完全に別の置き場所を指定してください。
  C6 の golden を比較対象にするかどうかは今後の判断です（段5 時点では未定）。
- 比較するのは `link-manifest.json`（バイト一致。時刻系キーだけの差は注記つき
  MATCH）、`objects.rsp`、`objs/*.o` の sha256（`banner.o` は `--strict`
  無しでは除外）、`lib/*.a`、その他のファイル。stage が片側にしか無ければ
  差分です。
- **baseline は段の起点・作業ツリーが clean な状態で採り、編集後に採り直さない
  でください。** 編集後の生成物どうしを比べても、編集が Xtensa の配布物を
  変えたかどうかは分かりません。

## リリース経路の検証（`scripts/verify_package.py`）

パッケージを組み、Boards Manager 経由で入れ直し、対応するボードx構成を
建て直す（既定は 4 板すべて、上記「`verify_package.py` の板と構成」参照）。

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

