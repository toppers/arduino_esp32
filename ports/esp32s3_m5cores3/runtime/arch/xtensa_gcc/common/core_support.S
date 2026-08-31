/*
 *  TOPPERS/FMP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2000-2003 by Embedded and Real-Time Systems Laboratory
 *                              Toyohashi Univ. of Technology, JAPAN
 *  Copyright (C) 2005-2020 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，以下の(1)〜(4)の条件を満たす場合に限り，本ソフトウェ
 *  ア（本ソフトウェアを改変したものを含む．以下同じ）を使用・複製・改
 *  変・再配布（以下，利用と呼ぶ）することを無償で許諾する．
 *  (1) 本ソフトウェアをソースコードの形で利用する場合には，上記の著作
 *      権表示，この利用条件および下記の無保証規定が，そのままの形でソー
 *      スコード中に含まれていること．
 *  (2) 本ソフトウェアを，ライブラリ形式など，他のソフトウェア開発に使
 *      用できる形で再配布する場合には，再配布に伴うドキュメント（利用
 *      者マニュアルなど）に，上記の著作権表示，この利用条件および下記
 *      の無保証規定を掲載すること．
 *  (3) 本ソフトウェアを，機器に組み込むなど，他のソフトウェア開発に使
 *      用できない形で再配布する場合には，次のいずれかの条件を満たすこ
 *      と．
 *    (a) 再配布に伴うドキュメント（利用者マニュアルなど）に，上記の著
 *        作権表示，この利用条件および下記の無保証規定を掲載すること．
 *    (b) 再配布の形態を，別に定める方法によって，TOPPERSプロジェクトに
 *        報告すること．
 *  (4) 本ソフトウェアの利用により直接的または間接的に生じるいかなる損
 *      害からも，上記著作権者およびTOPPERSプロジェクトを免責すること．
 *      また，本ソフトウェアのユーザまたはエンドユーザからのいかなる理
 *      由に基づく請求からも，上記著作権者およびTOPPERSプロジェクトを
 *      免責すること．
 *
 *  本ソフトウェアは，無保証で提供されているものである．上記著作権者お
 *  よびTOPPERSプロジェクトは，本ソフトウェアに関して，特定の使用目的
 *  に対する適合性も含めて，いかなる保証も行わない．また，本ソフトウェ
 *  アの利用により直接的または間接的に生じたいかなる損害に関しても，そ
 *  の責任を負わない．
 *
 */

/*
 *  プロセッサ依存モジュール アセンブリ言語部（Xtensa用）
 *
 *  同期ディスパッチ（タスクコンテキストからサービスコール内で発生する
 *  コンテキストスイッチ）を実装する。方式はwindowed ABIの「手放すタスク
 *  の全ライブウィンドウを自スタックへスピルし、TCBにはSPと復帰PCだけを
 *  保存、復帰はretw（ウィンドウフィル）に委ねる」
 *  （参考実装 atk2-sc1/arch/xtensa_xcc/prc_support.S）。
 *
 *  VECBASEは変更せず、Window Overflow/Underflow例外ハンドラはROM常駐の
 *  ものを利用する。全ウィンドウスピルはROM常駐のxthal_window_spill_nw
 *  （リンカスクリプトで固定アドレスに結合）を用いる。
 *
 *  割込み駆動プリエンプション（割込み復帰時ディスパッチ）は本作業単位の
 *  スコープ外（request_dispatch_retintはno-opのまま）。
 */

#define TOPPERS_MACRO_ONLY
#include "kernel_impl.h"
#include "offset.h"
#include "target_asm.inc"

/*
 *  ★重要な副次発見：本ファイルの
 *  `#if XCHAL_HAVE_LOOPS`ブロック（LBEG/LEND/LCOUNTの退避・復元、割込み/
 *  例外のentry/exit双方）は、XCHAL_HAVE_LOOPSマクロが未定義のため
 *  「常に無効（死んだコード）」だった。
 *
 *  トゥールチェーン付属の汎用<xtensa/config/core-isa.h>は
 *  `#include_next <xtensa/config/core-isa.h>`でチップ固有ヘッダへ委譲する
 *  設計だが、本ファイルはそのヘッダ自体をインクルードしておらず（kernel_impl.h/
 *  offset.h/target_asm.incのみ）、ESP32-S3固有core-isa.h（ESP-IDFの
 *  xtensa/esp32s3/include。XCHAL_HAVE_LOOPS=1）への到達手段が無かった。かつ
 *  そのESP32S3固有ヘッダへの-IパスはWiFi/ESP_PLATFORMビルドにしか無く
 *  （QEMU/実機非WiFiテストビルドには無い）、`#include`を素朴に足すと
 *  QEMUビルドが壊れる。よってPS_WOE等と同じ方針（ヘッダ依存を避け必要な
 *  マクロのみ自前定義）で、ここで直接定義する。
 *
 *  ESP32-S3（Xtensa LX7）はゼロオーバーヘッドループオプション
 *  （LBEG/LEND/LCOUNT）を実装している（ESP-IDF core-isa.hでXCHAL_HAVE_LOOPS=1、
 *  JTAG実測でもLBEG/LEND/LCOUNTレジスタの実在を確認済み——追記53）。
 *
 *  影響：このブロックが常に無効だった結果、割込み/例外がZOL実行中の
 *  コンテキストを中断しても、中断側のLBEG/LEND/LCOUNTは一切退避されず、
 *  割込みハンドラ側で別のZOL（例：ROM memcpyの内部ループ、wDev_IndicateFrame
 *  のRXフレーム処理）が実行されるとハードウェアのLBEG/LEND/LCOUNTがそのまま
 *  上書きされる。rfe復帰時にも一切復元されないため、中断されていたタスクは
 *  「無関係な割込みハンドラ側ZOLの終了直後の状態」のLBEG/LEND/LCOUNTを引き
 *  継いだままループ本体へ戻ることになる——追記53が単一原因と結論した
 *  「例外フレームのF_lcountスロット破壊」仮説と並ぶ（あるいはそれ以上に
 *  直接的な）暴走機序であり、追記53自身が「代替仮説：entry/exit経路の
 *  未知の穴」として明記していたものそのものと考えられる。本ブロックを
 *  有効化することで、退避・復元自体が機能するようになり、この経路の暴走は
 *  原理的に解消される（ネストしたZOL利用が中断側の状態を破壊しなくなる）。
 *  下記のLCOUNTサニティガードは、それでもなお残る別経路（例外フレーム上の
 *  F_lcountスロットそのものへの直接OOB書込み等）に対する多層防御として
 *  引き続き有効。
 */
#ifndef XCHAL_HAVE_LOOPS
#define XCHAL_HAVE_LOOPS 1
#endif

/*
 *  LCOUNT暴走ガード閾値
 *
 *  当初案の0x00100000（約100万回転、「正当なZOLはROM memcpy等の手書きasm
 *  のみで100万回転未満」という想定）は誤りだったため0xF0000000へ変更した：
 *  QEMU実測（sample1を190秒ソーク）で、**gccのdoloop最適化がC言語の
 *  カウントループにもZOL（loop命令）を生成する**ことが判明し、sample1の
 *  consume_time()のビジーループが正当にLCOUNT≈0x3da73e0（約6400万）を
 *  使っていた（旧閾値では毎tick誤発動し、正当なループを勝手に打ち切って
 *  いた＝190秒で20693回ヒット）。
 *
 *  0xF0000000（約40億回転）なら、正当なloop命令でこの値に達するには
 *  カウント引数が0xF0000001以上（240MHzで17秒超の単一ループ）必要で
 *  現実的に存在しない一方、実際の暴走はLCOUNT=0xFFFFFFFF（ラップ由来、
 *  実機実測。追記53/54）から始まり、ガードが検査するのは割込みエントリ
 *  時点の退避値＝ラップ直後〜高々数百万回転しか進んでいない値
 *  （0xFFF*****台）なので、確実に閾値以上に留まり検出できる。
 */
#define LCOUNT_GUARD_THRESHOLD  0xF0000000

	.text

/*
 *  タスクディスパッチ  void dispatch(void)
 *
 *  タスクコンテキストからCPUロック状態（PS.INTLEVEL=15）で呼ばれる。
 *  手放すタスクの全ウィンドウをスピルし、SPと復帰点dispatch_rをTCBへ
 *  保存してからディスパッチャへ。再度このタスクが選ばれるとdispatch_r
 *  から復帰し、呼び出し元へretwで戻る（CPUロック状態は呼び出し時のまま）。
 */
	.global dispatch
	.align  4
dispatch:
	entry   a1, 32
	s32i    a0, a1, 0              /* 復帰アドレス退避（call0がa0を壊すため） */
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS                 /* WOE off */
	rsync
	call0   xthal_window_spill_nw  /* 全ライブウィンドウを自スタックへ */
	load_my_pcb a2, a3             /* a2 = 自コアの &pcb */
	l32i    a2, a2, PCB_p_runtsk   /* a2 = p_runtsk */
	movi    a3, dispatch_r
	s32i    a1, a2, TCB_sp         /* runtsk->tskctxb.sp = sp */
	s32i    a3, a2, TCB_pc         /* runtsk->tskctxb.pc = dispatch_r */
	j       dispatcher

	.align  4
dispatch_r:
	rsr     a3, PS
	movi    a0, PS_UM | PS_WOE     /* UM/WOEをセット（INTLEVELは復帰時のまま） */
	or      a3, a3, a0
	l32i    a0, a1, 0              /* 復帰アドレス復元 */
	wsr     a3, PS                 /* WOE on */
	rsync
	retw

