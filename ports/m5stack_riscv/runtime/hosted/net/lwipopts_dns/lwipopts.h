/*
 *  ESP32-P4 + ESP-Hosted — lwIP 設定の**重ね書き**: DNS だけを有効にする（段 7g・W2）
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 *
 *  ============================================================================
 *  なぜ「重ね書き」なのか（**土台の lwipopts.h を編集しない理由**）
 *  ============================================================================
 *  土台は `esp/eth/lwip_port/include/lwipopts.h` で、これは**取込み物**である:
 *
 *    - 出典 P4 repo `wifi_p4_module/lwip_port/include/lwipopts.h` と**バイト同一**
 *    - `esp/eth/IMPORT_PROVENANCE.md`（表の 30 行目）に 225 行・sha256
 *      `4ef55749…`・改変「無」として登録されている
 *    - `cmake/a1_p4_eth_import_audit.sh` がその sha256 を毎回照合する
 *
 *  よって `#define LWIP_DNS 0` を `1` に書き換えると、**監査が落ちる**。
 *  通すには `IMPORT_PROVENANCE.md` の sha256 を録り直すことになるが、
 *  **録り直しは退行を隠す装置になり得る**（段 7b -> 7f §7 が繰り返し述べている形。
 *  「意図した 1 文字の変更」と「同時に紛れ込んだ乖離」が、機械には同じ 1 個の
 *  sha256 の差にしか見えない）。⇒ **取込み物には 1 バイトも触らない。**
 *
 *  代わりに、**探索路の手前**にこのファイルを置く。lwIP の `opt.h` は
 *  `#include "lwipopts.h"` としか書かないので、`-I` の順序でこちらが先に
 *  見つかる。こちらは土台を**相対パスで名指しして取り込み**、そのあとで
 *  `LWIP_DNS` だけを上書きする。⇒ **土台の 224 行はそのまま効く。**
 *
 *  ============================================================================
 *  使い方（**2 箇所で同じヘッダを使わないと壊れる**）
 *  ============================================================================
 *  lwIP の設定は「アプリのコンパイル」と「`liblwip.a` のコンパイル」の**両方**に
 *  効く（`memp.c` のプール表や `dns.c` の実体は .a 側に焼かれる）。片方だけに
 *  重ね書きを効かせると、**ヘッダ上は DNS が在るのにライブラリに実体が無い**
 *  という、リンクエラーで気づくならまだよい、最悪は無言で壊れる形になる。
 *  ⇒ 次の 2 つを**必ず対にする**:
 *
 *    (1) `liblwip.a`:
 *          PORT_EXTRA=$PWD/esp/p4hosted/net/lwipopts_dns \
 *          OUT_DIR=$PWD/esp/lib/lwip_esp32p4_espidf_dns \
 *            bash esp/boot/build_lwip_lib_espidf_esp32p4.sh
 *    (2) アプリ: `-DA1_P4_HOSTED_DNS=ON`
 *        （`cmake/a1_p4_stage1.cmake` が同じディレクトリを探索路の手前に置き、
 *          `esp/lib/lwip_esp32p4_espidf_dns/liblwip.a` をリンクする。
 *          **無ければ FATAL_ERROR で止まる**——「DNS が効かない .a を黙って使う」
 *          形を作らない）
 *
 *  ============================================================================
 *  数の確認（**足りるか勘で決めない**）
 *  ============================================================================
 *  - `MEMP_NUM_UDP_PCB` = 5（土台 :140）。使うのは DHCP 1 ＋ DNS 1 ＋ 本段の
 *    UDP スモーク 1 = **3**。足りる。
 *  - `MEMP_NUM_SYS_TIMEOUT` = 10（土台 :142）。lwIP が要求する内部タイマ数は
 *    `LWIP_NUM_SYS_TIMEOUT_INTERNAL`（`opt.h:501`）＝
 *    TCP(1) ＋ IP_REASSEMBLY(1) ＋ ARP(1) ＋ 2*DHCP(2) ＋ ACD(0: `DHCP_DOES_ARP_CHECK`
 *    が 1 なので項ごと落ちる) ＋ IGMP(0) ＋ **DNS(1)** = **6**。足りる。
 *    （足りなければ lwIP 自身が `#error` で止まる——fail-closed である）
 *  - `DNS_MAX_SERVERS` / `DNS_TABLE_SIZE` は lwIP 既定（2 / 4）のまま。
 *    DHCP が配った DNS サーバは `dhcp.c:813` の `dns_setserver()` で自動登録される
 *    （`LWIP_DHCP_PROVIDE_DNS_SERVERS`。同 :175-179）。
 */
#ifndef P4HOSTED_LWIPOPTS_DNS_H
#define P4HOSTED_LWIPOPTS_DNS_H

/*
 *  土台を**相対パスで**取り込む（自分のディレクトリ基準。`#include_next` は
 *  探索路の並び順に依存して読みにくいので使わない）。
 */
#include "../../../eth/lwip_port/include/lwipopts.h"

/*
 *  ここから重ね書き。**変えるのは 1 個だけ**である。
 *  土台は `#define LWIP_DNS 0`（`esp/eth/lwip_port/include/lwipopts.h:169`）。
 */
#undef LWIP_DNS
#define LWIP_DNS                    1

/*
 *  ----------------------------------------------------------------------------
 *  `LWIP_RAND`（**DNS を入れて初めて要ることが判った**。段 7g の実測）
 *  ----------------------------------------------------------------------------
 *  `LWIP_DNS=1` だけにして建てたら、`dns.c:120` が
 *  `implicit declaration of function 'LWIP_RAND'` で落ちた。
 *  ⇒ **DNS は乱数源を要求する**（問合せ ID `txid` とソースポートの無作為化。
 *    同じ値を使い回すと、経路上の第三者が応答を偽装しやすくなる）。
 *
 *  土台の lwipopts.h には `LWIP_RAND` が無い（DNS が OFF だったので要らなかった）。
 *  ESP-IDF の port は `esp_random`（`esp-idf/components/lwip/port/include/lwipopts.h:86`）を
 *  差しているが、**本 repo の P4 hosted 構成は `esp_hw_support` をリンクしていない**
 *  ので、その名前は解決しない。⇒ **自前の 1 個を差す。**
 *
 *  実装は `esp/p4hosted/net/p4hosted_dns.c`。**暗号論的に安全ではない**
 *  （そう書いてある）。ここで要るのは「毎回同じ値にならない」ことまでである。
 */
#ifndef __ASSEMBLER__
#include <stdint.h>
extern uint32_t	p4hosted_lwip_rand(void);
#endif
#define LWIP_RAND()                 (p4hosted_lwip_rand())

#endif /* P4HOSTED_LWIPOPTS_DNS_H */
