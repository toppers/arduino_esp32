/*
 *  esp_hosted の RPC とトランスポート（ESP32-P4、hosted Wi-Fi）
 *
 *  出典: 開発リポジトリの `esp/p4hosted/app/rpc_probe/rpc_probe.c`
 *  （計画 7 段 B2a で切り出し。IMPORT_PROVENANCE_p4.md 7 節に差分表）。
 *  あちらは 3,494 行のプローブアプリで、「SDIO の上で esp_hosted の RPC を
 *  話す部分」と「それを使って測る部分」が同居している。ここに持ってきたのは
 *  **前者だけ**である。
 *
 *  持ってきた範囲（出典の行番号。行は 1 行も書き換えていない）:
 *    246-387   vtable 経由の SDIO ラッパと protobuf の手書き符号化/復号
 *    475-2090  送受信（credit / send / read_avail / pump / dispatch）、
 *              RPC（rp_rpc_call / rp_resp_status）、Wi-Fi 操作
 *              （init / set_mode / start / scan / set_sta_config /
 *               connect / wait_connected / get_mac）
 *
 *  持ってこなかったもの（すべてプローブ側の道具）:
 *    219-245   `chk()` / `verdict()` と PASS/FAIL の計数。**この切り出し範囲は
 *              1 度も呼んでいない**（実測: 該当範囲の grep が 0 件）ので、
 *              落としても振る舞いは変わらない。
 *    388-474   符号化器・復号器自身の positive control（R2-s1 / R2-s2）。
 *    2091-3494 スレッドプールの実験、ネットワークの測定、プローブ本体の task。
 *
 *  **1 ファイルにした理由**（計画 B-2 は 2 ファイルへ分ける案だった）: Wi-Fi 操作の
 *  側が RPC 側の static を 10 個（`rp_body` / `rp_body_len` / `rp_body_i32` /
 *  `rp_tx_has_secret` と、パーサが書いて Wi-Fi 側が読む接続イベントの 6 個）
 *  踏んでいる。分けると `static` を外して内部ヘッダで共有することになり、
 *  出典との差が増えるうえ、64 バイト境界に置いた DMA バッファの初期化順という
 *  一番壊れたときに分かりにくいものを触ることになる。計画が用意していた退避路
 *  （「1 ファイルに丸ごと」）を選んだ。
 *
 *  **出典との差はこの 2 つだけ**:
 *    1. 上の「持ってこなかったもの」を落とした。
 *    2. アダプタ（段 B2b）が呼ぶ 12 個の関数と 1 つの束縛関数から `static` を
 *       外した（下の宣言を参照）。他の記号は `static` のまま。
 */



#include <kernel.h>
#include <t_syslog.h>
#include <sil.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "p4sdio_host.h"
#include "p4sdio_board.h"
#include "p4sdio_pins.h"
#include "p4hosted_osi.h"

/*  上流の契約（**無改変で取り込んだヘッダ**）  */
#include "esp_hosted_header.h"
#include "esp_hosted_interface.h"
#include "esp_hosted_transport.h"
#include "esp_hosted_transport_init.h"
#include "sdio_reg.h"

/*  vtable の契約（7b の H1）と RPC の ID（本段 R2-i）。どちらも**生成物**  */
#include "generated/hosted_vtable_contract.h"
#include "generated/hosted_rpc_ids.h"

/*  プール用タスクの ID（`HOSTED_TSK1`）を見るため。**診断のみ**——
 *  取込み物（`p4hosted_pools.c`）には触らない。 */


#if defined(P4HOSTED_NET)
/*
 *  段7d: 802.3 データパスと lwIP netif（`-DA1_P4_HOSTED_NET=ON` のときだけ）
 *
 *  疎通先の既定値。CMake から `-DA1_P4_NET_SERVER_IP=...` で上書きできる
 *  （段E-seam の `NET_SERVER_IP` と同じ扱い）。**IP は秘密ではない**ので
 *  値を書いてよい（SSID/PASS とは扱いが違う）。
 */
#ifndef RP_NET_SERVER_IP
#define RP_NET_SERVER_IP	"192.168.1.48"
#endif
/*  R3-c の negative control 用（**同一サブネット内の未使用アドレス**）  */
#ifndef RP_NET_NC_IP
#define RP_NET_NC_IP		"192.168.1.253"
#endif

/*
 *  TCP の相手（段 7f・F3）。**IP もポートも秘密ではない**ので値を書いてよい。
 *    エコー   : 送った物がそのまま返る（段E-seam の `NET_SERVER_TCP_PORT` と同じ 5002）
 *    シンク   : 受け捨てる（簡易スループット用）
 *    NC       : **待ち受けの無いポート**（同じ判定器が失敗を返すことの実演）
 */
#ifndef RP_NET_TCP_ECHO_PORT
#define RP_NET_TCP_ECHO_PORT	5002
#endif
#ifndef RP_NET_TCP_SINK_PORT
#define RP_NET_TCP_SINK_PORT	5003
#endif
#ifndef RP_NET_TCP_NC_PORT
#define RP_NET_TCP_NC_PORT		5009
#endif
/*  簡易スループットで流すバイト数（既定 100KB）  */
#ifndef RP_NET_TP_BYTES
#define RP_NET_TP_BYTES			(100U * 1024U)
#endif

/*
 *  ----------------------------------------------------------------------------
 *  段 7g: UDP / HTTP / DNS の相手（**IP もポートも名前も秘密ではない**）
 *  ----------------------------------------------------------------------------
 *  UDP エコーは TCP エコーと**同じ番号（5002）を UDP で**開ける
 *  ——番号を増やさないのは、PC 側の台本の面倒を増やさないためであって、
 *  TCP と UDP が同じ物だからではない（別のプロトコルである）。
 *  NC の 5009 も同様に「TCP でも UDP でも開けない番号」を 1 つに寄せてある。
 */
#ifndef RP_NET_UDP_ECHO_PORT
#define RP_NET_UDP_ECHO_PORT	5002
#endif
/*
 *  H3-c（§31 先読み fast path の positive control。段 7b-PC-impl）
 *    ITER … 滞留は毎回出るとは限らないので複数回撃つ
 *    TMO  … 出典の滞留は 12 秒。**それを測れる長さ**にしておく
 *           （5 秒だと「滞留した」と「相手が居ない」が区別できない）
 */
#ifndef RP_H3C_ITER
#define RP_H3C_ITER				5U
#endif
#ifndef RP_H3C_TMO_MS
#define RP_H3C_TMO_MS			15000U
#endif
/*
 *  R12-b の閾値（**段 7g-H3c-live で 1 秒から置き直した**）。
 *
 *  旧: 1 000 000 us（1 秒）。根拠は「出典の滞留 12 秒と健全な往復 数ms の間」。
 *  ⇒ **その根拠は消えた**。本 repo の待ちは `P4HOSTED_SDIO_WAIT_CAP_MS=100`
 *     で頭打ち（`p4hosted_osi.c`）なので、wakeup を 1 回落としても**遅れは
 *     高々 100ms** であり、秒オーダーの滞留は構造的に起き得ない
 *     （段 7b-PC-impl §4-5）。1 秒の閾値は「壊した版でも通る」＝
 *     positive control にならない。
 *
 *  新: 50 000 us（50ms）。実測（段 7g-H3c-live、同じ AP・同じ PC 台本）:
 *      fast path ON  max=20 077us / mean=17 570us
 *      fast path OFF max=118 269us / mean=105 649us   （差 98 192us ＝ 頭打ち 1 回分）
 *      閾値は両者の**幾何平均** sqrt(20077 * 118269) = 48 730us を
 *      10ms 格子へ切り上げた値。ON 側の最大の 2.5 倍・OFF 側の最小の 1/2 以下。
 */
#ifndef RP_H3C_MAX_US
#define RP_H3C_MAX_US			50000U
#endif
#ifndef RP_NET_UDP_NC_PORT
#define RP_NET_UDP_NC_PORT		5009
#endif
/*  連射の本数と 1 通の大きさ（**期待値は置かない**。AC W1-b）  */
#ifndef RP_NET_UDP_BURST_N
#define RP_NET_UDP_BURST_N		100U
#endif
#ifndef RP_NET_UDP_BURST_LEN
#define RP_NET_UDP_BURST_LEN	64U
#endif

/*  HTTP は**閉域の PC** へ向ける（外へ出すのは DNS の問合せだけ。AC §1-W2）  */
#ifndef RP_NET_HTTP_PORT
#define RP_NET_HTTP_PORT		5004
#endif
#ifndef RP_NET_HTTP_PATH
#define RP_NET_HTTP_PATH		"/probe"
#endif
#ifndef RP_NET_HTTP_NC_PATH
#define RP_NET_HTTP_NC_PATH		"/does-not-exist"
#endif

/*
 *  DNS で引く名前。NC 側は **RFC 2606 が「絶対に存在しない」と定めた `.invalid`**
 *  を使う——「たぶん無いだろう」で選んだ名前は、いつか誰かが登録し得る。
 */
#ifndef RP_NET_DNS_NAME
#define RP_NET_DNS_NAME			"pool.ntp.org"
#endif
#ifndef RP_NET_DNS_NC_NAME
#define RP_NET_DNS_NC_NAME		"no-such-host-fmp3-p4-7g.invalid"
#endif

#include "p4hosted_net.h"
#include "p4hosted_netops.h"
#include "netif_esp_hosted.h"

#include "lwip/inet.h"			/* inet_addr（判定側で相手の番地を作るため） */
#endif

/*  `send_slave_config` の `host_cap`。**既定は出典どおり 0**  */
#ifndef RP_HOST_CAP
#define RP_HOST_CAP		0
#endif

/*
 *  段7d 改変 D-9 の計数（`esp/p4hosted/p4hosted_pools.c` が定義する）。
 *  ヘッダ（取込み物）を触らずに済ませるため、ここで extern 宣言する。
 */
extern volatile uint32_t	p4hosted_n_thread_act_ok;
extern volatile uint32_t	p4hosted_n_thread_act_fail;
extern volatile uint32_t	p4hosted_n_thread_pool_full;

/*
 *  ----------------------------------------------------------------------------
 *  同期出力（1 行を不可分にする。7a/7b と同じ作法）
 *  ----------------------------------------------------------------------------
 *  **段 7f で `esp/p4hosted/p4hosted_prt.{h,c}` へ移した**（F2 の括り出し）。
 *  データパスの実体を別の翻訳単位へ出したので、同じ小道具が両方から要る。
 *  呼び出し側の綴り（`pbeg`/`ps`/`pu`/`px`/`pb`/`pnl`/`pkv`/`pkx`/`pdump`）は
 *  **1 文字も変えていない**（ヘッダが `static inline` の別名を与えている）。
 *  設計の意図・7a で踏んだ罠は `p4hosted_prt.h` の冒頭コメントを参照。
 */
#include "p4hosted_prt.h"

#include "p4hosted_rpc.h"

/*
 *  資格情報の出どころ（切り出しに際して足した唯一の仕掛け）。出典では
 *  WIFI_STA_SSID / WIFI_STA_PASS は CMake が -include した生成ヘッダの
 *  コンパイル時定数だった。Arduino では WiFi.begin(ssid, pass) が実行時に
 *  来るので、同じ綴りのマクロが**実行時に設定されたポインタ**を指すように
 *  する。これで下の本体は 1 行も書き換えずに済む。
 */
static const char	*rp_cred_ssid = "";
static const char	*rp_cred_pass = "";

void
p4hosted_rpc_set_credentials(const char *ssid, const char *pass)
{
	rp_cred_ssid = (ssid != NULL) ? ssid : "";
	rp_cred_pass = (pass != NULL) ? pass : "";
}

