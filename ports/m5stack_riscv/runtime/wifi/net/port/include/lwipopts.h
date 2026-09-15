/*
 *  lwIP コンフィギュレーション（ASP3／ESP32-C3．BSDソケット互換化）
 *
 *  NO_SYS=0．lwIP自身が生成する唯一のスレッド（tcpip_thread）を
 *  cfg生成のNET_TSK（port/sys_arch.c参照）に割り当てる．
 *  LWIP_TCPIP_CORE_LOCKING=0＝メッセージパッシングモデル（ソケット呼出し
 *  は各アプリタスク文脈でmboxにメッセージを積みop_completedセマフォで
 *  待つ．core全体を保護する巨大ミューテックスは使わない．必要な
 *  セマフォ／ミューテックスの数が少なく済む）．
 *  docs/tcpip-integration.md は**存在しない**（2026-07-22 実測。git 履歴にも一度も無い。外部指摘 `../esp32_s31/docs/reference_repo_issues.md` S3-1）。設計・経緯は `esp/debug/JTAG_DEBUG.md` と `docs/status.md` の Wi-Fi/lwIP 節を参照．
 *
 *  個別ファイルは全て自己ガード（#if LWIP_XXX）されているため，
 *  Filelists.cmakeの lwipcore_SRCS／lwipcore4_SRCS／lwipapi_SRCS を
 *  まるごと採用し，ここでの機能フラグでコンパイル内容を絞る
 *  （IPv6・PPP・6LoWPANは非採用＝ソースリスト自体に含めない）．
 */
#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/*
 *  NO_SYS=0（BSDソケット／netconn API．tcpip_thread経由のメッセージ
 *  パッシング）．sys_now()／sys_arch_protect/unprotect／セマフォ・
 *  メールボックス・スレッド生成はport/sys_arch.cで実装する．
 */
#define NO_SYS                      0
#define SYS_LIGHTWEIGHT_PROT        1
#define LWIP_TCPIP_CORE_LOCKING     0
#define LWIP_COMPAT_MUTEX           1

/*
 *  tcpip_thread自身の受信箱および各netconnのrecv/acceptメールボックス
 *  の深さ．port/net_cfg.hのSYS_ARCH_MBOX_DEPTH（プール共通深さ）以下
 *  にすること．
 */
/*  tcpip_thread受信箱．wifi_rx_cb→tcpip_input()がここに積む．満杯だと
 *  tcpip_inputが失敗しRXパケットを破棄するため、DHCP安定化のため深くする
 *  （net_cfg.hのSYS_ARCH_MBOX_DEPTH以下であること）．*/
#define TCPIP_MBOX_SIZE             12
#define DEFAULT_RAW_RECVMBOX_SIZE   6
#define DEFAULT_UDP_RECVMBOX_SIZE   6
#define DEFAULT_TCP_RECVMBOX_SIZE   6
#define DEFAULT_ACCEPTMBOX_SIZE     6

/*
 *  ソケット／netconn（BSD互換．LWIP_COMPAT_SOCKETSは既定1のため
 *  socket()/connect()/send()/recv()等の標準名がそのまま使える）
 */
#define LWIP_NETCONN                1
#define LWIP_SOCKET                 1
#define MEMP_NUM_NETCONN            4

/*
 *  SO_RCVTIMEO（tcp_socket_clientがrecv()をブロックしっぱなしに
 *  しないため）．NONSTANDARD=1でint（ミリ秒）指定にし，struct
 *  timeval／sys/time.hへの依存を避ける．
 */
#define LWIP_SO_RCVTIMEO            1
#define LWIP_SO_SNDTIMEO           1	/* スループット計測でlwip_sendの無限ブロック回避 */
#define LWIP_SO_SNDRCVTIMEO_NONSTANDARD  1

/*
 *  TCPスループット向上設定。既定はTCP_MSS=536・小ウィンドウで低速なため、
 *  Ethernet MSS(1460)＋大きめウィンドウ/送信バッファにする。TCP_SND_BUFは
 *  MEM_SIZE(可変長確保)から取るためMEM_SIZEと整合させること。
 */
