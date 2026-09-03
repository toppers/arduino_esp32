# Windows 側テストの残り開発項目

2026-09-02 の初回実施（`runs/2026-09-02-first-windows-run.md`）で 15 本すべてに
実結果が付き、そのとき直したものは main に入っている。ここは**直さずに残した
もの**と、実施の過程で見えた**穴**を並べたもの。各項目の一次情報は run の記録に
ある（★番号がその節を指す）。

優先順ではなく、性質で分けてある。上から順にやるべきという意味ではない。

## A. 判断が要るもの（設計上の問いを含む）

### A-1 `Test-RecipeOverride.ps1`（★14 / ★13）— 解決済み

**当初この項目には「方向が二つあり、(a) ブリッジのオブジェクトを繋ぐ /
(b) 単体で建つアプリを新設」と書いていたが、(a) の評価が誤っていた。**
コードを読み直すと、(a) はこのテストの唯一の存在意義を消す道だった。

`Test-SketchBridge.ps1` がこのテストの主張を**既に全部**検証している:

| 主張 | RecipeOverride | SketchBridge |
| --- | --- | --- |
| merged 0x10000 でのバイト一致 | ある | ある |
| `_start` / `_kernel_start_dispatch` / `sta_ker` の存在 | ある | ある |
| `app_main` / `vTaskStartScheduler` / `loopTask` の不在 | ある | ある |
| `recipe.objcopy.bin.pattern` の override | ある | ある |
| **スケッチのオブジェクトを一切リンクしない** | ある | **無い** |

差異は最後の 1 行だけ。ブリッジを繋げばその差異が消えて
`Test-SketchBridge.ps1` の重複になる。したがって決めるべきは 1 点だった:

> スケッチがリンクに一切関与せずとも FMP3 イメージを arduino-cli 経由で
> publish できることは、独立したテストに値する性質か。

**値するとして (b) を採った。** Arduino が一切関与しないところまで FMP3 が
独立していることは、この移植の設計上の主張そのもので、機械で守る場所が他に
無い。

実際の失敗も確認した。`undefined reference to 'toppers_arduino_task'` で、
`toppers_arduino_task` は `src/bridge/ArduinoSketchBridge.cpp` にある。
**アプリの cfg 7 本すべてが `ARDUINO_TASK` を作る**ので、単体で建つものが
1 つも無かった（★13）。

`fmp_app/standalone/` を新設した（`.c` / `.cfg` / `.h`、自分のタスク 1 本、
Arduino への参照なし）。テストはそれを `-Profile minimal` で建てる
（M5 にも依存しないので主張を狭く保てるうえ、M5GFX を無駄に建てない）。

**PASS。** `スケッチが 0 バイト（0%）を使っています` が主張そのもの。

これで `Test-Regression.ps1` も **7/7 PASS（149.7 秒）** になり、赤かった 2 本が
両方解消した。

### A-2 2 つのインストーラが知っているボードが食い違う（★18）

| インストーラ | 経路 | 知っているボード |
| --- | --- | --- |
| `scripts/install_platform.py` | 出荷・CI | 3 枚（`m5cores3_fmp3` / `m5core_fmp3` / `m5sticks3_fmp3`） |
| `scripts/Install-ArduinoIdeIntegration.ps1` | legacy ZIP | M5StickS3 を知らない |

上流の M5StickS3 追加（`3de123c` 以降）が、既にあった分岐を露わにしたもの。
`4247b19 Say three boards where the metadata still said two` はメタデータの
整合を取ったコミットだが PowerShell 側は対象外だったらしい。

**判断が要る点**: legacy ZIP 経路が 3 枚目を提供すべきなのか。提供するなら
`Test-ArduinoReleasePackage.ps1` の boards.txt 照合も一緒に見ること。
提供しないなら、そう決めた理由を PowerShell 側に書いておくべき
（でないと次に見た人が「漏れ」として直す）。

## B. スイートの穴（`README.md` の「★対象外」）

いずれも Linux 側の `scripts/verify_package.py` では通っている。**Windows の
ホスト経路そのもの**（PowerShell のステージビルド、Windows の arduino-cli、
gen_esp32part）はこのスイートでしか通らないので、穴はそこに残る。

### B-1 M5Core (ESP32/LX6) がスイートに 1 本も無い

15 本すべて CoreS3 前提で、実機系は `--chip esp32s3` 固定。

2026-09-02 の run で、m5-unified profile については**スイート外で**実機まで
通した（LX6 のステージを Windows で作り、焼き、両プロセッサ起動を確認）。
一度の確認はテストではないので穴は開いたままにしてある。

### B-2 M5StickS3 がスイートに無い（README の穴リストにも未記載）

上流が追加した 3 枚目。`install_platform.py` は知っているが Windows 側の
テストは 1 本も触らない。**B-1 と同じ穴が 1 枚増えた状態**で、README の
「★対象外」にも書かれていないので、まず書くべき。

