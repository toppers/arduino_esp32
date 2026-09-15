/*
 *  TOPPERS/ASP Kernel
 *      Toyohashi Open Platform for Embedded Real-Time Systems/
 *      Advanced Standard Profile Kernel
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアを TOPPERS ライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソフ
 *  トウェアは無保証で提供される．
 *
 */

/*
 *		ESP32-C5 USB Serial/JTAGコントローラ用 簡易SIOドライバ
 *		（esp-hal LL層版・非TECS版専用）
 *
 *  asp3_core の arch/riscv_gcc/esp32c5/esp32c5_usbjtag.c（レジスタ直
 *  叩き版）を，esp-hal-3rdparty の LL 層（hal/usb_serial_jtag_ll.h＝
 *  static inline のレジスタ薄層・RTOS非依存）で置き換えたもの
 *  （Phase B-1：esp-hal統合の実証）．公開シンボル（esp32c5_usbjtag_*）
 *  は同一のため，chip_serial.c（asp3_core側）はそのままリンクできる．
 */

#include <sil.h>
#include "target_syssvc.h"
#include "esp32c5_usbjtag.h"
#include "hal/usb_serial_jtag_ll.h"

/*
 *  SIOポート管理ブロックの定義（LL層はデバイス単一のためbaseを持たない）
 */
struct sio_port_control_block {
	EXINF		exinf;			/* 拡張情報 */
	bool_t		opened;			/* オープン済み */
};

/*
 *  SIOポート管理ブロックのエリア
 */
SIOPCB	siopcb_table[TNUM_SIOP];

/*
 *  SIOポートIDから管理ブロックを取り出すためのマクロ
 */
#define INDEX_SIOP(siopid)	((uint_t)((siopid) - 1))
#define get_siopcb(siopid)	(&(siopcb_table[INDEX_SIOP(siopid)]))

#ifdef ESP32C5_USJ_PROBE
/*
 *  段3 Task 1: USJ 1 文字落ちの計測用カウンタ（計測専用、ESP32C5_USJ_PROBE
 *  未定義なら 1 バイトも変わらない）。
 *    [0] ISR で IN_EMPTY を受けた回数
 *    [1] そのうち txfifo_writable()==0 だった回数（upper bound。controller
 *        ルーリングにより「実際に送信を試みて失敗した回数」の正確な指標
 *        ではないと判明した。[4] を参照）
 *    [2] ena_cbr(SIO_RDY_SND) の時点で int_raw の IN_EMPTY が既に 1 だった回数
 *    [3] snd_chr が false を返した回数（ISR 経由と target_fput_log の
 *        busy-poll 経由の両方を含む。ISR 経由分だけを分離するのが [4]）
 *    [4] isr_sndfail -- ISR の IN_EMPTY 分岐から irdy_snd を呼んでいる
 *        最中に snd_chr が false を返した回数。serial.c:573（sio_irdy_snd
 *        内の sio_snd_chr の戻り値の握り潰し）で実際に失われる文字数と
 *        厳密に一致するはずのカウンタ（controller ルーリング）
 *    [5] 段4 fix wave 1 (G)：低レベル出力経路（snd_chr）でポーリングによる
 *        BUS_RESET 再武装が実際に発火した回数。A1_C5_APPDIR=test_ovr の
 *        ように serial.cfg が無く ISR（INTNO_SIO）が有効化されない構成
 *        （ena_int が E_OBJ を返す）でも、この経路だけは動く
 */
volatile uint32_t	esp32c5_usj_cnt[6];

/*
 *  [4] 用: ISR が irdy_snd 呼出し中かどうかのフラグ。ISR はネストしない
 *  （割込みハンドラは同一線から再入しない）ため単純な file-static で足りる。
 */
static volatile bool_t	usj_in_irdy;
#endif /* ESP32C5_USJ_PROBE */