#define WIFI_STA_SSID	(rp_cred_ssid)
#define WIFI_STA_PASS	(rp_cred_pass)

/*
 *  ----------------------------------------------------------------------------
 *  判定（`chk()` が唯一の比較器。R1-0 が同じ比較器をわざと外して落ちることを実演）
 *  ----------------------------------------------------------------------------
 */
/*
 *  ----------------------------------------------------------------------------
 *  vtable 経由の薄いラッパ（**必ず `g_h.funcs` を通す**）
 *  ----------------------------------------------------------------------------
 */
static void	*g_ctx;

static int
v_rd_reg(uint32_t reg, uint8_t *p, uint16_t n)
{
	return(g_h.funcs->_h_sdio_read_reg(g_ctx, reg, p, n, true));
}

static int
v_wr_reg(uint32_t reg, uint8_t *p, uint16_t n)
{
	return(g_h.funcs->_h_sdio_write_reg(g_ctx, reg, p, n, true));
}

static int
v_rd_blk(uint32_t reg, uint8_t *p, uint16_t n)
{
	return(g_h.funcs->_h_sdio_read_block(g_ctx, reg, p, n, true));
}

static int
v_wr_blk(uint32_t reg, uint8_t *p, uint16_t n)
{
	return(g_h.funcs->_h_sdio_write_block(g_ctx, reg, p, n, true));
}

static uint32_t
le32(const uint8_t *p)
{
	return((uint32_t) p[0] | ((uint32_t) p[1] << 8)
		   | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24));
}

/*
 *  ============================================================================
 *  自前 protobuf（**符号化 1 本ぶん ＋ 復号**。ランタイムはリンクしない）
 *  ============================================================================
 *  出典が使う protobuf-c は 3672 行、生成コードは 32955 行ある。本段が必要と
 *  するのは varint と length-delimited の 2 つだけなので、ここに書く。
 *
 *  **「スレーブが応答した」は、こちらの符号化が正しいことの証拠にはならない**
 *  ——間違った符号化でもエラー応答は返り得る。⇒ R2-s1/s2 で
 *  **符号化器・復号器自身の positive control を先に撃つ**。
 */
#define PB_WT_VARINT	0U
#define PB_WT_64BIT		1U
#define PB_WT_LEN		2U
#define PB_WT_32BIT		5U

/*  復号の結果（**「読めなかった」を「0 だった」に畳まない**）  */
#define PB_OK			0
#define PB_ETRUNC		1		/* 途中で尽きた */
#define PB_EVARINT		2		/* varint が 10 バイトを超えた／終端が無い */
#define PB_EWIRETYPE	3		/* 知らない wire type */
#define PB_ELEN			4		/* 長さが残りを超える */

static uint32_t
pb_put_varint(uint8_t *p, uint64_t v)
{
	uint32_t	n = 0U;

	do {
		uint8_t	b = (uint8_t)(v & 0x7FU);

		v >>= 7;
		if (v != 0U) { b |= 0x80U; }
		p[n++] = b;
	} while (v != 0U);
	return(n);
}

/*  戻り値: PB_OK / PB_E*。`used` には消費バイト数を入れる。  */
static int
pb_get_varint(const uint8_t *p, uint32_t left, uint64_t *out, uint32_t *used)
{
	uint64_t	v = 0U;
	uint32_t	i;

	for (i = 0U; i < 10U; i++) {
		if (i >= left) { return(PB_ETRUNC); }
		v |= ((uint64_t)(p[i] & 0x7FU)) << (7U * i);
		if ((p[i] & 0x80U) == 0U) {
			*out = v;
			*used = i + 1U;
			return(PB_OK);
		}
	}
	/*  10 バイト読んでも終端が来ない＝壊れている。**黙って打ち切らない。**  */
	return(PB_EVARINT);
}

/*  1 フィールド読む。`fnum`/`wt`/`val`(varint) or `dat`/`dlen`(len-delimited)  */
struct pb_field {
	uint32_t	fnum;
	uint32_t	wt;
	uint64_t	val;
	const uint8_t *dat;
	uint32_t	dlen;
};

static int
pb_next(const uint8_t *p, uint32_t left, struct pb_field *f, uint32_t *used)
{
	uint64_t	tag, v;
	uint32_t	u, n = 0U;
	int			rc;

	rc = pb_get_varint(p, left, &tag, &u);
	if (rc != PB_OK) { return(rc); }
	n += u;
	f->fnum = (uint32_t)(tag >> 3);
	f->wt = (uint32_t)(tag & 7U);
	f->dat = NULL;
	f->dlen = 0U;
	f->val = 0U;

	switch (f->wt) {
	case PB_WT_VARINT:
		rc = pb_get_varint(&p[n], left - n, &v, &u);
		if (rc != PB_OK) { return(rc); }
		f->val = v;
		n += u;
		break;
	case PB_WT_LEN:
		rc = pb_get_varint(&p[n], left - n, &v, &u);
		if (rc != PB_OK) { return(rc); }
		n += u;
		if (v > (uint64_t)(left - n)) { return(PB_ELEN); }
		f->dat = &p[n];
		f->dlen = (uint32_t) v;
		n += (uint32_t) v;
		break;
	case PB_WT_64BIT:
		if ((left - n) < 8U) { return(PB_ETRUNC); }
		n += 8U;
		break;
	case PB_WT_32BIT:
		if ((left - n) < 4U) { return(PB_ETRUNC); }
		n += 4U;
		break;
	default:
		/*  wire type 3/4（group）と 6/7 は出典の schema に出ない。
		 *  **知らないものを素通りさせない**（読み飛ばす長さが分からない）。 */
		return(PB_EWIRETYPE);
	}
	*used = n;
	return(PB_OK);
}

/*
 *  ============================================================================
 *  フレーミング（esp_payload_header ＋ serial TLV）
 *  ============================================================================
 *  出典: `sdio_drv.c:667-696`（ヘッダ）と `serial_if.c:37-65`（TLV）。
 *
 *   | 12B esp_payload_header | 12B TLV | protobuf |
 *   TLV = 0x01 | ep_len LE16 | "RPCRsp" | 0x02 | data_len LE16
 *
 *  **`offset` は必ず 12**（出典は if_type によらず `sizeof(header)` を入れる）。
 *  checksum は本 port では無効（`H_SDIO_CHECKSUM=0`）なので 0 のまま。
 */
#define RP_TLV_T_EPNAME		0x01U
#define RP_TLV_T_DATA		0x02U
#define RP_EPNAME			RPC_EP_NAME_RSP		/* "RPCRsp"（出典ヘッダ） */
#define RP_EPNAME_LEN		6U
#define RP_TLV_HDR_LEN		(1U + 2U + RP_EPNAME_LEN + 1U + 2U)		/* = 12 */

/*  DMA バッファ（**64B 整列必須**）  */
/*
 *  送信バッファ。
 *
 *  【段7e で潰した潜在バグ】7d までは 640 バイトだった（「本段が送る最大フレームは
 *  70 バイト弱」という当時の前提）。**データパスを通すと前提が消える**:
 *  ブロック切上げ後の `blk` が `sizeof(rp_tx)` を超えると `rp_send` は
 *  **送らずに RET_FAIL を返す**ので、ペイロード 501 バイト以上
 *  （`data_left >= 513` -> `blk = 1024 > 640`）のフレームは 1 本も出ない。
 *  ARP(42B) と DHCP DISCOVER(~342B) は通るので**症状が出ない**まま、
 *  ping の大きめのペイロードと TCP のフルセグメントだけが黙って消える。
 *  ⇒ 出典の `MAX_SDIO_BUFFER_SIZE`(1536) を下回らない大きさにする。
 *  MTU 1500 -> Ethernet 1514 + ヘッダ 12 = 1526 -> 切上げ 1536。
 *  （**7d では顕在化していない**。7d は ARP と DHCP しか送っていない） */
static uint8_t	rp_tx[1600] __attribute__((aligned(64)));
/*  受信バッファ。**512 の倍数**にしておく（ブロック切上げ読みが収まるため）。
 *  run4 で `len=2330`（複数パケットの束）が来て 2048 に入らず、
 *  **読まずに捨てた結果バイト数の勘定がずれて以後の受信が全滅した**。 */
static uint8_t	rp_rx[4096] __attribute__((aligned(64)));
static uint8_t	rp_reg[32]  __attribute__((aligned(64)));

/*  スレーブ受信バッファのクレジット（出典 `sdio_tx_buf_count`）  */
static uint32_t	rp_tx_buf_count;
static uint32_t	rp_last_avail;
static uint32_t	rp_n_tx;
static uint32_t	rp_n_tx_nobuf;

static bool
rp_credit(uint32_t buf_needed, uint32_t *p_avail)
{
	uint32_t	tok, avail;
	/*
	 *  【段7d で潰した現行バグ】ここは以前**共有の `rp_reg`** を使っていた。
	 *  7c までは呼び手が main タスク 1 本だったので問題にならなかったが、
	 *  7d で**受信ポンプを別スレッドにした**瞬間に競合になる
	 *  ——ポンプは同じ `rp_reg` へ 20 バイト（INT_RAW と PACKET_LEN）を
	 *  読み込む。踏むと `tok` に割込みステータスが入り、
	 *  **クレジットが実際より多く見える**（＝スレーブに空きが無いのに
	 *  送ってしまい、フレームは黙って捨てられる）。
	 *  ⇒ **スタック上のバッファ**にする（スレッドごとに別なので競合しない）。
	 *  ※ 4 バイトの読出しは `p4sdio_read_bytes` 経路であり、
	 *    `read_blocks` のような 64 バイト整列要求は無い。
	 */
	uint8_t		reg[8];

	if (v_rd_reg(SDIO_REG(ESP_SLAVE_TOKEN_RDATA), reg, 4U) != RET_OK) {
		return(false);
	}
	tok = le32(reg);
	avail = (tok >> 16) & (uint32_t) ESP_TX_BUFFER_MASK;
	avail = (avail + (uint32_t) ESP_TX_BUFFER_MAX - rp_tx_buf_count)
			% (uint32_t) ESP_TX_BUFFER_MAX;
	*p_avail = avail;
	return(avail >= buf_needed);
}

/*
 *  1 パケット送る。**送る前にクレジットを確かめる**（足りなければ送らない）。
 *
 *  【限界を先に書く】`RET_OK` が返るのは「ホスト側の DMA が完了した」までで、
 *  **スレーブが受け取ったことは言わない**。往復の証拠は R2（応答が返る）で得る。
 */
/*
 *  **秘密を含むフレームを生ダンプしない**ためのフラグ（2026-08-16 の事故の対処）。
 *
 *  【実際に起こしたこと・記録】`set_config` の要求は SSID と PASS を**平文で**
 *  含む。そこへ無条件の生ダンプ（`pdump`）をかけたため、採取ログに
 *  **16 進で**秘密が焼かれた。`scripts/redact_secrets.sh` は**平文の針**しか
 *  見ないので、この形は検査を素通りする（ログは即削除した）。
 *  ⇒ **秘密を運ぶフレームは、そもそもダンプしない。**
 *     ヘッダと TLV ヘッダ（先頭 24 バイト＝秘密が入らない部分）までに切る。
 */
static bool	rp_tx_has_secret;

/*
 *  【段7d】データパスの送信は**1 本ずつ印字しない**。
 *  理由は 2 つある:
 *    (1) DHCP/ARP/ICMP は秒あたり何本も出る。全部印字すると採取が溢れ、
 *        肝心の判定行が流れる（7c で「短いログを結果として読まない」と
 *        書いたのと同じ問題の裏返し）。
 *    (2) **生ダンプは秘密を運ぶ経路になり得る**（7c §9-1 の事故）。
 *        DHCP に SSID は乗らないが、hostname 等が乗り得る。
 *        ⇒ **データフレームは要約統計だけにする。**
 *  制御面（7a/7b/7c の TLV/RPC）は従来どおり印字する。
 */
