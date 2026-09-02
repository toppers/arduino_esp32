/*
 *  esp_vfs.h コンパイル用スタブ（W3④ BlueDroid SPP、btc_spp.c限定）
 *
 *  実ESP-IDFのVFS(Virtual File System)コンポーネントは本ポートには
 *  移植していない（`esp_vfs`コンポーネント自体がesp/shim配下の
 *  HAL checkoutに存在しない、asp3_esp_idf/hal/componentsにesp_vfsは
 *  無い）。btc_spp.cはSPP_MODE_VFS（POSIXファイルディスクリプタ経由の
 *  read()/write()）でのみこのAPIを使う。本ポートのSPP MVPスコープは
 *  ESP_SPP_MODE_CB（esp_spp_write/受信コールバック）単独のため、
 *  VFS関連関数は実際には呼ばれない想定（呼ばれた場合も安全に失敗を
 *  返すだけで、リンク/実行を破壊しない）。実体は
 *  esp/bt/classic/bluedroid_glue.cのスタブ実装を参照。
 */
#ifndef TOPPERS_BT_STUB_ESP_VFS_H
#define TOPPERS_BT_STUB_ESP_VFS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "esp_err.h"

#define ESP_VFS_FLAG_DEFAULT   0
#define ESP_VFS_FLAG_CONTEXT_PTR   1
#define ESP_VFS_FLAG_STATIC    2

typedef int esp_vfs_id_t;

/*  btc_spp.cが実際に設定するのはwrite/close/readの3フィールドのみ
 *  （grep確認済み）。本スタブは実使用分に限定する（フル版
 *  esp_vfs_fs_ops_tはopen/fstat/fcntl/ioctl/select等さらに多くの
 *  フィールドを持つが、本ポートはVFSを実装しないため不要）。 */
typedef struct {
	ssize_t (*write)(int fd, const void *data, size_t size);
	ssize_t (*read)(int fd, void *dst, size_t size);
	int (*close)(int fd);
} esp_vfs_fs_ops_t;

#if defined(TOPPERS_ESPIDF_SUPPLY)
/*  ESP-IDF v5.5.4 の btc_spp.c は旧VFS API（esp_vfs_t / esp_vfs_register_with_id）を
 *  使う（esp-hal-3rdparty master は新API esp_vfs_fs_ops_t / esp_vfs_register_fs_with_id）。
 *  本ポートはVFSモード未実装（CBモードのみ）のため、btc_spp_vfs_register() のコンパイルを
 *  通すための最小 esp_vfs_t を供給する。btc_spp.c は designated initializer で
 *  flags/write/read/open/close/fstat/fcntl のみ設定し、&vfs を stub の
 *  esp_vfs_register_with_id へ渡すだけ（未使用・gc-sections対象）。実体は bluedroid_glue.c。 */
struct stat;
typedef struct {
	int flags;
	ssize_t (*write)(int fd, const void *data, size_t size);
	ssize_t (*read)(int fd, void *dst, size_t size);
	int (*open)(const char *path, int flags, int mode);
	int (*close)(int fd);
	int (*fstat)(int fd, struct stat *st);
	int (*fcntl)(int fd, int cmd, int arg);
} esp_vfs_t;
extern esp_err_t esp_vfs_register_with_id(const esp_vfs_t *vfs, void *ctx, esp_vfs_id_t *vfs_id);
#endif /* TOPPERS_ESPIDF_SUPPLY */

#ifndef TOPPERS_MACRO_ONLY
extern esp_err_t esp_vfs_register_fs_with_id(const esp_vfs_fs_ops_t *vfs, int flags,
											  void *ctx, esp_vfs_id_t *vfs_id);
extern esp_err_t esp_vfs_unregister_with_id(esp_vfs_id_t vfs_id);
extern esp_err_t esp_vfs_register_fd(esp_vfs_id_t vfs_id, int *fd);
extern esp_err_t esp_vfs_unregister_fd(esp_vfs_id_t vfs_id, int fd);
#endif /* TOPPERS_MACRO_ONLY */

#endif /* TOPPERS_BT_STUB_ESP_VFS_H */