/*
 *  ディスパッチャ本体
 *
 *  p_schedtskを実行タスクに選び、そのSP・復帰PCへ切り替える。
 *  WOE offの状態で到達する（dispatch経由）。復帰先（dispatch_r/start_r）が
 *  WOEを適切に設定する。
 *
 *  FPU（コプロセッサCP0）のeager保存・復元もここで行う（lazyは採らない）：
 *   - 全てのコンテキストスイッチ経路（同期dispatch・割込み復帰
 *     .Lint_norm/.Ll3int_norm・CPU例外復帰・exit_and_dispatch・アイドル
 *     復帰）がdispatcher_1へ合流するため、保存・復元をこの1箇所に
 *     集約すれば経路の漏れが構造的に起きない。
 *   - 保存：手放すタスク（この時点のp_runtsk）がTA_FPU（TCB_fpu_flag=1）
 *     ならf0〜f15/FCR/FSRをTCBのfpu_areaへstoreする。割込み経路では
 *     ISR中もFPUレジスタは中断タスクの値のまま生きている（ISRのFPU
 *     使用は禁止）ため、ここでの保存で正しい値が取れる。
 *   - 復元：入るタスクがTA_FPUならfpu_areaからload＋CPENABLE=1、
 *     TA_FPU無しならCPENABLE=0（FPU命令実行はEXCCAUSE=32の
 *     コプロセッサ例外＝バグ検出）。
 *   - fpu_areaのレイアウトは [FCR, FSR, f0..f15]（core_kernel_impl.h、
 *     ESP-IDF tie.hのXCHAL_CP0_SA_LISTと同順）。
 *   - この時点でWOE off・全窓スピル済みのためa2〜a15はスクラッチ
 *     （dispatcher_1以降は必要な値を全てメモリから読み直す）。
 */
	.align  4
dispatcher:
dispatcher_1:
	/*
	 *  ★ウィンドウ正規化（全ディスパッチ経路共通の不変条件、2026-07-10）
	 *
	 *  ここでWINDOWSTART=1/WINDOWBASE=0に正規化し、「dispatcher_2の
	 *  resume（jx）は残存ライブ窓ゼロの状態で行う」ことを構造的に保証する。
	 *
	 *  背景（test_mtrans3実機/QEMU共通ハングの根本原因）：ディスパッチャ
	 *  へ到達する一部経路で、現行チェーンと無関係な残存WINDOWSTARTビット
	 *  （放棄されたコンテキストの物理窓）が生き残ることがある。その状態で
	 *  dispatch_r復帰のretwを実行すると、当該ビットの窓がliveと判定されて
	 *  ウィンドウアンダーフローが起きず、復帰タスクのスタックからでは
	 *  なく**残骸物理窓の中身（無関係なa0/a1）**で呼出し元フレームが
	 *  再構成され、タスクが他コンテキストのSP（割込みスタック等）上で
	 *  走り出す（JTAG/QEMU実測：WS=0x14でWB=4からのretw→ビット2の残骸窓
	 *  a1=istack値を継承→カーネル脱走）。
	 *
	 *  dispatcher_1は「以降必要な値を全てメモリから読み直す」契約で、
	 *  全entry経路（dispatch/exit_and_dispatch/l1int/l3int/CPU例外/idle
	 *  wake/start_dispatch/dispatch_and_migrate/exit_and_migrate）は
	 *  ここに来る前にコンテキスト保存（スピル）または破棄を完了している
	 *  ため、残存ライブ窓は定義上すべてゴミであり破棄してよい。
	 *  ブートチェーン（start.S→sta_ker→start_dispatch）の残存窓が
	 *  後のスピルで割込みスタック上に残骸を撒く問題もここで同時に消える。
	 *
	 *  注意：WINDOWBASE書込みで物理レジスタの見えが回転するため、
	 *  これより前のレジスタ値は以降参照しない（直後にload_my_pcbで
	 *  読み直す）。到達時のWOEはoff（dispatch系）/on（exit_and_dispatch）
	 *  混在だが、wsrは非ウィンドウ命令であり、以降jxまでentry/call/retw
	 *  を実行しないためどちらでも安全。
	 */
	movi    a2, 1
	movi    a3, 0
	wsr     a2, WINDOWSTART        /* ライブウィンドウを現窓のみに */
	wsr     a3, WINDOWBASE
	rsync
	load_my_pcb a3, a4             /* a3 = 自コアの &pcb */
	/* --- FPUコンテキストのeager保存（手放すタスク） --- */
	l32i    a6, a3, PCB_p_runtsk   /* a6 = 手放すタスク */
	beqz    a6, .Lfpu_save_done    /* アイドル復帰等：TCB無し */
	l32i    a7, a6, TCB_fpu_flag
	beqz    a7, .Lfpu_save_done    /* TA_FPU無し：FPUは使っていない */
	movi    a7, 1                  /* CPENABLE=1（CP0。通常は既に1だが防御的に設定） */
	wsr     a7, CPENABLE
	rsync
	rur.fcr a7
	s32i    a7, a6, TCB_fpu_area + 0
	rur.fsr a7
	s32i    a7, a6, TCB_fpu_area + 4
	ssi     f0,  a6, TCB_fpu_area + 8
	ssi     f1,  a6, TCB_fpu_area + 12
	ssi     f2,  a6, TCB_fpu_area + 16
	ssi     f3,  a6, TCB_fpu_area + 20
	ssi     f4,  a6, TCB_fpu_area + 24
	ssi     f5,  a6, TCB_fpu_area + 28
	ssi     f6,  a6, TCB_fpu_area + 32
	ssi     f7,  a6, TCB_fpu_area + 36
	ssi     f8,  a6, TCB_fpu_area + 40
	ssi     f9,  a6, TCB_fpu_area + 44
	ssi     f10, a6, TCB_fpu_area + 48
	ssi     f11, a6, TCB_fpu_area + 52
	ssi     f12, a6, TCB_fpu_area + 56
	ssi     f13, a6, TCB_fpu_area + 60
	ssi     f14, a6, TCB_fpu_area + 64
	ssi     f15, a6, TCB_fpu_area + 68
.Lfpu_save_done:
	l32i    a5, a3, PCB_p_schedtsk /* a5 = p_schedtsk */
	s32i    a5, a3, PCB_p_runtsk   /* p_runtsk = p_schedtsk */
	beqz    a5, dispatcher_idle    /* 実行可能タスク無し→アイドル */
	l32i    a1, a5, TCB_sp         /* ★SP切替：入るタスクのSP */
	load_my_intnest_addr a4, a6
	movi    a6, 0
	s32i    a6, a4, 0              /* 割込みネスト=0（自コア、タスクコンテキスト） */
dispatcher_2:
	load_my_pcb a5, a4
	l32i    a5, a5, PCB_p_runtsk
	/* --- FPUコンテキストのeager復元（入るタスク） --- */
	l32i    a4, a5, TCB_fpu_flag
	beqz    a4, .Lfpu_disable      /* TA_FPU無し：CPENABLE=0（使用は例外＝バグ検出） */
	movi    a4, 1                  /* CPENABLE=1（CP0） */
	wsr     a4, CPENABLE
	rsync
	l32i    a4, a5, TCB_fpu_area + 0
	wur.fcr a4
	l32i    a4, a5, TCB_fpu_area + 4
	wur.fsr a4
	lsi     f0,  a5, TCB_fpu_area + 8
	lsi     f1,  a5, TCB_fpu_area + 12
	lsi     f2,  a5, TCB_fpu_area + 16
	lsi     f3,  a5, TCB_fpu_area + 20
	lsi     f4,  a5, TCB_fpu_area + 24
	lsi     f5,  a5, TCB_fpu_area + 28
	lsi     f6,  a5, TCB_fpu_area + 32
	lsi     f7,  a5, TCB_fpu_area + 36
	lsi     f8,  a5, TCB_fpu_area + 40
	lsi     f9,  a5, TCB_fpu_area + 44
	lsi     f10, a5, TCB_fpu_area + 48
	lsi     f11, a5, TCB_fpu_area + 52
	lsi     f12, a5, TCB_fpu_area + 56
	lsi     f13, a5, TCB_fpu_area + 60
	lsi     f14, a5, TCB_fpu_area + 64
	lsi     f15, a5, TCB_fpu_area + 68
	j       .Lfpu_restore_done
.Lfpu_disable:
	movi    a4, 0
	wsr     a4, CPENABLE
	rsync
.Lfpu_restore_done:
	l32i    a0, a5, TCB_pc         /* 復帰PC（dispatch_r / start_r） */
	jx      a0

/*
 *  アイドル（実行可能タスク無し）
 *
 *  割込みスタックへ切り替え、割込み許可のままp_schedtskをポーリングする。
 *  tick等の割込みがタスクをreadyにするとループを抜けてディスパッチする。
 *  （割込み駆動プリエンプションではなく、アイドルループのポーリングで
 *  検出するため、request_dispatch_retint未実装でも成立する）。
 */
	.align  4
dispatcher_idle:
	load_my_istkpt a1, a2         /* SP = 自コアの割込みスタック */
#ifdef TOPPERS_S3_BT_L3LAT_DIAG
	/*
	 *  ★BT-4診断計装（既定OFF）：rsil>=3マスク窓計測（esp_shim.cの
	 *  l3ld_win_start）のarmをアイドル入場前にクリアする。ここで
	 *  rsil 0によりマスクは実際に解けるが、Cのunlockフックを通らない
	 *  ため、クリアしないと「タスクのlock〜アイドル〜復帰後のunlock」
	 *  までのアイドル滞在時間全体が巨大なマスク窓として誤計上される。
	 *  窓変数はコア0専用のため、コア0のみ実施（コア1が触るとコア0の
	 *  計測中の窓を破壊する）。この時点ではまだINTLEVEL=15なので
	 *  排他は不要。a3/a4はこの直後のidle_loopで再ロードされるため
	 *  破壊してよい。
	 */
	my_prcidx a3
	bnez    a3, 1f
	movi    a3, l3ld_win_start
	movi    a4, 0
	s32i    a4, a3, 0
