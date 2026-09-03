# Windows 側テストの残り開発項目

2026-09-02 の初回実施（`runs/2026-09-02-first-windows-run.md`）で 15 本すべてに
実結果が付き、そのとき直したものは main に入っている。ここは**直さずに残した
もの**と、実施の過程で見えた**穴**を並べたもの。各項目の一次情報は run の記録に
ある（★番号がその節を指す）。

優先順ではなく、性質で分けてある。上から順にやるべきという意味ではない。

## A. 判断が要るもの（設計上の問いを含む）

### A-1 `Test-RecipeOverride.ps1` — 15 本で唯一の FAIL（★14 / ★13）

`Test-Regression.ps1` もこれで abort するので、実質 2 本が赤いまま。

判定内容そのもの（published ELF/BIN が FMP3 のものと一致、`_start` /
`_kernel_start_dispatch` / `sta_ker` があり `app_main` /
`vTaskStartScheduler` / `loopTask(void*)` が無い）は、スケッチのオブジェクトを
繋いでも壊れない。禁止シンボルは M5Stack コアの Arduino main が持ち込むもので、
override がまさにそれを置き換えているからである。詰まっているのは
「Arduino タスクを作らない単体で建つアプリが無い」（★13）という前提のほう。

方向が二つあり、**どちらもこのテストの存在意義に関わる**:

| | 内容 | 代償 |
| --- | --- | --- |
| (a) | ブリッジ（＋スケッチ）のオブジェクトを繋ぐ | `Invoke-FmpImageRecipe.ps1` が `Invoke-SketchLinkRecipe.ps1` とほぼ同一になり、2 本を分けている理由が「どの recipe 行を override するか」だけになる |
| (b) | Arduino タスクを作らない最小アプリを新設 | 製品に入らないテスト専用アプリが 1 本増える |

先に決めるべきは「このテストは何を守っているのか」。それが決まれば実装は小さい。

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

### B-3 `bt-classic` profile と `BluetoothSPP` 例題

M5Core 専用。ランタイム側は受け付ける
（`ports/m5stack_xtensa/runtime/CMakeLists.txt` の
`^(minimal|m5-unified|wifi-connect|all-in-one|bt-classic)$`）のに、
`New-Fmp3PrebuiltStages.ps1` の `-Profiles` の ValidateSet は
`minimal / m5-unified / wifi-connect / all-in-one` の 4 つで **`bt-classic` が
無い**。つまり Windows ではステージを作る口が塞がっている。
Linux 側は `tools/bt/spp_echo_test.py` で実機確認済み。

### B-4 複数ボードが 1 パッケージに入った Release ZIP

`Test-ArduinoReleasePackage.ps1` は CoreS3 の FQBN しか建てない。
ボードが 3 枚になったので、A-2 と合わせて見るのが自然。

## C. テスト基盤の質

### C-1 `Test-Regression.ps1` が最初の失敗で abort する

失敗した 1 本で `throw` するので、**残りが通るのか落ちるのかが分からない**。
2026-09-02 の run では 2 回とも 1〜2 本目で止まり、以降は 1 本ずつ手で回した。
全部走らせて最後に PASS/FAIL を集計するほうが、実施記録を書くうえでも有用。
`$results` を集めて表にする作りは既にあるので、`throw` を最後に移すだけ。

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
