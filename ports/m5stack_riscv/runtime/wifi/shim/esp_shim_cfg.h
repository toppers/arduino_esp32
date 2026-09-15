/*
 *  Wi-Fi os_adapter shimの静的プール構成（esp_shim.cfgと一致させること）
 */
#ifndef ESP_SHIM_CFG_H
#define ESP_SHIM_CFG_H

/*  無印ESP32：WiFi driver/PHY blob タスクは FPU（rate制御・PHY較正、
 *  ram_rfpll_set_freq 等）を使う。本ポートは eager FPU 保存方式で、TA_FPU 無しの
 *  タスクは CPENABLE=0 で走るため FPU 命令で Coprocessor0Disabled(EXCCAUSE=32)。
 *  shim タスクに TA_FPU を付与する。TOPPERS_ESP32_LX6 は arch/xtensa_gcc/esp32/
 *  chip_stddef.h で定義（S3 では未定義＝非対象・非退行）。 */
#if defined(TOPPERS_ESP32_LX6)
#define SHIM_TA_FPU  TA_FPU
#else
#define SHIM_TA_FPU  TA_NULL
#endif

/*
 *  2026-08-04（段1）: **`ESP_SHIM_NUM_MTX` は廃止した。**
 *
 *  ミューテックスは静的プールをやめ、`AID_MTX(16)`＋`acre_mtx`/`del_mtx` へ
 *  無条件で移した（`esp/shim/esp_shim.cfg`・`esp/shim/esp_shim_mtx.c`）。
 *  ⇒ **個数を書く場所は cfg の `AID_MTX()` ただ 1 箇所**になり、
 *  「cfg と C 配列とマクロの 3 箇所に同じ数を書く」構造そのものが消えた。
 *  数の根拠（および「16 は決めた数ではない」こと）は `esp_shim.cfg` の
 *  `AID_MTX` のコメントが正本。記録: `.steering/20260804-dcre-stage1-mtx/`。
 *
 *  2026-08-04（段2）: **`ESP_SHIM_NUM_SEM` も同じ理由で廃止した。**
 *  `AID_SEM(28)`＋`acre_sem`/`del_sem`（`esp/shim/esp_shim.cfg`・
 *  `esp/shim/esp_shim_sem.c`）。数の根拠（および「28 は決めた数ではない」こと、
 *  Classic の 96 を採らなかった理由）は `esp_shim.cfg` の `AID_SEM` の
 *  コメントが正本。記録: `.steering/20260804-dcre-stage2-sem/`。
 *  シム自身の固定セマフォ（`SHIM_TIMER_SEM`／`SHIM_QSEM*`／`ESP_TIMER_SEM`）は
 *  **静的のまま**——プールではないので射程外である。
 *  他の `ESP_SHIM_NUM_*`（DTQ/TSK/FLG）は**この段でも触っていない**。
 *
 *  2026-08-04（段3）: **`ESP_SHIM_NUM_DTQ` は残すが、意味が変わった。**
 *  データキュー実体は静的プールをやめ、`AID_DTQ(ESP_SHIM_NUM_DTQ)`＋
 *  `acre_dtq`/`del_dtq`（`esp/shim/esp_shim.cfg`・`esp/shim/esp_shim_dtq.c`）へ
 *  無条件で移した。管理領域（`dtqmb`）はカーネルメモリプールではなく
 *  **シムヒープから要求された深さちょうど**を供給する（案B。理由と却下した
 *  選択肢は `esp_shim_dtq.c` 冒頭が正本）。
 *  ⇒ 本マクロは今や **「シムのキュースロット数」**であり、
 *    **cfg の `AID_DTQ()` と `esp_shim.c` の `shim_que[]` の両方がここを読む**
 *    ＝**個数を書く場所は 1 箇所**（旧: cfg の `CRE_DTQ` 17 行＋C 配列＋本マクロの 3 箇所）。
 *  `ESP_SHIM_NUM_TSK`／`ESP_SHIM_NUM_FLG` は**この段でも触っていない**。
 *  記録: `.steering/20260804-dcre-stage3-dtq/`。
 *
 *  2026-08-04（段4）: **`ESP_SHIM_NUM_TSK` は残すが、意味が変わった。**
 *  タスク実体は静的プールをやめ、`AID_TSK(ESP_SHIM_NUM_TSK)`＋
 *  `acre_tsk`/`mact_tsk`/`del_tsk`（`esp/shim/esp_shim.cfg`・`esp/shim/esp_shim_tsk.c`）へ
 *  無条件で移した。スタックはカーネルメモリプールではなく
 *  **シムが持つ静的スロット配列**（`shim_tsk_stack[][]`。旧 `_kernel_stack_SHIM_TSK*` と
 *  同じ本数・同じサイズ）から供給する（案B。理由と却下した選択肢は
 *  `esp_shim_tsk.c` 冒頭が正本）。
 *  ⇒ 本マクロは今や **「シムのタスクスロット数」**であり、
 *    **cfg の `AID_TSK()`・`esp_shim.c` の `shim_tsk[]`・`esp_shim_tsk.c` の
 *      `shim_tsk_stack[]` の 3 つが**すべて**ここを読む**
 *    ＝**個数を書く場所は 1 箇所**（旧: cfg の `CRE_TSK` 8 行＋C 配列 `tskid_tbl[]`＋本マクロ）。
 *  `ESP_SHIM_TSK_STKSZ`（下記）の意味は**変えていない**——1 スロットあたりの
 *    スタック長であり、`stack_size > ESP_SHIM_TSK_STKSZ` の fail-closed も従来どおり。
 *  `ESP_SHIM_NUM_FLG` と `SHIM_AUDIO_TSK1/2` は**この段でも触っていない**。
 *  記録: `.steering/20260804-dcre-stage4-tsk/`。
 *
 *  プールの規模．
 *
 *  NimBLE（Phase BT-2）を積むBTビルド（TOPPERS_BT_HOST_NIMBLE）では，NPL
 *  （npl_os_freertos.c）がeventq→xQueueCreate（DTQ），mutex→
 *  xSemaphoreCreateRecursiveMutex（MTX），sem→xSemaphoreCreateCounting
 *  （SEM），ホストタスク→esp_shim_task_create（TSK）へ写像するため，
 *  コントローラ使用分と衝突しないよう各プールを小幅に拡張する。
 *
 *  esp_shimはWi-Fiビルドとも共有だが，拡張分はTOPPERS_BT_HOST_NIMBLE
 *  （NimBLEホストを積むビルドのみ立つ．esp/build_ble_incflags.txtで定義）
 *  限定にして，WiFiビルドおよびBTコントローラ単体（bt_smoke）のRAMを
 *  従来通りに保つ（esp_shim.cfg／esp_shim.cの配列・CRE_*と一致させること）。
 */
