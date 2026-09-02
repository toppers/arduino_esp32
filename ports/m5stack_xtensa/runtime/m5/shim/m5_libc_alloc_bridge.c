/*
 *  ============================================================================
 *  ★★libc の `free()` と M5 アリーナの橋渡し
 *  ============================================================================
 *
 *  【解く問題】ESP-IDF のドライバ（`esp_driver_i2s` 等）は
 *      確保: `heap_caps_malloc` / `heap_caps_calloc` / `heap_caps_aligned_calloc`
 *      解放: ★**素の `free()`**
 *  という組み合わせを使う。本ポートでは前者は **M5 アリーナ**（`m5_heap[]`）から
 *  返すので、`free` が **newlib のもの**へ解決されると:
 *   1. ★**アリーナが回収されない**（Speaker と Mic を同時に開けなくなる）。
 *   2. ★**newlib のヒープに他人のポインタが入る**。newlib の `free` は
 *      渡された番地の手前を自分の chunk ヘッダとして解釈するので、
 *      これは**未定義動作**である（今すぐ落ちていないのは運）。
 *
 *  【直し方】`free` をここで定義し、★**番地の範囲でどちらのヒープのものかを
 *  判定して振り分ける**。魔法数ではなく `m5_heap[]` という**実体の配列の範囲**
 *  なので誤判定の余地が無い。
 *
 *  ★**新規確保の振る舞いは一切変えない**（`malloc` は newlib へそのまま委譲）。
 *  変えるのは「アリーナのポインタが newlib へ流れ込むのを止める」ことだけ。
 *  ⇒ 回帰の面積を最小にしてある。
 *
 *  ★リンク上の注意: newlib の `libc_a-malloc.o` は `malloc` と `free` を**同じ
 *  オブジェクト**に持つ。片方だけ定義するとそのオブジェクトが引き込まれて
 *  **多重定義**になる。⇒ **両方ここで定義する**。失敗はリンクエラーとして出る
 *  （fail-closed）。
 */
#include <kernel.h>
#include <stddef.h>

extern int		m5_heap_contains(const void *p);
extern void		esp_shim_free(void *ptr);
extern void		*_malloc_r(void *reent, size_t size);
extern void		_free_r(void *reent, void *ptr);
extern void		*_realloc_r(void *reent, void *ptr, size_t size);
extern void		*_impure_ptr;

/*  ★診断カウンタ（外から読む）。「黙って振り分けた」を残さないため。 */
volatile uint32_t	m5_alloc_bridge_n_arena;	/* アリーナ側へ回した free の回数 */
volatile uint32_t	m5_alloc_bridge_n_libc;		/* newlib 側へ回した free の回数 */

/*
 * ★合成構成（M5_USE_ESP_SHIM）では esp/shim/esp_shim_libc.c が malloc/free/
 *   realloc を定義するので、この橋渡しごと落とす。M5 は Wi-Fi 側と同じヒープを
 *   使うことになり、この TU が抱える 16KiB の静的アリーナも消える。
 */
#ifndef M5_USE_ESP_SHIM
void	*malloc(size_t size);
void	free(void *ptr);
void	*realloc(void *ptr, size_t size);

void *
malloc(size_t size)
{
	/*  ★新規確保は**今までどおり newlib**。振る舞いを変えない。 */
	return(_malloc_r(_impure_ptr, size));
}

void
free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}
	if (m5_heap_contains(ptr)) {
		m5_alloc_bridge_n_arena++;
		esp_shim_free(ptr);
	}
	else {
		m5_alloc_bridge_n_libc++;
		_free_r(_impure_ptr, ptr);
	}
}

void *
realloc(void *ptr, size_t size)
{
	/*  ★アリーナのポインタを newlib の realloc へ渡さない。
	 *  本ポートのアリーナに realloc は無いので、**縮小・拡大とも新規確保＋コピー**は
	 *  実装しない——★**呼ばれたら分かるように NULL を返す**（黙って壊すより良い）。
	 *  実際 I2S/GDMA 経路は realloc を使わない（リンク実測）。 */
	if ((ptr != NULL) && m5_heap_contains(ptr)) {
		return(NULL);
	}
	return(_realloc_r(_impure_ptr, ptr, size));
}
#endif /* !M5_USE_ESP_SHIM */
