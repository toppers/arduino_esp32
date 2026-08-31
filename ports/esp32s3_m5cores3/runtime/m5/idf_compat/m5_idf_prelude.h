/*
 *  M5（CoreS3）C++ ビルド用の force-include プレリュード．
 *
 *  M5GFX の lgfx/v1/platforms/esp32/common.hpp などは，実 ESP-IDF ビルド環境で
 *  「常に間接 include されている」ことを前提に，明示 include せずに ESP-IDF の
 *  ambient なシンボル（ESP_IDF_VERSION・heap_caps_malloc・esp_rom_delay_us 等）を
 *  使う．本ポートの compat freertos は最小で，それらを transitively 引き込まない．
 *
 *  M5GFX 側ソースは無改変契約（fmp3_m5 上流 submodule）なので，代わりに
 *  この 1 ファイルを `-include` で全 M5 翻訳単位の先頭に注入し，M5GFX が仮定する
 *  ambient ヘッダを供給する．追加が必要になったらここへ集約する（M5 専用・
 *  golden 5 構成には一切載らない）．
 */
#ifndef TOPPERS_M5_IDF_PRELUDE_H
#define TOPPERS_M5_IDF_PRELUDE_H

#include <esp_idf_version.h>   /* ESP_IDF_VERSION / ESP_IDF_VERSION_VAL（common.cpp:43 等） */
#include <esp_heap_caps.h>     /* heap_caps_malloc/free・MALLOC_CAP_*（common.hpp:143-）    */
#include <esp_rom_sys.h>       /* esp_rom_delay_us（common.hpp:123）                        */

/*
 *  ---- SPI の DMA 経路をビルド時に封鎖する ----
 *
 *  【なぜ必要か】本ポートは **GDMA のクロック投入も割当もしていない**
 *  （m5_spi_bus_stub.c は dma_chan を受け取るだけ）。一方 M5GFX の CoreS3 autodetect は
 *  `bus_cfg.dma_channel = SPI_DMA_CH_AUTO`（M5GFX.cpp:1430）を渡す。lgfx で DMA 経路に
 *  入るか否かの門は **`_cfg.dma_channel` ただ一つ**（Bus_SPI.cpp:569/701/883/1068/1256）で，
 *  `search_dma_out_ch()` の戻り値は門に関与しない。したがって dma_channel が非0のままだと
 *  64 バイトを超える書込みで必ず DMA 経路に入り，GDMA 未クロックのため転送が完了しない
 *  （＝DMA 経路を塞ぐこと自体は正しい対策である）。
 *
 *  ★`rst:0x7 (TG0WDT_SYS_RST)` は**前回**のリセット理由の表示である。本ポートは
 *    start.S:72 の chip_wdt_early_disable で起動時に全 WDT を停止しているので、
 *    これを今回のハングの根拠にしてはならない。
 *
 *  【なぜここ（プレリュード）か】`display.begin()` の **後**に公開 API で
 *  `dma_channel=0` へ差し替えるのでは**間に合わない**——begin() の内部でパネル
 *  初期化と画面クリアが既に走る。バス設定が作られる時点から 0 で
 *  なければならない。M5GFX/M5Unified submodule は無改変契約なので，enum 定義を取り込んだ
 *  上でマクロで覆い，以降の使用箇所（M5GFX.cpp / Bus_SPI.cpp）が 0 を見るようにする。
 *
 *  ★enum 定義より前に #define するとコンパイルエラーになる（`SPI_DMA_CH_AUTO = 3` が
 *    `0 = 3` になる）ので，**必ず先に spi_common.h を include してから** #undef/#define する。
 *  ★将来 GDMA を実装したら，この 2 行を消すだけで DMA 経路へ戻せる（スタブの
 *    spi_bus_initialize が GDMA_OUT_PERI_SEL_CHn に SOC_GDMA_TRIG_PERIPH_SPI2(=0) を
 *    書く実装とセット）。
 */
/*
 *  ★`M5_SDSPI` では**この封鎖を解除する。**
 *
 *  封鎖の理由は「**我々のスタブ** `m5_spi_bus_stub.c` が dma_chan を受け取るだけで
 *  GDMA のクロック投入も割当もしない」ことである。
 *    ・★`M5_SDSPI` では**そのスタブを使わない**。IDF の本物の `spi_bus_initialize` が
 *      `spicommon_dma_chan_alloc()` → `gdma_new_ahb_channel()` で**正規に確保する**
 *      （どちらも既にリンク済みで、`m5_spi_trace.c` で入口/出口を追える）
 *
 *  【なぜ解除が**必要**か】DMA 無しでは
 *  `spi_bus_initialize` が `max_transfer_sz = SOC_SPI_MAXIMUM_BUFFER_SIZE = 64` に
 *  固定する（`spi_common.c:847`。DMA を確保したときだけ :853 で上書きされる）。
 *  `spi_master.c:1121-1122` は超過を `ESP_ERR_INVALID_ARG` で**弾く**。
 *  ★microSD の 1 セクタは `SDSPI_MAX_DATA_LEN = 512` バイトを**1 回の転送**で送る
 *  （`sdspi_host.c:829`。分割は 512 を超えたときだけ＝`sdspi_transaction.c:112`）。
 *  ⇒ ★★**DMA 無しでは 1 セクタも読めない。** 解除は回避策ではなく**前提条件**である。
 *
 *  ★★これは**表示の挙動も変える**（lgfx は `_cfg.dma_channel` 非 0 で DMA 経路へ入る＝
 *  上の 27-28 行）。⇒ 表示の非退行を**実機で確かめること**が必須。
 *  ★スタブ構成（`M5_SDSPI` 無し）では**封鎖したままにする**＝退行させない。
 */
#include <driver/spi_common.h>	/* SPI_DMA_CH_AUTO / SPI_DMA_DISABLED の enum 定義 */
#if !defined(M5_SDSPI)
#undef  SPI_DMA_CH_AUTO
#define SPI_DMA_CH_AUTO		SPI_DMA_DISABLED	/* = 0：DMA 経路を全面封鎖（CPU FIFO へ） */
#endif

#endif /* TOPPERS_M5_IDF_PRELUDE_H */
