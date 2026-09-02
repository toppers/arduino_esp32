/*
 *  ⚠ [孤児 / CMake 未対応] classic 全廃(2026-07-20)で本シムの consumer（BT Classic
 *     SPP=W3 の seam ビルド）は削除済み。復元は branch archive/trb-classic-cfg 参照（B-2/P-2）。
 */
/*
 *  arpa/inet.h（W3④ SPP本体用の優先シム）
 *
 *  背景：BlueDroidの共通ヘッダ common/bt_defs.h が冒頭で
 *  `#include <arpa/inet.h>` を行う（bt_defs.h は btm_api.h/bta_api.h 等
 *  経由でホスト側ほぼ全ファイル＝実測85ファイルに transitively 波及する）。
 *  ESP-IDF 本流では arpa/inet.h は lwip の POSIX 互換ラッパ
 *  （asp3_esp_idf/lwip/src/include/compat/posix/arpa/inet.h）で解決され、
 *  その実体は `#include "lwip/sockets.h"`（lwip ソケットスタック一式）を
 *  引き込む。本ポートの BT Classic ビルドは lwip ソケット層を必要とせず
 *  （Wi-Fi 非使用の BT 単独構成）、85ファイルすべてに lwip/sockets.h を
 *  展開するのは過剰かつ型/マクロ衝突リスクがある。
 *
 *  一方、BlueDroid が arpa/inet.h から実際に使うのはバイトオーダ変換
 *  （grep 実測：ntohs 7箇所・ntohl 4箇所のみ。htons/htonl/inet_* は
 *  未使用）だけである。そこで sys/errno.h シムと同じく本ディレクトリ
 *  （esp/bt/classic/libc_override、probe_bluedroid_classic_esp32.sh の
 *  -I 検索順で最優先）に最小 arpa/inet.h を置き、ネットワークバイト
 *  オーダ変換関数のみを供給する。ESP32(LX6) はリトルエンディアンのため
 *  16/32bit いずれもバイトスワップとなる（__builtin_bswap を使用）。
 */
#ifndef TOPPERS_BT_SPP_ARPA_INET_H
#define TOPPERS_BT_SPP_ARPA_INET_H

#include <stdint.h>

/*
 *  ESP32 は little-endian 固定。host<->network(big-endian) 変換は
 *  16/32bit ともバイトスワップ。BlueDroid が使うのは ntohs/ntohl のみだが、
 *  対称性のため htons/htonl も供給する（いずれも #ifndef ガード）。
 */
#ifndef ntohs
#define ntohs(x)   ((uint16_t) __builtin_bswap16((uint16_t) (x)))
#endif
#ifndef htons
#define htons(x)   ((uint16_t) __builtin_bswap16((uint16_t) (x)))
#endif
#ifndef ntohl
#define ntohl(x)   ((uint32_t) __builtin_bswap32((uint32_t) (x)))
#endif
#ifndef htonl
#define htonl(x)   ((uint32_t) __builtin_bswap32((uint32_t) (x)))
#endif

#endif /* TOPPERS_BT_SPP_ARPA_INET_H */
