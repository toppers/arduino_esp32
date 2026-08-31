/*
 *  M5 表示スモーク（段階1/1-4）— 自己完結ヒープ＋abort（operator new/delete 写像先）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ★なぜ本ファイルが要るか：
 *    cxx_runtime.cpp の operator new/delete は esp_shim_malloc/esp_shim_free へ写る。
 *    しかし実 esp_shim ヒープ（wifi/shim/esp_shim.c）は psa/crypto（mbedtls）・
 *    Wi-Fi 静的プール・diag_recorder 等 Wi-Fi サブシステム一式へ依存するので、
 *    **Wi-Fi 非リンクの M5 構成**へ持ち込むと不適当な依存を大量に引き込む。
 *    → M5 構成専用の自己完結ヒープをここに置く（内蔵SRAM上の静的アリーナ、
 *      first-fit、free で結合）。
 *
 *  ★SMP 注意：本ヒープはコア間排他を持たない（グローバルロック無し）。M5 系タスクは
 *    コア0固定（SHIM_LOCK 規約）かつ本アプリは PRC1 の 1 タスクのみなので競合は
 *    起きない。**多コアから同時確保する用途には使わないこと。**
 */

#include <stddef.h>
#include <stdint.h>

/*  ヒープサイズ（内蔵SRAM .bss 上）。M5 スモークの確保量は小さいが、実 M5GFX の
 *  ラインバッファ/フォント確保を見据えて 32KB 確保しておく。 */
/*
 *  ★★2026-07-26: **実測に基づいて縮める**。既定は 16KiB。
 *  実測（p51・`free` の橋渡しを入れた後）: **high-water = 0x2450 = 9,296 バイト**。
 *  ⇒ 32KiB は **3.5 倍の過剰**だった。16KiB でも実測の 1.76 倍ある。
 *  ★縮めすぎは**黙って壊れない**——`[HEAP-M5] ★malloc 失敗(枯渇)回数` が
 *  そのまま検出器になる（実際、橋渡しを入れる前は 2 と出ていた）。
 *  ★なぜ縮めるか: DRAM が 276,224 バイトしかなく、無線を載せるには
 *  **252,839 バイト足りない**。
 *  机上で削れるものを実機の前に削る。
 */
#ifndef M5_HEAP_SIZE
#define M5_HEAP_SIZE	(16U * 1024U)
#endif

/*
 *  ★ヘッダは 16 バイト。
 *    修正前は next/size/free の 12 バイトで、アリーナ先頭が 16B 整列でも
 *    **ペイロードの実効整列は 4B** にしかならなかった（ヘッダ 12B ぶんずれる。
 *    size を 16B 単位へ丸めていたので「ずれ幅は常に 12」＝どのブロックも 4B 整列）。
 *    にもかかわらず下の丸め処理には「16 バイト整列」というコメントが付いており、
 *    コメントが実態と食い違っていた。
 *    対処として pad を 1 ワード足してヘッダを 16B にし、**コメントどおり
 *    ペイロードが本当に 16B 整列する**ようにした（コメント側を 4B へ直すのでは
 *    なく実装側を合わせた）。理由：
 *      * operator new の写像先なので、C++ 的には最低でも alignof(max_align_t)
 *        （Xtensa では 8）を満たすべきで、4B は規格上の潜在バグである
 *        （long long/double を含む型を new した場合。Xtensa LX7 は
 *         非整列でもトラップしないため現行バグではない）。
 *      * 段階1では DMA を使わない方針（_cfg.dma_channel=0＝CPU FIFO 経路）だが、
 *        将来 DMA バッファを本ヒープから取る場合の 16B 要件を先に満たしておく。
 *    コストは 1 ブロックあたり 4 バイト（32KB アリーナに対し無視できる）。
 */
typedef struct m5_block {
	struct m5_block   *next;	/* 次ブロック（アドレス昇順） */
	uint32_t			size;	/* ペイロードバイト数（ヘッダ除く） */
	uint32_t			free;	/* 1=空き, 0=使用中 */
	uint32_t			pad;	/* ヘッダを 16B にしペイロードを 16B 整列させる */
} m5_block_t;

#define M5_HDR	((uint32_t) sizeof(m5_block_t))

static uint8_t		m5_heap[M5_HEAP_SIZE] __attribute__((aligned(16)));
static m5_block_t   *m5_head = NULL;

