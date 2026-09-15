/*
 *  esp-halヘッダ用のassert.hスタブ
 *
 *  ツールチェーン（riscv64-unknown-elf-gcc）にnewlibヘッダが無い環境の
 *  ためのスタブ．ASP3カーネルはlibcヘッダを使わない（自己完結）が，
 *  esp-halのesp_assert.h／hal/assert.hがassert.hを要求する．
 *  HAL_ASSERTは無効化する（LL層はassertを実質使用しない）．
 */
#ifndef TOPPERS_HAL_STUB_ASSERT_H
#define TOPPERS_HAL_STUB_ASSERT_H

/*  newlib の <assert.h> が先に入っている構成があるので、まず外す
 *  （2026-08-03: 外さないと "assert" redefined が全 TU で出ていた。
 *   本スタブが後勝ちで無効化する、という従来の意図は変えていない）。 */
#undef assert
#define assert(x) ((void)0)

/*
 *  static_assert（C11 _Static_assertのマクロ形）．esp_assert.hの
 *  ESP_STATIC_ASSERTがこの名前をそのまま使う．コンパイラ組込みの
 *  _Static_assertはfreestandingでも利用可能．
 */
#ifndef __cplusplus
#ifndef static_assert
#define static_assert _Static_assert
#endif
#endif

#endif /* TOPPERS_HAL_STUB_ASSERT_H */
