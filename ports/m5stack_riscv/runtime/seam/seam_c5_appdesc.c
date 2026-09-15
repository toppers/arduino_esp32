/*
 *  seam-C5（ESP32-C5 / RV32）: esp_app_desc スタブ。
 *
 *  計画 3 段2（2026-09-16）。esp/boot/seam_c6_appdesc.c（C6 段2）の写し。
 *  構造体レイアウトは共通（esp_app_desc_t の ABI はチップ非依存）だが、
 *  version/project_name は C5 を名乗る（別ファイルにする理由も C6 版と同じ:
 *  別チップの名前を名乗ると採取ログの読み手を誤らせる）。
 *
 *  【なぜ要るか（一次資料）】
 *  esp-idf v5.5.4 の esp_image_format.c:767-777 は、`#if !CONFIG_IDF_TARGET_ESP32`
 *  の下で **イメージの segment #0 の先頭を esp_app_desc_t として読み**、
 *  bootloader_common_check_efuse_blk_validity(min_efuse_blk_rev_full,
 *  max_efuse_blk_rev_full) で efuse ブロックリビジョンを検証する。
 *  FMP3 イメージが有効な app_desc を持たないと、そこにあった任意のバイト列が
 *  efuse 要件として読まれて起動が拒否される（S3/P4/C6 で踏んだ既知の罠）。
 *
 *  min_efuse_blk_rev_full=0    -> IS_FIELD_SET が偽になり min チェックを skip
 *  max_efuse_blk_rev_full=0xFFFF -> max チェックは必ず通る
 *  mmu_page_size=0             -> **C5 は SOC_MMU_PAGE_SIZE_CONFIGURABLE を定義しない**
 *                                 （soc/esp32c5/include/soc/soc_caps.h に無い。C6 は
 *                                 定義する）ため、esp_image_format.c:834-862 の
 *                                 `#if SOC_MMU_PAGE_SIZE_CONFIGURABLE` 枝は C5 では
 *                                 コンパイルされず、このフィールドは**読まれない**。
 *                                 bootloader は SPI_FLASH_MMU_PAGE_SIZE
 *                                 （= CONFIG_MMU_PAGE_SIZE = 0x10000、Task 1 実測）を
 *                                 無条件に使う。0 を入れておくのは C6 との対称性と、
 *                                 万一将来 configurable になっても「既定に従う」の
 *                                 意味になるため。
 *
 *  配置は fmp3/target/m5stampc5_gcc/esp32c5_xip.ld が `.flash.appdesc` という
 *  **名前**のセクションへ KEEP(*(.appdesc)) で置く。esptool は
 *  `.flash.appdesc` という名前のセクションを segment #0 の先頭へ特別扱いする
 *  （esptool bin_image.py。C6 段2 で実測済み）。
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

const struct fmp_esp_app_desc seam_c5_app_desc
	__attribute__((section(".appdesc"), used)) = {
	.magic_word             = 0xABCD5432U,      /* ESP_APP_DESC_MAGIC_WORD */
	.secure_version         = 0U,
	.version                = "FMP3-seam-C5",
	.project_name           = "fmp3_esp32c5",
	.min_efuse_blk_rev_full = 0U,
	.max_efuse_blk_rev_full = 0xFFFFU,
	.mmu_page_size          = 0U,
};