#define TCP_MSS                     1460
#define TCP_WND                     (6 * TCP_MSS)	/* 受信ウィンドウ 8760 */
#define TCP_SND_BUF                 (6 * TCP_MSS)	/* 送信バッファ 8760 */
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)
#define MEMP_NUM_TCP_SEG            24

/*
 *  errno（hal_stub/include/errno.hへ委譲．lwip/src/include/lwip/
 *  errno.hがLWIP_ERRNO_STDINCLUDE経由で<errno.h>にフォールバックする
 *  仕組みを利用．詳細はerrno.h先頭コメント参照）
 */
#define LWIP_ERRNO_STDINCLUDE       1
/*
 *  arduino_esp32 (2026-09-15, stage 4 Task 0): ERANGE for netdb.c.
 *  With LWIP_DNS 1 (and LWIP_SOCKET 1) lwip/src/api/netdb.c compiles
 *  lwip_gethostbyname_r(), which uses ERANGE. The flat errno.h stub that
 *  <errno.h> resolves to here (hal_stub_include/errno.h, "add missing
 *  values when needed") does not define it, and the dev build script's
 *  include path is not ours to change from this repository, so the value
 *  is supplied from the one file both the archive build and the stage
 *  TUs read. 34 is the Linux/newlib number, the same numbering the stub
 *  uses for its other values. The guard keeps a real <errno.h> in charge
 *  wherever one is on the include path (the stage's stub is included
 *  first there and lacks ERANGE, so the guard is not taken today).
 */
#ifndef ERANGE
#define ERANGE                      34  /* Math result not representable */
#endif

/*
 *  ヒープ・プール
 *
 *  MEM_SIZEはlwIP内部の可変長確保（raw pcb・dhcp構造体・ソケット層の
 *  一時バッファ等）用．PBUF_POOL_BUFSIZEは1イーサネットフレーム
 *  （MTU 1500＋ヘッダ）を1個のpoolバッファに収める値．
 *  ESP32-C3のRAMはWi-Fi blob側の静的ヒープ（192KB．esp_shim_cfg.h）
 *  と共存するため小さめに抑える．
 */
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (16 * 1024)
/*  RX pbufプール．wifi_rx_cb()はpbuf_alloc(PBUF_POOL)失敗時にパケットを破棄する
 *  ため、起動時のトラフィックバースト(beacon/ARP/broadcast+DHCP)中にプールが
 *  枯渇するとDHCP OFFER/ACKが落ちて間欠的にDHCPタイムアウトする．4→10に増量．*/
#define PBUF_POOL_SIZE              10
#define PBUF_POOL_BUFSIZE           1600
#define MEMP_NUM_PBUF               8

#define MEMP_NUM_RAW_PCB            3	/* ping(1) + 予備 */
/*  UDP PCB: DHCPクライアント(1) + udpecho_rawサーバ(1) + アプリのUDPソケット用
 *  余裕。2だとDHCP+udpechoで満杯になりアプリのlwip_socket(SOCK_DGRAM)が
 *  ENOBUFS(errno=105)で失敗する（実機確認）。 */
#define MEMP_NUM_UDP_PCB            5
/*
 *  sys_timeout pool. arduino_esp32 (2026-09-15, stage 4 Task 0): 8 -> 9.
 *  The dev value 8 covered the 6 cyclic timers this configuration had with
 *  LWIP_DNS 0 (tcp, ip_reass, etharp, dhcp coarse/fine, acd; timeouts.c
 *  lwip_num_cyclic_timers = 6) plus the ping chain that netif_esp32s3.c
 *  starts once, with one slot to spare. LWIP_DNS 1 adds dns_tmr as a 7th
 *  cyclic timer; 9 keeps the same one-slot margin. Exhausting this pool is
 *  an LWIP_ASSERT in sys_timeout() (the port's assert handler parks the
 *  tcpip thread), so the margin is not optional.
 */