/*
 *  ------------------------------------------------------------------
 *  ★S5：high-water 計装
 *  ------------------------------------------------------------------
 *  【なぜ要るか】段階1 では OOM か否かを abort() が呼ばれたときにしか読めなかった。
 *    ＝**枯渇しなかった run では余裕がどれだけあったのか全く分からない**。
 *    実機 run の回数が厳しく制限されている（AXP2101 再初期化を伴うため原則 2 回）
 *    以上、「今回は落ちなかった」ではなく「ピークで何バイト使い、何バイト余った」を
 *    1 run で持ち帰らなければならない。
 *
 *  ★このヒープ（esp_shim_malloc / operator new の写像先・32KB）は
 *    m5_libcxx_glue.c の `_sbrk` アリーナ（newlib malloc 用・32KB）とは
 *    **完全に別のアリーナ**である。両方を別々に報告する（混同禁止。
 *     §5-4）。
 *
 *  used は「ヘッダ込みで確保に取られたバイト数」。分割の都合で丸めが入るので、
 *  要求バイト数の総和とは一致しない（一致させると実際の消費を過小評価する）。
 */
static uint32_t		m5_heap_used;		/* 現在使用中（ヘッダ込み） */
static uint32_t		m5_heap_used_hw;	/* そのピーク＝high-water */
static uint32_t		m5_heap_nalloc;		/* 確保成功回数 */
static uint32_t		m5_heap_nfail;		/* 確保失敗（枯渇）回数 */
static uint32_t		m5_heap_nfree;		/* 解放回数 */

static void
m5_heap_init(void)
{
	m5_head = (m5_block_t *) (void *) m5_heap;
	m5_head->next = NULL;
	m5_head->size = M5_HEAP_SIZE - M5_HDR;
	m5_head->free = 1U;
}

/*
 *  ★1-13：operator new の写像先はここ（cxx_runtime.cpp → esp_shim_malloc）。
 *  1-12 の `[MK] heap_caps_malloc` は heap_caps_malloc しか見ておらず、
 *  `[Autodetect]` 直後に走る `new Panel_M5StackCoreS3` / `new Light_M5StackCoreS3` /
 *  `new Touch_M5StackCoreS3` は**素通り**していた（実機ログに heap マーカが
 *  1 行も無いのはこのため）。ここに入口/出口ブラケットを足す。
 */
extern void m5_mark_u32(const char *tag, unsigned int id, unsigned int v);
extern void m5_ps_snapshot(unsigned int site);
extern volatile unsigned int m5_ctr[];
#define M5_CTR_MALLOC		0
#define M5_CTR_FREE			1
#define M5_PSSITE_MALLOC	0x02U
#define M5_MALLOC_MARK_MAX	32U
static unsigned int	m5_malloc_marks;

#ifndef M5_USE_ESP_SHIM	/* combined build: esp/shim defines these */
void *
esp_shim_malloc(size_t size)
{
	m5_block_t *b;
	uint32_t	need;
	int			mark;

	m5_ctr[M5_CTR_MALLOC]++;
	m5_ps_snapshot(M5_PSSITE_MALLOC);
	mark = (m5_malloc_marks < M5_MALLOC_MARK_MAX);
	if (mark) {
		m5_malloc_marks++;
		m5_mark_u32("[MK] esp_shim_malloc in  size=", 0x40U, (unsigned int) size);
	}

	if (m5_head == NULL) {
		m5_heap_init();
	}
	if (size == 0U) {
		size = 1U;
	}
	/*  確保量を 16 バイト単位へ切り上げる。ヘッダも 16B なので、アリーナ先頭が
	 *  16B 整列であることと合わせて全ペイロードが 16B 整列になる。 */
	need = (uint32_t) ((size + 15U) & ~((size_t) 15U));

	for (b = m5_head; b != NULL; b = b->next) {
		if ((b->free != 0U) && (b->size >= need)) {
			/*  ヘッダ＋最小ペイロードが残るなら分割する。 */
			if (b->size >= (need + M5_HDR + 16U)) {
				m5_block_t *nb = (m5_block_t *) (void *)
					((uint8_t *) b + M5_HDR + need);
				nb->next = b->next;
				nb->size = b->size - need - M5_HDR;
				nb->free = 1U;
				b->next = nb;
				b->size = need;
			}
			b->free = 0U;
			/*  ★S5 high-water：分割後の b->size が実際に取られたペイロード。 */
			m5_heap_nalloc++;
			m5_heap_used += b->size + M5_HDR;
			if (m5_heap_used > m5_heap_used_hw) {
				m5_heap_used_hw = m5_heap_used;
			}
			if (mark) {
				m5_mark_u32("[MK] esp_shim_malloc out ptr=", 0x41U,
							(unsigned int)(uintptr_t)((uint8_t *) b + M5_HDR));
			}
			return (void *) ((uint8_t *) b + M5_HDR);
		}
	}
	m5_heap_nfail++;		/* ★S5：枯渇は「黙って NULL」にしない（後で必ず報告） */
	if (mark) {
		m5_mark_u32("[MK] esp_shim_malloc out ptr=", 0x41U, 0U);	/* 0=枯渇 */
	}
	return NULL;	/* 枯渇 */
}

