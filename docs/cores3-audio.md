# CoreS3 の内蔵スピーカーと画面・タッチの C ブリッジ（2026-09-29）

`m5-unified` ランタイムに、CoreS3 / CoreS3-SE の内蔵スピーカー（AW88298 アンプ、
I2S1）でトーンを鳴らす経路と、ゲーム（`ArtosRoguelike` の `DisplayProbe`）が使う
画面・タッチの C API を足した記録です。**音はまだ人の耳で確かめていません**
（下の「未確認」）。

## 何を足したか

| 部品 | 場所 | 内容 |
| --- | --- | --- |
| 画面・タッチの C API | `src/ToppersFMP3_M5UnifiedBridge.h`、`m5/adapter/m5_arduino_adapter.cpp` | `toppers_m5_poll_touch`（印を描かない）、`draw_begin` / `draw_end`、`fill_rect`、`draw_text`、`push_rgb565`（行優先 RGB565 を 1 回の `pushImage` で送る） |
| FreeRTOS ABI | `m5/shim/m5_freertos_abi.c` | キュー・セマフォ・mutex・クリティカル区間を**外部関数として**持つ（既製の IDF アーカイブや、本物のヘッダで書いたドライバが名前で呼ぶもの） |
| cfg | `fmp_app/phase5/phase5_m5_app.cfg` | ABI のスピンロック `M5_ABI_SPN`、ワーカー枠 2 本（`M5_AUDIO_TSK1` は PRC1、`M5_AUDIO_TSK2` は PRC2）、割込み確保 shim の受け口 `CRE_ISR` 4 本（線 4/8/9/12） |
| I2S 送信 | `m5/audio/m5_i2s_std_tx.c`（ESP32-S3 だけ） | `Speaker_Class` が呼ぶ 6 関数（`i2s_new_channel` ほか）。GDMA の記述子リングと EOF 割込み、空きバッファのキュー |
| `Speaker_Class.cpp` | `CMakeLists.txt` | ESP32-S3 でだけビルド対象にした（`Mic_Class` は引き続き外す） |
| スピーカーの C API | 同上のヘッダ・アダプタ | `toppers_m5_speaker_begin` / `ready` / `tone` / `stop` / `set_volume` / `amp_reg` |

### 途中で直したもの

- **`esp_timer_get_time` がコア 1 から呼ぶと 26.8 秒跳んでいた。** コアごとの CCOUNT を
  両コア共有の変数 1 組で 64bit に伸ばしていた。カーネルの `target_hrt_get_current64`
  へ置き換えた（Wi-Fi 側の shim は 9/20 にすでにこの形だった）。
- **配布している m5 の cfg に、割込み確保 shim の `CRE_ISR` が無かった。** 配線しても
  誰も受けない状態だった。
- **通知（`ulTaskNotifyTake` / `xTaskNotifyGive` の実体）が `loc_cpu` で守られていた。**
  ワーカー枠の 2 本目が PRC2 にあるので、ABI のクリティカル区間に替えた。
- **`xTaskGetCurrentTaskHandle` の実体（`esp_shim_task_get_current`）が m5 側に無かった。**
  `Speaker_Class::_in_task` が使う。

## 依存するもの

利用者のスケッチの最終リンクで、次を新たに使う（すべて M5Stack core 3.3.9 に同梱）。

| もの | どこから | 何のため |
| --- | --- | --- |
| `i2s_hal_std_set_tx_slot` / `i2s_hal_set_tx_clock` / `i2s_hal_init` / `i2s_hal_std_enable_tx_channel` | `esp32s3-libs/lib/libhal.a`（`i2s_hal.c.obj`、`hal_utils.c.obj`） | I2S の形式と初期クロックのレジスタ設定。RTOS に依存しない。ESP32-S3 の `m5-unified` の `linkLibGroup` に `-lhal` を足した |
| `gpio_matrix_out`（`esp_rom_gpio_connect_out_signal`） | ROM（`esp32s3.rom.api.ld`） | BCK / WS / DATA の配線 |
| GDMA OUT チャンネル 0、割込みソース 71 | ハードウェア | I2S1 への DMA。割込みは shim のスロット 0（CPU 線 4、PRC1） |

**LCD の SPI DMA は開けていない。** GDMA のクロックを入れるが、LCD が DMA を使うかは
lgfx の `dma_channel` で決まり、それは `phase5DisableDisplayDma` と
`m5_idf_prelude.h` で 0 のまま。`push_rgb565` が戻った時点でバッファを再利用して
よいのは、この前提があるから。

### 使っていないもの

`libesp_driver_i2s.a` と GDMA ドライバは**リンクしていない**。引き込むと FreeRTOS の
外部関数 17 本（ISR 版 3 本を含む）、`_frxt_setup_switch`、クロックツリー、GPIO 予約
API が要り、音が出ないときに中を調べられない。ABI は、将来これらを載せるための土台
として本物の意味で実装してある（下の試験）。

## 実機での確認（CoreS3、2026-09-29）

### FreeRTOS ABI

ピンに触れない試験スケッチで、割込み源はソフトウェア割込み `FROM_CPU_INTR3`
（source 82）。GDMA と同じ `esp_intr_alloc_intrstatus` の経路を通る。本物の IDF
ヘッダでコンパイルした。