static bool	rp_tx_quiet;

static int
rp_send(uint8_t if_type, const uint8_t *payload, uint16_t plen, const char *tag)
{
	struct esp_payload_header	hdr;
	uint32_t	data_left = (uint32_t) plen + sizeof(hdr);
	uint32_t	blk = ((data_left + (uint32_t) ESP_BLOCK_SIZE - 1U)
					   / (uint32_t) ESP_BLOCK_SIZE) * (uint32_t) ESP_BLOCK_SIZE;
	uint32_t	buf_needed = (data_left + (uint32_t) ESP_RX_BUFFER_SIZE - 1U)
							 / (uint32_t) ESP_RX_BUFFER_SIZE;
	uint32_t	avail = 0U;
	uint32_t	addr;
	int			rc;

	if ((data_left > sizeof(rp_tx)) || (blk > sizeof(rp_tx))) {
		pbeg(); ps("RPROBE tx **too long** "); pkv("len", data_left); pnl();
		return(RET_FAIL);
	}

	if (!rp_credit(buf_needed, &avail)) {
		rp_n_tx_nobuf++;
		rp_last_avail = avail;
		pbeg();
		ps("RPROBE tx **no slave buffer** ");
		pkv("need", buf_needed); pkv("avail", avail); pnl();
		return(RET_FAIL);
	}
	rp_last_avail = avail;

	memset(&hdr, 0, sizeof(hdr));
	hdr.if_type = if_type & 0x0FU;
	hdr.if_num = 0U;
	hdr.len = plen;
	hdr.offset = (uint16_t) sizeof(hdr);
	/*  checksum は 0 のまま（本 port は H_SDIO_CHECKSUM=0）。seq_num も 0。 */

	memset(rp_tx, 0, blk);
	memcpy(rp_tx, &hdr, sizeof(hdr));
	memcpy(&rp_tx[sizeof(hdr)], payload, plen);

	/*
	 *  送信番地は `ESP_SLAVE_CMD53_END_ADDR - data_left`（出典 sdio_drv.c:731）。
	 *  ブロック境界へ**切上げて**送る（出典 `H_SDIO_TX_BLOCK_ONLY_XFER=1` と同じ。
	 *  スレーブは `data_left` までしか読まないので余りは捨てられる）。
	 */
	addr = (uint32_t) ESP_SLAVE_CMD53_END_ADDR - data_left;
	rc = v_wr_blk(addr, rp_tx, (uint16_t) blk);

	if (rp_tx_quiet) {
		/*  静かな経路。計数は上位（`p4hosted_net_tx`）が持つ。  */
		if (rc == RET_OK) {
			rp_tx_buf_count = (rp_tx_buf_count + buf_needed)
							  % (uint32_t) ESP_TX_BUFFER_MAX;
			rp_n_tx++;
		}
		return(rc);
	}

	pbeg();
	ps("RPROBE tx "); ps(tag); target_fput_log(' ');
	pkv("if_type", if_type); pkv("plen", plen); pkv("total", data_left);
	pkx("addr", addr); pkv("blk", blk);
	pkv("need", buf_needed); pkv("avail", avail);
	pkv("rc", (uint32_t)(rc == RET_OK ? 0U : 1U));
	pnl();
	if (rp_tx_has_secret) {
		/*  秘密を含むので**本体を出さない**。ヘッダ＋TLV ヘッダ（24B）まで。 */
		pdump(tag, rp_tx, (data_left < 24U) ? data_left : 24U);
		pbeg(); ps("RPROBE raw "); ps(tag);
		ps(" **以降は秘密を含むので出力しない** "); pkv("suppressed", data_left - 24U);
		pnl();
	}
	else {
		pdump(tag, rp_tx, data_left);
	}

	if (rc == RET_OK) {
		rp_tx_buf_count = (rp_tx_buf_count + buf_needed) % (uint32_t) ESP_TX_BUFFER_MAX;
		rp_n_tx++;
	}
	return(rc);
}

/*
 *  ============================================================================
 *  受信
 *  ============================================================================
 */
#define RP_REG_BUF_LEN		(SDIO_REG(ESP_SLAVE_PACKET_LEN_REG) \
							 - SDIO_REG(ESP_SLAVE_INT_RAW_REG) + 4U)	/* = 20 */
#define RP_PKT_LEN_INDEX	(SDIO_REG(ESP_SLAVE_PACKET_LEN_REG) \
							 - SDIO_REG(ESP_SLAVE_INT_RAW_REG))			/* = 0x10 */

static uint32_t	rp_rx_byte_count;
static uint32_t	rp_n_pkt;
static uint32_t	rp_n_unknown_tag;
static uint32_t	rp_n_pb_unknown_field;

/*  7b の非退行（R6-c）で見る値  */
static bool		rp_init_seen;
static uint32_t	rp_chip_id = 0xFFFFFFFFU;
static uint32_t	rp_caps = 0xFFFFFFFFU;


/*  RPC の応答スロット（**要求ごとに uid で照合する**）  */
static bool		rp_rpc_seen;
static uint32_t	rp_rpc_msg_type;
static uint32_t	rp_rpc_msg_id;
static uint32_t	rp_rpc_uid;
static bool		rp_rpc_ep_ok;
static uint32_t	rp_req_uid;

/*  応答本体は `rp_rx` を上書きされる前に**写す**（次のパケットを読むと消える）  */
/*  応答本体の受け皿。**1600 は上流の `ESP_TRANSPORT_MAX_BUF_SIZE`**（同名の
 *  上流ヘッダ定数）。1024 では scan の AP レコード（実測 1034 バイト）が
 *  入らず「too long for slot」で捨てていた（run1 で踏んだ）。 */
static uint8_t	rp_body[ESP_TRANSPORT_MAX_BUF_SIZE];
static uint32_t	rp_body_len;

/*  スレーブから来たイベント（RPCEvt）の記録  */
static uint32_t	rp_n_event;
static uint32_t	rp_last_event_id;
static uint32_t	rp_n_ev_connected;
static uint32_t	rp_n_ev_disconnected;
static uint32_t	rp_last_disc_reason;

static uint32_t
rp_len_from_slave(uint32_t raw)
{
	uint32_t	len = raw & (uint32_t) ESP_SLAVE_LEN_MASK;

	if (len >= rp_rx_byte_count) {
		len = (len + (uint32_t) ESP_RX_BYTE_MAX - rp_rx_byte_count)
			  % (uint32_t) ESP_RX_BYTE_MAX;
	}
	else {
		len = ((uint32_t) ESP_RX_BYTE_MAX - rp_rx_byte_count) + len;
	}
	return(len);
}

/*  INIT event の TLV（7b と同じ。R6-c の非退行判定に使う）  */
static void
rp_parse_init_tlv(const uint8_t *pos, uint32_t left)
{
	while (left >= 2U) {
		uint8_t	tag = pos[0];
		uint8_t	tlen = pos[1];

		if ((uint32_t) tlen + 2U > left) {
			pbeg(); ps("RPROBE init tlv **overrun** "); pkv("left", left); pnl();
			return;
		}
		pbeg();
		ps("RPROBE init tlv "); pkx("tag", tag); pkv("len", tlen); ps("val=");
		{
			uint8_t	k;
			for (k = 0U; k < tlen; k++) { pb(pos[2U + k]); }
		}
		pnl();
		switch (tag) {
		case ESP_PRIV_CAPABILITY:        if (tlen >= 1U) { rp_caps = pos[2]; } break;
		case ESP_PRIV_FIRMWARE_CHIP_ID:  if (tlen >= 1U) { rp_chip_id = pos[2]; } break;
		case ESP_PRIV_TEST_RAW_TP:
		case ESP_PRIV_RX_Q_SIZE:
		case ESP_PRIV_TX_Q_SIZE:
		case ESP_PRIV_CAP_EXT:
		case ESP_PRIV_FIRMWARE_VERSION:
		case ESP_PRIV_TRANS_SDIO_MODE:
			break;
		default:
			/*  **知らないタグを黙って捨てない。**  */
			rp_n_unknown_tag++;
			break;
		}
		pos += (uint32_t) tlen + 2U;
		left -= (uint32_t) tlen + 2U;
	}
	rp_init_seen = true;
}

/*
 *  ----------------------------------------------------------------------------
 *  RPC envelope の復号（**汎用**）
 *  ----------------------------------------------------------------------------
 *  出典 `rpc_core.c:190` が `payload_case = (Rpc__PayloadCase) msg_id` と
 *  書いている＝**payload の field 番号 == msg_id**（生成物の `_Static_assert`
 *  が本段の使う分について機械的に確かめている）。⇒ envelope を 2 度舐めて、
 *  1 周目で msg_id を取り、2 周目で `fnum == msg_id` の length-delimited を本体とする。
 */
static void
rp_parse_rpc(const uint8_t *p, uint32_t left)
{
	uint32_t		msg_type = 0U, msg_id = 0U, uid = 0U;
	const uint8_t	*body = NULL;
	uint32_t		blen = 0U;
	const uint8_t	*p0 = p;
	uint32_t		left0 = left;

	while (left > 0U) {
		struct pb_field	f;
		uint32_t		used;

		if (pb_next(p, left, &f, &used) != PB_OK) { break; }
		if (f.fnum == (uint32_t) HOSTED_RPC_FIELD_MSG_TYPE) { msg_type = (uint32_t) f.val; }
		else if (f.fnum == (uint32_t) HOSTED_RPC_FIELD_MSG_ID) { msg_id = (uint32_t) f.val; }
		else if (f.fnum == (uint32_t) HOSTED_RPC_FIELD_UID) { uid = (uint32_t) f.val; }
		p += used;
		left -= used;
	}

	p = p0;
	left = left0;
	while (left > 0U) {
		struct pb_field	f;
		uint32_t		used;
		int				rc = pb_next(p, left, &f, &used);

		if (rc != PB_OK) {
			pbeg(); ps("RPROBE rpc envelope **decode error** ");
			pkv("rc", (uint32_t) rc); pnl();
			/*  **秘密を含みうるので全数は出さない**（応答には SSID が乗る）。 */
			pdump("bad-envelope", p0, (left0 < 32U) ? left0 : 32U);
			return;
		}
		if ((f.fnum == msg_id) && (f.wt == PB_WT_LEN)) {
			body = f.dat;
			blen = f.dlen;
		}
		else if ((f.fnum != (uint32_t) HOSTED_RPC_FIELD_MSG_TYPE)
				 && (f.fnum != (uint32_t) HOSTED_RPC_FIELD_MSG_ID)
				 && (f.fnum != (uint32_t) HOSTED_RPC_FIELD_UID)) {
			/*  **知らない field を黙って捨てない。**  */
			rp_n_pb_unknown_field++;
			pbeg(); ps("RPROBE rpc envelope **unknown field** ");
			pkv("f", f.fnum); pkv("wt", f.wt); pkv("msg_id", msg_id); pnl();
		}
		p += used;
		left -= used;
	}

	pbeg();
	ps("RPROBE rpc envelope ");
	pkv("msg_type", msg_type); pkv("msg_id", msg_id);
	pkv("uid", uid); pkv("body_len", blen);
	pnl();

	if (msg_type == (uint32_t) HOSTED_RPC_TYPE_EVENT) {
		/*  イベントは応答スロットを上書きしない（uid は 0 で来る）  */
		rp_n_event++;
		rp_last_event_id = msg_id;
		if (msg_id == (uint32_t) HOSTED_RPC_ID_EVENT_STA_CONNECTED) {
			rp_n_ev_connected++;
			pbeg(); ps("RPROBE event **STA_CONNECTED**"); pnl();
		}
		else if (msg_id == (uint32_t) HOSTED_RPC_ID_EVENT_STA_DISCONNECTED) {
			rp_n_ev_disconnected++;
			/*  **理由コードを出す**（「繋がらなかった」だけでは直せない）。
			 *  SSID/BSSID は**出さない**。 */
			if (body != NULL) {
				const uint8_t	*q = body;
				uint32_t		l2 = blen;

				while (l2 > 0U) {
					struct pb_field	g;
					uint32_t		u2;

					if (pb_next(q, l2, &g, &u2) != PB_OK) { break; }
					if ((g.fnum == HOSTED_RPC_F_EV_DISC_STA_DISCONNECTED)
						&& (g.wt == PB_WT_LEN)) {
						const uint8_t	*r = g.dat;
						uint32_t		l3 = g.dlen;

						while (l3 > 0U) {
							struct pb_field	h;
							uint32_t		u3;

							if (pb_next(r, l3, &h, &u3) != PB_OK) { break; }
							if (h.fnum == HOSTED_RPC_F_DISC_REASON) {
								rp_last_disc_reason = (uint32_t) h.val;
							}
							r += u3;
							l3 -= u3;
						}
					}
					q += u2;
					l2 -= u2;
				}
			}
			pbeg(); ps("RPROBE event **STA_DISCONNECTED** ");
			pkv("reason", rp_last_disc_reason); pnl();
		}
		return;
	}

	rp_rpc_msg_type = msg_type;
	rp_rpc_msg_id = msg_id;
	rp_rpc_uid = uid;
	rp_rpc_seen = true;
	rp_body_len = 0U;
	if (body != NULL) {
		if (blen <= sizeof(rp_body)) {
			memcpy(rp_body, body, blen);
			rp_body_len = blen;
		}
		else {
			pbeg(); ps("RPROBE rpc body **too long for slot** ");
			pkv("blen", blen); pnl();
		}
	}
}

