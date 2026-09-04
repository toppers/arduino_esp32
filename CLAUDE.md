# CLAUDE.md

Claude Code（claude.ai/code）がこのリポジトリで作業するときの指針。

## このリポジトリ

M5Stack の Xtensa 機（CoreS3 / M5StickS3 = ESP32-S3、M5Stack Basic = ESP32）で、
Arduino の `setup()` / `loop()` を **TOPPERS/FMP3 SMP カーネルのタスクとして**
動かす Arduino ボードパッケージ。FreeRTOS ではなく FMP3 がブート・割込み・
スケジューラを所有する。

**開発はこのリポジトリで行う。** ここが正本で、変更はここへ入れる。

文書は日本語、ソースコメントは英語。

## 先に読むもの

このファイルは方針だけを持つ。内容を二重に書くと、次の変更で片方が古くなる。

| 知りたいこと | 場所 |
| --- | --- |
| 何ができて、何ができないか | [`README.md`](README.md) |
| リポジトリの配置と役割 | [`README.md`](README.md) の「リポジトリの構成」 |
| ビルド・パッケージング・リリースの手順 | [`BUILDING.md`](BUILDING.md) |
| **変更するときに守る制約** | [`BUILDING.md`](BUILDING.md) の「変更するときに守ること」 |
| 利用者向けの導入手順 | [`packaging/README.release.md`](packaging/README.release.md) |

**制約は `BUILDING.md` が正本。** ここには再掲しない。破ると壊れるものが
そこに並んでいるので、`ports/`・`src/`・`scripts/` を触る前に一度読むこと。

## 検査は名前ではなく事実を見る

このツリーには、リンクや目視では捕まらないものを捕まえる検査が入っている。
**通ったことを結果と読み替えないこと。**

```bash
python scripts/audit_duplicate_symbols.py    # ステージ生成の最後に自動実行
python scripts/check_host_paths.py <dir|zip> # ビルド機の絶対パスの混入
python scripts/test_check_host_paths.py      # 上の検出パターンの回帰
python scripts/check_release_artifacts.py --release-dir <dir>
```

- **リンクが通ることは動くことの証明にならない。** weak スタブは実装が無い
  構成で失敗値を返し、リンクは通る。
- **多重定義もリンクでは捕まらない。** `-Wl,--allow-multiple-definition` が
  常に付くので、どちらが生き残るかはオブジェクト名の順序で決まる。

## 実機の単発失敗を実装のせいにしない

Wi-Fi の失敗は、実装ではなく相手側の事情であることが多い。**Android の
テザリングは偽の失敗を返す。**

| 症状 | 実際の意味 |
| --- | --- |
| `NO_AP_FOUND` | AP がまだ上がっていない。再起動待ち |
| `AUTH_FAIL` | 連続試行後の一時的な拒否 |
| `4WAY_HANDSHAKE_TIMEOUT` | **パスワードが違う。** セキュリティ方式を変えると Android はパスワードを再生成する |

切り分けはこの順で行う。

1. scan で見えるか確認する
2. 再試行する
3. 変更を `git stash` して、基準側でも再現するか確かめる

**実機ログを 1 回だけ見て実装を疑わないこと。** 逆に、`AUTH_EXPIRE` で
オープン AP が落ちるのは実装側の症状で、supplicant の初期化順序を壊したときに
出る（`BUILDING.md` の「壊れ方が静かなもの」参照）。

## 出してはいけないもの

- **Wi-Fi の SSID / パスワードを commit しない。** 実機ログを文書化するときは
  SSID、BSSID、割当 IP を書かない。
- **ビルド機の絶対パスを配布物へ入れない。** `check_host_paths.py` が検出する。

いずれも `BUILDING.md` の「配布物に入れる／入れない」に理由がある。
