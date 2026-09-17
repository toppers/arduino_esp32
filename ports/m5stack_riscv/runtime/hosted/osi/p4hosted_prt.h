/*
 *  ESP32-P4 + ESP-Hosted — 同期出力の小道具（段 7f で probe から括り出した）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  なぜ括り出したか（段 7f・F2）
 *  ============================================================================
 *  7d/7e までデータパスの実体が `app/rpc_probe/rpc_probe.c` の中にあり、
 *  その中で `static` に書かれていた同期出力の小道具（`pbeg`/`ps`/`pu`/…）を
 *  データパス側も使っていた。データパスを独立した翻訳単位へ移すと、
 *  この小道具が両方から要る。⇒ **移動する。書き直さない。**
 *
 *  【出力が同期でなければならない理由（7a 以来の規律）】
 *  `syslog()` は logtask 経由の**非同期**出力なので、停止直前の行を取りこぼす。
 *  「応答が無かった」と「出力が間に合わなかった」を区別できなくなるため、
 *  実機診断の根拠には使わない。ここは `target_fput_log()` 直呼びである。
 *
 *  【1 行を不可分にする作法・7a で踏んだ罠】
 *  `pbeg()` は `loc_cpu()`、`pnl()` は `unl_cpu()` である。
 *  **読み出しを `pbeg()` の内側でやってはならない**——`loc_mtx()` が `E_CTX` を
 *  返して全部読めなくなる。**読むのはロックの外・印字だけロックの中。**
 *
 *  【短い名前をリンク時にばら撒かない】
 *  `ps`/`pb`/`px` のような 2 文字の外部シンボルは、lwIP 等と衝突し得る。
 *  実体は `p4hosted_prt_*` という長い名前で持ち、呼び出し側の綴り
 *  （移動前と同じ `pbeg`/`ps`/…）は `static inline` の薄い別名で与える。
 *  ⇒ 呼び出し側は **1 文字も変えずに済む**（＝機械的な移動であることの担保）。
 */
#ifndef P4HOSTED_PRT_H
#define P4HOSTED_PRT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void	target_fput_log(char c);

extern void	p4hosted_prt_beg(void);					/* loc_cpu */
extern void	p4hosted_prt_nl(void);					/* '\n' + unl_cpu */
extern void	p4hosted_prt_str(const char *s);
extern void	p4hosted_prt_u32(uint32_t v);			/* 10 進 */
extern void	p4hosted_prt_hex32(uint32_t v);			/* 0x???????? */
extern void	p4hosted_prt_byte(uint8_t b);			/* 2 桁 16 進 */
extern void	p4hosted_prt_kv(const char *k, uint32_t v);
extern void	p4hosted_prt_kx(const char *k, uint32_t v);

/*
 *  バイト列を全数印字する（**黒だったときの唯一の材料**。切り詰めない）。
 *  **秘密を運び得るフレームには使わないこと**（7c §9-1 の事故。呼び手が
 *  `rp_tx_has_secret` 等で自分を止める責任を持つ）。
 */
extern void	p4hosted_prt_dump(const char *tag, const uint8_t *p, uint32_t n);

/*  ---- 移動前と同じ綴り（呼び出し側を変えないための薄い別名）----  */
static inline void pbeg(void) { p4hosted_prt_beg(); }
static inline void pnl(void) { p4hosted_prt_nl(); }
static inline void ps(const char *s) { p4hosted_prt_str(s); }
static inline void pu(uint32_t v) { p4hosted_prt_u32(v); }
static inline void px(uint32_t v) { p4hosted_prt_hex32(v); }
static inline void pb(uint8_t b) { p4hosted_prt_byte(b); }
static inline void pkv(const char *k, uint32_t v) { p4hosted_prt_kv(k, v); }
static inline void pkx(const char *k, uint32_t v) { p4hosted_prt_kx(k, v); }
static inline void
pdump(const char *tag, const uint8_t *p, uint32_t n)
{
	p4hosted_prt_dump(tag, p, n);
}

#ifdef __cplusplus
}
#endif

#endif /* P4HOSTED_PRT_H */