/*
 *  BUS_RESET 再武装（段4 fix wave 1、G）
 *
 *  ホスト不在のまま WR_DONE したパケットは，バスリセット後も
 *  SERIAL_IN_EP_DATA_FREE=0 のまま残り，IN_EMPTY が二度と立たない
 *  （esp32c5_usbjtag_isr_siop の BUS_RESET 分岐のコメント参照）。
 *  この再武装ロジックは ISR（esp32c5_usbjtag_isr_siop）と，ISR が
 *  一度も動かない構成（A1_C5_APPDIR=test_ovr のように serial.cfg が
 *  無く ena_int(INTNO_SIO) が E_OBJ を返し INTNO_SIO が有効化されない
 *  場合）向けの低レベル出力側ポーリング（esp32c5_usbjtag_snd_chr）の
 *  両方から同じ内容で呼ばれる -- 重複させず本関数へ集約する。
 */
/*
 *  IN トークン監視（段4 Task 4、2026-09-14、真cold の無音の根治）
 *
 *  上の BUS_RESET 再武装だけでは足りない cold の系列が実機で 8/8 再現した:
 *  ROM/bootloader が host 不在のまま EP1 へ WR_DONE したパケットが残り、その後に
 *  host が列挙（バスリセット）し、さらに **アプリが動く前に** host 側の何かが
 *  tty を開閉した（INT_RAW に SET_LINE_CODE/DTR_CHG/RTS_CHG/IN_TOKEN が既に立って
 *  いる）状態でアプリが起動すると、opn_por の再武装（WR_DONE 再発行、t=1.0s）
 *  では DATA_FREE が 0 のまま戻らず、host が後で tty を開いて IN トークンを
 *  送り続けても一度も配送されない（21 秒間 ep1_conf=0、IN_EMPTY 無し）。
 *  同じ状態で **host が IN トークンを送っている最中に** WR_DONE を再発行すると
 *  解放され、滞留していた全行が届いた（.steering/20260913-c6-stage4/logs/
 *  task4-cold-snap8-hardreset.log と task4-probe-cold9*.log）。
 *
 *  そこで「未送信パケットが残っている間だけ」IN_TOKEN_REC_IN_EP1 割込みを開け、
 *  トークンが来ても DATA_FREE=0 のままなら WR_DONE を再発行する。FIFO が空いた
 *  （DATA_FREE=1）ら自分で閉じる（host が tty を開いている間は IN トークンが
 *  絶え間なく来るので、常時有効にすると割込みの嵐になる）。
 */
static void
esp32c5_usbjtag_in_token_watch_arm(void)
{
	usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1);
	usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1);
}

static void
esp32c5_usbjtag_bus_reset_rearm(void)
{
	usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);
	if (usb_serial_jtag_ll_txfifo_writable() == 0) {
		usb_serial_jtag_ll_txfifo_flush();
		esp32c5_usbjtag_in_token_watch_arm();
	}
}

/*
 *  SIOドライバの初期化
 */
void
esp32c5_usbjtag_initialize(void)
{
	SIOPCB	*p_siopcb;
	uint_t	i;

	for (p_siopcb = siopcb_table, i = 0; i < TNUM_SIOP; p_siopcb++, i++) {
		p_siopcb->opened = false;
	}
}

/*
 *  SIOドライバの終了処理
 */
void
esp32c5_usbjtag_terminate(void)
{
	uint_t	i;

	for (i = 0; i < TNUM_SIOP; i++) {
		esp32c5_usbjtag_cls_por(&(siopcb_table[i]));
	}
}

/*
 *  SIOポートのオープン
 */
SIOPCB *
esp32c5_usbjtag_opn_por(ID siopid, EXINF exinf)
{
	SIOPCB	*p_siopcb;

	p_siopcb = get_siopcb(siopid);

	if (!(p_siopcb->opened)) {
		/*
		 *  全割込みの禁止とクリア（ハードウェアの動作設定は不要）
		 */
		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT
						| USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		usb_serial_jtag_ll_clr_intsts_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT
						| USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);

		/*
		 *  バスリセット割込みは opn_por 以降ずっと有効にする（他の2本の
		 *  ように SIO_RDY_SND/RCV に連動して開閉しない）。ホスト不在で
		 *  カーネルが起動しWR_DONEしたパケットが，ホストの列挙（バス
		 *  リセット）後もSERIAL_IN_EP_DATA_FREE=0のまま残り，IN_EMPTY
		 *  が二度と立たない欠陥が実機で6/6再現した（2026-09-14）。
		 *  isr_siopでの再武装のためにこの割込みだけは常時有効にする。
		 */
		usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);
		usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_BUS_RESET);

		/*
		 *  開く前に起きたバスリセット（起動直後にホストが列挙した場合）を
		 *  取りこぼさないよう，未送信パケットが残っていればここでも再武装する。
		 */
		if (usb_serial_jtag_ll_txfifo_writable() == 0) {
			usb_serial_jtag_ll_txfifo_flush();
			esp32c5_usbjtag_in_token_watch_arm();
		}

		p_siopcb->opened = true;
	}
	p_siopcb->exinf = exinf;
	return(p_siopcb);
}

