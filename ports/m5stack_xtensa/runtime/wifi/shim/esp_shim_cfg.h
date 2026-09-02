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
 *  ため，
 *  NimBLEより大きめに確保する。TSK/MTX/SEMも同様の余裕を持たせる
 *  （esp_shim.cfg／esp_shim.cの配列・CRE_*と一致させること）。
 */
#ifdef TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
/*  W3④(SPP)：BlueDroidホストはosi_sem(fixed_queue/fixed_pkt_queueの
 *  enqueue/dequeue毎に1個、future/alarm/RFCOMM/SDP/L2CAP各所)を多用し、
 *  esp_spp_start_srv()到達時点で32枠を使い切って"sem pool exhausted"→
 *  SPP_START_EVT status=4(NO_RESOURCE)で失敗（実機WROVER確認）。RFCOMM
 *  接続確立時の追加分も見込み48へ拡張（メモリはヒープでなく静的SEMCB
 *  プールで、DRAM影響は僅少）。 */
#define ESP_SHIM_NUM_SEM    96    /* 32→96：start_srvは48+、RFCOMM接続確立(SRV_OPEN)で64も枯渇(11件storm)。接続分の余裕込みで96(SEMCBは静的.bssで小、ヒープ非依存) */
#define ESP_SHIM_NUM_MTX    16    /* 8→16：BlueDroid host分+8 */
#define ESP_SHIM_NUM_DTQ    16    /* 4→16：BTU/BTC複数workqueue分 */
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
#define ESP_SHIM_NUM_SEM    28    /* 24→28：NimBLE分+4 */
#define ESP_SHIM_NUM_MTX    12    /* 8→12：NimBLE分+4 */
#define ESP_SHIM_NUM_DTQ    8     /* 4→8：NimBLE eventq分+4 */
#define ESP_SHIM_NUM_TSK    8     /* 6→8：NimBLEホストタスク分+2 */
#else
#define ESP_SHIM_NUM_SEM    24    /* セマフォプール */
#define ESP_SHIM_NUM_MTX    8     /* ミューテックスプール */
#define ESP_SHIM_NUM_DTQ    4     /* キュープール（各深さESP_SHIM_DTQ_CNT） */
#define ESP_SHIM_NUM_TSK    6     /* タスクプール（各スタックESP_SHIM_TSK_STKSZ） */
#endif
#define ESP_SHIM_DTQ_CNT    256   /* WiFiドライバタスクのイベントキューは200深を要求 */

/*
 *  shimタスクプール1スロットあたりのスタックサイズ
 *
 *  ★Wi-Fi構成は 6656、それ以外は 8192。
 *
 *  Wi-Fi blobが生成するタスクは **'wifi' の1本のみ**（要求6656B）で、
 *  残り5スロットは一度も起動されない。'wifi'タスクの実使用量は 1840B である。
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
 *  ★縮小対象を「S3のWi-Fi構成」だけに絞る理由（＝測っていない構成は縮めない）：
 *
 *  - BT（NimBLE/BlueDroid Classic）：スタック実使用量が未実測。BTビルドは
 *    現状PRC=1/2ともリンクできておりDRAMを空ける必要が無い。→ 8192据置。
 *  - LX6（無印ESP32、TOPPERS_ESP32_LX6）：本ヘッダはLX6と共有だが、DRAM超過は
 *    S3固有の問題（`2ff830f`は`#ifdef TOPPERS_ESP32S3`ガードでLX6のTCBは
 *    不変＝148B）。LX6側は逼迫しておらず、かつ上記実測はS3実機のものなので
 *    LX6へ流用しない。→ 8192据置＝厳密な非回帰。
 *
 *  TOPPERS_ESP32_LX6はLX6ビルドのコマンドラインで`-DTOPPERS_ESP32_LX6`として必ず
 *  定義され、S3ビルドでは定義されない（S3は`-DTOPPERS_ESP32S3`）。
 */
#if defined(TOPPERS_BT_HOST_NIMBLE) || defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
#define ESP_SHIM_TSK_STKSZ  8192  /* BT：未実測のため据置（非回帰） */
#elif defined(TOPPERS_ESP32_LX6)
#define ESP_SHIM_TSK_STKSZ  8192  /* LX6：未実測＆DRAM逼迫なしのため据置（非回帰） */
#else
#define ESP_SHIM_TSK_STKSZ  6656  /* S3 Wi-Fi：blob要求値ちょうど */
#endif

/*
 *  ヒープサイズ（静的配列．Wi-Fi blobは実測で数十KBを要求する）
 */