1:
#endif /* TOPPERS_S3_BT_L3LAT_DIAG */
	rsil    a2, 0                  /* 割込み許可（CPUロック解除） */
idle_loop:
	load_my_pcb a3, a4
	l32i    a4, a3, PCB_p_schedtsk
	beqz    a4, idle_loop          /* readyになるまで待つ */
	rsil    a2, 15                 /* CPUロック（INTLEVEL=15） */
	j       dispatcher_1

/*
 *  ディスパッチャの動作開始  void start_dispatch(void)
 *
 *  カーネル起動時（sta_ker末尾）に、割込みスタック上・CPUロック状態で
 *  呼ばれる。p_schedtskを選び最初のタスクへstart_r経由で切り替わる。
 */
	.global start_dispatch
	.align  4
start_dispatch:
	entry   a1, 32
dispatcher_0:
	j       dispatcher_1

/*
 *  現在のコンテキストを捨ててディスパッチ  void exit_and_dispatch(void)
 *
 *  ext_tsk/ter_tskから呼ばれる。手放すコンテキストは破棄するのでスピル
 *  せず、WINDOWSTART=1/WINDOWBASE=0でウィンドウ状態を正規化してから
 *  ディスパッチャへ（次タスクはstart_rで起動、entryがウィンドウ1へ回転
 *  してもWINDOWSTART的に空きのため安全）。
 */
	.global exit_and_dispatch
	.align  4
exit_and_dispatch:
	entry   a1, 16
	movi    a3, ~PS_WOE
	rsr     a2, PS
	and     a2, a2, a3
	wsr     a2, PS                 /* WOE off */
	rsync
	movi    a2, 1
	movi    a3, 0
	wsr     a2, WINDOWSTART        /* ライブウィンドウを1つに正規化 */
	wsr     a3, WINDOWBASE
	rsync
	movi    a0, 0                  /* フレームチェーン終端 */
	rsr     a2, PS
	movi    a3, PS_WOE
	or      a2, a3, a2
	wsr     a2, PS                 /* WOE on */
	rsync
	load_my_istkpt a1, a2         /* 破棄するタスクスタックから離れ、自コア割込みスタックへ */
	j       dispatcher_0

/*
 *  自タスクのマイグレーション
 *  void dispatch_and_migrate(PCB *p_my_pcb, TCB *p_selftsk)
 *
 *  mig_tsk（kernel/task_manage.c）から、タスクコンテキスト・CPUロック
 *  状態（PS.INTLEVEL=15）・ジャイアントロック取得状態で呼ばれる。
 *  呼出し時点で自タスクはmake_non_runnable済みで、p_selftsk->p_pcbは
 *  マイグレート先コアのPCBを指している。
 *
 *  FMP3のマイグレーションプロトコル（参考実装：fmp3_trunk/arch/
 *  riscv_gcc/common/core_support.S）：
 *   1. 自タスクのコンテキストを完全に保存する
 *      （windowed ABIでは全ライブウィンドウのスピル＋TCBへSP/復帰PC）
 *   2. 自タスクのスタックから離脱する（自コアの割込みスタックへ）
 *   3. migrate_self（kernel/task.c）を呼ぶ：make_runnableで対象コアの
 *      レディキューへ挿入（update_schedtsk_dsp→request_dispatch_prcで
 *      IPI）→release_glock→exit_and_dispatchで自コアは次タスクへ。
 *      戻らない。
 *  「コンテキスト保存完了 → 公開（make_runnable）」の順序が本質：
 *  対象コアはp_schedtsk更新を見た瞬間からこのTCB（sp/pc/fpu_area）を
 *  復元して走り出し得るため、公開後に自コア側からTCB・タスクスタックへ
 *  書き込んではならない。
 *
 *  FPU（eager方式）：手放すタスクのFPU保存は通常
 *  dispatcher_1で行うが、dispatcher_1に到達するのはmigrate_self内の
 *  公開・glock解放より後になるため、ここで先に保存し、PCB_p_runtsk=NULL
 *  にしてdispatcher_1側の保存（公開後のTCB書込み＝対象コアとの競合）を
 *  抑止する。eager方式なのでこれで完全：TCBのfpu_areaが常に最新状態を
 *  持ち、lazy方式で必要になる「他コアに残った所有者状態の回収
 *  （FPU save/release IPI）」は構造的に存在しない（マイグレーション用の
 *  FPU処理はこの退避のみで、追加のsave/release手続きは空）。
 *  p_runtsk==NULLはアイドル中に常用される正当な状態（dispatcher_1が
 *  p_schedtskで上書きする）。
 *
 *  自タスクが再選択されると（マイグレート先コアで）dispatch_rから再開し、
 *  retwのウィンドウアンダーフローが1.でスピルした呼出し元フレームを
 *  タスクスタックから充填して、mig_tsk内の呼出し直後へ戻る（この時
 *  ジャイアントロックは解放済み＝dispatch()と同じ契約）。
 */
	.global dispatch_and_migrate
	.align  4
dispatch_and_migrate:
	entry   a1, 32
	s32i    a0, a1, 0              /* 復帰アドレス退避（dispatch_rが復元） */
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS                 /* WOE off */
	rsync
	call0   xthal_window_spill_nw  /* 全ライブウィンドウを自スタックへ */
	load_my_pcb a2, a3             /* a2 = 自コアの &pcb (= p_my_pcb) */
	l32i    a3, a2, PCB_p_runtsk   /* a3 = p_selftsk (= p_runtsk) */
	movi    a4, dispatch_r
	s32i    a1, a3, TCB_sp         /* selftsk->tskctxb.sp = sp */
	s32i    a4, a3, TCB_pc         /* selftsk->tskctxb.pc = dispatch_r */
	/* --- FPUコンテキストのeager保存（公開前に必須。dispatcher_1と同内容） --- */
	l32i    a4, a3, TCB_fpu_flag
	beqz    a4, .Lmig_fpu_done     /* TA_FPU無し：FPUは使っていない */
	movi    a4, 1                  /* CPENABLE=1（CP0。通常は既に1だが防御的に設定） */
	wsr     a4, CPENABLE
	rsync
	rur.fcr a4
	s32i    a4, a3, TCB_fpu_area + 0
	rur.fsr a4
	s32i    a4, a3, TCB_fpu_area + 4
	ssi     f0,  a3, TCB_fpu_area + 8
	ssi     f1,  a3, TCB_fpu_area + 12
	ssi     f2,  a3, TCB_fpu_area + 16
	ssi     f3,  a3, TCB_fpu_area + 20
	ssi     f4,  a3, TCB_fpu_area + 24
	ssi     f5,  a3, TCB_fpu_area + 28
	ssi     f6,  a3, TCB_fpu_area + 32
	ssi     f7,  a3, TCB_fpu_area + 36
	ssi     f8,  a3, TCB_fpu_area + 40
	ssi     f9,  a3, TCB_fpu_area + 44
	ssi     f10, a3, TCB_fpu_area + 48
	ssi     f11, a3, TCB_fpu_area + 52
	ssi     f12, a3, TCB_fpu_area + 56
	ssi     f13, a3, TCB_fpu_area + 60
	ssi     f14, a3, TCB_fpu_area + 64
	ssi     f15, a3, TCB_fpu_area + 68
.Lmig_fpu_done:
	/* --- タスクスタックから離脱：窓正規化→WOE on→割込みスタックへ --- */
	movi    a2, 1
	movi    a3, 0
	wsr     a2, WINDOWSTART        /* ライブウィンドウを1つに正規化 */
	wsr     a3, WINDOWBASE
	rsync
	/* WINDOWBASE書込みで窓が回転しレジスタは全て無効。以降メモリから再取得 */
	movi    a0, 0                  /* フレームチェーン終端 */
	rsr     a2, PS
	movi    a3, PS_WOE
	or      a2, a3, a2
	wsr     a2, PS                 /* WOE on（INTLEVEL=15のまま＝CPUロック） */
	rsync
	load_my_istkpt a1, a2          /* SP = 自コアの割込みスタック */
	load_my_pcb a2, a3
	l32i    a7, a2, PCB_p_runtsk   /* a7 = p_selftsk → 第2引数 */
	movi    a3, 0
	s32i    a3, a2, PCB_p_runtsk   /* p_runtsk = NULL（上記コメント参照） */
	memw                           /* TCB保存完了を公開（make_runnable）より前に確定 */
	mov     a6, a2                 /* a6 = p_my_pcb → 第1引数 */
	call4   migrate_self           /* 戻らない（公開→release_glock→exit_and_dispatch） */
1:
	j       1b