/*
 *  SIOポートのクローズ
 */
void
esp32c5_usbjtag_cls_por(SIOPCB *p_siopcb)
{
	uint32_t	retry;

	if (p_siopcb->opened) {
		/*
		 *  送信FIFOが掃けるのを待つ（ホストが読み出さない場合は
		 *  空かないため，リトライ上限を設けて打ち切る）
		 */
		for (retry = 100000U; retry > 0U; retry--) {
			if (usb_serial_jtag_ll_txfifo_writable() != 0) {
				break;
			}
		}

		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT
						| USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);

		p_siopcb->opened = false;
	}
}

/*
 *  SIOポートへの文字送信（1文字毎にパケットとして送出する）
 */
bool_t
esp32c5_usbjtag_snd_chr(SIOPCB *p_siopcb, char c)
{
	SIL_PRE_LOC;

	/*
	 *  段4 fix wave 1 (G): ISR（esp32c5_usbjtag_isr_siop）が動かない構成
	 *  （A1_C5_APPDIR=test_ovr。上の esp32c5_usbjtag_bus_reset_rearm の
	 *  コメント参照）では、open 時の1回きりの再武装（opn_por）だけでは
	 *  opn_por より後に起きたバスリセットを取りこぼす。ここは
	 *  esp32c5_sio_fput（chip_serial.c）のリトライループから毎回呼ばれる
	 *  低レベル出力経路そのものなので、書込み可否を見る前に BUS_RESET を
	 *  ポーリングし，立っていれば ISR と同じ手順で再武装する。ISR が動く
	 *  構成（serial.cfg あり）でも呼んで安全（W1C レジスタへの二重クリアと，
	 *  再武装済みでの txfifo_writable!=0 時のスキップはいずれも無害）。
	 */
	/*
	 *  段4 最終レビュー是正（2026-09-15）: 本関数は serial.c（CPU ロック下）と
	 *  ISR だけでなく、esp32c5_sio_fput（target_fput_log のポーリング出力）から
	 *  タスク文脈・割込み許可のまま呼ばれる。IN トークン監視の武装
	 *  （esp32c5_usbjtag_in_token_watch_arm）は INT_ENA の read-modify-write で、
	 *  ISR 側（esp32c5_usbjtag_isr_siop）が同じレジスタを disable_intr_mask で
	 *  RMW するので、タスク側の read と write の間に ISR が挟まると ISR の
	 *  disable（または opn_por で常時有効にした BUS_RESET の enable）が
	 *  失われうる。武装（と、同じ RMW を含む bus_reset_rearm）だけを
	 *  SIL_LOC_INT/SIL_UNL_INT で囲む。ISR や CPU ロック下から呼ばれても
	 *  SIL_LOC_INT は保存・復元なので無害。他の 2 箇所の武装はそのまま:
	 *  isr_siop 内（ISR 自身、同一線の再入は無い）と opn_por 内（chip_serial.c の
	 *  sio_opn_por が ena_int(INTNO_SIO) を呼ぶのは opn_por から戻った後）。
	 */
	if ((usb_serial_jtag_ll_get_intsts_mask() & USB_SERIAL_JTAG_INTR_BUS_RESET) != 0U) {
		SIL_LOC_INT();
		esp32c5_usbjtag_bus_reset_rearm();
		SIL_UNL_INT();
#ifdef ESP32C5_USJ_PROBE
		esp32c5_usj_cnt[5]++;
#endif /* ESP32C5_USJ_PROBE */
	}
	if (usb_serial_jtag_ll_txfifo_writable() != 0) {
		usb_serial_jtag_ll_write_txfifo((const uint8_t *) &c, 1U);
		usb_serial_jtag_ll_txfifo_flush();
		SIL_LOC_INT();
		esp32c5_usbjtag_in_token_watch_arm();
		SIL_UNL_INT();
		return(true);
	}
#ifdef ESP32C5_USJ_PROBE
	esp32c5_usj_cnt[3]++;
	if (usj_in_irdy) {
		esp32c5_usj_cnt[4]++;
	}
#endif /* ESP32C5_USJ_PROBE */
	return(false);
}

