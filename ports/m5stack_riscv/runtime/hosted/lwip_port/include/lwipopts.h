/*
 *  ESP32-P4 + M5Stack Stamp-AddOn C6 (ESP-Hosted) — lwIP コンフィギュレーション
 *  （FMP3 自前 lwIP ビルド方式(D)。NO_SYS=0）
 *
 *  方式(D): IDF の lwip/esp_netif コンポーネントを使わず、素の lwIP
 *  （~/TOPPERS/asp3_esp_idf/lwip, v2.2.1）を FMP3 ネイティブ port
 *  （wifi_p4_module/lwip_port/sys_arch.c + fmp3_lwip_pools.c）でビルドする。
 *  これにより「生 FreeRTOS の sys_arch がビルドに一切入らない」＝
 *  「xTaskCreate したタスクが FMP3 配下で走らない」問題が構造的に消滅する
 *  （経緯は esp_hosted_core/PROVENANCE.md の 2026-07-10 各節参照）。
 *
 *  本ファイルは esp32_s3 プロジェクト
 *  （~/TOPPERS/esp32_s3/wifi/net/port/include/lwipopts.h）を土台に、
 *  FMP3/lwip_port の設計（lwipopts_fmp3_notes.md）と矛盾しない値へ調整した。
 *
 *  【第一次ブリングアップ完了 → 第二段階: 実データ通信】
 *  第一次ブリングアップ（STA接続→DHCP→IP取得）は 2026-07-10 に実機達成
 *  （HANDOFF §27）。本段階では netconn/socket 層を有効化し、TCP/UDP の実
 *  データ通信（fmp_tcp_probe 等）を可能にする。socket 有効化に伴いプール
 *  （fmp3_lwip_pools.h の mbox/sem）を増量した（下記 §netconn/socket 参照）。
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/*
 *  =====================================================================
 *  OS 抽象層（sys_arch.c の実装前提。lwipopts_fmp3_notes.md「必須設定」）
 *  =====================================================================
 */
#define NO_SYS                      0
#define SYS_LIGHTWEIGHT_PROT        1   /* sys_arch_protect/unprotect を使う */
#define LWIP_TCPIP_CORE_LOCKING     0   /* tcpip_thread 経由の直列化 */
#define LWIP_COMPAT_MUTEX           0   /* real mutex（sys_mutex_*）を提供済み */
#define MEMP_MEM_MALLOC             0   /* memp プール方式（notes 推奨） */

/*
 *  =====================================================================
 *  tcpip_thread（＝lwIP唯一の常駐スレッド）の優先度・スタック
 *  =====================================================================
 *  【重要】fmp3_lwip_pools.c の sys_thread_new(prio) は写像・反転を行わず、
 *  prio をそのまま FMP3 の PRI（1=最高〜16=最低）として扱う。よって以下は
 *  FreeRTOS 流の値ではなく FMP3 PRI を直接書く（lwipopts_fmp3_notes.md）。
 *
 *  FMP3_LWIP_TCPIP_THREAD_PRI（tiT 相当の優先度）は本ファイルで 1 箇所だけ
 *  定義し、後から調整しやすくする（コーディネータ指示）。PRI=9 の根拠:
 *    - esp-hosted プールタスク（RX/transport 系, PRI≈8）より低い
 *      → SDIO 受信系が lwIP プロトコル処理に先行して走れる（RXバッファ滞留回避）
 *    - connect probe アプリ（WIFI_CONNECT_PROBE_PRIORITY=10）より高い
 *      → アプリが IP 取得待ちでブロックする間に tiT が走れる
 *  ※ esp-hosted 実タスクの実効優先度は実装時に JTAG/ログで確認し、想定と
 *     異なれば本値を調整する。
 */
#ifndef FMP3_LWIP_TCPIP_THREAD_PRI
#define FMP3_LWIP_TCPIP_THREAD_PRI  9
#endif