| 試験 | 結果 |
| --- | --- |
| 同じコアのタスク同士、4 要素のキューに 2000 件 | 全件、順序どおり |
| コアをまたぐタスク同士、同上 | 全件、順序どおり |
| **ロックなしの対照**（両コアで 10 万回ずつ非原子的に加算） | **100,099 / 200,000**（競合が実際に起きる） |
| ABI のクリティカル区間で同じこと | 200,000 / 200,000 |
| ISR からタスクへ 500 回 | 全件 ISR 文脈、順序どおり |
| ISR の受信が PRC2 の送信待ちを起こす | 100/100、最悪 31 µs |
| 50 tick の timeout | 50,023 µs |
| プール枯渇 | NULL を返し、カウンタが 1 増える |

**対照は最初「競合が出ない」結果だった**（PRC1 の worker がスケッチより優先度が高く、
指令を受けた瞬間に走り切ってしまい PRC2 と重なっていなかった）。その状態の PASS は
ロックの証拠にならないので、同時に始まるよう組み直してから本番の行を読んでいる。

### 時刻関数

両コアで同時に `esp_shim_time_us` を 32 秒（カーネル時刻で計った。CCOUNT の 1 周
26.8 秒より長い）読み続けた。

| | 修正前 | 修正後 |
| --- | --- | --- |
| 32 秒の経過（PRC1 / PRC2） | 約 22.8 年 / 約 22.8 年 | 31,997,582 / 31,999,989 µs |
| 1 回の最大の跳び | 26,843,559 µs | 222 / 8 µs |

### スピーカー

試験スケッチ（208x192 の一括転送を毎フレーム行いながら、500 ms ごとに
`AudioCues.h` と同じ 3 音を鳴らす）。

| 見たもの | 結果 |
| --- | --- |
| ゲームの弱参照 `toppers_m5_speaker_tone` | ELF で `T`（強い定義に解決） |
| AW88298 の ID（reg 0x00） | 0x1852 |
| SYSCTRL（0x04） | begin 前 0x4003 → begin 後 **0x4040**（M5Unified が書く I2SEN=1 AMPPD=0 PWDN=0） |
| I2SCTRL（0x06） | 0x14C8（48 kHz に対して M5Unified が計算する値そのもの） |
| SYSST（0x01） | I2S 開始前 0x4800 または 0x0021 → 開始後 0x0311（ビットの意味はデータシートで未確認） |
| **実際のサンプルレート** | バッファ完了割込み 187〜188 回/秒 × 256 フレーム = **47,872〜48,128 Hz** |
| 表示 | 30 fps、最悪フレーム 21.4 ms（音を鳴らさないときと同等） |
| トーンの受付 | 30/30。範囲外の引数（0 Hz、0 ms、30 kHz）は -2 |
| リセット・例外 | なし（16 秒） |

**音量**：振幅は「全体音量の二乗 × トーンの音量の二乗」に比例する（M5Unified の
`Speaker_Class` の計算そのまま）。1 kHz のトーンで書かれた PCM の最大振幅
（最大 32767）:

| 全体音量 | トーンの音量 | 最大振幅 |
| --- | --- | --- |
| 64（M5 の既定） | 48 | 36 |
| 64 | 64 | 64 |
| 64 | 255 | 1,009 |
| 128 | 64 | 254 |
| 255 | 48 | 568 |
| 255 | 64 | 1,009 |
| 255 | 255 | 16,004 |

`AudioCues.h` の 64 / 48 を既定の全体音量のまま鳴らすと、最大振幅は 64 / 36
（-54 dBFS 前後）でとても小さい。`toppers_m5_speaker_set_volume` で全体音量を
上げるか、トーンの音量を上げる必要がありそう。**どの値が適当かは試聴で決める。**

### ほかの構成が変わっていないこと

X-check（基準は変更前の `dbbb054`）で、`esp32s3` の `minimal` / `wifi-connect`、
`esp32` の `minimal` / `wifi-connect` / `bt-classic` の 5 ステージがバイト単位で一致。
差分は両チップの `m5-unified` だけで、中身は上の変更そのもの。

## 未確認

- **音そのもの。** 耳で聞いていない。起動音（1 kHz、200 ms、音量 24）と 3 つの効果音が
  鳴るか、音切れ・雑音が無いかは、移植先で書き込んで確かめる。
- **CoreS3-SE。** 同じ経路（M5Unified の設定も同じ）だが実機が無い。
- **ゲーム本体の `DisplayProbe`。** この PC に無いので、同じ API を同じ使い方で呼ぶ
  代用スケッチで確かめた。
- **スクロールとチラつきの目視。** 代用スケッチの fps と転送時間までしか見ていない。
- **タッチの実入力。** 毎フレームの取得が回り続けることまで（人が触れていない）。
- **M5Stack Basic（LX6）と M5StickS3。** スピーカーの C API は -1 を返す（Basic は
  `Speaker_Class` をビルドしない、StickS3 は経路を試していないので断る）。
- **BGM（PCM の連続出力と混合）。** メモの段階 5。トーンの試聴のあとに進める。
- **mutex の優先度継承。** ABI の mutex は排他はするが継承しない。
- **ABI 上で既製の IDF ドライバを動かすこと。** 自前のドライバが ABI の上で動くことは
  確かめたが、`libesp_driver_i2s.a` を載せる突き合わせはしていない。