#define MEMP_NUM_SYS_TIMEOUT        9

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0

#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_UDP                    1

/*
 *  TCP（tcpecho_raw＝ポート7のraw APIエコーサーバ）．
 *
 *  2026-07-22 訂正（外部からの指摘。`../esp32_s31/docs/reference_repo_issues.md` S3-3）:
 *  本コメントは長らく「＋tcp_socket_echo＝ポート8のBSDソケットAPIエコーサーバ．
 *  **両方が同時にlistenするため**MEMP_NUM_TCP_PCB_LISTENは最低2必要」と書いていたが、
 *  **`tcp_socket_echo` はリポジトリのどこにも実装が無い**（全 .c/.h を grep して 0 件）。
 *  ESP32-C3 由来の残骸と思われる（`tcpecho_raw` のほうは wifi_sta.c 等に実在する）。
 *  ⇒ **値 2 の根拠として書かれていた説明が実態とズレていた。**
 *  値 2 自体は変更しない（下げる根拠が無く、下げれば将来 listen を 2 本張ったときに
 *    ERR_MEM で失敗する。「実機で確認済」の実験そのものは行われたと思われる）。
 *  現在の実態としては「listen は 1 本だが、2 本目に備えて 2 を確保している」が正しい。
 *
 *  プールは小さめに抑える（RAM予算）．
 */
#define LWIP_TCP                    1
#define MEMP_NUM_TCP_PCB            3
#define MEMP_NUM_TCP_PCB_LISTEN     2

#define LWIP_DHCP                   1
#define LWIP_AUTOIP                 0
#define LWIP_IGMP                   0
/*
 *  arduino_esp32 (2026-09-15, stage 4 Task 0): LWIP_DNS 0 -> 1. This is the
 *  one deviation from the dev repository's lwipopts.h (R12 exception in
 *  IMPORT_PROVENANCE.md). The same file configures both the prebuilt
 *  liblwip.a (dev build_lwip_lib_espidf_esp32c6.sh with PORT_EXTRA pointing
 *  here) and the stage TUs that include lwip headers; the two must not
 *  disagree (docs/c6-port.md, stage 3 "entry conditions for stage 4").
 *  DNS_TABLE_SIZE (4), DNS_MAX_SERVERS (2), DNS_MAX_RETRIES (4) and
 *  LWIP_DNS_SECURE (random XID + random source port + no multiple
 *  outstanding queries per name) stay at the lwIP defaults. LWIP_RAND is
 *  esp_shim_random() (arch/cc.h), not the ROM rand(). With LWIP_DHCP 1 the
 *  DHCP client requests option 6 and calls dns_setserver() itself
 *  (dhcp.c, LWIP_DHCP_PROVIDE_DNS_SERVERS), so no manual server setup.
 */
#define LWIP_DNS                    1

/*
 *  ソフトウェアチェックサム（esp_wifiはホストスタック向けの
 *  チェックサムオフロードを行わない）
 */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1

#define LWIP_STATS                  0

/*
 *  netifステータス変化（DHCP完了等）のコールバック．ポーリング不要で
 *  IPアドレス取得を検出するために使用（netif_esp32c3.c参照）．
 */
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    0
#define LWIP_NETIF_HOSTNAME         0

/*
 *  contrib/apps/ping．PING_USE_SOCKETSは既定でLWIP_SOCKET追従だが，
 *  ここでは明示的に0固定＝raw API経路に留める．sys_thread_new()は
 *  port/sys_arch.cが「生涯に一度だけ（tcpip_thread用）」しか想定して
 *  いないため，ping_thread()（ソケット版．独自にsys_thread_newする）
 *  を使うと壊れる．
 */
#define PING_USE_SOCKETS            0

#ifdef __cplusplus
extern "C" {
#endif
extern void net_ping_result(int ok);
#ifdef __cplusplus
}
#endif
#define PING_RESULT(ping_ok)  net_ping_result(ping_ok)

#endif /* LWIP_LWIPOPTS_H */
