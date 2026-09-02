/*
 *  ★`__containerof` の供給（ESP-IDF の gdma.c が使う BSD 由来マクロ）
 *
 *  【なぜ要るか】`esp_hw_support/dma/gdma.c` は `__containerof(ptr, type, member)` を
 *  使うが、**自分では `sys/param.h` / `sys/cdefs.h` を include していない**
 *  （IDF の本ビルドでは推移的に入っている）。本ポートの include 集合では入らず
 *  `expected expression before 'gdma_tx_channel_t'` で落ちた。
 *
 *  ★newlib の `sys/cdefs.h` に定義はあるが、feature マクロ次第で出てこない。
 *  依存を newlib の内部事情に預けたくないので、**無ければ自分で定義する**形にした。
 *  定義は BSD の標準形（`offsetof` を使う）。
 */
#ifndef M5_IDF_CONTAINEROF_H
#define M5_IDF_CONTAINEROF_H

#include <stddef.h>		/* offsetof */

#ifndef __containerof
#define __containerof(ptr, type, member) \
	((type *)(void *)((char *)(ptr) - offsetof(type, member)))
#endif

#endif /* M5_IDF_CONTAINEROF_H */