/*
 *  現在のコンテキストを捨ててマイグレートと起動
 *  void exit_and_migrate(PCB *p_my_pcb, TCB *p_selftsk)
 *
 *  自タスクの終了＋他コアでの再起動（ext_tsk等でtask_terminateがtrueを
 *  返した場合。task_term.c/sys_manage.c/interrupt.c）に、タスクコンテキ
 *  スト・CPUロック状態・ジャイアントロック取得状態で呼ばれる。この時点で
 *  自タスクはrelease_context/make_dormant済みで、p_pcbは再起動先コアの
 *  PCBを指している（マイグレート先が自コアであってはならない仕様）。
 *
 *  現コンテキストは破棄する：exit_and_dispatchと同じ窓正規化を行う。
 *  スピルしてはならない（再起動先コアがmake_active公開直後から同じ
 *  タスクスタックを使い始めるため、古いライブウィンドウのスピルが
 *  遅れて走ると再起動後のスタックを破壊する）。FPU保存も不要
 *  （タスク終了→再起動なのでFPU状態は引き継がない。activate_contextが
 *  fpu_areaをゼロ初期化する。eager方式のためマイグレーション用の
 *  FPU save/release処理はここでも空）。
 *
 *  migrate_activate_self（kernel/task.c）がmake_active（activate_context
 *  済みTCBを再起動先コアへ公開→IPI）→release_glock→exit_and_dispatch。
 *  戻らない。
 */
	.global exit_and_migrate
	.align  4
exit_and_migrate:
	entry   a1, 16
	movi    a3, ~PS_WOE
	rsr     a2, PS
	and     a2, a2, a3
	wsr     a2, PS                 /* WOE off */
	rsync
	movi    a2, 1
	movi    a3, 0
	wsr     a2, WINDOWSTART        /* 破棄：ライブウィンドウを1つに正規化 */
	wsr     a3, WINDOWBASE
	rsync
	/* WINDOWBASE書込みで窓が回転しレジスタは全て無効。以降メモリから再取得 */
	movi    a0, 0                  /* フレームチェーン終端 */
	rsr     a2, PS
	movi    a3, PS_WOE
	or      a2, a3, a2
	wsr     a2, PS                 /* WOE on（INTLEVEL=15のまま＝CPUロック） */
	rsync
	load_my_istkpt a1, a2          /* SP = 自コアの割込みスタック */
	load_my_pcb a2, a3
	l32i    a7, a2, PCB_p_runtsk   /* a7 = p_selftsk（全呼出し元でp_runtsk） */
	movi    a3, 0
	s32i    a3, a2, PCB_p_runtsk   /* p_runtsk = NULL（公開後のdispatcher_1 FPU保存抑止） */
	memw
	mov     a6, a2                 /* a6 = p_my_pcb → 第1引数 */
	call4   migrate_activate_self  /* 戻らない（公開→release_glock→exit_and_dispatch） */
1:
	j       1b

/*
 *  新規タスク起動トランポリン  start_r
 *
 *  dispatcherからjxで、SP＝タスクスタック末尾の状態で来る。PSを
 *  ユーザモード＋WOE＋CALLINC=1に設定し、exinfをa6（タスクのentryが
 *  ウィンドウを1回転させた後のa2＝第1引数）に、復帰ガードexit_taskを
 *  a4（同a0）に置いてタスク本体へjxする。
 */
	.global start_r
	.align  4
start_r:
	load_my_pcb a5, a4
	l32i    a4, a5, PCB_p_runtsk   /* a4 = runtsk (TCB*) */
	l32i    a3, a4, TCB_p_tinib    /* a3 = p_tinib */
	movi    a6, PS_START_TASK      /* UM|WOE|CALLINC1, INTLEVEL=0 */
	wsr     a6, PS
	rsync
	l32i    a6, a3, TINIB_exinf    /* a6 = exinf → タスクのa2（第1引数） */
	l32i    a2, a3, TINIB_task     /* a2 = タスク本体アドレス（jx先） */
	movi    a4, exit_task          /* a4 = 復帰ガード → タスクのa0 */
	jx      a2

/*
 *  タスクが（ext_tskを呼ばず）本体からreturnしてきた場合の保護
 *
 *  通常のタスクは無限ループかext_tsk()で終了するため、ここへは到達しない
 *  想定。到達した場合はext_tsk()相当としてタスクを終了させる。
 */
	.global exit_task
	.align  4
exit_task:
	call4   ext_tsk                /* 復帰しない */
1:
	j       1b

/*
 *  カーネル終了呼び出し  call_exit_kernel(PCB *p_my_pcb)
 *
 *  ext_ker()等からカーネル終了時に呼ばれる。自コアの割込みスタックへ
 *  切り替え、非タスクコンテキストで exit_kernel(p_my_pcb) を呼び出す
 *  （exit_kernelはbarrier_sync後にtarget_exit()へ至り戻らない）。
 *  a2 = p_my_pcb（第1引数）。
 */
	.global call_exit_kernel
	.align  4
call_exit_kernel:
	entry   a1, 32
	mov     a5, a2                 /* a5 = p_my_pcb（istkpt読込みを跨いで保持） */
	load_my_istkpt a1, a4          /* SP = 自コアの割込みスタック */
	movi    a0, 0                  /* フレームチェーン終端 */
	mov     a6, a5                 /* a6 → exit_kernelの第1引数(a2) */
	call4   exit_kernel            /* 戻らない（target_exitへ） */
2:
	j       2b

/*
 *  Level-1割込みエントリ  _kernel_l1int_entry
 *
 *  ROMの_UserExceptionVector(0x40000340)がEXCCAUSE=4でここへjxする。
 *  エントリ前提条件（design.md §2、ROM実測）：
 *   a1 = 中断SP - 0x100 = FB（中断タスクスタック上の256Bフレーム）
 *   FB[F_a0]=中断a0, FB[F_oldsp]=中断SP, FB[F_a2/a3/a4]=中断a2/a3/a4（ROMが退避）
 *   a5〜a15 = 中断タスクのライブ窓のまま（未退避）
 *   EPC1=復帰PC, PS.EXCM=1（窓例外・Level1抑止）
 *
 *  処理：中断コンテキストをFBへ退避 → 中断タスクの全窓をスピル →
 *  割込みスタックへ切替 → 擬似call4でEXCM解除・windowed化しCハンドラ
 *  _kernel_l1int_dispatch（target_timer.c、tick/IPIのdemux）を実行 →
 *  復帰時にp_runtsk≠p_schedtskなら遅延ディスパッチ（既存dispatcher_1へ）、
 *  等しければFBから復元しrfe。
 *  [追記56] 旧コメントは"target_hrt_handler_prc1を実行"としていたが誤り
 *  （同関数は呼ばれないデッドコード）。実際に呼ばれるのは
 *  _kernel_l1int_dispatchのみ。target/esp32s3_devkitc_gcc/target_timer.c参照。
 *
 *  ［レビュー指摘#16］本エントリはCPENABLEを保存・変更しない（FPU eager
 *  save/restoreはタスクディスパッチ時のみ、dispatcher_1/2参照）。ISR
 *  （_kernel_l1int_dispatch以下のCハンドラ）はFPU命令を使用禁止
 *  （docs/development-guidelines.md参照）。違反するとコプロセッサ例外が
 *  発生せず、中断中タスクがTA_FPUの場合そのf0-f15を静かに破壊するため
 *  厳守すること。
 */
	.global _kernel_l1int_entry
	.align  4
_kernel_l1int_entry:
	/*
	 * ★重要：中断タスクの元のPSを、割込みマスク(rsil)より前に退避する。
	 * rsil a2,15 はPS.INTLEVELを15へ書き換えるため、これより後にPSを読むと
	 * 中断タスク本来のINTLEVEL(=0)ではなく15が保存されてしまい、rfe復帰時に
	 * タスクが割込み禁止状態のまま再開してしまう（API実行中の割込みで
	 * 割込み禁止状態が壊れる不具合の原因。ユーザ指摘で判明）。
	 * a2はROMがフレーム[F_a2]へ退避済みなのでスクラッチに使ってよい。
	 */
	rsr     a2, PS                 /* 中断タスクの元PS（INTLEVEL保持、EXCM=1） */
	s32i    a2, a1, F_ps
	rsil    a2, 15                 /* 以降 全割込みマスク（PS.INTLEVEL=15） */
	/* 中断窓 a5-a15 をFBへ退避（a0/a2/a3/a4はROMが退避済み） */
	s32i    a5, a1, F_a5
	s32i    a6, a1, F_a6
	s32i    a7, a1, F_a7
	s32i    a8, a1, F_a8
	s32i    a9, a1, F_a9
	s32i    a10, a1, F_a10
	s32i    a11, a1, F_a11
	s32i    a12, a1, F_a12
	s32i    a13, a1, F_a13
	s32i    a14, a1, F_a14
	s32i    a15, a1, F_a15
	rsr     a2, EPC1
	s32i    a2, a1, F_pc
	rsr     a2, SAR
	s32i    a2, a1, F_sar
	rsr     a2, EXCCAUSE
	s32i    a2, a1, F_exccause
#if XCHAL_HAVE_LOOPS
	rsr     a2, LBEG
	s32i    a2, a1, F_lbeg
	rsr     a2, LEND
	s32i    a2, a1, F_lend
	movi    a2, 0
	xsr     a2, LCOUNT
	isync
	s32i    a2, a1, F_lcount
