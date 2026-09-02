/*
 *  seam-S3 LX7(ESP32-S3): esp_app_desc スタブ。
 *
 *  esp32s3 の実ESP-IDF bootloader(v5.5.4) は、classic(esp32)と違い
 *  (#if !CONFIG_IDF_TARGET_ESP32) イメージの segment#0 先頭を esp_app_desc_t
 *  として読み、min/max_efuse_blk_rev_full で efuse block revision を検証する
 *  (esp_image_format.c:773)。FMP3イメージは segment#0(.data)先頭に有効な
 *  app_desc を持たず、garbage が不正なefuse要件として読まれて起動拒否される
 *  ("Image requires efuse blk rev >= v5.24" 等の矛盾値)。
 *
 *  ここで有効な esp_app_desc を .appdesc セクションに定義し、リンカスクリプトで
 *  segment#0(.data)の先頭に KEEP 配置する。min_efuse_blk_rev_full=0 で
 *  bootloader の IS_FIELD_SET 判定を偽にして min チェックをskip、
 *  max_efuse_blk_rev_full=0xFFFF で max チェックを通す(chip rev v1.4=104 <= 65535)。
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

const struct fmp_esp_app_desc seam_s3_app_desc
	__attribute__((section(".appdesc"), used)) = {
	.magic_word             = 0xABCD5432U,      /* ESP_APP_DESC_MAGIC_WORD */
	.secure_version         = 0U,
	.version                = "FMP3-seam-S3",
	.project_name           = "fmp3_esp32s3",
	.min_efuse_blk_rev_full = 0U,               /* IS_FIELD_SET偽→minチェックskip */
	.max_efuse_blk_rev_full = 0xFFFFU,          /* chip 104<=65535でpass */
	.mmu_page_size          = 0U,
};