/*  serial TLV を解いて protobuf 本体を取り出す（R2-c）  */
static void
rp_parse_serial(const uint8_t *p, uint32_t plen)
{
	uint32_t	eplen, dlen;

	if (plen < RP_TLV_HDR_LEN) {
		pbeg(); ps("RPROBE serial **too short** "); pkv("plen", plen); pnl();
		return;
	}
	if (p[0] != RP_TLV_T_EPNAME) {
		pbeg(); ps("RPROBE serial **not EPNAME** "); pkx("t", p[0]); pnl();
		return;
	}
	eplen = (uint32_t) p[1] | ((uint32_t) p[2] << 8);
	if ((eplen != RP_EPNAME_LEN) || ((3U + eplen + 3U) > plen)) {
		pbeg(); ps("RPROBE serial **bad ep len** "); pkv("eplen", eplen); pnl();
		return;
	}
	rp_rpc_ep_ok = (memcmp(&p[3], RPC_EP_NAME_RSP, RP_EPNAME_LEN) == 0)
				   || (memcmp(&p[3], RPC_EP_NAME_EVT, RP_EPNAME_LEN) == 0);
	pbeg();
	ps("RPROBE serial ep=\"");
	{
		uint32_t	k;
		for (k = 0U; k < eplen; k++) { target_fput_log((char) p[3U + k]); }
	}
	ps("\" "); pkv("ep_ok", (uint32_t)(rp_rpc_ep_ok ? 1U : 0U));
	if (p[3U + eplen] != RP_TLV_T_DATA) {
		ps("**not DATA** "); pkx("t", p[3U + eplen]); pnl();
		return;
	}
	dlen = (uint32_t) p[4U + eplen] | ((uint32_t) p[5U + eplen] << 8);
	pkv("dlen", dlen);
	pnl();
	if ((6U + eplen + dlen) > plen) {
		pbeg(); ps("RPROBE serial **data overruns payload** ");
		pkv("dlen", dlen); pkv("plen", plen); pnl();
		return;
	}
	rp_parse_rpc(&p[6U + eplen], dlen);
}

/*
 *  ----------------------------------------------------------------------------
 *  受信: **len バイトを必ず読み切り**、その中の**全パケット**を振り分ける
 *  ----------------------------------------------------------------------------
 *  【run4 で踏んだ罠・その修正】
 *  接続が成立するとスレーブは STA のデータフレーム（`if_type=1`）を流し始める。
 *  スレーブは複数パケットを**束ねて**渡すので、`len` は 1 パケットより大きくなる
 *  （実測 2330）。旧実装は「バッファに入らない」と**読まずに戻り**、
 *  `rp_rx_byte_count` を進めなかった。⇒ 以後すべての長さ計算がずれ、
 *  **RPC の応答が二度と解けなくなった**（`sta_get_ap_info` が 15 回とも無応答）。
 *
 *  ⇒ 出典（`sdio_drv.c:1240-1268` の `do { ... } while (data_left)` と
 *     `sdio_push_data_to_queue:967-1022`）と同じ形にする:
 *       (1) `0x1F800 - data_left` から**読み切る**（入らなければ分割して読む）
 *       (2) `rp_rx_byte_count` は**読んだ len だけ必ず進める**
 *       (3) バッファ内を `len + offset` 刻みで歩いて**全パケット**を処理する
 *     本 probe は IP スタックを持たないので `if_type=STA/AP` は**捨てる**が、
 *     **捨てたことを数える**（黙って落とさない）。
 */
static uint32_t	rp_n_drop_data;
static uint32_t	rp_n_split;

/*
 *  【穴 15・2026-08-18】滞留長の計器。
 *
 *  `len`（スレーブ側に溜まっていて、まだ読んでいないバイト数）は
 *  **ホスト側の `rp_rx_byte_count` との差**でしか分からない。
 *  ⇒ 「どこまで育つのか」「バッファに入らない事態が起きたのか」を
 *  **数えていないと、詰まったときに何が起きたのか後から言えない**。
 *  **0 でも印字する**（「数えていない」と「0 だった」は違う）。
 */
static uint32_t	rp_max_len;			/* 観測した len の最大値 */
static uint32_t	rp_n_len_over;		/* len > sizeof(rp_rx) だった回数（分割読み） */
static uint32_t	rp_n_len_zero;		/* NEW_PACKET なのに len==0 だった回数 */
static uint32_t	rp_n_walk_break;	/* 塊の途中でパケット歩きが解けなくなった回数 */
static uint32_t	rp_n_walk_lost;	/* 上記で捨てたバイト数の合計 */

static void
rp_dispatch_pkt(const uint8_t *base, uint32_t off, uint32_t plen, uint8_t if_type)
{
	if (if_type == (uint8_t) ESP_PRIV_IF) {
		const struct esp_priv_event	*ev =
			(const struct esp_priv_event *)(const void *)(base + off);

		pbeg(); ps("RPROBE rx priv "); pkx("event_type", ev->event_type);
		pkv("event_len", ev->event_len); pnl();
		if ((ev->event_type == (uint8_t) ESP_PRIV_EVENT_INIT)
			&& (((uint32_t) ev->event_len + 2U) <= plen)) {
			rp_parse_init_tlv(ev->event_data, ev->event_len);
		}
	}
	else if (if_type == (uint8_t) ESP_SERIAL_IF) {
		rp_parse_serial(base + off, plen);
	}
#if defined(P4HOSTED_NET)
	else if (if_type == (uint8_t) ESP_STA_IF) {
		/*
		 *  【段7d】STA の 802.3 フレームを lwIP へ渡す。
		 *  **ここが 7c との唯一の差**である（7c は同じ場所で捨てて数えていた）。
		 *  `base + off` はポンプの作業バッファ `rp_rx` の中を指す
		 *  ——**渡した先から戻ると無効**（`p4hosted_net.h` の所有権の節）。
		 *
		 *  【段 7f】渡す/捨てるの判断とその計数は**データパス側**が持つ
		 *  （`P4HOSTED_NET_NO_RX` の negative control も含めて）。
		 *  probe に残るのは「**捨てられた本数を数えること**」だけである。
		 */
		if (!p4hosted_net_rx_deliver(base + off, (uint16_t) plen)) {
			rp_n_drop_data++;
		}
	}
#endif
	else {
		/*  STA/AP のデータフレーム。**IP スタックが無いので捨てる**が数える。 */
		rp_n_drop_data++;
	}
}

/*  読み切って、バッファ内の全パケットを処理する  */
static bool
rp_read_avail(uint32_t len)
{
	uint32_t	data_left = len;
	bool		ok = true;
	bool		first = true;
	/*
	 *  【穴 15・2026-08-18】**塊の境界をまたぐパケットを持ち越す。**
	 *
	 *  `len > sizeof(rp_rx)` のときは 1 回の CMD53 で読み切れないので分割する。
	 *  分割そのものは以前から書いてあったが、**一度も実行されていなかった**
	 *  （`rp_pump()` が `len > 4096` を読まずに捨てていたため。同関数の
	 *  修正コメント参照）。修正でこの経路が生きるので、**その正しさは
	 *  こちらが持たなければならない。**
	 *
	 *  持ち越さないと何が起きるか（実測）: 塊の末尾に頭だけ入ったパケットは
	 *  「解けない」ので捨てられ、**次の塊は先頭がパケットの途中**になるため
	 *  丸ごと解けなくなる。詰まりはしない（バイト数の勘定は合う）が、
	 *  **RPC の応答がそこに入っていれば失われる**
	 *  （`logs/24-fix-gap-r1.log` の `H15-1` が実際にそれで FAIL した）。
	 *
	 *  スレーブのデータは**再読みできない**（CMD53 の番地は「残り長」を
	 *  符号化しており、DMA は読んだぶん進む）。⇒ 既に読んだ末尾の断片を
	 *  バッファ内で**前へ寄せて**、その続きを読む。
	 *
	 *  寄せ先に細工が要る: `p4sdio_read_blocks()` は **転送先の 64B 整列が必須**
	 *  （`p4sdio_host.c` の `rw_blocks`。整列していないと `E_PAR` を返す）。
	 *  ⇒ 断片を `pad` バイトずらして置き、**断片の直後が 64B 境界**になるように
	 *     する（`pad = (64 - carry % 64) % 64`）。
	 */
	uint32_t	carry = 0U;			/* 未処理の断片の長さ */
	uint32_t	carry_at = 0U;		/* 断片の位置（rp_rx 内の添字） */

	while (data_left > 0U) {
		uint32_t	pad, base, room, chunk, blk, pos, vend;
		int			rc;

		/*  断片を「直後が 64B 境界」になる位置へ寄せる  */
		pad = (64U - (carry & 63U)) & 63U;
		if ((carry > 0U) && (carry_at != pad)) {
			memmove(&rp_rx[pad], &rp_rx[carry_at], carry);
		}
		base = pad + carry;					/*  64B 整列（次の読み先）  */
		room = (uint32_t) sizeof(rp_rx) - base;
		room &= ~((uint32_t) ESP_BLOCK_SIZE - 1U);	/*  ブロック単位で読む  */
		if (room == 0U) {
			/*  断片が大きすぎて続きが読めない。**捨てたことを数える。**  */
			rp_n_walk_break++;
			rp_n_walk_lost += carry;
			carry = 0U;
			continue;
		}

		chunk = (data_left > room) ? room : data_left;
		if (chunk < data_left) { rp_n_split++; }
		blk = ((chunk + (uint32_t) ESP_BLOCK_SIZE - 1U) / (uint32_t) ESP_BLOCK_SIZE)
			  * (uint32_t) ESP_BLOCK_SIZE;
		if (blk > room) { blk = room; }

		rc = v_rd_blk((uint32_t) ESP_SLAVE_CMD53_END_ADDR - data_left,
					  &rp_rx[base], (uint16_t) blk);
		if (rc != RET_OK) {
			pbeg(); ps("RPROBE rx read_block **fail** ");
			pkv("rc", (uint32_t)(-rc)); pkv("left", data_left); pnl();
			ok = false;
			break;
		}
		data_left -= chunk;
		vend = base + chunk;				/*  有効データは [pad, vend)  */

		/*  この塊の中のパケットを歩く（`len + offset` 刻み。出典と同じ）  */
		pos = pad;
		while ((pos + sizeof(struct esp_payload_header)) <= vend) {
			struct esp_payload_header	hdr;
			uint32_t					off, plen;

			memcpy(&hdr, rp_rx + pos, sizeof(hdr));
			off = hdr.offset;
			plen = hdr.len;

			if ((off != sizeof(struct esp_payload_header)) || (plen == 0U)) {
				/*  **ヘッダが壊れている。** 持ち越しても解けない。  */
				if (first && (pos == pad)) {
					pbeg(); ps("RPROBE rx **先頭パケットが整合しない** ");
					pkv("off", off); pkv("hlen", plen); pkv("chunk", chunk); pnl();
					pdump("bad-hdr", &rp_rx[pos], 32U);
				}
				rp_n_walk_break++;
				rp_n_walk_lost += (vend - pos);
				pos = vend;
				break;
			}
			if ((pos + off + plen) > vend) {
				/*
				 *  **塊の末尾をまたいでいる。** 捨てずに持ち越す
				 *  （まだ読むものが残っていれば、次の塊の先頭に繋がる）。
				 */
				break;
			}
			rp_n_pkt++;
			pbeg();
			ps("RPROBE rx pkt ");
			pkv("n", rp_n_pkt); pkv("if_type", hdr.if_type);
			pkv("hlen", plen); pkv("off", off); pkv("seq", hdr.seq_num);
			pkx("priv_type", hdr.reserved3);
			pnl();
			rp_dispatch_pkt(rp_rx, pos + off, plen, hdr.if_type);
			pos += off + plen;
		}
		carry = vend - pos;
		carry_at = pos;
		first = false;
	}
	if (carry > 0U) {
		/*  最後まで解けなかった切れ端（本来は起きない）。**黙って捨てない。**  */
		rp_n_walk_break++;
		rp_n_walk_lost += carry;
	}

	/*  **読んだぶんは必ず進める**（進めないと以後の長さ計算が全部ずれる）  */
	rp_rx_byte_count = (rp_rx_byte_count + (len - data_left))
					   % (uint32_t) ESP_RX_BYTE_MAX;
	return(ok);
}

