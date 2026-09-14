/*
 *  seam-C6（ESP32-C6 / RV32）: esp_app_desc スタブ。
 *
 *  段2（2026-09-13）。esp/boot/seam_p4_appdesc.c（P4 版）と同じ役割の C6 版で、
 *  構造体レイアウトは共通（esp_app_desc_t の ABI はチップ非依存）だが、
 *  version/project_name は C6 を名乗る（別ファイルにする理由も P4 版と同じ:
 *  seam-c6 が別チップの名前を名乗ると採取ログの読み手を誤らせる）。
 *
 *  【なぜ要るか（一次資料）】
 *  esp-idf v5.5.4 の esp_image_format.c:767-777 は、`#if !CONFIG_IDF_TARGET_ESP32`
 *  の下で **イメージの segment #0 の先頭を esp_app_desc_t として読み**、
 *  bootloader_common_check_efuse_blk_validity(min_efuse_blk_rev_full,
 *  max_efuse_blk_rev_full) で efuse ブロックリビジョンを検証する。
 *  FMP3 イメージが有効な app_desc を持たないと、そこにあった任意のバイト列が
 *  efuse 要件として読まれて起動が拒否される（S3/P4 で実際に踏んだ既知の罠）。
 *
 *  min_efuse_blk_rev_full=0    -> IS_FIELD_SET が偽になり min チェックを skip
 *  max_efuse_blk_rev_full=0xFFFF -> max チェックは必ず通る
 *  mmu_page_size=0             -> C6 は SOC_MMU_PAGE_SIZE_CONFIGURABLE を
 *                                 定義する（esp32c6/soc_caps.h）ため、この
 *                                 フィールドは esp_image_format.c:834-862 の
 *                                 `#if SOC_MMU_PAGE_SIZE_CONFIGURABLE` 枝で
 *                                 実際に読まれる。0 は「bootloader の既定
 *                                 （CONFIG_MMU_PAGE_SIZE=0x10000＝Task 2 実測）
 *                                 に従う」を意味する（同ファイルの
 *                                 `mmu_page_size == 0` 特別扱い）。もし
 *                                 実測ページサイズが 0x10000 以外なら
 *                                 log2(page) を明示的に入れる必要がある。
 *
 *  配置は fmp3/target/m5nanoc6_gcc/esp32c6_xip.ld が `.flash.appdesc` という
 *  **名前**のセクションへ KEEP(*(.appdesc)) で置く。esptool は
 *  `.flash.appdesc` という名前のセクションを segment #0 の先頭へ特別扱いする
 *  （esptool bin_image.py の `.flash.appdesc` 名前一致処理。段2 実装計画
 *  .steering/20260913-c6-fmp3-wifi-plan/PLAN-stage2-impl.md Task 3 参照）。
 */
#include <stdint.h>

struct fmp_esp_app_desc {
	uint32_t magic_word;
	uint32_t secure_version;
	uint32_t reserv1[2];
	char     version[32];
	char     project_name[32];
	char     time[16];
	char     date[16];
	char     idf_ver[32];
	uint8_t  app_elf_sha256[32];
	uint16_t min_efuse_blk_rev_full;
	uint16_t max_efuse_blk_rev_full;
	uint8_t  mmu_page_size;
	uint8_t  reserv3[3];
	uint32_t reserv2[18];
};

const struct fmp_esp_app_desc seam_c6_app_desc
	__attribute__((section(".appdesc"), used)) = {
	.magic_word             = 0xABCD5432U,      /* ESP_APP_DESC_MAGIC_WORD */
	.secure_version         = 0U,
	.version                = "FMP3-seam-C6",
	.project_name           = "fmp3_esp32c6",
	.min_efuse_blk_rev_full = 0U,
	.max_efuse_blk_rev_full = 0xFFFFU,
	.mmu_page_size          = 0U,
};