#endif

	/*
	 * int6の再入防止：INTENABLEでint6を落とす。Cハンドラ(signal_time)は
	 * INTLEVEL=0で動く必要がある（assert(!sense_lock())）ため、INTLEVELで
	 * はなくINTENABLEでマスクする。int6が唯一の割込み源なので全マスクでよい。
	 * 出口でint6を再許可する。
	 */
	movi    a2, 0
	wsr     a2, INTENABLE
	rsync

	/* 割込みネストカウンタ++（自コア。asmが唯一の管理者。Cのi_begin/i_end_intは削除済み） */
	load_my_intnest_addr a2, a3
	l32i    a3, a2, 0
	addi    a5, a3, 1
	s32i    a5, a2, 0
	bnez    a3, .Lint_nested_panic  /* int6単独源・全マスクのためネストは起きない。起きたら要調査 */

	/* --- 外側（タスク→ISR）：中断タスクの全窓をスピル --- */
	/* スピルは現窓の実SPを基準にするため、a1を中断実SP(FB+0x100)へ戻す */
	addmi   a1, a1, EXC_FRAME_SIZE  /* a1 = FB + 0x100 = 中断実SP */
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS                  /* WOE off */
	rsync
	call0   xthal_window_spill_nw   /* 直上の親窓を[中断SP-16..]=FB上部へ、それ以深はタスクスタックへ */
	/* a1は中断実SPのまま。FBを算出し割込みスタックへ切替 */
	addmi   a5, a1, -EXC_FRAME_SIZE /* a5 = FB（中断タスクフレーム、TCB_sp/復元に使う） */
	load_my_istkpt a1, a2          /* a1 = 自コアの割込みスタック先頭（a5=FBは保持） */
	/* FBをルートフレームの退避スロットへ（擬似call4を跨いで保持） */
	addi    a3, a1, -ROOT_FRAME_SIZE
	s32i    a5, a3, ISTK_SAVED_FB
	/* 擬似call4：PS=WOE|UM|CALLINC1、INTLEVEL=0（signal_timeのassert対応。
	 * 再入はINTENABLE=0で防止済み）、EXCM解除、ルート窓確立 */
	movi    a2, (PS_WOE | PS_UM | PS_CALLINC_CALL4)
	wsr     a2, PS
	rsync
	movi    a0, 0                   /* フレームチェーン終端 */
	/*
	 * entryは4バイト境界必須。本asmでは.alignが0埋め（=ill命令化）する
	 * ため、jで整列用パディングを飛び越し、パディングを実行経路から外す
	 * （.Ll1int_do_entryは.align 4直後で4整列。固定nop.n 1個での詰めは
	 * 本関数の直前に別ファイル・別コードの変更が入りリンクアドレスの
	 * 偶奇が変わると整列がズレて実行時にillegal instructionを起こす
	 * 不安定な手法だったため、cpuexc入口(.Lexc_do_entry)と同じ
	 * j-ジャンプ方式に統一した。2026-07-07）。
	 */
	j       .Ll1int_do_entry
	.align  4
.Ll1int_do_entry:
	entry   a1, ROOT_FRAME_SIZE     /* 割込みスタック上にルート窓を確立 */

	call4   _kernel_l1int_dispatch  /* Level-1 demux（tick/IPIを振分け。windowed） */

	/* --- イグジット --- */
	rsil    a2, 15
	/* 自コアの許可割込みマスク_kernel_intenable_mask[prcidx]をINTENABLEへ
	 * 復元する。tick(INT6)・IPIに加えCFG_INT/ena_intで許可した割込み
	 * （例：test_int1のINT7）の許可状態をtickをまたいで保持するため、
	 * ハードコードのINT_KERNEL_MASKではなくソフトウェアマスクを使う。
	 * INTLEVEL=15でマスク中に書き換える。
	 * [2026-07-20 vpri] 実効値 = _kernel_intenable_mask[c] & _kernel_vpri_mask[c]。
	 * a3=prcidxを保持したままa4にvpriマスクを読み、ANDしてから書く。a4は
	 * この時点でdead（次のa4使用は後段のload_my_pcb a4のfresh load）。 */
	movi    a2, _kernel_intenable_mask
	my_prcidx a3
	addx4   a2, a3, a2
	l32i    a2, a2, 0
	movi    a4, _kernel_vpri_mask
	addx4   a4, a3, a4
	l32i    a4, a4, 0
	and     a2, a2, a4
	wsr     a2, INTENABLE
	rsync
	load_my_intnest_addr a2, a3
	l32i    a3, a2, 0
	addi    a3, a3, -1
	s32i    a3, a2, 0
	/* 外側復帰：ルートフレームからFBを復元 */
	l32i    a5, a1, ISTK_SAVED_FB   /* a5 = FB（entry後 a1 = istk - ROOT_FRAME_SIZE） */

	/*
	 * プリエンプト有無に依らず、中断タスクのTCBへ FB と復帰ラベル
	 * .Lret_int_r を保存し、窓を正規化してdispatcher_1へ渡す（統一経路）。
	 *
	 * これにより、プリエンプトが無い場合でもdispatcherが同じタスクを
	 * 再選択して.Lret_int_r経由で復帰する。擬似call4で作ったルート窓を
	 * そのまま復帰に使うと中断窓の二重化（WB0とWB0+1の重複）が起き、後の
	 * retwが誤った窓へ戻ってクラッシュするため、必ずdispatcher_1の窓正規化
	 * ＋メモリ経由の状態受け渡しを通す。プリエンプト時はdispatcherが高優先度
	 * タスクを選び、そのdispatch_r/start_r/.Lret_int_rで再開する。
	 */
	load_my_pcb a4, a2            /* a4 = 自コアの &pcb（a5=FBは保持） */
	l32i    a3, a4, PCB_p_runtsk
	/*
	 * アイドル（p_runtsk==NULL）を中断した場合はTCBが無い。アイドルは
	 * 状態を持たない（poll_loopに戻るだけ）ので、TCBへの保存はスキップし
	 * 窓正規化してdispatcher_1へ（タスクがreadyなら起こし、無ければ再アイドル）。
	 */
	beqz    a3, .Lint_norm
	s32i    a5, a3, TCB_sp          /* runtsk->tskctxb.sp = FB */
	movi    a2, .Lret_int_r
	s32i    a2, a3, TCB_pc          /* runtsk->tskctxb.pc = 復帰ラベル */
.Lint_norm:
	/* dispatcher_1はWOE off・窓正規化前提（既存dispatchと同じ）に合わせる。
	 * この後レジスタはdispatcher_1がメモリから読み直すため破壊されてよい */
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS
	rsync
	movi    a2, 1
	wsr     a2, WINDOWSTART
	movi    a2, 0
	wsr     a2, WINDOWBASE
	rsync
	j       dispatcher_1            /* 既存の同期ディスパッチャを再利用（プリエンプト有無共通） */

/*
 *  割込み中断タスクの再開点（dispatcher_2がjx TCB_pc=.Lret_int_r）
 *  a1 = TCB_sp = FB。dispatcher_1で窓正規化済み（単一窓）。
 *  フレームを復元しrfeで割込み前へ戻る。
 */
	.align  4
.Lret_int_r:
	rsil    a2, 15
.Lret_do_restore:
	l32i    a2, a1, F_exccause
	wsr     a2, EXCCAUSE
	l32i    a2, a1, F_sar
	wsr     a2, SAR
#if XCHAL_HAVE_LOOPS
	l32i    a2, a1, F_lbeg
	wsr     a2, LBEG
	l32i    a2, a1, F_lend
	wsr     a2, LEND
	/*
	 * LCOUNT暴走ガード：
	 * WiFi動作中、LCOUNTに0xFFFFFFFF（ラップ由来の異常値。実機実測で発動時の
	 * 退避値は毎回正確に0xFFFFFFFF）が載った状態が反復的に発生する。これを
	 * 無検査でLCOUNTへ書き戻すと、復帰PCがZOLループ範囲内だった場合に
	 * ROM memcpy等のZOLループが約43億回転の暴走掃射になり、上位DRAM約170KBを
	 * RXデータで埋め尽くす（実機JTAGで現行犯確認済み：PC=ROM memcpy内s32i.n、
	 * LCOUNT=0xFFFFCE53、コピー済みバイト数が0xFFFFFFFF-LCOUNTと厳密一致）。
	 *
	 * 閾値LCOUNT_GUARD_THRESHOLD（0xF0000000。根拠はファイル冒頭の定義コメント
	 * 参照）以上なら異常値とみなし、0へ置換して復元する（LCOUNT=0ならループ
	 * バックが起きず、中断していたZOLは現在の反復で自然終了する。半端なコピー
	 * になるがフレーム1個の欠損で済み、暴走掃射よりはるかに安全）。ヒットしたら
	 * 診断用にper-core配列（_kernel_lcount_guard_cnt/val/pc、core_kernel_impl.c）
	 * へ記録する。
	 *
	 * レジスタ制約：この時点でスクラッチ可能なのはa2/a3のみ（a4以降はこの
	 * 関数の後段で復元順序に使われるため未定義／破壊禁止）。a1(FB)は最後まで
	 * ポインタとして必要なため触れない。復元順序自体は変更しない。
	 */
	l32i    a2, a1, F_lcount
	movi    a3, LCOUNT_GUARD_THRESHOLD
	bltu    a2, a3, .Llcount_guard_wsr   /* 閾値未満＝正常範囲。a2はそのまま */
	/* --- 異常値検出：診断記録して0へ置換 --- */
	my_prcidx a3
	movi    a2, _kernel_lcount_guard_cnt
	addx4   a2, a3, a2
	l32i    a3, a2, 0
	addi    a3, a3, 1
	s32i    a3, a2, 0             /* guard_cnt[prcidx]++ */

	my_prcidx a3
	movi    a2, _kernel_lcount_guard_val
	addx4   a2, a3, a2
	l32i    a3, a1, F_lcount      /* 異常値そのもの（フレームから再読込） */
	s32i    a3, a2, 0             /* guard_val[prcidx] = 異常値 */

	my_prcidx a3
	movi    a2, _kernel_lcount_guard_pc
	addx4   a2, a3, a2
	l32i    a3, a1, F_pc          /* 復帰先PC（中断PC） */
	s32i    a3, a2, 0             /* guard_pc[prcidx] = F_pc */

	movi    a2, 0                 /* 復元値を0へ置換（ループバック抑止） */
.Llcount_guard_wsr:
	wsr     a2, LCOUNT
	isync
