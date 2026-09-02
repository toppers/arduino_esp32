/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 移植 — M5対応 esp_shim 拡張（C実装）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  esp_shim_log_emerg / esp_shim_get_core_id の実装。
 *
 *  ★esp/shim/esp_shim.c ではなく本ファイルに置く理由：esp_shim.c は
 *  Wi-Fi/BLE golden 構成の SEAM_OBJS に直接コンパイルされ，このビルドは
 *  未使用シンボルを dead-code-elimination で除去しない。本ファイルは
 *  M5 ビルドのみがコンパイル・リンクし，golden 5構成のソース列挙には
 *  含まれないため，golden バイナリへ影響しない。
 *
 *  宣言は esp/shim/esp_shim.h（Cコンシューマ向け）と esp_shim_public.h の
 *  __cplusplus ブロック（C++コンシューマ向け前方宣言）の両方にある。
 *  実装は本ファイル1本のみ（single source of truth）。
 */

#include <kernel.h>
#include <t_syslog.h>
#include <stdarg.h>
#include <stdio.h>

#include "esp_shim.h"

void
esp_shim_log_emerg(const char *format, ...)
{
	char	buf[128];
	va_list	args;

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	syslog(LOG_EMERG, "%s", buf);
}

/*
 *  実行中コアの0起点ID。get_pid()（1起点PRCID・通常のexternカーネル
 *  サービスコール）から変換する。
 *  ★get_my_prcidx()（アーキ層のInline=static inline関数）を使う実装を
 *    最初に試したが，blob構成（S3-BLE等）のesp_shim.oリンクで
 *    undefined referenceになった（当該構成のkernel.hインクルード連鎖では
 *    static inline本体が可視にならないため，static inlineとして解決
 *    されず外部シンボル参照が残る）。get_pid()は全構成で確実にリンクする。
 */
int32_t
esp_shim_get_core_id(void)
{
	ID	prcid = 1;

	(void) get_pid(&prcid);
	return (int32_t) (prcid - 1);
}

/*
 *  ------------------------------------------------------------------
 *  クリティカルセクション
 *
 *  ■なぜここに置くか
 *    esp_shim_int_disable/restore は esp/shim/esp_shim.c にも同名実装が
 *    あるが，esp_shim.c は esp_shim_cfg.h / diag_recorder.h /
 *    target_timer.h / psa/crypto.h に依存し（同ファイル 28-45 行），
 *    Wi-Fi/BT 構成専用である。**M5 構成へ丸ごと持ち込むことはできない**
 *    （これらのシンボルが 1 件も無く、使えば fail-at-link になる）。
 *    そこで rsil/wsr.ps の 2 命令だけを本 M5 専用 TU に複製する
 *    （esp_shim.c:162-181 と同一ロジック。★変更時は両方を追随させること）。
 *
 *  ■なぜ loc_cpu()/unl_cpu() ではなく rsil/wsr.ps 直叩きか（★根拠）
 *    (1) unlock_cpu()（fmp3/arch/xtensa_gcc/common/core_kernel_impl.h:284-302）は
 *        `rsil %0, 0` で **INTLEVEL を無条件に 0 へ落とす**。退避値を復元
 *        しないので，ネストしたクリティカルセクションの内側で呼ぶと
 *        外側の禁止が解除される＝本 B-3 と同種の欠陥をカーネル API 側から
 *        持ち込むことになる。
 *    (2) さらに unlock_cpu() は既知の設計課題として INTENABLE を触らない。
 *    (3) rsil は「旧 PS 全体を返す／wsr.ps でそれを丸ごと戻す」対称な対で，
 *        INTENABLE には一切触れない。よって上記課題の影響を受けない。
 *    (4) esp/shim/esp_shim.c・esp/bt/bt_shim.c・esp/wifi/net/port/sys_arch.c
 *        の既存クリティカルセクションも全て同じ rsil/wsr.ps 写像であり，
 *        本ポートのシム層の一貫した作法である。
 *
 *  ■ネスト設計
 *    退避状態を mux に置くと，同一 mux の内側 enter が外側の退避値を
 *    「割込み禁止状態」で上書きし，外側 exit が禁止を復元してしまう
 *    （＝B-3。区間を抜けたのに割込みが開かない）。よって
 *    **退避状態はネストカウンタ付きの大域に持ち，最外の enter でのみ退避・
 *    最外の exit でのみ復元する**。mux は無視する（M5 はコア0単一文脈規約）。
 *    ★static ではなく大域変数にすること：ヘッダに static を置くと TU ごとに
 *      別実体になり，ネストカウンタが TU をまたいだ瞬間に破綻する。
 *
 *  ■コア0単一文脈規約が破れたら気づけるようにする
 *    規約（m5/ 各所・bt.cfg と同趣旨）が破れても本実装はコア間排他を
 *    提供しない。そこで検出だけは行い，違反カウンタに記録する
 *    （クリティカルセクション内で syslog/assert すると復旧不能になり得る
 *    ため，記録に留めてテスト側で検査する）。
 */