/*
 *  受信を回す。**待ちは必ず vtable の `_h_sdio_wait_slave_intr` を通す**。
 *  `stop_uid` が非 0 なら、その uid の**応答**が来た時点で戻る。
 */
uint32_t
rp_pump(uint32_t max_tries, uint32_t stop_uid)
{
	uint32_t	tries, got = 0U;

	for (tries = 0U; tries < max_tries; tries++) {
		uint32_t	intr, lenraw, len;
		int			rc;

		rc = g_h.funcs->_h_sdio_wait_slave_intr(g_ctx, 0xFFFFFFFFU);
		if (rc != RET_OK) {
			pbeg(); ps("RPROBE wait **fail** "); pkv("rc", (uint32_t)(-rc)); pnl();
			break;
		}
		if (v_rd_reg(SDIO_REG(ESP_SLAVE_INT_RAW_REG), rp_reg,
					 (uint16_t) RP_REG_BUF_LEN) != RET_OK) {
			pbeg(); ps("RPROBE read_regs **fail**"); pnl();
			break;
		}
		intr = le32(&rp_reg[0]);
		lenraw = le32(&rp_reg[RP_PKT_LEN_INDEX]);
		(void) v_wr_reg(SDIO_REG(ESP_SLAVE_INT_CLR_REG), (uint8_t *) &intr, 4U);

		if ((intr & (uint32_t) BIT(SDIO_INT_NEW_PACKET)) == 0U) {
			continue;
		}
		len = rp_len_from_slave(lenraw);
		if (len > rp_max_len) { rp_max_len = len; }
		if (len == 0U) {
			/*  NEW_PACKET なのに 0。読むものが無いので進めようが無い。  */
			rp_n_len_zero++;
			pbeg(); ps("RPROBE rx len **zero** "); pkx("lenraw", lenraw); pnl();
			continue;
		}
#if defined(RPCPROBE_H15_NOFIX)
		/*
		 *  ====================================================================
		 *  **穴 15 の negative control（既定では入らない）**
		 *  ====================================================================
		 *  直す前の姿に戻す。`-DA1_P4_H15_NOFIX=ON` のときだけ入る。
		 *  「直したことが効いている」ことを A/B で実演するためだけの構成である。
		 */
		if (len > sizeof(rp_rx)) {
			pbeg(); ps("RPROBE rx len **out of range** "); pkv("len", len); pnl();
			continue;
		}
#else
		/*
		 *  ====================================================================
		 *  【穴 15・2026-08-18 の修正】**受信バッファの大きさで、読むかどうかを
		 *  決めてはならない。**
		 *  ====================================================================
		 *  ここには以前 `if ((len == 0U) || (len > 4096U)) continue;` と書いてあった
		 *  （4096 は `sizeof(rp_rx)`）。これが**恒久的な詰まり**の正体である:
		 *
		 *    - `len` は「スレーブが積んだ通算バイト数」と `rp_rx_byte_count`
		 *      （**ホスト側が読んだ通算**）の差である。
		 *    - 読まずに `continue` すると `rp_rx_byte_count` が進まない。
		 *      ⇒ **`len` は二度と減らない。**
		 *    - スレーブは自分のキューが埋まると積むのをやめる
		 *      ⇒ 新しい `NEW_PACKET` も来なくなる。
		 *    - 結果、**受信は永久に止まり、RPC の応答も永久に届かない**
		 *      （実測: `.steering/20260818-p4-hole14-assoc-stability/logs/` の
		 *      3 本のログすべてで、この行の**直後**から `rx pkt` が 1 本も出ず、
		 *      以後の RPC が 20〜24 本すべて「応答なし」になっている）。
		 *
		 *  皮肉なことに、この対処は `rp_read_avail()` の中に**既に在った**
		 *  （`chunk > sizeof(rp_rx)` で分割して読み、`len` 全部を必ず進める）。
		 *  **その分割コードは、この門があるせいで一度も実行されていなかった。**
		 *
		 *  ⇒ 門を外す。`len` は `rp_len_from_slave()` の剰余演算により
		 *     構造的に `ESP_RX_BYTE_MAX`(1 MiB) 未満であり、`rp_read_avail()` は
		 *     どんな `len` でも「読み切って、読んだ分だけ進める」ので、
		 *     **上限を置かなくても有限時間で必ず前へ進む**
		 *     （最悪 1 MiB ＝ 256 回の CMD53。20MHz でも 1 秒未満）。
		 *
		 *  出典（`sdio_drv.c:1237-1275`）も同じ形である——`len_from_slave` に
		 *  合わせてバッファを**確保してから**読み、`sdio_rx_byte_count` へ
		 *  `len_from_slave` を必ず足す。**「入らないから読まない」という枝は無い。**
		 */
		if (len > sizeof(rp_rx)) {
			/*  **分割して読む**（詰まらせない）。起きたことは印字して数える。 */
			rp_n_len_over++;
			pbeg(); ps("RPROBE rx len **bufより大きい・分割して読む** ");
			pkv("len", len); pkv("bufsz", (uint32_t) sizeof(rp_rx)); pnl();
		}
#endif	/* RPCPROBE_H15_NOFIX */
		if (rp_read_avail(len)) { got++; }
		if ((stop_uid != 0U) && rp_rpc_seen && (rp_rpc_uid == stop_uid)) {
			break;
		}
	}
	return(got);
}

#if defined(P4HOSTED_NET)
/*
 *  ============================================================================
 *  段7d/7e の 802.3 データパスの**実体は、段 7f でここから出た**
 *  ============================================================================
 *  移動先: `esp/p4hosted/net/p4hosted_net.c`（境界は `net/p4hosted_net.h`）。
 *
 *  【7d が残していた宿題そのもの】7d はこう書いていた——「フレーミング・
 *  クレジット管理・受信ポンプは 7c が probe に書いて実機で通したコードである。
 *  ライブラリへ括り出すのが構造としては正しいが、7d と同じ一手でやると
 *  7a/7b/7c の非退行の測定が括り出しの回帰と混ざる。⇒ **境界だけ先に切って、
 *  実体はここに置く**」。7e で DHCP と ping が通って非退行の基準が確定したので、
 *  7f がその後半（実体の移動）を実行した。
 *
 *  probe に残るのは**下側の境界を差すこと**だけである
 *  ——送信・受信ポンプ・クレジットは SDIO フレーミング側（この 1 枚）が持つ。
 */
static int
rp_xport_send_sta(const uint8_t *buf, uint16_t len)
{
	int		rc;

	/*  データフレームは 1 本ごとに印字しない（要約統計だけ・生ダンプもしない）  */
	rp_tx_quiet = true;
	rc = rp_send((uint8_t) ESP_STA_IF, buf, len, "sta");
	rp_tx_quiet = false;
	return((rc == RET_OK) ? 0 : -1);
}

static void
rp_xport_pump_once(void)
{
	(void) rp_pump(1U, 0U);
}

static bool
rp_xport_credit(uint32_t *p_avail)
{
	return(rp_credit(0U, p_avail));
}

static uint32_t
rp_xport_txcnt(void)
{
	return(rp_tx_buf_count);
}

static const struct p4hosted_net_xport	rp_net_xport = {
	rp_xport_send_sta,
	rp_xport_pump_once,
	rp_xport_credit,
	rp_xport_txcnt,
};
#endif /* P4HOSTED_NET */

/*
 *  ============================================================================
 *  R1: `send_slave_config` 相当（ホスト -> スレーブの TLV）
 *  ============================================================================
 *  出典 `transport_drv.c:726-794`。タグ値は上流ヘッダ
 *  （`esp_hosted_transport.h` の `SLAVE_CONFIG_PRIV_TAG_TYPE`）から取る。
 *  **数字をここへ書かない。**
 *
 *  `firmware_chip_id` は出典と同じく「**スレーブが INIT で送ってきた値をそのまま
 *  返す**」（`transport_drv.c:953` の `chip_type`）。定数を書くと、7a §6-3 で踏んだ
 *  「名前から期待値を作る」型に戻る。
 */
int
rp_send_slave_config(uint8_t chip_id)
{
	uint8_t		pl[2 + 5 * 3];
	uint8_t		*pos = &pl[2];
	uint8_t		len = 0U;

	pl[0] = (uint8_t) ESP_PRIV_EVENT_INIT;

	/*
	 *  `host_cap`。出典（2.12.9）は **0** を渡す（`transport_drv.c:953`）。
	 *  段7d でここを疑ったので、**値を差し替えられる**ようにした
	 *  （`-DA1_P4_HOSTED_HOST_CAP=1` で `ESP_WLAN_SDIO_SUPPORT`）。
	 *  既定は出典どおり 0 である——**「効きそうだから」で既定を変えない。**
	 */
	*pos++ = (uint8_t) HOST_CAPABILITIES;
	*pos++ = 1U;
	*pos++ = (uint8_t) RP_HOST_CAP;
	len += 3U;
	*pos++ = (uint8_t) RCVD_ESP_FIRMWARE_CHIP_ID;           *pos++ = 1U; *pos++ = chip_id;
	len += 3U;
	*pos++ = (uint8_t) SLV_CONFIG_TEST_RAW_TP;              *pos++ = 1U; *pos++ = 0U;
	len += 3U;
	*pos++ = (uint8_t) SLV_CONFIG_THROTTLE_HIGH_THRESHOLD;  *pos++ = 1U; *pos++ = 0U;
	len += 3U;
	*pos++ = (uint8_t) SLV_CONFIG_THROTTLE_LOW_THRESHOLD;   *pos++ = 1U; *pos++ = 0U;
	len += 3U;

	pl[1] = len;
	return(rp_send((uint8_t) ESP_PRIV_IF, pl, (uint16_t)(len + 2U), "slave_config"));
}

