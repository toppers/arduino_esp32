/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5ライブラリ対応 C++ ランタイム最小セット
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  operator new/delete と .init_array 走査（design.md §3）
 *
 *  ビルド前提：-fno-exceptions -fno-rtti -fno-threadsafe-statics -std=gnu++17
 *    - -fno-exceptions のため operator new は失敗時に throw できない。
 *      設計方針（design.md §3）に従い，確保失敗は esp_shim_log_emerg（LOG_EMERG
 *      折返し。C++からkernel.h由来のsyslog()を直接呼べないための薄いラッパー，
 *      esp_shim_public.h参照）+ abort で顕在化させる（黙って nullptr を返して
 *      握り潰さない）。
 *    - 写像先は既存の esp_shim ヒープ（esp/shim/esp_shim.c。静的配列上の
 *      first-fit，内蔵SRAM）。独自ヒープは実装しない。
 *
 *  ★このファイルは既存 5 seam 構成（golden）のソース列挙に含まれない。
 *    M5 ビルド（fmp3_m5）と C++ ランタイム smoke テストのみがコンパイル・
 *    リンクする。したがって golden バイナリには影響しない。
 */

#include <cstddef>
#include <new>

#include "esp_shim_public.h"		/* esp_shim_malloc/free/log_emerg */
#include "cxx_runtime.h"

extern "C" void abort(void);		/* esp/shim/esp_shim_libc.c */

/*
 *  ----------------------------------------------------------------
 *  operator new / delete → esp_shim ヒープ写像
 *  ----------------------------------------------------------------
 */

static void *
cxx_alloc(std::size_t size)
{
	/*  size==0 でも 1 バイト確保して一意な非 NULL ポインタを返す（規格要件） */
	if (size == 0U) {
		size = 1U;
	}
	void *p = esp_shim_malloc(size);
	if (p == nullptr) {
		/*  -fno-exceptions のため throw できない。顕在化させて停止する。 */
		esp_shim_log_emerg("operator new: out of memory (size=%u)",
							(unsigned int) size);
		abort();
	}
	return p;
}

void *operator new(std::size_t size)			{ return cxx_alloc(size); }
void *operator new[](std::size_t size)			{ return cxx_alloc(size); }

/*  nothrow 版：失敗時は nullptr を返す（throw しない）。 */
void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
	if (size == 0U) {
		size = 1U;
	}
	return esp_shim_malloc(size);
}
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
	if (size == 0U) {
		size = 1U;
	}
	return esp_shim_malloc(size);
}

void operator delete(void *p) noexcept			{ esp_shim_free(p); }
void operator delete[](void *p) noexcept		{ esp_shim_free(p); }

/*  サイズ付き delete（C++14。GCC 14 は _ZdlPvj/_ZdaPvj を要求する） */
void operator delete(void *p, std::size_t) noexcept		{ esp_shim_free(p); }
void operator delete[](void *p, std::size_t) noexcept	{ esp_shim_free(p); }

void operator delete(void *p, const std::nothrow_t &) noexcept	{ esp_shim_free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept	{ esp_shim_free(p); }

/*
 *  ----------------------------------------------------------------
 *  グローバル/静的 C++ コンストラクタ実行（.init_array + .ctors）
 *  ----------------------------------------------------------------
 *  ★本プロジェクトのツールチェーン xtensa-esp-elf esp-14.2.0 は
 *    **--enable-initfini-array 無効**でビルドされており，グローバル C++
 *    コンストラクタを **.init_array ではなくレガシー .ctors に出力する**
 *    （コンパイルフラグでは切り替わらない＝ツールチェーンのビルド時既定）。
 *    ⇒ **`.init_array` だけを走査する実装では動かない。**両方を走査する：
 *
 *      - .init_array … 前方（昇順）に呼ぶ（規格どおり。本ツールチェーンでは空）。
 *      - .ctors      … **後方（逆順）**に呼ぶ（GCC の .ctors 規約。crtend の
 *                       __do_global_ctors_aux と同じ向き。TU 内の定義順を復元する）。
 *
 *    シンボル __init_array_start/end・__ctors_start/end はリンカスクリプトで
 *    定義する（esp32s3_xip.ld / esp32s3_devkitc.ld）。空（start==end）なら何もしない。
 *
 *  ★呼出し位置は cxx_runtime.h のコメント参照（共有起動シーケンスからは
 *    呼ばず，アプリ main_task 先頭で1コア・1回のみ呼ぶ）。
 */
extern "C" {

typedef void (*cxx_init_fn_t)(void);

/*  リンカ定義シンボル（各配列の先頭・末尾）。 */
extern cxx_init_fn_t __init_array_start[];
extern cxx_init_fn_t __init_array_end[];
extern cxx_init_fn_t __ctors_start[];
extern cxx_init_fn_t __ctors_end[];

void
esp_run_init_array(void)
{
	/*  .init_array：前方（規格順）。本ツールチェーンでは通常空。 */
	for (cxx_init_fn_t *p = __init_array_start; p < __init_array_end; ++p) {
		if (*p != nullptr) {
			(*p)();
		}
	}
	/*  .ctors：後方（GCC 逆順規約）。 */
	for (cxx_init_fn_t *p = __ctors_end; p-- > __ctors_start; ) {
		if (*p != nullptr) {
			(*p)();
		}
	}
}

} /* extern "C" */