#ifndef M5_USE_ESP_SHIM	/* ★合成構成では esp/shim が定義する（多重定義になる） */
uint32_t
esp_shim_int_disable(void)
{
	uint32_t	state;

	/* PS.INTLEVEL を 15 へ上げ，旧 PS 全体を返す（esp_shim.c:162-172 と同一） */
	Asm("rsil %0, 15" : "=r"(state) :: "memory");
	return(state);
}

void
esp_shim_int_restore(uint32_t state)
{
	/* 旧 PS を丸ごと復元（INTENABLE には触れない。esp_shim.c:175-181 と同一） */
	Asm("wsr.ps %0; rsync" :: "r"(state) : "memory");
}
#endif /* !M5_USE_ESP_SHIM */

/*
 *  ネスト状態（大域＝全 TU で 1 実体）。
 */
volatile uint32_t	esp_shim_m5_crit_nest;		/* ネスト深さ */
static uint32_t		esp_shim_m5_crit_saved;		/* 最外 enter 時の PS */

/*  規約違反の検出カウンタ（テストが 0 であることを検査する） */
volatile uint32_t	esp_shim_m5_crit_viol_core;	/* コア0以外から呼ばれた */
volatile uint32_t	esp_shim_m5_crit_viol_under;	/* exit が enter より多い */

void
esp_shim_m5_enter_critical(void)
{
	uint32_t	state;

	/*  自コアの割込みを禁止する（rsil はコアローカル操作）。 */
	state = esp_shim_int_disable();

	if (esp_shim_m5_crit_nest == 0U) {
		esp_shim_m5_crit_saved = state;		/*  ★最外だけ退避 */
	}
	esp_shim_m5_crit_nest++;

	/*
	 *  コア0単一文脈規約の検査。
	 *
	 *  ★**ここで get_pid() を使ってはならない。** get_pid() はカーネル
	 *  サービスコールであり CHECK_UNL()（kernel/check.h）が sense_lock() を
	 *  通す。sense_lock() は PS.INTLEVEL != 0 を「ロック中」と判定するため，
	 *  直前の rsil 15 のせいで get_pid() は必ず E_CTX を返し，prcid を書かず
	 *  初期値 1 のまま返る＝**違反カウンタが絶対に増えない**。
	 *
	 *  カーネルサービスコールを経由せず，arch 層の自コア判定
	 *  （fmp3/arch/xtensa_gcc/esp32s3/chip_kernel_impl.h の
	 *  get_my_prcidx()）と同一の機構＝ PRID 特殊レジスタの bit13 を
	 *  直読する。レジスタ読み出しのみで待ちに入らないため，rsil による
	 *  割込み禁止下・ネストの内側（INTLEVEL=15）のどちらでも常に有効。
	 *  コア0＝PRO_CPU は bit13=0。
 */
	{
		uint32_t	prid;

		Asm("rsr.prid %0 \n\t"
			"extui %0, %0, 13, 1" : "=a"(prid));
		if (prid != 0U) {
			esp_shim_m5_crit_viol_core++;
		}
	}
}

void
esp_shim_m5_exit_critical(void)
{
	if (esp_shim_m5_crit_nest == 0U) {
		/*  enter していないのに exit された（下溢れ）。割込み状態は触らない。 */
		esp_shim_m5_crit_viol_under++;
		return;
	}
	esp_shim_m5_crit_nest--;
	if (esp_shim_m5_crit_nest == 0U) {
		esp_shim_int_restore(esp_shim_m5_crit_saved);	/*  ★最外だけ復元 */
	}
}

/*
 *  現在の PS を読む（テスト用の観測点）。
 *  ★カーネルサービスコールを一切経由しないこと：unlock_cpu() が
 *    `rsil 0` で INTLEVEL を無条件に 0 へ落とすため，サービスコールを
 *    挟むと「割込みが復元された」ように見えてしまう（＝常に PASS する
 *    テストになる）。本関数は rsr.ps 1 命令のみ。
 */
uint32_t
esp_shim_m5_read_ps(void)
{
	uint32_t	ps;

	Asm("rsr.ps %0" : "=r"(ps));
	return(ps);
}