/*
 *  SIOポートからの文字受信
 */
int_t
esp32c5_usbjtag_rcv_chr(SIOPCB *p_siopcb)
{
	uint8_t	c;

	/*
	 *  段4 fix wave 1 round 2（Important #4）: 以前は
	 *  usb_serial_jtag_ll_rxfifo_data_available() で「読めるはず」と
	 *  確認してから usb_serial_jtag_ll_read_rxfifo() を呼び、その戻り値
	 *  （実際に読めたバイト数）を捨てていた。esp-idf の実装
	 *  （hal/usb_serial_jtag_ll.h の read_rxfifo）は呼出し時点で改めて
	 *  serial_out_ep_data_avail を見ており、2回の呼出しの間に空になれば
	 *  1バイトも書かずに0を返す -- このとき c は未初期化のまま
	 *  (int_t) c を返していた（-Wmaybe-uninitialized の指摘どおり）。
	 *  事前チェックをやめ、read_rxfifo の戻り値（実際に読めたバイト数）
	 *  を直接見る fail-closed な形にする。戻り値の意味、「読めなかった
	 *  ら -1」という呼び出し側の規約（chip_serial.c の sio_rcv_chr、
	 *  fmp3_core/syssvc/serial.c:610）は不変。
	 */
	if (usb_serial_jtag_ll_read_rxfifo(&c, 1U) == 1) {
		return((int_t) c);
	}
	return(-1);
}

/*
 *  SIOポートからのコールバックの許可
 */
void
esp32c5_usbjtag_ena_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	switch (cbrtn) {
	case SIO_RDY_SND:
#ifdef ESP32C5_USJ_PROBE
		if ((usb_serial_jtag_ll_get_intraw_mask()
				& USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY) != 0U) {
			esp32c5_usj_cnt[2]++;
		}
#endif /* ESP32C5_USJ_PROBE */
		usb_serial_jtag_ll_ena_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		break;
	case SIO_RDY_RCV:
		usb_serial_jtag_ll_ena_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートからのコールバックの禁止
 */
void
esp32c5_usbjtag_dis_cbr(SIOPCB *p_siopcb, uint_t cbrtn)
{
	switch (cbrtn) {
	case SIO_RDY_SND:
		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		break;
	case SIO_RDY_RCV:
		usb_serial_jtag_ll_disable_intr_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
		break;
	default:
		break;
	}
}

/*
 *  SIOポートに対する割込み処理
 */
static void
esp32c5_usbjtag_isr_siop(SIOPCB *p_siopcb)
{
	uint32_t	stat;

	stat = usb_serial_jtag_ll_get_intsts_mask();

	if ((stat & USB_SERIAL_JTAG_INTR_BUS_RESET) != 0U) {
		/*
		 *  USB バスリセット（ホストの列挙）。ホスト不在のまま WR_DONE した
		 *  パケットは，バスリセット後も SERIAL_IN_EP_DATA_FREE=0 のまま残り，
		 *  IN_EMPTY が二度と立たない（2026-09-14 実機で 6/6 再現。JTAG で
		 *  WR_DONE を再発行すると解放された）。IDF のドライバと同じく，
		 *  未送信データが残っていれば WR_DONE を再発行して IN エンドポイントを
		 *  再武装する。writable なら残りは無いので何もしない。
		 */
		esp32c5_usbjtag_bus_reset_rearm();
	}
	if ((stat & USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1) != 0U) {
		/*
		 *  host が EP1 へ IN トークンを送ってきた（esp32c5_usbjtag_in_token_watch_arm
		 *  のコメント参照）。まだ DATA_FREE=0 なら WR_DONE を再発行し、空いて
		 *  いれば監視を閉じる。
		 */
		usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1);
		if (usb_serial_jtag_ll_txfifo_writable() == 0) {
			usb_serial_jtag_ll_txfifo_flush();
		}
		else {
			usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1);
		}
	}
	if ((stat & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY) != 0U) {
		/*
		 *  送信FIFOエンプティはイベント（送信完了時）として立つため，
		 *  クリアしてから送信可能コールバックルーチンを呼び出す．
		 */
		usb_serial_jtag_ll_clr_intsts_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
		/*  パケットは配送された。IN トークン監視（上）は閉じる。  */
		usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_TOKEN_REC_IN_EP1);