#ifndef ESP_SHIM_HEAP_SIZE
#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
/*
 *  ★bt-classic は 124KB では DRAM に入らない。
 *
 *  この profile の DRAM は BT コントローラの exchange memory
 *  (CONFIG_BTDM_RESERVE_DRAM = 0xdb5c) ぶん狭い（esp32_xip_btc.ld 冒頭）。
 *  一方 124KB という値は Wi-Fi blob の実測要求から来ており、この profile は
 *  Wi-Fi blob を一切リンクしない。124KB のままだと .bss が DRAM を
 *  35,988 バイト超過する（2026-09-02 実測）。
 *
 *  2026-09-02 に 80KB → 72KB へ。BlueDroid をソースからビルドする構成
 *  （third_party/bluedroid）は .bss/.data がアーカイブ版より大きく、80KB では
 *  1,728 バイト溢れた。72KB は「入る最小」ではなく余裕を見た値で、BlueDroid の
 *  実際のピークは未計測。実機で詰めるときは bt_stubs.c の
 *  esp_shim_heap_largest_free_block() を見ること。
 */
#define ESP_SHIM_HEAP_SIZE  (72 * 1024)
#else
#define ESP_SHIM_HEAP_SIZE  (124 * 1024)
#endif
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
 *  詳細: 
 */
#define ESP_SHIM_BT_CTRL_TASK_PRI          2   /* SHIM_TIMER_TSKと同格 */
#define ESP_SHIM_BT_CTRL_FREERTOS_PRIO_MIN 22  /* ESP_TASK_BT_CONTROLLER_PRIO(=23)以上を
                                                   高優先度スロットへ。nimble_host(=21)は
                                                   対象外＝従来通りESP_SHIM_WIFI_TASK_PRI */
#endif

/*
 *  Wi-Fi用CPU割込み線
 *
 *  blobは_set_intr/_ints_onで自身が決めたCPU割込み線番号を指定して
 *  くるため，blobの選択（小さい番号）と衝突しないよう，ターゲットの
 *  ペリフェラル（SYSTIMER/コンソール/テスト用）は線16以降に退避して
 *  いる（target_timer.h等参照）．DEF_INHを静的登録できるのは既知の線
 *  のみ＝blobが使う線はcfg（esp_shim.cfg）に列挙する．
 */
#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
/*  ★BT Classic は CPU 割込み 29 も使う。
 *  ESP32 の BT コントローラは esp_intr_alloc(ETS_INTERNAL_SW1_INTR_SOURCE) を
 *  呼び、xt_ints_on() のマスクにも bit29 を立てる（実測 0x200001a0）。
 *  Xtensa ESP32 で SW1 は CPU 割込み 29（レベル3）。27 止まりだと
 *  esp_shim_set_isr() が "out of range" で弾き、ena_int(29) も cfg 未宣言で
 *  失敗する——どちらも黙って失敗し、コントローラが永久に待つ。 */
#define ESP_SHIM_MAX_WIFI_INTNO   29
#else
#define ESP_SHIM_MAX_WIFI_INTNO   27  /* 1〜15をblob用に開放。23/27はBTコントローラの
									Level-3割込み（BT-4調査、esp_intr_alloc()の
									ESP_INTR_FLAG_LEVEL3対応）専用に予約
									（bt_shim.cが明示的に配線、blobが動的に選ぶ
									範囲ではないため16〜22等との衝突は無い。
									.claude/plans/sparkling-forging-taco.md参照） */
#endif

/*
 *  cfg（esp_shim.cfg）から参照する関数（実体はesp_shim.c）
 */
#ifndef TOPPERS_MACRO_ONLY
#include <kernel.h>
extern void esp_shim_task_entry(EXINF exinf);
extern void esp_shim_timer_task(EXINF exinf);
extern void esp_shim_inthdr_0(void);
extern void esp_shim_inthdr_1(void);
extern void esp_shim_inthdr_2(void);
extern void esp_shim_inthdr_3(void);
extern void esp_shim_inthdr_5(void);
extern void esp_shim_inthdr_7(void);
extern void esp_shim_inthdr_8(void);
extern void esp_shim_inthdr_23(void);
/*
 *  esp_timer shim（wifi/shim/esp_timer_shim.c）。bt-classic のみ実体を持つ。
 *  ESP_TIMER_NUM は BlueDroid の osi_alarm(CONFIG_BT_ALARM_MAX_NUM=16)と
 *  同じプールを使うので 16 で揃える。
 */
#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
#define ESP_TIMER_NUM       16
#ifndef ESP_TIMER_TASK_PRI
#define ESP_TIMER_TASK_PRI  5
#endif
#define ESP_TIMER_STKSZ     4096
extern void esp_timer_shim_task(intptr_t exinf);
#endif

extern void esp_shim_inthdr_27(void);
#if defined(TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
extern void esp_shim_inthdr_29(void);
#endif
#endif /* TOPPERS_MACRO_ONLY */

#endif /* ESP_SHIM_CFG_H */