/*
 *  W3(BT Classic/SPP)②：BlueDroidホスト（TOPPERS_ESP32_BT_BLUEDROID_CLASSIC、
 *  build_bt_classic_bluedroid_smoke_esp32.sh限定のEXTRA_OFLAGS）は，NimBLE
 *  （薄い単一eventq/NPL）よりosi面が広い（BTU_TASK/BTC_TASK各々が内部
 *  ワークキューを複数持つ＋osi_alarm/futureが追加のキュー・セマフォを要求）。
 *  実機ログ（esp_bluedroid_init()直後、BTC_TASK生成前後で計4件のqueue_create
 *  が連鎖し，従来の4枠プールでは"queue pool exhausted"に到達することを確認
 *  済み，.steering/20260714-esp32-classic-wifi-bt/steering.md参照）ため，
 *  NimBLEより大きめに確保する。TSK/MTX/SEMも同様の余裕を持たせる
 *  （esp_shim.cfg／esp_shim.cの配列・CRE_*と一致させること）。
 */
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
/*  2026-08-04（段2）: ここに在った `ESP_SHIM_NUM_SEM 96` は削除した。
 *  当時の観測（BlueDroidホストは osi_sem を多用し、`esp_spp_start_srv()` 到達時点で
 *  32 枠を使い切って "sem pool exhausted"→SPP_START_EVT status=4(NO_RESOURCE)、
 *  RFCOMM 接続確立で 64 も枯渇）自体は**取り消していない**。
 *  しかし本分岐は現在**どのビルド定義からも立たない**（classic 全廃・
 *  復元は branch `archive/trb-classic-cfg`）ので、96 を無条件予約にすると
 *  **建てられる構成の DRAM を +2.3 KB 食うだけ**になる。
 *  ⇒ `AID_SEM(28)` を採った。**Classic を復活させる人は数を測り直すこと。**
 *  根拠と実測は `esp/shim/esp_shim.cfg` の `AID_SEM` コメントが正本。 */
