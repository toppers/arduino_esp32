/*
 *  ⚠ [孤児 / CMake 未対応] classic 全廃(2026-07-20)で本シムの consumer（BT Classic
 *     SPP=W3 の seam ビルド）は削除済み。復元は branch archive/trb-classic-cfg 参照（B-2/P-2）。
 */
/*
 *  sys/errno.h（W3④ SPP本体用の優先シム）
 *
 *  背景：本ツールチェーン既定libc（picolibc）の sys/errno.h は
 *  PICOLIBC_TLS（picolibc.hが常時define）に従い errno を
 *  `extern __thread int errno;`（TLSモデル変数）として宣言する。一方、
 *  本プロジェクトの errno 実体は asp3_esp_idf/asp3/target/
 *  esp32c3_espidf/hal_stub/include/errno.h（`extern int errno;`、
 *  非TLSの単一グローバル変数）に統一されており、実体
 *  （esp/shim/esp_shim_libc.cの`int errno;`）も非TLSで定義されて
 *  いる。btc_spp.c（W3④ SPP本体、host/bluedroid/btc/profile/std/spp/
 *  btc_spp.c）だけが`#include <sys/errno.h>`（フラットな`<errno.h>`
 *  ではなく`sys/`付き）を使うため、hal_stubのerrno.hには`sys/errno.h`
 *  が無く素通りしてpicolibc本体のTLS版へ解決されてしまい、リンク時に
 *  「TLS reference ... mismatches non-TLS definition in
 *  objs/esp_shim_libc.o section .bss」で失敗する
 *  （2026-07-15 W3④セッションで発見・特定。詳細は
 *  非公開作業記録/20260714-esp32-classic-wifi-bt/steering.md参照）。
 *
 *  対策：本ディレクトリ（esp/bt/classic/libc_override）を
 *  probe_bluedroid_classic_esp32.shの-I検索順序で最優先に置き、
 *  `#include <sys/errno.h>`をpicolibc本体より先にここへ解決させる。
 *  hal_stubのerrno.hと同じ非TLS宣言を独立して再掲し（インクルード
 *  連鎖に頼らない＝picolibcのflat errno.hを再度踏んで別の解決結果に
 *  なることを避けるため）、btc_spp.cが実際に使うがhal_stubには無い
 *  エラーコード（ENOENT/ESRCH/EPIPE、grep確認済み。EBUSYはhal_stubに
 *  既存）だけを追加で補う。
 */
#ifndef TOPPERS_BT_SPP_SYS_ERRNO_H
#define TOPPERS_BT_SPP_SYS_ERRNO_H

extern int errno;

#ifndef ENOENT
#define ENOENT   2   /* No such file or directory */
#endif
#ifndef ESRCH
#define ESRCH    3   /* No such process */
#endif
#ifndef EBUSY
#define EBUSY   16   /* Device or resource busy */
#endif
#ifndef EPIPE
#define EPIPE   32   /* Broken pipe */
#endif

#endif /* TOPPERS_BT_SPP_SYS_ERRNO_H */
