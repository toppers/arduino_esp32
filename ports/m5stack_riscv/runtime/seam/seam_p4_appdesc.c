/*
 *  seam-P4（ESP32-P4 / RV32）: esp_app_desc スタブ。
 *
 *  段4（2026-08-14）。esp/boot/seam_s3_appdesc.c（S3 版）と同じ役割の P4 版で、
 *  中身の値も同じ考え方だが、**別ファイルにした**（S3 版は
 *  `TOPPERS_ESP32_LX6` 等の分岐を持たず、project_name/version も S3 を名乗る。
 *  seam-p4 が S3 の名前を名乗ると採取ログの読み手を誤らせる）。
 *
 *  【なぜ要るか（一次資料）】
 *  esp-idf v5.5.4 の esp_image_format.c:767-777 は、`#if !CONFIG_IDF_TARGET_ESP32`
 *  の下で **イメージの segment #0 の先頭を esp_app_desc_t として読み**、
 *  bootloader_common_check_efuse_blk_validity(min_efuse_blk_rev_full,
 *  max_efuse_blk_rev_full) で efuse ブロックリビジョンを検証する。
 *  FMP3 イメージが有効な app_desc を持たないと、そこにあった任意のバイト列が
 *  efuse 要件として読まれて起動が拒否される（S3 で実際に踏んだ:
 *  "Image requires efuse blk rev >= v5.24" 等の矛盾値）。
 *
 *  min_efuse_blk_rev_full=0    → IS_FIELD_SET が偽になり min チェックを skip
 *  max_efuse_blk_rev_full=0xFFFF → max チェックは必ず通る
 *  mmu_page_size=0             → 「legacy image」として既定ページサイズが使われる
 *                                 （P4 は SOC_MMU_PAGE_SIZE_CONFIGURABLE を定義
 *                                   しないので、この値はそもそも読まれない。
 *                                   esp_image_format.c:834-862 の #if 参照）
 *
 *  配置は fmp3/target/m5stamp_esp32p4_gcc/esp32p4_xip.ld が
 *  `.flash_rodata` の先頭へ KEEP(*(.appdesc)) で置く。DROM は IROM より低い
 *  番地なので esptool の load_addr 昇順ソートで segment #0 になる。
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

const struct fmp_esp_app_desc seam_p4_app_desc
	__attribute__((section(".appdesc"), used)) = {
	.magic_word             = 0xABCD5432U,      /* ESP_APP_DESC_MAGIC_WORD */
	.secure_version         = 0U,
	.version                = "FMP3-seam-P4",
	.project_name           = "fmp3_esp32p4",
	.min_efuse_blk_rev_full = 0U,
	.max_efuse_blk_rev_full = 0xFFFFU,
	.mmu_page_size          = 0U,
};
