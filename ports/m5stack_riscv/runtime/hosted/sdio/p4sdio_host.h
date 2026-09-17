/*
 *  TOPPERS/FMP3 ESP32-P4 移植 —
 *  SDMMC ホストドライバ（外付け ESP32-C6 / esp-hosted スレーブ向け）の公開面
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  【出典】P4 repo `~/TOPPERS/ESP32/esp32_p4`（読み取り専用・参照専用で凍結中）
 *          `wifi_p4_module/sdio_host/fmp3_sdmmc.h`（144 行）
 *  取込みの全数と改変点は `esp/p4sdio/IMPORT_PROVENANCE.md` を参照。
 *
 *  移植元の設計をそのまま引き継ぐ:
 *    IDF は**ヘッダのみ**利用する（`hal/sdmmc_ll.h` の inline LL 関数・
 *    レジスタ構造体・SD プロトコル定数）。IDF の .c ・FreeRTOS・ROM 関数は
 *    使わない。⇒ 本ドライバのために `esp/lib/*.a` を新設する必要が無い。
 */

#ifndef P4SDIO_HOST_H
#define P4SDIO_HOST_H

#include <kernel.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 *  SDIO 転送の上限
 */
#define P4SDIO_BLOCK_SIZE		512U	/* esp-hosted slave の func1 ブロック長 */

/*
 *  ホストコントローラ初期化
 *    クロック/リセット→ピン(GPIO マトリクス)→FIFO/DMA/割込みマスク→
 *    割込み確保（C-1 CLIC シムの `esp_intr_alloc`）→カードクロック 400kHz。
 */
extern ER	p4sdio_host_init(void);

/*
 *  SDIO カード（esp-hosted slave）の初期化
 *    CMD52(IO reset)→CMD0→CMD5(OCR)→CMD3(RCA)→CMD7(select)→
 *    CCCR: 4-bit バス幅/FN1 有効化/ブロックサイズ(func0/1=512)/割込み許可→
 *    カードクロックを freq_khz へ引上げ。
 */
extern ER	p4sdio_card_init(uint32_t freq_khz);

/*
 *  CMD52（1 バイトレジスタ R/W）
 */
extern ER	p4sdio_read_reg(uint_t func, uint32_t reg, uint8_t *p_val);
extern ER	p4sdio_write_reg(uint_t func, uint32_t reg, uint8_t val);

/*
 *  CMD53（マルチバイト/ブロック転送, DMA）
 *    buf はキャッシュライン(64B)整列・長さ 4B 倍数を推奨。
 *    非整列時はドライバ内部のバウンスバッファ経由（性能低下）。
 *    addr_fixed=true で FIFO アドレス固定（esp-hosted のストリーミング読出し用）。
 */
extern ER	p4sdio_read_bytes(uint_t func, uint32_t addr, void *dst,
							  uint32_t len, bool addr_fixed);
extern ER	p4sdio_write_bytes(uint_t func, uint32_t addr, const void *src,
							   uint32_t len, bool addr_fixed);

/*
 *  ブロックモードの nblk 上限は DMA ディスクリプタチェーンで決まる
 *  （NDESC*DESC_MAX_SIZE/P4SDIO_BLOCK_SIZE = 64）。CMD53 の count フィールド
 *  上限（9bit=511）ではない。超えると E_PAR（移植元のレビュー指摘⑥）。
 */
extern ER	p4sdio_read_blocks(uint_t func, uint32_t addr, void *dst,
							   uint32_t nblk, bool addr_fixed);
extern ER	p4sdio_write_blocks(uint_t func, uint32_t addr, const void *src,
								uint32_t nblk, bool addr_fixed);

/*
 *  スレーブ(カード)割込み待ち
 *    tmout_ms は【ミリ秒】単位（内部で FMP3 の TMO=マイクロ秒へ変換する）。
 */
#define P4SDIO_WAIT_FOREVER		0xFFFFFFFFU
extern ER	p4sdio_wait_int(uint32_t tmout_ms);

/*
 *  診断
 */
extern uint32_t		p4sdio_last_resp(void);
extern uint32_t		p4sdio_last_err(void);
extern uint16_t		p4sdio_rca(void);
/*  直近の CMD5(IO_SEND_OP_COND) の R4 応答（bit31=C / bits30:28=IO 機能数）  */
extern uint32_t		p4sdio_r4(void);

/*  カウンタ（黙って落とさないための口）  */
extern volatile uint32_t	p4sdio_n_isr;			/* ISR 突入回数 */
extern volatile uint32_t	p4sdio_n_isr_io;		/* うち IO_SLOT1（スレーブ割込み） */
extern volatile uint32_t	p4sdio_n_cmd;			/* 発行したコマンド数 */
extern volatile uint32_t	p4sdio_n_cmd_err;		/* 失敗したコマンド数 */

/*
 *  D-14: R5 応答フラグの計数（CMD52/CMD53）
 *
 *  **数えるだけで、戻り値は変えない。** 移植元も本ドライバも R5 のエラービットを
 *  一度も見ておらず、カードがエラーを申告してもホストは E_OK を返してきた
 *  （段 7a §6-4 の「潜在バグ」）。まず数字を出す。直すかどうかはその後の判断。
 *
 *  `p4sdio_r5_err_or` に立つビット: b7 COM_CRC_ERROR / b6 ILLEGAL_COMMAND /
 *  b3 ERROR / b1 FUNCTION_NUMBER / b0 OUT_OF_RANGE（b5..4 は状態なので数えない）。
 */
extern volatile uint32_t	p4sdio_n_r5;			/* R5 応答を見た回数 */
extern volatile uint32_t	p4sdio_n_r5_err;		/* うちエラービットが立っていた回数 */
extern volatile uint32_t	p4sdio_last_r5_flags;	/* 最後に見たフラグ 8bit */
extern volatile uint32_t	p4sdio_r5_err_or;		/* 立ったエラービットの累積 OR */

/*  同期出力（`target_fput_log()` 直呼び。`syslog` を実機診断の根拠にしない）  */
extern void			p4sdio_puts(const char *s);
extern void			p4sdio_put_kv(const char *k, uint32_t v);

/*  最後に確保した CLIC 線（-1 = 未確保）。試験が番号を決め打ちしないための口  */
extern int					p4sdio_clic_line;

#endif /* P4SDIO_HOST_H */