/*  TSKはESP_SHIM_TSK_STKSZ(8192B)/slotとDRAM予算への影響が大きいため
 *  NimBLE同様8（+2）に留める（実機ログでbtController/BTC_TASKの2枠しか
 *  同時使用を確認しておらず，BTU_TASK追加を見込んでも3〜4枠で足りる
 *  想定。8超が必要と判明した場合はDRAM予算（W3③）と要相談で再検討）。 */
#define ESP_SHIM_NUM_TSK    8     /* 6→8：BTU_TASK/BTC_TASK分+2 */
/*  W3④（SPP本体）：btc_spp.cのtx_event_group（EventGroupHandle_t、
 *  xEventGroupCreate 1回のみ）向けのイベントフラグプール。BlueDroid
 *  ClassicのSPP専用のためBLUEDROID_CLASSIC限定（esp_shim.c側もこの
 *  マクロでガードし，NUM_FLG未定義のビルド（Wi-Fi/NimBLE）ではプール
 *  実装自体をコンパイルしない＝W1/W2への影響ゼロ）。1個で足りるが
 *  再利用サイクル（delete直後の再create等）に余裕を持たせ2個確保。 */
#define ESP_SHIM_NUM_FLG    2     /* SPP tx_event_group用（新規） */
#elif defined(TOPPERS_BT_HOST_NIMBLE)
#define ESP_SHIM_NUM_TSK    8     /* 6→8：NimBLEホストタスク分+2 */
#else
/*
 *  ------------------------------------------------------------------------
 *  2026-08-15（BL-G-3 段3）: **`ESP_SHIM_NUM_DTQ` と `ESP_SHIM_DTQ_CNT` を削除した。**
 *  ------------------------------------------------------------------------
 *  キュー実装（`esp_shim.c` の `esp_shim_queue_*`＝FMP3 のデータキュー）を
 *  撤去したので、その本数と深さを決めるマクロも要らなくなった。
 *  ここには 3 分岐（BlueDroid Classic 16 ／ NimBLE 8 ／ 既定 4）と、
 *  「なぜ M5_SDSPI の +2 が要らなくなったか」という 2026-08-04 の実測記録が在った。
 *  その実測（SD は `m5_que_*` へ写っていて DTQ を 1 本も取らない）は
 *  **本段でさらに一歩進んだ**——`m5_que_*` 自体も `esp_shim_ring_*` へ寄せ、
 *  DTQ の消費者は 0 になった。⇒ プールごと消えた。
 *  今のキューの本数上限は `ESP_SHIM_RING_MAX`（`esp/shim/esp_shim_ring.c`。8）。
 *  復元は `git show <このコミットの親>:esp/shim/esp_shim_cfg.h`。
 */
#define ESP_SHIM_NUM_TSK    6     /* タスクプール（各スタックESP_SHIM_TSK_STKSZ） */
#endif