/*
 *  ============================================================================
 *  汎用 RPC 呼出し（要求を組んで送り、**uid が一致する応答**を待つ）
 *  ============================================================================
 */
static uint8_t	rp_reqbuf[256];		/*  最大の要求（set_config）で 70 バイト弱  */

/*  protobuf の小道具（**field 番号は必ず生成物のマクロから渡す**）  */
static uint32_t
pb_add_varint(uint8_t *b, uint32_t n, uint32_t fnum, uint64_t v)
{
	n += pb_put_varint(&b[n], ((uint64_t) fnum << 3) | PB_WT_VARINT);
	n += pb_put_varint(&b[n], v);
	return(n);
}

static uint32_t
pb_add_bytes(uint8_t *b, uint32_t n, uint32_t fnum, const void *d, uint32_t dl)
{
	n += pb_put_varint(&b[n], ((uint64_t) fnum << 3) | PB_WT_LEN);
	n += pb_put_varint(&b[n], dl);
	if (dl > 0U) { memcpy(&b[n], d, dl); n += dl; }
	return(n);
}

/*
 *  1 回の RPC。戻り値は「応答が来て uid が一致した」かどうか。
 *  応答本体は `rp_body` / `rp_body_len` に入る（呼出し側が field を解く）。
 */
static bool
rp_rpc_call(uint32_t req_id, uint32_t resp_id, const uint8_t *body, uint32_t blen,
			uint32_t pump_tries, const char *tag)
{
	uint8_t		*pl = rp_reqbuf;
	uint8_t		*pbp = &pl[RP_TLV_HDR_LEN];
	uint32_t	n = 0U;
	uint32_t	tot;
	uint32_t	uid;
	int			rc;

	rp_req_uid++;
	if (rp_req_uid == 0U) { rp_req_uid = 1U; }	/* uid は非 0（出典 rpc_core.c:996） */
	uid = rp_req_uid;

	n = pb_add_varint(pbp, n, (uint32_t) HOSTED_RPC_FIELD_MSG_TYPE,
					  (uint64_t) HOSTED_RPC_TYPE_REQ);
	n = pb_add_varint(pbp, n, (uint32_t) HOSTED_RPC_FIELD_MSG_ID, (uint64_t) req_id);
	n = pb_add_varint(pbp, n, (uint32_t) HOSTED_RPC_FIELD_UID, (uint64_t) uid);
	/*  oneof payload。field 番号 == msg_id（生成物の `_Static_assert` が担保）  */
	n = pb_add_bytes(pbp, n, req_id, body, blen);

	pl[0] = RP_TLV_T_EPNAME;
	pl[1] = (uint8_t)(RP_EPNAME_LEN & 0xFFU);
	pl[2] = (uint8_t)((RP_EPNAME_LEN >> 8) & 0xFFU);
	memcpy(&pl[3], RP_EPNAME, RP_EPNAME_LEN);
	pl[3 + RP_EPNAME_LEN] = RP_TLV_T_DATA;
	pl[4 + RP_EPNAME_LEN] = (uint8_t)(n & 0xFFU);
	pl[5 + RP_EPNAME_LEN] = (uint8_t)((n >> 8) & 0xFFU);
	tot = RP_TLV_HDR_LEN + n;

	pbeg();
	ps("RPROBE rpc-> "); ps(tag); target_fput_log(' ');
	pkv("req_id", req_id); pkv("uid", uid); pkv("body", blen); pkv("pb_len", n);
	pnl();

	rp_rpc_seen = false;
	rp_rpc_uid = 0U;
	rp_body_len = 0U;

	rc = rp_send((uint8_t) ESP_SERIAL_IF, pl, (uint16_t) tot, tag);
	if (rc != RET_OK) { return(false); }

	(void) rp_pump(pump_tries, uid);

	if (!rp_rpc_seen || (rp_rpc_uid != uid)) {
		pbeg(); ps("RPROBE rpc<- "); ps(tag);
		ps(" **応答なし/uid 不一致** "); pkv("seen", (uint32_t)(rp_rpc_seen ? 1U : 0U));
		pkv("got_uid", rp_rpc_uid); pkv("want_uid", uid); pnl();
		return(false);
	}
	if (rp_rpc_msg_id != resp_id) {
		pbeg(); ps("RPROBE rpc<- "); ps(tag);
		ps(" **msg_id が対応しない** "); pkv("got", rp_rpc_msg_id);
		pkv("want", resp_id); pnl();
		return(false);
	}
	return(true);
}

/*
 *  応答本体から int32 の field を 1 つ取る。
 *  **無いときは `dflt` を返す**——proto3 は 0 のスカラを**省く**ので、
 *  「無い」は「壊れている」ではなく「0」である
 *  （run1 で番兵 -12345 をそのまま印字して外した。その修正）。
 */
static int32_t
rp_body_i32(uint32_t fnum, int32_t dflt)
{
	const uint8_t	*p = rp_body;
	uint32_t		left = rp_body_len;

	while (left > 0U) {
		struct pb_field	f;
		uint32_t		used;

		if (pb_next(p, left, &f, &used) != PB_OK) { break; }
		if ((f.fnum == fnum) && (f.wt == PB_WT_VARINT)) { return((int32_t) f.val); }
		p += used;
		left -= used;
	}
	return(dflt);
}

/*  応答本体の `resp`（＝ esp_err_t）。**無ければ 0＝成功**（proto3）  */
static int32_t
rp_resp_status(uint32_t fnum)
{
	return(rp_body_i32(fnum, 0));
}

/*
 *  ============================================================================
 *  R3 / R4a: Wi-Fi の制御面（scan と STA 接続）
 *  ============================================================================
 *  **秘密（SSID/PASS）の値は 1 バイトも印字しない。** 照合はデバイス内で行い、
 *  `found=1/0` と RSSI・チャネルだけを出す（AC §5-1・§5-2）。
 */
/*  `WIFI_STA_SSID` / `WIFI_STA_PASS` は CMake が `-include` で渡す生成ヘッダ
 *  （build ツリーの generated/a1_wifi_credentials.h・mode 0600・コミットしない）。
 *  既定はコミット済みダミー値。実 AP へ繋ぐときだけ `-DA1_LIVE_CREDS=ON`。 */

/*
 *  `wifi_init_config`。**これは期待値ではなく「入力」である**——
 *  可否はスレーブの `resp` が答える。値は ESP-IDF の
 *  `WIFI_INIT_CONFIG_DEFAULT()`（`esp-idf/components/esp_wifi/include/esp_wifi.h:308`）
 *  の Kconfig 既定に相当するものを置いた。`magic` だけは同ヘッダ :189 の
 *  `WIFI_INIT_CONFIG_MAGIC` と一致していなければ `esp_wifi_init` が
 *  `ESP_ERR_INVALID_ARG` を返す。
 */
#define RP_WIFI_INIT_MAGIC		0x1F2F3F4F

static uint32_t	rp_scan_ap_num;
static bool		rp_ssid_found;
static int32_t	rp_ssid_rssi;
static uint32_t	rp_ssid_chan;
static bool		rp_nc_ssid_found;		/* R3-d: 実在しない SSID の対照 */
static bool		rp_connected;
static int32_t	rp_conn_rssi;
static uint32_t	rp_conn_chan;

bool
rp_wifi_init(void)
{
	uint8_t		cfg[192];
	uint8_t		req[224];
	uint32_t	n = 0U, m = 0U;

	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_STATIC_RX_BUF_NUM,  10U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_DYNAMIC_RX_BUF_NUM, 32U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_TX_BUF_TYPE,        1U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_DYNAMIC_TX_BUF_NUM, 32U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_AMPDU_RX_ENABLE,    1U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_AMPDU_TX_ENABLE,    1U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_NVS_ENABLE,         1U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_RX_BA_WIN,          6U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_BEACON_MAX_LEN,     752U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_MGMT_SBUF_NUM,      32U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_ESPNOW_MAX_ENCRYPT_NUM, 7U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_RX_MGMT_BUF_NUM,    5U);
	n = pb_add_varint(cfg, n, HOSTED_RPC_F_INITCFG_MAGIC,
					  (uint64_t)(uint32_t) RP_WIFI_INIT_MAGIC);

	m = pb_add_bytes(req, m, HOSTED_RPC_F_WIFI_INIT_REQ_CFG, cfg, n);

	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_WIFI_INIT,
					 (uint32_t) HOSTED_RPC_ID_RESP_WIFI_INIT,
					 req, m, 40U, "wifi_init")) {
		return(false);
	}
	{
		int32_t	st = rp_resp_status(HOSTED_RPC_F_RESP_WIFI_INIT_RESP);

		pbeg(); ps("RPROBE R4-1 wifi_init "); pkx("resp", (uint32_t) st); pnl();
		return(st == 0);
	}
}

bool
rp_wifi_set_mode(uint32_t mode)
{
	uint8_t		req[16];
	uint32_t	m = pb_add_varint(req, 0U, HOSTED_RPC_F_SET_MODE_REQ_MODE, mode);

	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_SET_WIFI_MODE,
					 (uint32_t) HOSTED_RPC_ID_RESP_SET_WIFI_MODE,
					 req, m, 40U, "set_mode")) {
		return(false);
	}
	{
		int32_t	st = rp_resp_status(1U);	/* Rpc_Resp_SetMode { resp = 1 } */

		pbeg(); ps("RPROBE R4-2 set_mode "); pkv("mode", mode);
		pkx("resp", (uint32_t) st); pnl();
		return(st == 0);
	}
}

bool
rp_wifi_start(void)
{
	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_WIFI_START,
					 (uint32_t) HOSTED_RPC_ID_RESP_WIFI_START,
					 NULL, 0U, 80U, "wifi_start")) {
		return(false);
	}
	{
		int32_t	st = rp_resp_status(1U);

		pbeg(); ps("RPROBE R4-3 wifi_start "); pkx("resp", (uint32_t) st); pnl();
		return(st == 0);
	}
}

bool
rp_scan_start(void)
{
	uint8_t		req[16];
	uint32_t	m = 0U;

	/*  `config_set = 0` なら `config` を送らなくてよい（出典の schema どおり）。
	 *  `block = true` でスレーブ側がスキャン完了まで応答を返さない。 */
	m = pb_add_varint(req, m, HOSTED_RPC_F_SCAN_START_REQ_BLOCK, 1U);

	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_SCAN_START,
					 (uint32_t) HOSTED_RPC_ID_RESP_SCAN_START,
					 req, m, 300U, "scan_start")) {	/* 最長 30 秒待つ */
		return(false);
	}
	{
		int32_t	st = rp_resp_status(1U);

		pbeg(); ps("RPROBE R3-1 scan_start "); pkx("resp", (uint32_t) st); pnl();
		return(st == 0);
	}
}

bool
rp_scan_get_ap_num(void)
{
	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_SCAN_AP_NUM,
					 (uint32_t) HOSTED_RPC_ID_RESP_SCAN_AP_NUM,
					 NULL, 0U, 80U, "scan_ap_num")) {
		return(false);
	}
	{
		int32_t	st = rp_resp_status(HOSTED_RPC_F_AP_NUM_RESP);
		int32_t	num = rp_body_i32(HOSTED_RPC_F_AP_NUM_NUMBER, 0);

		rp_scan_ap_num = (uint32_t)((num > 0) ? num : 0);
		pbeg(); ps("RPROBE R3-2 scan_ap_num "); pkx("resp", (uint32_t) st);
		pkv("number", rp_scan_ap_num); pnl();
		return(st == 0);
	}
}