void
esp_shim_free(void *ptr)
{
	m5_block_t *b;
	m5_block_t *p;

	if (ptr == NULL) {
		return;
	}
	m5_ctr[M5_CTR_FREE]++;
	if (m5_malloc_marks < M5_MALLOC_MARK_MAX) {
		m5_malloc_marks++;
		m5_mark_u32("[MK] esp_shim_free in  ptr=", 0x42U,
					(unsigned int)(uintptr_t) ptr);
	}
	b = (m5_block_t *) (void *) ((uint8_t *) ptr - M5_HDR);
	if (b->free == 0U) {			/* 二重 free で下振れさせない */
		m5_heap_nfree++;
		if (m5_heap_used >= (b->size + M5_HDR)) {
			m5_heap_used -= b->size + M5_HDR;
		}
		else {
			m5_heap_used = 0U;
		}
	}
	b->free = 1U;

	/*  隣接する空きブロックを結合（アドレス昇順リストを1回走査）。 */
	for (p = m5_head; p != NULL; p = p->next) {
		while ((p->free != 0U) && (p->next != NULL) && (p->next->free != 0U)) {
			p->size += M5_HDR + p->next->size;
			p->next = p->next->next;
		}
	}
}
#endif /* !M5_USE_ESP_SHIM */

/*
 *  cxx_runtime.cpp の OOM 経路（operator new 失敗）が呼ぶ abort。
 *  ここで提供しておくことで newlib の abort（_exit/_kill/_getpid を要求）を
 *  リンクに引き込まない（seam XIP は -nostdlib）。
 *
 *  ★1-11：従来は**無音 spin**（for(;;){}）だった。operator new の OOM・
 *    __cxa_pure_virtual・std::terminate はいずれもここへ落ちるため、
 *    「M5GFX が黙って止まる」症状の有力な着地点でありながら、それを
 *    区別する手段が無かった。target_fput_log 直（logtask 非経由＝即時）で
 *    到達を宣言してから停止する。
 *    ヒープ統計も添える：枯渇なら OOM が原因だと 1 行で確定できる。
 */
extern void	m5_log_now(const char *msg);
extern void	m5_log_now_u32(const char *msg, unsigned int v);

/*
 *  ★S5：M5 ヒープ（esp_shim_malloc = operator new の写像先・32KB）の一括報告。
 *
 *  ★これは `_sbrk` アリーナ（m5_libcxx_glue.c・newlib malloc 用・32KB）とは
 *    **別アリーナ**である。両方を採るために m5_sbrk_report() も併せて呼ぶこと。
 *
 *  「余裕」は high-water からではなく **arena - high-water** で読む。
 *  nfail>0 なら high-water は「実際に必要だった量」を**下回っている**（枯渇して
 *  確保できなかった分は計上されない）ので、その場合の余裕には意味が無い。
 *  そのことも 1 行で出す（沈黙にしない）。
 */
void
m5_heap_report(const char *tag)
{
	m5_block_t	*b;
	uint32_t	 used = 0U, freed = 0U, largest = 0U, nblk = 0U;

	m5_log_now(tag);
	for (b = m5_head; b != NULL; b = b->next) {
		nblk++;
		if (b->free != 0U) {
			freed += b->size;
			if (b->size > largest) {
				largest = b->size;
			}
		}
		else {
			used += b->size;
		}
	}
	m5_log_now_u32("[HEAP-M5] arena バイト=            ", (unsigned int) M5_HEAP_SIZE);
	m5_log_now_u32("[HEAP-M5] high-water(ピーク使用)=  ", (unsigned int) m5_heap_used_hw);
	m5_log_now_u32("[HEAP-M5] 余裕 = arena - HW =      ",
				   (unsigned int) (M5_HEAP_SIZE - m5_heap_used_hw));
	m5_log_now_u32("[HEAP-M5] 現在使用中(走査値)=      ", (unsigned int) used);
	m5_log_now_u32("[HEAP-M5] 現在空き合計(走査値)=    ", (unsigned int) freed);
	m5_log_now_u32("[HEAP-M5] 最大連続空き=            ", (unsigned int) largest);
	m5_log_now_u32("[HEAP-M5] ブロック数=              ", (unsigned int) nblk);
	m5_log_now_u32("[HEAP-M5] malloc 成功回数=         ", (unsigned int) m5_heap_nalloc);
	m5_log_now_u32("[HEAP-M5] free 回数=               ", (unsigned int) m5_heap_nfree);
	m5_log_now_u32("[HEAP-M5] ★malloc 失敗(枯渇)回数= ", (unsigned int) m5_heap_nfail);
	m5_log_now((m5_heap_nfail == 0U)
			   ? "[HEAP-M5] → 枯渇なし。上の余裕はそのまま読んでよい"
			   : "[HEAP-M5] → ★枯渇あり。HW は必要量を下回るので余裕は無意味（増量が要る）");
}