#define TCPIP_THREAD_PRIO           FMP3_LWIP_TCPIP_THREAD_PRI
#define DEFAULT_THREAD_PRIO         FMP3_LWIP_TCPIP_THREAD_PRI
/*  スタックサイズは sys_thread_new で無視され、fmp3_lwip_pools.h の
 *  FMP3_LWIP_THREAD_STACK_SIZE（既定4096）が使われる。ここの値は参考。
 *  4096 が tcpip_thread+DHCP に不足する場合は FMP3_LWIP_THREAD_STACK_SIZE を
 *  増やすこと（lwipopts_fmp3_notes.md）。 */
#define TCPIP_THREAD_STACKSIZE      4096
#define DEFAULT_THREAD_STACKSIZE    4096

/*
 *  =====================================================================
 *  メールボックス深さ（fmp3_lwip_pools.h の FMP3_LWIP_MBOX_MAX_ELEMS=32 以下）
 *  =====================================================================
 *  tcpip_thread 受信箱。wifi_rx_cb→tcpip_input() がここへ積む。satur 時に
 *  RX パケットを取りこぼすため DHCP 安定化のためやや深くする。
 */
#define TCPIP_MBOX_SIZE             16
#define DEFAULT_RAW_RECVMBOX_SIZE   6
#define DEFAULT_UDP_RECVMBOX_SIZE   6
#define DEFAULT_TCP_RECVMBOX_SIZE   6
#define DEFAULT_ACCEPTMBOX_SIZE     6

/*
 *  =====================================================================
 *  tcpip_thread 入力メッセージプール（MEMP_TCPIP_MSG_INPKT）
 *  =====================================================================
 *  【2026-07-10 実機診断で判明・重要】既定 8 では TCP 受信が不達になる。
 *  wifi_rx_cb（esp-hosted RX タスク, PRI≈8=高）が tcpip_input() でこのプールから
 *  msg を確保して tcpip mbox へ積み、tcpip_thread（PRI 9=低）が処理して解放する。
 *  RX タスクが高優先でバッチ受信するため、低優先の tcpip_thread が排出する前に
 *  8 深のプールを使い切り、tcpip_inpkt が memp_malloc 失敗で受信 TCP セグメントを
 *  黙って捨てる（tcp_input に届かず P4 が ACK せず、対向が再送地獄）。実機計装で
 *  「RXstat input_fail=14 / TRYPOST fail=0」＝mbox ではなくこの INPKT プール枯渇を
 *  確認済み（HANDOFF 該当節）。TCPIP_MBOX_SIZE(16) 以上にしてバーストを吸収する。 */
#define MEMP_NUM_TCPIP_MSG_INPKT    32

/*
 *  =====================================================================
 *  netconn / socket 層（第二段階: 実データ通信のため有効化）
 *  =====================================================================
 *  netconn/socket を 1 にすると api_lib/api_msg/sockets/netbuf/netdb/if_api
 *  （build_lwip_lib.sh のソース集合に既に含む。今まで #if LWIP_SOCKET/
 *  LWIP_NETCONN の自己ガードで空コンパイルだったもの）が実体化する。
 *
 *  【ビルド上の前提（socket 有効化でも変わらず）】fmp_loader の main に
 *  REQUIRES が無いため IDF は全コンポーネントをビルドしようとする。<sys/socket.h>/
 *  struct sockaddr を要求する不要コンポーネント（esp_http_client / mqtt 等）は
 *  build script の EXCLUDE_COMPONENTS で刈る（HANDOFF §26/§27 参照）。socket
 *  有効化後もこの前提は不変（我々の lwip が posix 型を供給するのは lwip_port を
 *  参照する自コードのみで、IDF 側コンポーネントは対象外）。
 *
 *  【struct timeval を要求しない工夫（S3 と同方式）】SO_RCVTIMEO/SO_SNDTIMEO は
 *  LWIP_SO_SNDRCVTIMEO_NONSTANDARD=1 で int(ミリ秒)指定にし、sockets.h が
 *  struct timeval / <sys/time.h> に依存する経路を避ける。 */
#define LWIP_NETCONN                1
#define LWIP_SOCKET                 1
#define MEMP_NUM_NETCONN            4   /* 同時 netconn 数（S3 と同値） */