### B-3 `bt-classic` — ステージを作る口は開いた（例題の実機確認は未）

**Windows でステージを作れなかったのは PowerShell 側の取り残しだった。**
ランタイム側は最初から `bt-classic` を受け付け
（`ports/m5stack_xtensa/runtime/CMakeLists.txt` の
`^(minimal|m5-unified|wifi-connect|all-in-one|bt-classic)$`）、Linux 側の
`scripts/build_prebuilt_stages.py` も profile 表・アプリ対応・チップ制約
（`CHIP_ONLY_PROFILES = {"bt-classic": "esp32"}`）を全部持っていたのに、
`New-Fmp3PrebuiltStages.ps1` にはその 3 つともなかった。

Python 側を写した。ValidateSet に `bt-classic` を追加し、アプリ名
（`bt_classic_app`）とディレクトリ（`bt_classic`）を対応させ、SDK ヘッダを
要する profile の一覧にも加え、**LX6 以外では拒否する**ようにした
（S3 に Bluetooth Classic は無いので、どのボードも選べないステージを作っても
意味がない）。

実測:

```
$ New-Fmp3PrebuiltStages.ps1 -Chip esp32s3 -Profiles bt-classic
bt-classic is LX6 only: the ESP32-S3 has no Bluetooth Classic,
so -Chip esp32s3 cannot build it. Use -Chip esp32.        EXIT=1

$ New-Fmp3PrebuiltStages.ps1 -Chip esp32 -Profiles bt-classic
bt-classic     183    1.9 build\prebuilt\esp32t-classic          EXIT=0
```

同じ ValidateSet にありながら Windows で一度も建てていなかった
`all-in-one` も確認した: `all-in-one 163 5.2`（esp32s3）で通る。

**残っている穴**: `BluetoothSPP` 例題の実機確認。M5Core が必要で、Linux 側は
`tools/bt/spp_echo_test.py` で済ませている。スイートには `bt-classic` を
建てる／焼くテストが 1 本も無いので、B-1 と同じ話になる。

### B-4 複数ボードが 1 パッケージに入った Release ZIP

`Test-ArduinoReleasePackage.ps1` は CoreS3 の FQBN しか建てない。
ボードが 3 枚になったので、A-2 と合わせて見るのが自然。

## C. テスト基盤の質

### C-1 `Test-Regression.ps1` が最初の失敗で abort する — 解決済み

失敗した 1 本で `throw` していたので、**残りが通るのか落ちるのかが分からな
かった**。2026-09-02 の run では 2 回とも 1〜2 本目で止まり、以降は 1 本ずつ
手で回した。全部走らせて最後に集計するようにした。`$results` を表にする作りは
既にあったので、`throw` を末尾へ移して落ちた本数と名前を出すだけで済んだ。

先のテストが落ちたせいで後のテストが落ちることはあり得るが、それは表に出る。
分からないより有用である。実測:

```
Arduino library                 PASS      13.5
recipe override                 FAIL      21.2
sketch bridge                   PASS      21.6
API boundary and M5Unified link PASS      85.2
M5Unified integration           PASS        69
SMP                             PASS      24.7
credential-free Wi-Fi scan      PASS      29.6
1 of 7 host-side test(s) failed: recipe override
```

（この表は A-1 を直す前のもの。7 本走ったことと集計が出ることを示すため
そのまま残す。A-1 を直した後は 7/7 PASS・149.7 秒。）

### C-2 人の操作が要る実機テストの時刻整合（★17 の教訓）

`Test-Touch.ps1` は焼き込みの**前**に「画面を触れ」と出すが、そこから判定窓が
開くまで数十秒ある。2026-09-02 の run では窓の外で押していて空振りし、
**実機の欠陥だと誤診した**。窓が開いた瞬間を出力で示すか、操作の時刻を記録
しないと同じ取り違えが起きる。

### C-3 判定行の接頭辞が毎回きっかり 8 文字欠ける（原因未特定）

```
e] FreeRTOS API boundary probe PASS      <- [APIProb が消えている
```

★19 で logtask との競合は解消し、8 回連続で末尾は無傷になった。だが接頭辞の
欠けは残っており、**毎回同じ 8 文字**である。ばらつきが無いので競合ではなく
FIFO かバッファ境界の類だと思われるが、**特定していない**。

現状の実害は無い（判定は末尾一致で読んでいる）。ただし他の出力も同じだけ
削られている可能性があり、そこは確かめていない。

### C-4 クリーンツリーからの通し実行

記録の数字は増分実行の積み重ね。`build/` を消してから 15 本を通せば本当の
回帰確認になるが、legacy 経路は例題ごとに FMP3 ランタイムを建て直すので
数時間かかる。CI ではなく人が居るときに一度やる性質のもの。

## 参照

- 実施記録と一次情報: `runs/2026-09-02-first-windows-run.md`
- スイートの一覧と前提: `README.md`
- Linux 側の対応物: `scripts/verify_package.py`