/*
 *  shimタスクプール1スロットあたりのスタックサイズ
 *
 *  Wi-Fi構成は8192→6656（2026-07-17、実機実測に基づく）。
 *
 *  背景：`2ff830f`（ROM newlib stub table）がTCBを148B→416B（+268B/task）に
 *  肥大させた結果、S3のWi-Fi PRC_NUM=2が`.kernel_bss`のDRAM超過1648Bで
 *  リンク不能になった（.steering/20260717-s3-wifi-build-broken-by-rom-libc/
 *  evidence-00 §5-2）。`struct _reent`はROMが`_r48`をoffset 56で決め打ちで
 *  辿るため縮小できない。よってSHIM_TSKスタック側でDRAMを空ける
 *  （ユーザー決定 2026-07-17）。
 *
 *  実測（DevKitC-1 f4:12:fa:5b:4a:58、W1＝STA接続+GOT IP+ping、
 *    -DTOPPERS_STACK_PROBE でスタック全域を0xA5A5A5A5に塗ってhigh-water
 *    markを採取。独立3回・各4サンプルで同値＝飽和を確認）：
 *
 *    - Wi-Fi blobが生成するタスクは **'wifi' の1本のみ**（要求6656B）。
 *      残り5スロットは used=0＝一度も起動されない。
 *    - 'wifi'タスクの実使用量 **1840B**（8192B中22.5%）。
 *
 *  6656はblob自身の要求値（＝ESP-IDFのCONFIG_ESP32_WIFI_TASK_STACK_SIZE
 *  既定値）そのものであり、これを下回らない値を選ぶことで
 *  「blobが要求した分は必ず与える」というESP-IDF同等の前提を保つ。
 *  実測ピーク1840Bに対して余裕4816B（72.4%）。
 *  esp_shim_task_create()の`stack_size > ESP_SHIM_TSK_STKSZ`警告も
 *  6656==6656で成立しない（＝要求を切り詰めていない）。
 *
 *  削減量：(8192-6656)×6スロット＝**9216B**（必要量1648Bの5.6倍）。
 *
 *  縮小対象を「S3のWi-Fi構成」だけに絞る理由（＝測っていない構成は縮めない）：
 *
 *  - BT（NimBLE/BlueDroid Classic）：スタック実使用量が未実測。BTビルドは
 *    現状PRC=1/2ともリンクできておりDRAMを空ける必要が無い。→ 8192据置。
 *  - LX6（無印ESP32、TOPPERS_ESP32_LX6）：本ヘッダはLX6と共有だが、DRAM超過は
 *    S3固有の問題（`2ff830f`は`#ifdef TOPPERS_ESP32S3`ガードでLX6のTCBは
 *    不変＝148B）。LX6側は逼迫しておらず、かつ上記実測はS3実機のものなので
 *    LX6へ流用しない。→ 8192据置＝厳密な非回帰。
 *
 *  TOPPERS_ESP32_LX6はLX6ビルドのコマンドラインで`-DTOPPERS_ESP32_LX6`として必ず
 *  定義され、S3ビルドでは定義されない（S3は`-DTOPPERS_ESP32S3`）＝実測確認済み。
 *
 *  ------------------------------------------------------------------------
 *  2026-08-04（C-2-4）：上の「未実測」は**要求量については解消した**
 *  ------------------------------------------------------------------------
 *  ここで解消したのは **「blob がいくら要求するか」**であって、
 *    **「実際に何バイト使うか（high-water mark）」ではない**。
 *    BT・LX6 の**実使用量は今も未実測**であり、上の 8192 据置の根拠
 *    （＝実使用量を測っていない構成は縮めない）は**そのまま有効**である。
 *
 *  要求量の実測（生ログ `.steering/20260804-blobpool-stack/logs/`）:
 *    | 構成                | プール | タスク         | 要求  | 出所                     |
 *    |---------------------|-------:|----------------|------:|--------------------------|
 *    | S3 Wi-Fi(6656)      |   6656 | 'wifi'         |  6656 | 実機（CoreS3・m5-wifi）  |
 *    | LX6 Wi-Fi(8192)     |   8192 | 'wifi'         |  6656 | 実機（78:21:84:a6:5c:64）|
 *    | NimBLE(8192)        |   8192 | 'btController' |  4096 | 静的（逆アセンブル）     |
 *    | NimBLE(8192)        |   8192 | 'nimble_host'  |  4096 | 静的（即値 0x1000）      |
 *  Wi-Fi blob が作るタスクは 'wifi' **1 本のみ**（実機 TSKREQ total=1、両チップ）。
 *  `config_get_wifi_task_stack_size()` の逆アセンブル（S3/LX6 とも同一ロジック）から、
 *    'wifi' の要求は {3072, 3584, 6144, 6656} のいずれか＝**静的上限 6656**。
 *  BT の 2 値は**静的根拠のみ**（seam-s3-ble は UART0 コンソール構成で、手元の
 *    CoreS3 に UART ブリッジが無く実機で測れていない）。
 *  ⇒ 現行 7 構成のいずれも要求 ≦ プール。これを根拠に esp_shim.c の
 *    `stack_size > ESP_SHIM_TSK_STKSZ` を **fail-closed（return 0）** にした。
 */
#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
#define ESP_SHIM_TSK_STKSZ  8192  /* BT：実使用量が未実測のため据置（非回帰）。
                                     要求量は btController/nimble_host とも 4096（静的） */
#elif defined(TOPPERS_ESP32_LX6)
#define ESP_SHIM_TSK_STKSZ  8192  /* LX6：実使用量が未実測＆DRAM逼迫なしのため据置（非回帰）。
                                     要求量は 'wifi' 6656（2026-08-04 実機実測） */