/*  SO_RCVTIMEO/SO_SNDTIMEO を int(ミリ秒)指定にする（struct timeval 非依存）。
 *  recv/send の無限ブロック回避（テストアプリのタイムアウト用）。 */
#define LWIP_SO_RCVTIMEO                 1
#define LWIP_SO_SNDTIMEO                 1
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD  1

/*
 *  =====================================================================
 *  ヒープ・pbuf プール
 *  =====================================================================
 *  MEM_SIZE: lwIP 内部の可変長確保（dhcp 構造体・raw pcb・一時バッファ等）。
 *  PBUF_POOL: RX 用。wifi_rx_cb() は pbuf_alloc(PBUF_POOL) 失敗時にパケットを
 *  破棄するため、起動時トラフィックバースト中の枯渇で DHCP OFFER/ACK が落ちる。
 *  P4 は RAM に余裕があるため S3(C3) と同等〜やや大きめにする。
 */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)
/*  【2026-07-10 実機診断】RX バーストで pbuf_alloc(PBUF_POOL) が枯渇し受信 TCP を
 *  取りこぼす（MEMP_TCPIP_MSG_INPKT を 32 にした後、ボトルネックがここへ移った＝
 *  カスケード枯渇。RXstat pbuf_fail=7 で確認）。高優先の esp-hosted RX タスクが
 *  低優先の tcpip_thread より速く RX 経路を埋めるため、バースト吸収用に増量する。 */
#define PBUF_POOL_SIZE              48
#define PBUF_POOL_BUFSIZE           1600
#define MEMP_NUM_PBUF               24

#define MEMP_NUM_UDP_PCB            5   /* DHCP(1) + 予備 */
#define MEMP_NUM_RAW_PCB            3   /* ping(gw疎通確認) + 予備 */
#define MEMP_NUM_SYS_TIMEOUT        10  /* dhcp / etharp / tcp 等の内部タイマ */

/*
 *  =====================================================================
 *  プロトコル機能（IPv4 のみ。DHCP 有効）
 *  =====================================================================
 */
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0

#define LWIP_ICMP                   1
#define LWIP_RAW                    1   /* ping(gw疎通) 用に残す */
#define LWIP_UDP                    1
#define LWIP_TCP                    1   /* 後段のデータ通信用に残す（コスト小） */
#define MEMP_NUM_TCP_PCB            3
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG           16
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)

#define LWIP_DHCP                   1
#define LWIP_AUTOIP                 0
#define LWIP_IGMP                   0
#define LWIP_DNS                    0

/*
 *  =====================================================================
 *  チェックサム（esp-hosted/C6 はホストスタック向けオフロードをしない
 *  ＝ソフトウェアで全て計算/検証する）
 *  =====================================================================
 */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1

/*
 *  =====================================================================
 *  netif ステータスコールバック（DHCP 完了検出をポーリング無しで行う。
 *  netif_esp_hosted.c の netif_status_cb 参照）
 *  =====================================================================
 */
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_NETIF_HOSTNAME         0

/*
 *  =====================================================================
 *  errno（newlib の <errno.h> にフォールバック。S3 と同方式）
 *  =====================================================================
 */
#define LWIP_ERRNO_STDINCLUDE       1

/*  struct timeval は newlib の <sys/time.h> のものを使う（lwip 独自定義を
 *  抑止）。IDF lwip port と同じ設定。これをしないと lwip/sockets.h が
 *  struct timeval を再定義し、<sys/time.h> を含む他コンポーネント
 *  （mbedtls 等）と衝突する。 */
#define LWIP_TIMEVAL_PRIVATE        0

/*
 *  =====================================================================
 *  統計・デバッグ
 *  =====================================================================
 *  TCP エコー受信不達の原因診断（SDIO DAT1 割込みのロストウェイクアップ、
 *  修正済み）で使用した一時計装は撤去済み。経緯と結果は
 *  lwip_port/tcp_echo_recv_diagnosis.md を参照。
 */
#define LWIP_DEBUG                  0
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define LWIP_STATS                  0
#define TCP_STATS                   0
#define MEMP_STATS                  0

#endif /* LWIP_LWIPOPTS_H */