#endif
	l32i    a0,  a1, F_a0
	l32i    a6,  a1, F_a6
	l32i    a7,  a1, F_a7
	l32i    a8,  a1, F_a8
	l32i    a9,  a1, F_a9
	l32i    a10, a1, F_a10
	l32i    a11, a1, F_a11
	l32i    a12, a1, F_a12
	l32i    a13, a1, F_a13
	l32i    a14, a1, F_a14
	l32i    a15, a1, F_a15
	l32i    a5,  a1, F_a5
	l32i    a4,  a1, F_a4
	l32i    a3,  a1, F_pc
	wsr     a3, EPC1
	l32i    a2,  a1, F_ps
	wsr     a2, PS                  /* EXCM=1・中断時INTLEVELを復元 */
	rsync
	l32i    a3,  a1, F_a3
	l32i    a2,  a1, F_a2
	l32i    a1,  a1, F_oldsp        /* a1 = 中断時の実SP */
	rfe                             /* 割込み前へ復帰（EXCM解除・INTLEVEL復元） */

/*
 *  想定外の割込みネスト（int6単独源・全マスクのため到達しない）。
 *  複数割込み源・ネスト対応は別作業単位。到達したら停止して要調査。
 */
	.align  4
.Lint_nested_panic:
	j       .Lint_nested_panic

/*
 *  Level-3割込みエントリ  _kernel_l3int_entry
 *
 *  BT-4調査（.claude/plans/sparkling-forging-taco.md）：BTコントローラが
 *  要求するESP_INTR_FLAG_LEVEL3を実際にXtensa Level-3として動作させる
 *  ための新規エントリ。ROMの_Level3Vector(0x400001c0)は
 *      xsr.excsave3 a2 ; jx a2
 *  のみを行う（ROM ELF実測）ため、VECBASEは変更せず、起動時にEXCSAVE3へ
 *  本関数のアドレスを書き込むだけでフックできる（target_timer.c参照）。
 *
 *  Level-1と異なりROM側のフレーム事前退避が一切無い（_UserExceptionVector
 *  のような256Bフレーム確保・a0/a2/a3/a4退避は行われない）。到達時点で
 *  a0/a1(SP)/a3-a15は中断コンテキストのライブ値のまま、a2だけがROMの
 *  xsrで自由に使えるスクラッチ（元のa2はEXCSAVE3に退避済み）。
 *
 *  フレームは_kernel_l1int_entryと同一のF_*オフセット規約（core_asm.inc）
 *  を流用するが、EXCCAUSE概念が無いためF_exccauseは使わない。戻りは
 *  EPC1/PS/rfeではなくEPC3/EPS3/rfi 3を使う（RFI nはPS/PCをEPS_n/EPC_n
 *  から無条件に上書きするため、C側ディスパッチ中にINTLEVELが変化して
 *  いても正しく復帰できる）。
 *
 *  ★INTLEVELはLevel-1と同じくPS_L3INT_PSEUDO_CALL4（core_asm.inc、
 *  実質PS_START_TASKと同値＝INTLEVEL=0）で_kernel_l3int_dispatchを呼ぶ。
 *  当初はINTLEVEL=3を維持する設計だったが、実機検証（l3_uart_probe、
 *  UART割込み1回成功後に完全停止）でunlock_cpu()（core_kernel_impl.h、
 *  "rsil ,0"でINTLEVELを無条件に0へ落とす実装）との相性問題が判明した
 *  （詳細はcore_asm.incのPS_L3INT_PSEUDO_CALL4コメント参照）。Level-3の
 *  意義はハードウェアが実際にLevel-3例外として受理した時点で既に
 *  達成されているため、C側ディスパッチをINTLEVEL=3のまま維持する必要は
 *  無い。
 */
	.global _kernel_l3int_entry
	.align  4
_kernel_l3int_entry:
	addi    a2, a1, -EXC_FRAME_SIZE   /* a2 = FB = 中断SP - 0x100 */
	s32i    a1, a2, F_oldsp            /* 中断時の実SPを退避 */
	s32i    a0, a2, F_a0
	s32i    a3, a2, F_a3
	s32i    a4, a2, F_a4
	s32i    a5, a2, F_a5
	s32i    a6, a2, F_a6
	s32i    a7, a2, F_a7
	s32i    a8, a2, F_a8
	s32i    a9, a2, F_a9
	s32i    a10, a2, F_a10
	s32i    a11, a2, F_a11
	s32i    a12, a2, F_a12
	s32i    a13, a2, F_a13
	s32i    a14, a2, F_a14
	s32i    a15, a2, F_a15
	mov     a1, a2                     /* a1 = FB（以後Level-1と同じ意味で使う） */
	rsr     a3, EXCSAVE3                /* a3 = 中断時の元a2（ROMのxsrが退避） */
	s32i    a3, a1, F_a2
	/*
	 * ★EXCSAVE3の再アーム（必須）：ROMの_Level3Vectorは
	 *     xsr.excsave3 a2 ; jx a2
	 *  ＝「交換」命令のため、ベクタ通過後のEXCSAVE3には本ハンドラの
	 *  アドレスではなく中断時のa2（上でF_a2へ退避した値）が残っている。
	 *  ここで書き戻さないと、次のLevel-3割込みでjx a2がその古いa2値
	 *  （＝データ/ゴミ）へジャンプして即死する。実機l3_uart_probeで
	 *  「1文字＋割込み1回分だけ処理されて完全沈黙」の根本原因だった
	 *  （UART TXはレベルトリガで、待ちタスクが次データを書くまで
	 *  アサートされ続けるため、復帰直後に2回目のLevel-3割込みが
	 *  必ず発生する）。
	 */
	movi    a3, _kernel_l3int_entry
	wsr     a3, EXCSAVE3
	rsr     a3, EPC3
	s32i    a3, a1, F_pc
	rsr     a3, EPS3
	s32i    a3, a1, F_ps
	rsr     a3, SAR
	s32i    a3, a1, F_sar
#if XCHAL_HAVE_LOOPS
	rsr     a3, LBEG
	s32i    a3, a1, F_lbeg
	rsr     a3, LEND
	s32i    a3, a1, F_lend
	movi    a3, 0
	xsr     a3, LCOUNT
	isync
	s32i    a3, a1, F_lcount
#endif
	/*
	 * 再入・ネスト防止：INTENABLEを全マスクする（Level-1エントリと同じ手順）。
	 * この後の擬似call4でINTLEVELを0へ落とすため、これ無しでは
	 * (1) レベルトリガのLevel-3割込み源（UART TX等、ISRがまだ要因を
	 *     クリアしていない）が即座に再受理されて本エントリへ無限再入する、
	 * (2) ディスパッチ中にtick(INT6)等のLevel-1割込みが入ると
	 *     _kernel_l1int_entryが共有ネストカウンタ≠0を検出して
	 *     .Lint_nested_panicへ落ちる、
	 * の両方が起きる（実機l3_uart_probeで(1)/(2)複合の完全沈黙を確認済み。
	 * INTLEVEL=0化の初回修正時にこのマスク手順の移植が漏れていた）。
	 * 出口でLevel-1と同じく_kernel_intenable_mask[prcidx]から復元する。
	 */
	movi    a2, 0
	wsr     a2, INTENABLE
	rsync

	/* 割込みネストカウンタ++（Level-1と共有のper-coreカウンタ） */
	load_my_intnest_addr a2, a3
	l32i    a3, a2, 0
	addi    a4, a3, 1
	s32i    a4, a2, 0

	/* --- 中断タスクの全窓をスピル --- */
	addmi   a1, a1, EXC_FRAME_SIZE     /* a1 = 中断実SP（FB+0x100） */
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS                     /* WOE off */
	rsync
	call0   xthal_window_spill_nw
	addmi   a5, a1, -EXC_FRAME_SIZE    /* a5 = FB */
	load_my_istkpt a1, a2              /* a1 = 自コア割込みスタック（a5=FBは保持） */
	addi    a3, a1, -ROOT_FRAME_SIZE
	s32i    a5, a3, ISTK_SAVED_FB

	/* 擬似call4：INTLEVEL=0でCディスパッチを呼ぶ（Level-1と同じ。再入は
	 * 上記INTENABLE=0で防止済み。core_asm.inc PS_L3INT_PSEUDO_CALL4参照） */
	movi    a2, PS_L3INT_PSEUDO_CALL4
	wsr     a2, PS
	rsync
	movi    a0, 0                      /* フレームチェーン終端 */
	j       .Ll3int_do_entry
	.align  4
.Ll3int_do_entry:
	entry   a1, ROOT_FRAME_SIZE        /* 割込みスタック上にルート窓を確立 */

	call4   _kernel_l3int_dispatch     /* Level-3 demux（target_timer.c） */

	/* --- イグジット --- */
	rsil    a2, 15
	/* INTENABLEを自コアの許可マスクから復元（Level-1イグジットと同じ手順。
	 * INTLEVEL=15でマスク中に書き換える）
	 * [2026-07-20 vpri] 実効値 = _kernel_intenable_mask[c] & _kernel_vpri_mask[c]。
	 * a4はこの時点でdead（次のa4使用は後段のload_my_pcb a4のfresh load）。 */
	movi    a2, _kernel_intenable_mask
	my_prcidx a3
	addx4   a2, a3, a2
	l32i    a2, a2, 0
	movi    a4, _kernel_vpri_mask
	addx4   a4, a3, a4
	l32i    a4, a4, 0
	and     a2, a2, a4
	wsr     a2, INTENABLE
	rsync
	load_my_intnest_addr a2, a3
	l32i    a3, a2, 0
	addi    a3, a3, -1
	s32i    a3, a2, 0
	l32i    a5, a1, ISTK_SAVED_FB       /* a5 = FB（entry後 a1 = istk - ROOT_FRAME_SIZE） */

	load_my_pcb a4, a2                 /* a4 = 自コアの &pcb（a5=FBは保持） */
	l32i    a3, a4, PCB_p_runtsk
	beqz    a3, .Ll3int_norm            /* アイドル中断はTCB無し。保存スキップ */
	s32i    a5, a3, TCB_sp              /* runtsk->tskctxb.sp = FB */
	movi    a2, .Lret_l3int_r
	s32i    a2, a3, TCB_pc              /* runtsk->tskctxb.pc = 復帰ラベル */