#else
#define ESP_SHIM_TSK_STKSZ  6656  /* S3 Wi-Fi：blob要求値ちょうど（実測使用1840B） */
#endif

/*
 *  ヒープサイズ（静的配列．Wi-Fi blobは実測で数十KBを要求する）
 *
 *  2026-08-07（BL-E-1）: 旧値 124KB（=126,976B）は「現在の木では実測の
 *  裏づけを持たない数字」（esp_shim.c:631 参照）だった。実機で 4 構成を
 *  実測し（`esp_shim_heap_peak_used()`、`.steering/20260807-p17-realhw-batch/
 *  RESULT.md` §3・`.steering/20260807-p21-heap-reduction/RESULT.md`）、
 *  高水位の最大値が確定した:
 *    seam-s3-ble（未接続）      50,312B
 *    seam-s3-ble（BLEピア接続） 50,432B  ← 本段で追加測定
 *    seam-lx6-wifi              66,912B
 *    seam-s3-wifi               67,816B / 67,848B（2 回。32B 揺れる）
 *  ⇒ 実測最大値は 67,848B（seam-s3-wifi）。BLE はピア接続時でも
 *    Wi-Fi 系より小さいままだった（測る前の懸念は外れた）。
 *  ただし測ったのは「起動→（BLE は）ピア接続→ping/GATT操作」までの
 *    限られたシナリオであり、エラー・再送・長時間運用等の未測パスは
 *    ある。**安全側に寄せて**、実測最大値に対し約 31% の余裕
 *    （98,304 - 67,848 = 30,456B）を残す 96KB へ縮めた
 *    （旧値からの削減は 126,976 - 98,304 = 28,672B）。
 *    A1_SHIM_HEAP_SIZE（CMake opt-in）で個別に上書き測定できる。
 */
#ifndef ESP_SHIM_HEAP_SIZE
#define ESP_SHIM_HEAP_SIZE  (96 * 1024)
#endif

/*
 *  shimタスクの優先度（ASP3．小さいほど高優先度．アプリはより低い
 *  優先度（10前後）で動かすこと）
 */
#define ESP_SHIM_TIMER_TASK_PRI   2   /* ets_timerディスパッチ */
#define ESP_SHIM_WIFI_TASK_PRI    3   /* blobが生成するタスク（一律、下記BT_CTRL以外） */

#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
/*
 *  BT-4調査（接続確立直後の即時切断）の副産物：esp_shim_task_create()は
 *  freertos_prio引数を(void)で完全無視し，全shimタスクをESP_SHIM_WIFI_TASK_PRI
 *  一律で生成していた。実ESP-IDFではBTコントローラタスク（"btController"，
 *  ble_hs_smoke.c/bt_smoke.cのcfg.controller_task_prio=ESP_TASK_BT_CONTROLLER_PRIO
 *  =configMAX_PRIORITIES-2=23，実機ログで確認済み）がほぼ最高優先度で動作すべき
 *  ところ，本ポートではNimBLEホストタスク（"nimble_host"，configMAX_PRIORITIES-4
 *  =21で生成要求）と同一優先度になっていた。advertising中はホストタスクがidleで
 *  競合しないが，接続確立の瞬間からホストタスクが起床しコントローラタスクと
 *  同一優先度で競合する非対称性が「確立できるが維持できない」を説明し得るとの
 *  仮説に基づき，コントローラ級タスク（freertos_prio>=閾値）をSHIM_TIMER_TSKと
 *  同格の高優先度スロットへ静的に割り当てる（esp_shim.cfgのSHIM_TSK1参照）。
 *  TOPPERS_BT_HOST_NIMBLE限定のため，WiFiビルド・bt_smoke（BT-1）は
 *  従来通り無変更（ESP_SHIM_WIFI_TASK_PRI一律）のまま非退行。
 *  詳細: .steering/20260709-ble-bt4-connection/steering.md
 */
#define ESP_SHIM_BT_CTRL_TASK_PRI          2   /* SHIM_TIMER_TSKと同格 */
#define ESP_SHIM_BT_CTRL_FREERTOS_PRIO_MIN 22  /* ESP_TASK_BT_CONTROLLER_PRIO(=23)以上を
                                                   高優先度スロットへ。nimble_host(=21)は
                                                   対象外＝従来通りESP_SHIM_WIFI_TASK_PRI */