#ifdef ESP32C5_USJ_PROBE
		esp32c5_usj_cnt[0]++;
		if (usb_serial_jtag_ll_txfifo_writable() == 0) {
			esp32c5_usj_cnt[1]++;
		}
#endif /* ESP32C5_USJ_PROBE */
		/*
		 *  段3 Task 1（計測 -> 修正 -> 対照）: notwritable（cnt[1]）は upper
		 *  bound に過ぎなかった（controller ルーリング、cnt[1] のコメント
		 *  参照）が，isr_sndfail（cnt[4]，irdy_snd 呼出し中に限定した
		 *  snd_chr 失敗数）は修正前ビルドで hard_reset 4 回とも drops と
		 *  厳密に 1:1 だった（AC-1a 確定。.steering/20260913-c6-stage3/
		 *  README.md §1）。IN_EMPTY の raw は R/WTC で既定 1。ポーリング
		 *  経路（target_fput_log）はこれをクリアしないため stale-high に
		 *  なり，直接送信が失敗して ena_cbr(SIO_RDY_SND) がマスクを開けた
		 *  瞬間に DATA_FREE=0 のままここへ来ることがある。そのとき
		 *  irdy_snd を呼ぶと sio_snd_chr が false を返し（serial.c:573 は
		 *  戻り値を捨てる），1 文字消える。IDF usb_serial_jtag.c:73-76 と
		 *  同じく，書けないときは無視する（ホストが読めば本物の IN_EMPTY
		 *  が来る）。ESP32C5_USJ_NOFIX（既定 OFF）は修正前の無条件呼出しを
		 *  positive control 用に復元するスイッチ（Step 5、AC-1c）。
		 */
#ifdef ESP32C5_USJ_PROBE
		usj_in_irdy = true;
#endif /* ESP32C5_USJ_PROBE */
#ifdef ESP32C5_USJ_NOFIX
		esp32c5_usbjtag_irdy_snd(p_siopcb->exinf);
#else /* ESP32C5_USJ_NOFIX */
		if (usb_serial_jtag_ll_txfifo_writable() != 0) {
			esp32c5_usbjtag_irdy_snd(p_siopcb->exinf);
		}
#endif /* ESP32C5_USJ_NOFIX */
#ifdef ESP32C5_USJ_PROBE
		usj_in_irdy = false;
#endif /* ESP32C5_USJ_PROBE */
	}
	if ((stat & USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT) != 0U) {
		/*
		 *  受信パケット到着はパケット毎のイベントのため，クリアした
		 *  うえでFIFO内の全データを引き取るまで受信通知コールバック
		 *  ルーチンを呼び出す（1回の呼出しで1文字読み出される）．
		 */
		usb_serial_jtag_ll_clr_intsts_mask(
					USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
		while (usb_serial_jtag_ll_rxfifo_data_available() != 0) {
			esp32c5_usbjtag_irdy_rcv(p_siopcb->exinf);
		}
	}
}

/*
 *  SIOの割込みサービスルーチン
 */
void
esp32c5_usbjtag_isr(ID siopid)
{
	esp32c5_usbjtag_isr_siop(get_siopcb(siopid));
}