#ifndef M5_USE_ESP_SHIM	/* combined build: esp/shim defines these */
void
abort(void)
{
	m5_block_t	*b;
	uint32_t	 used = 0U, freed = 0U, largest = 0U, nblk = 0U;

	m5_log_now("*** ABORT *** （operator new OOM / __cxa_pure_virtual / std::terminate）");

	/*  ヒープ状態を添えて OOM か否かを判別可能にする（read-only 走査）。 */
	for (b = m5_head; b != NULL; b = b->next) {
		nblk++;
		if (b->free != 0U) {
			freed += b->size;
			if (b->size > largest) {
				largest = b->size;
			}
		}
		else {
			used += b->size;
		}
	}
	m5_log_now_u32("*** ABORT *** heap 使用中バイト=",       (unsigned int) used);
	m5_log_now_u32("*** ABORT *** heap 空きバイト合計=",     (unsigned int) freed);
	m5_log_now_u32("*** ABORT *** heap 最大連続空き(0近い=OOM)=", (unsigned int) largest);
	m5_log_now_u32("*** ABORT *** heap ブロック数=",         (unsigned int) nblk);
	/*  ★S5：high-water と枯渇回数も添える。abort に落ちた run では
	 *  m5_heap_report() まで到達できないので、ここで出さないと永久に読めない。 */
	m5_log_now_u32("*** ABORT *** heap high-water=",         (unsigned int) m5_heap_used_hw);
	m5_log_now_u32("*** ABORT *** heap malloc 失敗回数=",    (unsigned int) m5_heap_nfail);
	m5_log_now_u32("*** ABORT *** 呼出し元(戻りアドレス)=",
				   (unsigned int) (uintptr_t) __builtin_return_address(0));
	m5_log_now("*** ABORT *** 停止します（以降ログは出ません）");

	for (;;) {
		/*  停止。 */
	}
}
#endif /* !M5_USE_ESP_SHIM */

/*
 *  ★★アリーナの範囲判定。
 *
 *  【なぜ要るか】`heap_caps_malloc`/`heap_caps_calloc`/`heap_caps_aligned_calloc` は
 *  **このアリーナ**から返すのに、ESP-IDF のドライバ（`esp_driver_i2s` 等）は
 *  解放に**素の `free()`** を使う。素の `free` は **newlib のもの**へ解決される
 *  （`fmp_xip.map` で `libc_a-malloc.o` から来ていることを実測）。
 *  ⇒ ★**アリーナのポインタが newlib のヒープへ渡っている。**
 *    ・アリーナは回収されない（`free 回数=5` に対し `malloc 回数=156`）
 *    ・newlib 側は他人のポインタの手前を chunk ヘッダとして解釈する＝**未定義動作**
 *  ⇒ 番地の範囲で**どちらのヒープのものか**を判定できるようにする。
 *  ★魔法数ではなく**実体の配列の範囲**なので、誤判定の余地が無い。
 */
int	m5_heap_contains(const void *p);
int
m5_heap_contains(const void *p)
{
	const uint8_t	*q = (const uint8_t *) p;

	return((q >= &m5_heap[0]) && (q < &m5_heap[M5_HEAP_SIZE]) ? 1 : 0);
}

#if defined(M5_SDSPI)
/*
 *  ★確保済みブロックのペイロードサイズを返す。
 *  ★`heap_caps_get_allocated_size()`（ESP-IDF の SD/SPI が使う）のため。
 *  ★ヘッダに size を持っているので**正確に答えられる**——推定値を返さない。
 */
size_t	esp_shim_block_size(void *ptr);
size_t
esp_shim_block_size(void *ptr)
{
	const m5_block_t	*b;

	if ((ptr == NULL) || !m5_heap_contains(ptr)) {
		return(0U);
	}
	b = (const m5_block_t *) (void *) ((uint8_t *) ptr - M5_HDR);
	return((size_t) b->size);
}
#endif /* M5_SDSPI */