/*
 *  1 件の `wifi_ap_record` を見る。**SSID の値は印字しない**——
 *  こちらの SSID と一致したかどうか（と RSSI/チャネル）だけを出す。
 */
static void
rp_check_ap_record(const uint8_t *p, uint32_t left, uint32_t idx)
{
	const uint8_t	*ssid = NULL;
	uint32_t		slen = 0U;
	int32_t			rssi = 0;
	uint32_t		chan = 0U;
	uint32_t		authmode = 0U;
	bool			hit, nc_hit;
	static const char	nc[] = "FMP3-NO-SUCH-SSID-7C";

	while (left > 0U) {
		struct pb_field	f;
		uint32_t		used;

		if (pb_next(p, left, &f, &used) != PB_OK) { break; }
		switch (f.fnum) {
		case HOSTED_RPC_F_APREC_SSID:     ssid = f.dat; slen = f.dlen; break;
		case HOSTED_RPC_F_APREC_RSSI:     rssi = (int32_t) f.val; break;
		case HOSTED_RPC_F_APREC_PRIMARY:  chan = (uint32_t) f.val; break;
		case HOSTED_RPC_F_APREC_AUTHMODE: authmode = (uint32_t) f.val; break;
		default: break;
		}
		p += used;
		left -= used;
	}

	/*  末尾の NUL を落としてから比べる（出典は 33 バイト固定で送ることがある）  */
	while ((slen > 0U) && (ssid != NULL) && (ssid[slen - 1U] == 0U)) { slen--; }

	hit = (ssid != NULL) && (slen == strlen(WIFI_STA_SSID))
		  && (memcmp(ssid, WIFI_STA_SSID, slen) == 0);
	nc_hit = (ssid != NULL) && (slen == strlen(nc))
			 && (memcmp(ssid, nc, slen) == 0);

	if (hit) {
		rp_ssid_found = true;
		rp_ssid_rssi = rssi;
		rp_ssid_chan = chan;
	}
	if (nc_hit) { rp_nc_ssid_found = true; }

	/*  **SSID は出さない。** 長さ・RSSI・チャネル・authmode と、一致したかだけ。 */
	pbeg();
	ps("RPROBE ap "); pkv("i", idx); pkv("ssid_len", slen);
	pkv("rssi_neg", (uint32_t)((rssi < 0) ? (uint32_t)(-rssi) : 0U));
	pkv("chan", chan); pkv("auth", authmode);
	pkv("match", (uint32_t)(hit ? 1U : 0U));
	pnl();
}

bool
rp_scan_get_ap_records(uint32_t number)
{
	uint8_t		req[16];
	uint32_t	m = pb_add_varint(req, 0U, HOSTED_RPC_F_AP_RECS_REQ_NUMBER,
								  (uint64_t) number);
	const uint8_t	*p;
	uint32_t	left, idx = 0U;
	int32_t		st;

	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_SCAN_AP_RECORDS,
					 (uint32_t) HOSTED_RPC_ID_RESP_SCAN_AP_RECORDS,
					 req, m, 120U, "scan_ap_records")) {
		return(false);
	}
	st = rp_resp_status(HOSTED_RPC_F_AP_RECS_RESP);
	pbeg(); ps("RPROBE R3-3 scan_ap_records "); pkx("resp", (uint32_t) st);
	pkv("body_len", rp_body_len);
	pkv("number", (uint32_t) rp_body_i32(HOSTED_RPC_F_AP_RECS_NUMBER, 0)); pnl();

	p = rp_body;
	left = rp_body_len;
	while (left > 0U) {
		struct pb_field	f;
		uint32_t		used;

		if (pb_next(p, left, &f, &used) != PB_OK) { break; }
		if ((f.fnum == HOSTED_RPC_F_AP_RECS_AP_RECORDS) && (f.wt == PB_WT_LEN)) {
			rp_check_ap_record(f.dat, f.dlen, idx);
			idx++;
		}
		p += used;
		left -= used;
	}
	pbeg(); ps("RPROBE R3-3 records_parsed "); pkv("n", idx); pnl();
	return(st == 0);
}

bool
rp_wifi_set_sta_config(void)
{
	uint8_t		sta[160];
	uint8_t		wcfg[192];
	uint8_t		req[224];
	uint32_t	n = 0U, w = 0U, m = 0U;
	const char	*pass = WIFI_STA_PASS;
#if defined(RPCPROBE_BAD_PASS)
	/*  R4a-3 の negative control: **わざと外したパスワード**で 1 回撃つ。
	 *  値は印字しない（元も外した先も）。 */
	static char	bad[80];
	uint32_t	k;

	for (k = 0U; (k < (sizeof(bad) - 1U)) && (pass[k] != '\0'); k++) {
		bad[k] = (char)(pass[k] ^ 0x20);
	}
	bad[k] = '\0';
	pass = bad;
#endif

	/*
	 *  **長さは `strlen + 1`（NUL 込み）**。出典 `rpc_core.h:91` の
	 *  `RPC_REQ_COPY_STR` が `H_MIN(strlen(s)+1, MaxSize)` を送っているため。
	 *  ここを `strlen` にすると、スレーブ側の配列が終端されない。
	 */
	n = pb_add_bytes(sta, n, HOSTED_RPC_F_STACFG_SSID,
					 WIFI_STA_SSID, (uint32_t) strlen(WIFI_STA_SSID) + 1U);
	n = pb_add_bytes(sta, n, HOSTED_RPC_F_STACFG_PASSWORD,
					 pass, (uint32_t) strlen(pass) + 1U);
	/*
	 *  **`threshold` と `pmf_cfg` は空でも必ず送る。**
	 *
	 *  出典 `rpc_req.c` の `RPC_ID__Req_WifiSetConfig` は、値が既定でも
	 *  `RPC_ALLOC_ELEMENT(WifiScanThreshold, ...)` / `(WifiPmfConfig, ...)` で
	 *  **必ず実体を確保して送る**。⇒ スレーブ側は非 NULL を前提に
	 *  `sta->threshold->rssi` 等を触ると読める。
	 *  **run1 でこれを送らなかったところ、スレーブが応答しなくなり、
	 *  以後 CMD53 が data timeout（ev_sd=0x104）で全滅した**（＝スレーブが
	 *  落ちた）。proto3 では「空のサブメッセージ」も tag+len0 として出るので、
	 *  中身が全部 0 でも**実体は確保される**。
	 */
	n = pb_add_bytes(sta, n, HOSTED_RPC_F_STACFG_THRESHOLD, NULL, 0U);
	n = pb_add_bytes(sta, n, HOSTED_RPC_F_STACFG_PMF_CFG, NULL, 0U);

	w = pb_add_bytes(wcfg, w, HOSTED_RPC_F_WIFICFG_STA, sta, n);
	/*  iface: STA。**0 のスカラは proto3 で省かれる**ので送らない
	 *  （送らないことが 0 を送ることと同じ、が proto3 の定義である）。 */
	m = pb_add_bytes(req, 0U, HOSTED_RPC_F_SETCFG_REQ_CFG, wcfg, w);

	rp_tx_has_secret = true;		/*  この 1 本だけ生ダンプを抑止する  */
	{
		bool	ok = rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_WIFI_SET_CONFIG,
								 (uint32_t) HOSTED_RPC_ID_RESP_WIFI_SET_CONFIG,
								 req, m, 80U, "set_config");

		rp_tx_has_secret = false;
		if (!ok) { return(false); }
	}
	{
		int32_t	st = rp_resp_status(1U);

		pbeg(); ps("RPROBE R4-4 set_config "); pkx("resp", (uint32_t) st);
		pkv("sta_pb_len", n); pnl();
		return(st == 0);
	}
}

/*
 *  段7d: 明示的な切断（本体が空の RPC）。**失敗しても続行する**
 *  ——「切れていなかったから切る」ためのものであり、切れていれば
 *  スレーブがエラーを返すのが正常だからである。
 */
bool
rp_wifi_disconnect(void)
{
	bool	ok;

	ok = rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_WIFI_DISCONNECT,
					 (uint32_t) HOSTED_RPC_ID_RESP_WIFI_DISCONNECT,
					 NULL, 0U, 20U, "wifi_disconnect");
	pbeg(); ps("RPROBE R4a-0 wifi_disconnect ");
	pkv("rpc_ok", (uint32_t)(ok ? 1U : 0U));
	pkx("resp", (uint32_t) rp_resp_status(
			(uint32_t) HOSTED_RPC_F_WIFI_DISCONNECT_RESP));
	ps("(切れていなければ 0。既に切れていればエラーで正常)");
	pnl();
	return(ok);
}

bool
rp_wifi_connect(void)
{
	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_WIFI_CONNECT,
					 (uint32_t) HOSTED_RPC_ID_RESP_WIFI_CONNECT,
					 NULL, 0U, 120U, "wifi_connect")) {
		return(false);
	}
	{
		int32_t	st = rp_resp_status(1U);

		pbeg(); ps("RPROBE R4-5 wifi_connect "); pkx("resp", (uint32_t) st); pnl();
		return(st == 0);
	}
}

/*
 *  **接続したことの独立の証拠**: 「接続先 AP の情報を返す RPC」の実測値。
 *  `esp_wifi_sta_get_ap_info` は未接続なら `ESP_ERR_WIFI_NOT_CONNECT` を返す。
 *  ⇒ 「connect の応答が 0 だった」ではなく「**AP の情報が取れた**」で判定する。
 */
static bool
rp_sta_get_ap_info(void)
{
	int32_t		st;
	const uint8_t	*p;
	uint32_t	left;

	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_STA_GET_AP_INFO,
					 (uint32_t) HOSTED_RPC_ID_RESP_STA_GET_AP_INFO,
					 NULL, 0U, 80U, "sta_get_ap_info")) {
		return(false);
	}
	st = rp_resp_status(HOSTED_RPC_F_AP_INFO_RESP);
	pbeg(); ps("RPROBE R4-6 sta_get_ap_info "); pkx("resp", (uint32_t) st); pnl();
	if (st != 0) { return(false); }

	p = rp_body;
	left = rp_body_len;
	while (left > 0U) {
		struct pb_field	f;
		uint32_t		used;

		if (pb_next(p, left, &f, &used) != PB_OK) { break; }
		if ((f.fnum == HOSTED_RPC_F_AP_INFO_AP_RECORD) && (f.wt == PB_WT_LEN)) {
			const uint8_t	*q = f.dat;
			uint32_t		l2 = f.dlen;
			uint32_t		slen = 0U;
			const uint8_t	*ssid = NULL;

			while (l2 > 0U) {
				struct pb_field	g;
				uint32_t		u2;

				if (pb_next(q, l2, &g, &u2) != PB_OK) { break; }
				switch (g.fnum) {
				case HOSTED_RPC_F_APREC_SSID:    ssid = g.dat; slen = g.dlen; break;
				case HOSTED_RPC_F_APREC_RSSI:    rp_conn_rssi = (int32_t) g.val; break;
				case HOSTED_RPC_F_APREC_PRIMARY: rp_conn_chan = (uint32_t) g.val; break;
				default: break;
				}
				q += u2;
				l2 -= u2;
			}
			while ((slen > 0U) && (ssid != NULL) && (ssid[slen - 1U] == 0U)) { slen--; }
			/*  **SSID/BSSID の値は出さない**（AC §5-2 R4a-2）  */
			rp_connected = (ssid != NULL) && (slen == strlen(WIFI_STA_SSID))
						   && (memcmp(ssid, WIFI_STA_SSID, slen) == 0);
			pbeg();
			ps("RPROBE R4-6 connected_ap ");
			pkv("ssid_match", (uint32_t)(rp_connected ? 1U : 0U));
			pkv("rssi_neg", (uint32_t)((rp_conn_rssi < 0)
									   ? (uint32_t)(-rp_conn_rssi) : 0U));
			pkv("chan", rp_conn_chan);
			pnl();
		}
		p += used;
		left -= used;
	}
	return(rp_connected);
}