#endif

#ifdef M5_SHIM_AUDIO
/*
 *  音声（M5Unified Speaker/Mic）専用プール（2026-08-01・台帳5）。
 *
 *  `esp_shim_task_create()`（`esp_shim.c`）は本来 BT/WiFi blob 専用に設計されており、
 *  全生成タスクを一律 `SHIM_TA_FPU`（**S3 では TA_NULL**）・`ESP_SHIM_WIFI_TASK_PRI`
 *  （blob と同格）で `SHIM_TSK1..8` プールへ落とす。音声タスク（"spk_task"/"mic_task"）が
 *  そこへ紛れ込むと **FPU 無しでコンテキストスイッチされる**（音声は浮動小数点を使う）。
 *  ⇒ **名前で分岐**し、この専用プールへ振り分ける（`esp_shim.c` 側の実装）。
 *  設計 `.steering/20260801-audio-composite/DESIGN.md`。
 *
 *  `TA_FPU` は固定（`SHIM_TA_FPU` を使わない）——`SHIM_TA_FPU` は LX6 blob 都合の
 *  マクロ（無印ESP32=TA_FPU/それ以外=TA_NULL）であって音声とは無関係。音声は
 *  **両チップとも** FPU が要る。
 *  優先度 9 は `MAIN_PRIORITY(=10)-1`（`m5_wifi_smoke.h`）と**値を直接一致**させた
 *  （マクロは cfg 間で共有されていないため。ドキュメント化済みの優先度地図
 *  ―― 2=timer/3=blob/4=net/5=esp_timer/10=main ―― に対し 9 は未使用）。
 *  スタック 4096 は素の m5 側の音声プール（`m5_kernel_shim.c`）と同じ値
 *  （M5Unified の実要求「1280+dma_buf_len*4≈2.3KB」に対する既存の実績値）。
 *  blob 用の `ESP_SHIM_TSK_STKSZ`（BT/WiFi 用）とは別の値にする。
 *  `M5_USE_ESP_SHIM` は `A1_VARIANT=m5 かつ A1_M5_APP=wifi|factory` でのみ定義される
 *  （CMakeLists.txt）ため、wifi/ble/lx6-wifi の 3 構成では**このブロック自体が
 *  存在しない**＝golden 不変。
 *
 *  ------------------------------------------------------------------------
 *  C-2-5（2026-08-03・T-5）: **使わない構成でも占有する DRAM を記録する**
 *  ------------------------------------------------------------------------
 *  `SHIM_AUDIO_TSK1/2` は `esp_shim.cfg` で **`M5_USE_ESP_SHIM` だけを条件に**
 *  静的生成される。⇒ **音声を 1 度も呼ばない構成でも常駐する。**
 *  実測（seam-s3-m5-factory の ELF を nm。生ログ
 *  `.steering/20260803-t5-audio-and-checks/logs/c2-5-dram-occupancy.txt`）:
 *      _kernel_stack_SHIM_AUDIO_TSK1/2  各 0x1000 (4,096 B) ＝ 8,192 B
 *      _kernel_tcb_SHIM_AUDIO_TSK1/2    各 0x1A0  (  416 B) ＝   832 B
 *      shim_audio_tsk[2]                     0x28  (   40 B)
 *      ------------------------------------------------- 合計 **9,064 B**
 *  これを **4 preset**（seam-s3-m5-wifi / seam-lx6-m5-wifi /
 *  seam-lx6-m5-factory / seam-s3-m5-factory）が占有する。
 *  このうち **音声を実際に呼べるのは `M5_AUDIO` が立つ構成だけ**で、
 *    現状それは **seam-s3-m5-factory の 1 本のみ**（実測: build.ninja の
 *    `-DM5_AUDIO` 件数は s3-m5-factory=53・他 3 構成=0。生ログ
 *    `logs/link-set-build-ninja-*.txt`）。⇒ **残り 3 preset では完全に死んでいる。**
 *
 *  **切り分けずに「記録する」方を採った**（レビュー C-2 は「切り分けるか記録する」の
 *  どちらでもよいとしている）。理由:
 *    - 切り分けるには `M5_AUDIO` を **cfg のコンパイル行にも**届ける必要があるが、
 *      `M5_AUDIO` は 2026-08-03（T-2）に「m5 の各ターゲットへ直接付ける」形へ
 *      直したばかりで、`A1_EXTRA_DEFS`（＝cfg 側が読む集合）には**意図的に入れていない**。
 *      ここへ戻すと T-2 が塞いだ穴の逆をやることになる。
 *    - 3 preset の `.bss` レイアウトが動く＝golden が 3 本動く。本タスクは
 *      ビルドのみ（実機なし）で、動かした 3 本の起動を確認できない。
 *  ⇒ **golden 更新イベント送り**にし、ここに占有量を明記した。
 */