.Ll3int_norm:
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS
	rsync
	movi    a2, 1
	wsr     a2, WINDOWSTART
	movi    a2, 0
	wsr     a2, WINDOWBASE
	rsync
	j       dispatcher_1                /* 既存の同期ディスパッチャを再利用 */

/*
 *  Level-3割込み中断タスクの再開点（dispatcher_2がjx TCB_pc=.Lret_l3int_r）
 *  a1 = TCB_sp = FB。dispatcher_1で窓正規化済み（単一窓）。
 *  フレームを復元しrfi 3で割込み前へ戻る（Level-1のrfeに相当）。
 */
	.align  4
.Lret_l3int_r:
	rsil    a2, 15                      /* 復元シーケンス保護（.Lret_int_rと同じ） */
	l32i    a2, a1, F_sar
	wsr     a2, SAR
#if XCHAL_HAVE_LOOPS
	l32i    a2, a1, F_lbeg
	wsr     a2, LBEG
	l32i    a2, a1, F_lend
	wsr     a2, LEND
	/*
	 * LCOUNT暴走ガード（.Lret_int_rと同じ閾値・方針）。
	 *
	 * [BT-4緊急調査で追記] 当初「診断カウンタ記録は省略」としていたが、
	 * 実機JTAG診断（BT接続直後クラッシュ調査）で本ガードがBT稼働中に
	 * 実際に頻発（短時間の起動+advertising+接続試行だけで_kernel_
	 * lcount_guard_cnt[0]が32まで進行、val=0xFFFFFFFFで確認）している
	 * ことが判明し、L1側と同じ診断カウンタへ記録しないと「ヒットして
	 * いるのに見えない」状態でL1/L3どちらの経路で吸収されたかを含め
	 * 切り分けが不可能だった。L1と同じ per-core 配列
	 * （_kernel_lcount_guard_cnt/val/pc、core_kernel_impl.c）へ統合して
	 * 記録する（L1/L3別集計はせず、コアごとの合算とする。個別の発生源
	 * PCはguard_pc上書きで最後のヒットが残る）。
	 */
	l32i    a2, a1, F_lcount
	movi    a3, LCOUNT_GUARD_THRESHOLD
	bltu    a2, a3, .Ll3lcount_guard_wsr   /* 閾値未満＝正常範囲。a2はそのまま */
	/* --- 異常値検出：診断記録して0へ置換（.Lret_int_rと同じ手順） --- */
	my_prcidx a3
	movi    a2, _kernel_lcount_guard_cnt
	addx4   a2, a3, a2
	l32i    a3, a2, 0
	addi    a3, a3, 1
	s32i    a3, a2, 0             /* guard_cnt[prcidx]++ */

	my_prcidx a3
	movi    a2, _kernel_lcount_guard_val
	addx4   a2, a3, a2
	l32i    a3, a1, F_lcount      /* 異常値そのもの（フレームから再読込） */
	s32i    a3, a2, 0             /* guard_val[prcidx] = 異常値 */

	my_prcidx a3
	movi    a2, _kernel_lcount_guard_pc
	addx4   a2, a3, a2
	l32i    a3, a1, F_pc          /* 復帰先PC（中断PC） */
	s32i    a3, a2, 0             /* guard_pc[prcidx] = F_pc */

	movi    a2, 0                 /* 復元値を0へ置換（ループバック抑止） */
.Ll3lcount_guard_wsr:
	wsr     a2, LCOUNT
	isync
#endif
	l32i    a0,  a1, F_a0
	l32i    a3,  a1, F_a3
	l32i    a4,  a1, F_a4
	l32i    a5,  a1, F_a5
	l32i    a6,  a1, F_a6
	l32i    a7,  a1, F_a7
	l32i    a8,  a1, F_a8
	l32i    a9,  a1, F_a9
	l32i    a10, a1, F_a10
	l32i    a11, a1, F_a11
	l32i    a12, a1, F_a12
	l32i    a13, a1, F_a13
	l32i    a14, a1, F_a14
	l32i    a15, a1, F_a15
	l32i    a2,  a1, F_pc
	wsr     a2, EPC3
	l32i    a2,  a1, F_ps
	wsr     a2, EPS3
	rsync
	l32i    a2,  a1, F_a2
	l32i    a1,  a1, F_oldsp            /* a1 = 中断時の実SP */
	rfi     3                           /* 割込み前へ復帰（Level-1のrfeに相当） */

/*
 *  CPU例外エントリ（DEF_EXCで登録したCPU例外ハンドラのディスパッチ）
 *
 *  ROMの汎用例外ベクタが EXCCAUSE で xtos_exc_handler_table を引いて飛ぶ
 *  スロットに、define_exc（core_kernel_impl.c）経由で本エントリを登録する。
 *  _kernel_l1int_entry（Level-1割込み用）と同じフレーム規約・窓退避手法を
 *  用いるが、以下がCPU例外に固有：
 *   - xsns_dpn判定用に「発生時点の元の割込みネスト(exncnt)・IPM(intpri)」を
 *     フレーム(F_exncnt/F_intpri)へ保存してから割込みネストを++する
 *     （ハンドラ中は非タスク扱い＝check_tskctx()がtrueになり、xsns_dpnは
 *     フレーム保存値を見るexc_sense_intmaskで判定する。riscv版と同じ設計）。
 *   - スタックは、元がタスクコンテキスト(exncnt==0)なら割込みスタックへ
 *     切替え、元が非タスク(exncnt>0：割込みハンドラ内でのCPU例外)なら
 *     現スタック(FBの直下)にルート窓を作る（riscvのcore_exc_entry_1同様、
 *     割込みスタック上の中断フレームを壊さないため）。
 *   - 復帰は、元が非タスクなら中断ハンドラへ単純復帰(rfe)、元がタスクなら
 *     dispatcher_1経由（PREPARE_RETURN_CPUEXCでpcを進めた場合の再開、及び
 *     ハンドラ内でact_tsk等により高優先度タスクがreadyになった場合の
 *     タスク切替リカバリ＝test_cpuexc6 を両立）。
 *
 *  ROMエントリ時：a1=FB（例外フレーム基底 = 中断SP - EXC_FRAME_SIZE）、
 *  a0/a2/a3/a4/oldsp はROMが退避済み。
 */
	.global core_exc_entry
	.align  4
core_exc_entry:
	/*
	 * チップ層フック（デフォルト空、core_asm.inc）。
	 * S3(LX7)ROMはF_a0/F_oldspを退避済みなのでno-op。LX6 ROMは未退避の
	 * ため、esp32チップ層のchip_exc_entry_fixupがa0/中断SPをここで
	 * 自前退避し直す（下流の.Lret_int_r自己復帰でF_a0/F_oldspを読むため）。
	 * この時点でa0=中断時a0（ライブ）、a1=FB、a2はスクラッチ（直後の
	 * rsr a2,PSで上書きされる）。
	 */
	chip_exc_entry_fixup
	rsr     a2, PS                  /* 中断コンテキストの元PS（INTLEVEL保持、EXCM=1） */
	s32i    a2, a1, F_ps
	rsil    a2, 15                  /* 以降 全割込みマスク */
	s32i    a5, a1, F_a5
	s32i    a6, a1, F_a6
	s32i    a7, a1, F_a7
	s32i    a8, a1, F_a8
	s32i    a9, a1, F_a9
	s32i    a10, a1, F_a10
	s32i    a11, a1, F_a11
	s32i    a12, a1, F_a12
	s32i    a13, a1, F_a13
	s32i    a14, a1, F_a14
	s32i    a15, a1, F_a15
	rsr     a2, EPC1
	s32i    a2, a1, F_pc
	rsr     a2, SAR
	s32i    a2, a1, F_sar
	rsr     a2, EXCCAUSE
	s32i    a2, a1, F_exccause
#if XCHAL_HAVE_LOOPS
	rsr     a2, LBEG
	s32i    a2, a1, F_lbeg
	rsr     a2, LEND
	s32i    a2, a1, F_lend
	movi    a2, 0
	xsr     a2, LCOUNT
	isync
	s32i    a2, a1, F_lcount