/*
 *  ----------------------------------------------------------------------------
 *  接続の完了を待つ（**切断イベントが来たら撃ち直す**）
 *  ----------------------------------------------------------------------------
 *  【段7d で伸ばした】20 回 x 1 秒では足りなかった（「1 回おきに落ちる」が
 *  20 秒では抜けない）。**待ちを段階的に伸ばして**合計 90 秒程度まで粘る。
 *  ここを「回数」ではなく「時間」で書くのは、電波の側の事情が秒で決まるから。
 *
 *  **切断イベントが来たら接続を撃ち直す。** ESP のアプリは例外なくこれを
 *  やる（`esp_wifi_connect()` は 1 回きりの試行で、失敗すると
 *  `STA_DISCONNECTED` が出てそのまま止まる）。**1 回撃って諦める判定は、
 *  電波状況で実行ごとにぶれる。**
 *
 *  【穴 14・2026-08-18 に関数へ切り出した】もとは `rpc_probe_main()` に
 *  直書きで、基準点をこの中で `seen_disc = rp_n_ev_disconnected` と
 *  取っていた。**そこが現行バグだった**:
 *
 *      connect を撃つ -> **10 秒待つ**（この間に切断イベントが来る）
 *                     -> ここで基準点を取る  ⇒ **来ていたイベントを呑む**
 *                     -> 以後 40 回訊くだけで、**撃ち直しは一度も走らない**
 *
 *  1 回目の接続が失敗する型（`reason=201`(NO_AP_FOUND) や
 *  `reason=202`(AUTH_FAIL)）は、まさにこの 10 秒の窓でイベントが来る。
 *  ⇒ **その起動は丸ごと失われる**（段7g の 5 回目の実測: `n_disc=1`・
 *  「再接続を撃つ」0 件・`k=40`・`R4a-4` FAIL。`.steering/
 *  20260818-p4-hole14-assoc-stability/README.md`）。
 *
 *  **`seen_disc` には「connect を撃つ直前の切断イベント数」を渡すこと。**
 *  切り出したもう 1 つの理由は、穴 14 の実験（`RPCPROBE_H14_LOOP`）が
 *  **同じ待ち方**を使うためである（複製すると測っている物がずれる）。
 */
bool
rp_wait_connected(uint32_t seen_disc, uint32_t *tries)
{
	bool		b_info = false;
	uint32_t	k;

	for (k = 0U; (k < 40U) && !b_info; k++) {
		b_info = rp_sta_get_ap_info();
		if (!b_info) {
			(void) rp_pump(4U, 0U);
			if (rp_n_ev_disconnected != seen_disc) {
				seen_disc = rp_n_ev_disconnected;
				pbeg(); ps("RPROBE R4a **再接続を撃つ** ");
				pkv("disc", rp_n_ev_disconnected);
				pkv("reason", rp_last_disc_reason); pnl();
				/*  切ってから張り直す（AP 側の残骸を落とす狙い）  */
				(void) rp_wifi_disconnect();
				(void) dly_tsk(1000U * 1000U);
				(void) rp_wifi_connect();
			}
			/*  待ちを段階的に伸ばす（1s -> 最大 4s）  */
			(void) dly_tsk((1000U + ((k < 12U) ? 0U : 3000U)) * 1000U);
		}
	}
	if (tries != NULL) { *tries = k; }
	return(b_info);
}

#if defined(P4HOSTED_NET)
/*
 *  ============================================================================
 *  段7d: netif の立ち上げ・DHCP・ping（R2 / R3）
 *  ============================================================================
 */


/*
 *  ---- R2-b: C6 の MAC を RPC で訊く ----
 *
 *  【段7e で直した現行バグ・本段の本丸】
 *
 *  要求の `mode` フィールドは **`wifi_interface_t`** である。出典の本流経路が
 *  そう扱っている:
 *
 *      netif_esp_hosted.c:116   esp_wifi_get_mac(WIFI_IF_STA, mac)
 *   -> esp_wifi_weak.c:176      esp_wifi_remote_get_mac(ifx, mac)
 *   -> esp_hosted_api.c:288     rpc_wifi_get_mac(mode, mac)
 *   -> rpc_wrap.c:849           req->u.wifi_mac.mode = mode;   （素通し）
 *
 *  `wifi_interface_t` は **`WIFI_IF_STA = 0` / `WIFI_IF_AP = 1`** であり、
 *  `wifi_mode_t`（`WIFI_MODE_NULL=0` / `WIFI_MODE_STA=1` / `WIFI_MODE_AP=2`）
 *  とは**別の enum**である。
 *
 *  段7d までここへ **1** を入れていた。旧コメントは「1 は WIFI_IF_STA」
 *  「`set_mode`/`set_config` も同じ 1 で接続できている」と書いていたが、**両方とも誤り**:
 *    - `set_mode` の 1 は `wifi_mode_t` の `WIFI_MODE_STA`（別の enum の 1）。
 *    - `set_config` は `iface` を**送っていない**（proto3 で省略＝0＝`WIFI_IF_STA`）。
 *  ⇒ 「同じ 1 で通っている」という支えは実在しなかった。
 *     `~/agents_playbook/README.md` の「マクロ名は誤読される」そのもの。
 *
 *  **1 を送ると SoftAP の MAC が返る。** それを netif の `hwaddr` に入れると、
 *  送信フレームの送信元 MAC が STA のものと食い違い、**AP は電波に出たフレームを
 *  中継しない**（＝段7d が観測した「スレーブまで届くのに電波に出ない」）。
 *
 *  **両方訊いて両方印字する**（差が出ることを実測で示すため）。netif に入れるのは
 *  `RP_MAC_IFACE`（既定 0 = `WIFI_IF_STA`）で選んだ方。
 *  `-DA1_P4_HOSTED_MAC_IFACE=1` が **E1-c の negative control**（7d の挙動へ戻す）。
 */
#ifndef RP_MAC_IFACE
#define RP_MAC_IFACE	0U		/* WIFI_IF_STA（出典の本流経路と同じ） */
#endif

static bool
rp_get_mac_iface(uint32_t iface, uint8_t mac[6], const char *tag)
{
	uint8_t		body[8];
	uint32_t	blen = 0U;
	bool		got = false;

	/*
	 *  **field 番号を書き写さない**（`HOSTED_RPC_F_GET_MAC_REQ_MODE` は出典の
	 *  生成コードから機械抽出したもの。段7d で抽出器へ足した）。
	 *  **値 0 は proto3 で省略される**——「送らない」が「0 を送る」と同義である。
	 */
	if (iface != 0U) {
		blen = pb_add_varint(body, blen,
							 (uint32_t) HOSTED_RPC_F_GET_MAC_REQ_MODE, iface);
	}

	if (!rp_rpc_call((uint32_t) HOSTED_RPC_ID_REQ_GET_MAC,
					 (uint32_t) HOSTED_RPC_ID_RESP_GET_MAC,
					 body, blen, 40U, tag)) {
		return(false);
	}
	if (rp_resp_status((uint32_t) HOSTED_RPC_F_GET_MAC_RESP) != 0) {
		return(false);
	}

	/*  応答の `mac` は BYTES（**長さも生成物どおり 6 であることを確かめる**）  */
	{
		const uint8_t	*p = rp_body;
		uint32_t		left = rp_body_len;

		while (left > 0U) {
			struct pb_field	f;
			uint32_t		used;

			if (pb_next(p, left, &f, &used) != PB_OK) { break; }
			if ((f.fnum == (uint32_t) HOSTED_RPC_F_GET_MAC_MAC)
					&& (f.wt == PB_WT_LEN) && (f.dlen == 6U)) {
				memcpy(mac, f.dat, 6U);
				got = true;
			}
			p += used;
			left -= used;
		}
	}
	return(got);
}

static void
rp_put_mac(const uint8_t mac[6])
{
	int		i;
	static const char	hex[] = "0123456789abcdef";

	for (i = 0; i < 6; i++) {
		target_fput_log(hex[(mac[i] >> 4) & 0xF]);
		target_fput_log(hex[mac[i] & 0xF]);
		if (i != 5) { target_fput_log(':'); }
	}
}

bool
rp_get_mac(void)
{
	uint8_t		mac_if0[6], mac_if1[6];
	bool		ok0, ok1;

	memset(mac_if0, 0, sizeof(mac_if0));
	memset(mac_if1, 0, sizeof(mac_if1));

	ok0 = rp_get_mac_iface(0U, mac_if0, "get_mac_if0");
	ok1 = rp_get_mac_iface(1U, mac_if1, "get_mac_if1");

	/*  **両方印字する。**「同じか違うか」を実測で見せるため（MAC は秘密ではない）  */
	pbeg(); ps("RPROBE R2-b mac_by_iface ");
	ps("if0(WIFI_IF_STA)="); if (ok0) { rp_put_mac(mac_if0); } else { ps("**取得失敗**"); }
	ps(" if1(WIFI_IF_AP)="); if (ok1) { rp_put_mac(mac_if1); } else { ps("**取得失敗**"); }
	target_fput_log(' ');
	pkv("differ", (uint32_t)((ok0 && ok1
			&& (memcmp(mac_if0, mac_if1, 6) != 0)) ? 1U : 0U));
	ps("(MAC は秘密ではないので値を出す)");
	pnl();

	if (RP_MAC_IFACE == 0U) {
		if (!ok0) {
			pbeg(); ps("RPROBE R2-b get_mac **if0 が取れない**"); pnl();
			return(false);
		}
		p4hosted_net_set_mac(mac_if0);
	}
	else {
		if (!ok1) {
			pbeg(); ps("RPROBE R2-b get_mac **if1 が取れない**"); pnl();
			return(false);
		}
		p4hosted_net_set_mac(mac_if1);
	}

	pbeg(); ps("RPROBE R2-b sta_mac ");
	{
		uint8_t		used[6];

		(void) p4hosted_net_get_mac(used);
		rp_put_mac(used);
	}
	target_fput_log(' ');
	pkv("iface_used", (uint32_t) RP_MAC_IFACE);
	ps("(0=WIFI_IF_STA が出典の本流。1 は段7d の挙動＝negative control)");
	pnl();
	return(true);
}

/*
 *  ---- 段 7f: netif の立ち上げ・DHCP・ping・TCP は**ここから出た** ----
 *
 *  移動先: `esp/p4hosted/net/p4hosted_netops.c`（宣言は `net/p4hosted_netops.h`）。
 *  移したもの: `rp_put_ip` / 受信ポンプのスレッド / `rp_ping` /
 *              `rp_send_raw_arp` / `rp_net_bringup`。
 *  **ここに残したのは判定と、判定に使う RPC**（上の `rp_get_mac*`）である。
 *  probe は「期待値を書き、返ってきた数と突き合わせ、PASS/FAIL を数える」枠に戻した。
 */
#endif /* P4HOSTED_NET */
/*
 *  切り出しに際して足した 2 つ目（かつ最後）の関数。出典ではプローブの task が
 *  接続後に p4hosted_net_bind_xport(&rp_net_xport) を直接呼んでいた。
 *  rp_net_xport は static のままにしておきたいので、束ねる操作だけ公開する。
 */
void
p4hosted_rpc_bind_xport(void)
{
	p4hosted_net_bind_xport(&rp_net_xport);
}