#define SHIM_AUDIO_TASK_PRI   9
#define SHIM_AUDIO_TSK_STKSZ  4096

/*
 *  2026-08-05（段7）: 音声プールの**本数の単一真実源**。
 *
 *  従来これは `esp_shim.c` の中に `#define SHIM_AUDIO_NTSK 2U` として在り、
 *    同じ数が **cfg の `CRE_TSK` 2 行**にも書かれていた（＝2 箇所）。
 *    段2（sem）で「同じ数が 2 箇所に在ると実際に食い違う」ことを踏んでいるので、
 *    動的化に合わせてここへ集約する。
 *  **本数そのものは変えていない**（2 のまま）。変えたのは置き場所と、
 *    「カーネルタスクを静的生成するか動的生成するか」だけである。
 */
#define SHIM_AUDIO_NTSK       2U
#endif /* M5_SHIM_AUDIO */

/*
 *  2026-08-05（段7）: `AID_TSK()` に渡す**動的タスク ID の予約数**。
 *
 *  段4 は blob プール（`ESP_SHIM_NUM_TSK`）だけを動的化したので
 *    `AID_TSK(ESP_SHIM_NUM_TSK)` でよかった。段7 で音声プールも動的になったので、
 *    **同じ ID 空間から音声のぶんも取る**必要がある。
 *  音声プールが**同時に**占める ID は最大 `SHIM_AUDIO_NTSK` 本である
 *    （帳簿スロット 1 本につき ID 1 本。自己終了後の `pend_del` 中も 1 本を保持し、
 *     次の生成の先頭で `del_tsk` されて返る＝`esp_shim.c` の `shim_audio_reap_pending()`）。
 *  ⇒ **旧構造の静的 `CRE_TSK(SHIM_AUDIO_TSK1/2)` 2 本と同じ本数**であり、
 *    **ID 予約の総量は増えも減りもしない**（静的 2 本 → 動的枠 2 本）。
 */
#ifdef M5_SHIM_AUDIO
#define ESP_SHIM_AID_NTSK     (ESP_SHIM_NUM_TSK + SHIM_AUDIO_NTSK)
#else /* M5_SHIM_AUDIO */
#define ESP_SHIM_AID_NTSK     (ESP_SHIM_NUM_TSK)
#endif /* M5_SHIM_AUDIO */

/*
 *  esp_timer_*（esp/shim/esp_timer_shim.c）の静的構成
 *
 *  2026-07-27 レビュー esp-2：従来 BT/BLE 構成だけが esp/bt/bt_cfg.h の
 *  BT_TIMER_* で本物のタイマを持ち，Wi-Fi 構成は wifi_stubs.c の no-op
 *  だった（＝PHY PLL トラッキングが走らない）。実装を esp_timer_shim.c
 *  へ集約したのに伴い，cfg オブジェクトも全構成共通でここに定義する。
 *
 *  ESP_TIMER_NUM：BT/BLE 構成では BlueDroid の osi_alarm（ALARM_CBS_NUM＝
 *  CONFIG_BT_ALARM_MAX_NUM）と共用するプールなので従来通り16。Wi-Fi 構成
 *  での実需要は phy_track_pll の1個のみだが，将来の blob 要求に備え4。
 */