#endif

	/*
	 * CPU例外発生時点の元の状態（xsns_dpn判定用）をフレームへ保存する。
	 * 割込みネスト値の++より前に取得すること（元コンテキストの判別のため）。
	 */
	load_my_intnest_addr a2, a3
	l32i    a3, a2, 0               /* a3 = 元の割込みネスト(exncnt) */
	s32i    a3, a1, F_exncnt
	movi    a4, _kernel_ipm
	my_prcidx a5
	addx4   a4, a5, a4
	l32i    a4, a4, 0               /* a4 = 元のIPM(_kernel_ipm[prcidx]) */
	s32i    a4, a1, F_intpri
	/* 割込みネスト++（ハンドラ中はcheck_tskctx()がtrueを返すように） */
	addi    a5, a3, 1
	s32i    a5, a2, 0

	/* --- 中断コンテキストの全窓をスピル --- */
	addmi   a1, a1, EXC_FRAME_SIZE  /* a1 = FB + 0x100 = 中断実SP */
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS                  /* WOE off */
	rsync
	call0   xthal_window_spill_nw
	addmi   a5, a1, -EXC_FRAME_SIZE /* a5 = FB */

	/*
	 * 深いディスパッチでのウィンドウ・ラップ防止（実機必須）。
	 * 全窓スピル済みなのでwindowbase=0へリセットし、以降のルート窓＋ディスパッチ
	 * にwindowbase 0起点のheadroomを与える（起動時windowbase=0だと、tick→alarm→
	 * cpuexcの深いネストでwindowbaseがラップ(15→0)し、ルートフレームのSPが破壊
	 * される。exit側も既にwindowbase=0にするので整合）。FB(a5)はistack先頭相対の
	 * スクラッチ[istkpt-4]へ退避して跨ぐ（この時点でistack上にフレーム未確立）。
	 */
	load_my_istkpt a2, a3
	addi    a3, a2, -4
	s32i    a5, a3, 0              /* [istkpt-4] = FB */
	movi    a3, 0
	wsr.windowbase a3
	rsync
	movi    a3, 1
	wsr.windowstart a3
	rsync
	movi    a0, 0
	load_my_istkpt a2, a3
	addi    a3, a2, -4
	l32i    a5, a3, 0              /* a5 = FB（再ロード） */

	/*
	 * ルートスタックの決定：
	 *   元がタスク(F_exncnt==0) → 割込みスタックへ切替
	 *   元が非タスク(F_exncnt!=0) → 現スタック(FBの直下)にルート窓
	 */
	l32i    a3, a5, F_exncnt
	bnez    a3, .Lexc_root_here
	load_my_istkpt a1, a2           /* a1 = 割込みスタック先頭（a5=FBは保持） */
	j       .Lexc_root_set
.Lexc_root_here:
	mov     a1, a5                  /* ルートSP = FB（中断フレーム直下に窓を作る） */
.Lexc_root_set:
	/* FBをルートフレームの退避スロットへ（擬似call4を跨いで保持） */
	addi    a3, a1, -ROOT_FRAME_SIZE
	s32i    a5, a3, ISTK_SAVED_FB
	/*
	 * 擬似call4：PS=WOE|UM|CALLINC1、EXCM解除、ルート窓確立。
	 * INTLEVELは発生時PS(F_ps)の値を継承する（CPUロック状態＝loc_cpuで
	 * INTLEVEL=15の状態で発生したCPU例外は、ハンドラもCPUロック状態で
	 * 実行する必要がある。test_cpuexc2/5のsns_loc()==1）。l1int（tick）は
	 * INTLEVEL=0固定だが、CPU例外は同期例外で発生時状態を継承する点が異なる。
	 */
	l32i    a3, a5, F_ps
	extui   a3, a3, 0, 4            /* a3 = 発生時INTLEVEL */
	/*
	 * ★レビュー修正#2（割込みネスト保護の欠落）：Cディスパッチ突入前に
	 * INTENABLE=0で割込み源を静音化する（_kernel_l1int_entry/
	 * _kernel_l3int_entryと同じ再入防止手順）。
	 *
	 * 発生時INTLEVELが0（非CPUロック中のタスクコンテキストで発生した
	 * CPU例外。test_cpuexc3/6/7/8/9/10が実際にこの状態を作る）だと、
	 * 直後のwsr PSでINTLEVELが0へ下がりハードウェア的に割込みを受理
	 * 可能になる。従来はここでINTENABLEを変更していなかったため、
	 * _kernel_exc_dispatch実行中にtick(L1)が割り込むと、
	 * _kernel_l1int_entryが割込みネスト値≠0（本ハンドラが++した値）を
	 * 検出して.Lint_nested_panicへ落ち、全系ハングする経路が理論上
	 * 存在した（レビュー指摘、latent）。
	 *
	 * INTLEVEL=15（CPUロック中発生の例外。test_cpuexc2/5、
	 * sns_loc()==1）ではこの後もPS.INTLEVEL=15のままなのでハードウェア
	 * 的に既に全マスク済み＝本来無害だが、分岐を増やさずL1/L3と同一
	 * 手順に揃える（無条件でも副作用なし）。
	 *
	 * 復元は.Lexc_ret_task側でのみ行う。.Lexc_root_here経由の非タスク
	 * 復帰（＝外側のL1/L3/CPU例外ハンドラの中でネストして発生した
	 * CPU例外）では復元しない：外側ハンドラは自分がまだINTENABLE=0の
	 * 前提で動作中であり、その契約を守るには外側ハンドラ自身の出口が
	 * 復元するまで触ってはならない（詳細は.Lexc_ret_task直前のコメント
	 * 参照）。
	 */
	movi    a4, 0
	wsr     a4, INTENABLE
	rsync
	movi    a2, (PS_WOE | PS_UM | PS_CALLINC_CALL4)
	or      a2, a2, a3
	wsr     a2, PS
	rsync
	movi    a0, 0
	/*
	 * entryは4バイト境界必須。本asmでは.alignが0埋め（=ill命令化）する
	 * ため、jで整列用パディングを飛び越し、パディングを実行経路から外す
	 * （.Lexc_do_entryは.align 4直後で4整列。パディングはjと同ラベルの
	 * 間に置かれ実行されない）。
	 */
	j       .Lexc_do_entry
	.align  4
.Lexc_do_entry:
	entry   a1, ROOT_FRAME_SIZE
	/* p_excinf=FB を第1引数(call4：呼側a6→呼先a2)へ */
	l32i    a6, a1, ISTK_SAVED_FB
	call4   _kernel_exc_dispatch

	/* --- イグジット --- */
	rsil    a2, 15
	load_my_intnest_addr a2, a3
	l32i    a3, a2, 0
	addi    a3, a3, -1              /* 割込みネスト-- */
	s32i    a3, a2, 0
	l32i    a5, a1, ISTK_SAVED_FB   /* a5 = FB */
	l32i    a3, a5, F_exncnt        /* 元コンテキスト判定 */
	beqz    a3, .Lexc_ret_task

	/*
	 * 元が非タスク：中断ハンドラへ単純復帰（ディスパッチしない）。
	 * 窓を正規化(単一窓)しa1=FBとして.Lret_do_restoreでrfe。
	 *
	 * 注意：wsr WINDOWBASE でカレントバンクが切り替わりa5(FB)/a1が失われるため、
	 * FBをistack先頭相対スクラッチ[istkpt-8]へ退避してリセットを跨ぎ、リセット後に
	 * a1へ再ロードする（ルート窓がwindowbase!=0で確立される構成のため必須）。
	 */
	load_my_istkpt a2, a3
	addi    a3, a2, -8
	s32i    a5, a3, 0              /* [istkpt-8] = FB */
	movi    a2, ~PS_WOE
	rsr     a3, PS
	and     a2, a3, a2
	wsr     a2, PS
	rsync
	movi    a2, 1
	wsr     a2, WINDOWSTART
	movi    a2, 0
	wsr     a2, WINDOWBASE
	rsync
	load_my_istkpt a2, a3
	addi    a3, a2, -8
	l32i    a1, a3, 0              /* a1 = FB（再ロード） */
	j       .Lret_do_restore

.Lexc_ret_task:
	/*
	 * ★レビュー修正#2：INTENABLEを自コアの許可マスク
	 * (_kernel_intenable_mask[prcidx])へ復元する（L1/L3のイグジットと
	 * 同じ手順）。ここで復元しないと、エントリ側でINTENABLE=0にした
	 * ままdispatcher_1経由でタスクへ復帰することになり、以降tick等が
	 * 永久にマスクされたままになる。
	 *
	 * イグジット冒頭のrsil a2,15（このラベルの手前）でPS.INTLEVEL=15
	 * のままなので、ここでINTENABLEを書き換えてもハードウェア的には
	 * 割込みは受理されず安全。
	 *
	 * 非タスク復帰（このラベルに来ない側＝.Lexc_root_here経由）では
	 * 復元しないのが正しい：ネストしたCPU例外から見た「外側」は
	 * まだINTENABLE=0を前提に動作中のL1/L3/CPU例外ハンドラであり、
	 * ここで先に復元すると外側ハンドラの残り実行中に割込みが解けて
	 * しまう（外側ハンドラ自身の出口が復元するまで触ってはならない
	 * という契約を破る）。
	 *
	 * [2026-07-20 vpri] 実効値 = _kernel_intenable_mask[c] & _kernel_vpri_mask[c]。
	 * a4はこの時点でdead（次のa4使用は直後のload_my_pcb a4のfresh load）。
	 * a5(FB)はTCB_sp保存で後段liveだが、本挿入はa4のみ使用で非干渉。
	 */
	movi    a2, _kernel_intenable_mask
	my_prcidx a3
	addx4   a2, a3, a2
	l32i    a2, a2, 0
	movi    a4, _kernel_vpri_mask
	addx4   a4, a3, a4
	l32i    a4, a4, 0
	and     a2, a2, a4
	wsr     a2, INTENABLE
	rsync
	/*
	 * 元がタスク：dispatcher_1経由（l1intの復帰と同一経路）。
	 * 中断タスクのTCBへ FB と復帰ラベル.Lret_int_r を保存して窓正規化。
	 */
	load_my_pcb a4, a2
	l32i    a3, a4, PCB_p_runtsk
	beqz    a3, .Lint_norm          /* アイドル中断はTCB保存不要 */
	s32i    a5, a3, TCB_sp
	movi    a2, .Lret_int_r
	s32i    a2, a3, TCB_pc
	j       .Lint_norm