#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
#define ESP_TIMER_NUM       16
#else
#define ESP_TIMER_NUM       4
#endif
/*
 *  2026-07-28（part1-8）：ESP_TIMER_TASK_PRI を -D で上書き可能にした。
 *  既定は 5 のまま（＝golden 5 構成は 1 バイトも変わらない）。
 *  上書きは CMake の -DA1_ETMR_TASK_PRI=<n>（既定 unset）だけが行う。
 *  用途は「533ms 初回発火遅延」の切り分け実験（優先度を上げると消えるか）で
 *  あって恒久変更ではない。判定表は
 *  .steering/20260727-fable-fixes-batch1/part1-8-ac.md。
 *  参考：ESP-IDF 本家でも esp_timer タスク（ESP_TASK_TIMER_PRIO=22）は
 *    Wi-Fi タスク（23）より**低い**ので、既定の 5（blob=3 より低い）は
 *    本家の順序と整合している。上げるのは実験のためだけである。
 */
#ifndef ESP_TIMER_TASK_PRI
#define ESP_TIMER_TASK_PRI  5     /* 旧 BT_TIMER_TASK_PRI と同値 */
#endif
#define ESP_TIMER_STKSZ     4096  /* 旧 BT_TIMER_STKSZ と同値 */

/*
 *  part1-8 検証計装（ESP_TIMER_WITNESS・既定 OFF）で使う「目撃者」
 *  周期通知の周期（μs）。詳細は esp/shim/esp_timer_shim.c の計装ブロック。
 *  2026-07-28: ESP_TIMER_LATE_PROBE から分離した。Run D/E で
 *  esp_wifi_init_internal() が 26.9 秒（run C は 0.22 秒未満）かかり、
 *  D/E 2 本が 2.5 ms 以内で一致＝決定論的だったため、100ms 周期通知が
 *  第一容疑者になった。束ねたままだと犯人を切り分けられない。
 *  （part1-8-result.md §7）
 */
#ifdef ESP_TIMER_WITNESS
#ifndef ESP_TIMER_WITNESS_PERIOD_US
#define ESP_TIMER_WITNESS_PERIOD_US  100000
#endif
#endif /* ESP_TIMER_WITNESS */

/*
 *  Wi-Fi用CPU割込み線
 *
 *  blobは_set_intr/_ints_onで自身が決めたCPU割込み線番号を指定して
 *  くるため，blobの選択（小さい番号）と衝突しないよう，ターゲットの
 *  ペリフェラル（SYSTIMER/コンソール/テスト用）は線16以降に退避して
 *  いる（target_timer.h等参照）．DEF_INHを静的登録できるのは既知の線
 *  のみ＝blobが使う線はcfg（esp_shim.cfg）に列挙する．
 */
#define ESP_SHIM_MAX_WIFI_INTNO   27  /* 1〜15をblob用に開放。23/27はBTコントローラの
									Level-3割込み（BT-4調査、esp_intr_alloc()の
									ESP_INTR_FLAG_LEVEL3対応）専用に予約
									（bt_shim.cが明示的に配線、blobが動的に選ぶ
									範囲ではないため16〜22等との衝突は無い。
									.claude/plans/sparkling-forging-taco.md参照） */

/*
 *  cfg（esp_shim.cfg）から参照する関数（実体はesp_shim.c）
 */
#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
/*  段4 以降 `esp_shim_task_entry` は cfg からは参照されない（`CRE_TSK(SHIM_TSK*)`
 *  を廃したため）。`acre_tsk` へ実行時に渡す入口として `esp_shim.c` が使う。 */
extern void esp_shim_task_entry(EXINF exinf);
#ifdef M5_SHIM_AUDIO
/*  段7 以降 同様に cfg からは参照されない（`CRE_TSK(SHIM_AUDIO_TSK1/2)` を廃した）。
 *  `esp_shim_audio_tsk_create()` へ実行時に渡す入口。実体は esp_shim.c。 */
extern void esp_shim_audio_task_entry(EXINF exinf);
#endif
extern void esp_shim_timer_task(EXINF exinf);
extern void esp_timer_shim_task(EXINF exinf);	/* 実体はesp_timer_shim.c */
#ifdef ESP_TIMER_WITNESS
extern void esp_timer_witness_cyc(EXINF exinf);	/* part1-8 計装（検証専用） */
#endif
extern void esp_shim_inthdr_0(void);
extern void esp_shim_inthdr_1(void);
extern void esp_shim_inthdr_2(void);
extern void esp_shim_inthdr_3(void);
extern void esp_shim_inthdr_5(void);
extern void esp_shim_inthdr_7(void);
extern void esp_shim_inthdr_8(void);
extern void esp_shim_inthdr_23(void);
extern void esp_shim_inthdr_27(void);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* ESP_SHIM_CFG_H */
