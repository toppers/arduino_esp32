/*
 *  ペリフェラルのクロック／リセットレジスタのチップ差
 *
 *  S3 は SYSTEM、無印ESP32(LX6) は DPORT に同じ役割のレジスタがある。
 *  名前も置き場も違うだけで、意味とビット位置の使い方は同じなので、
 *  ここで中立な名前へ寄せる（値は各チップの SDK ヘッダが正本）。
 */
#ifndef M5_PERIPH_CLK_H
#define M5_PERIPH_CLK_H

#if defined(TOPPERS_ESP32_LX6)
#include <soc/dport_reg.h>
#define M5_PERIP_CLK_EN_REG	DPORT_PERIP_CLK_EN_REG
#define M5_PERIP_RST_EN_REG	DPORT_PERIP_RST_EN_REG
#define M5_I2C_EXT0_CLK_EN	DPORT_I2C_EXT0_CLK_EN
#define M5_I2C_EXT0_RST		DPORT_I2C_EXT0_RST
#define M5_I2C_EXT1_CLK_EN	DPORT_I2C_EXT1_CLK_EN
#define M5_I2C_EXT1_RST		DPORT_I2C_EXT1_RST
#define M5_SPI2_CLK_EN		DPORT_SPI2_CLK_EN
#define M5_SPI2_RST		DPORT_SPI2_RST
#else
#include <soc/system_reg.h>
#define M5_PERIP_CLK_EN_REG	SYSTEM_PERIP_CLK_EN0_REG
#define M5_PERIP_RST_EN_REG	SYSTEM_PERIP_RST_EN0_REG
#define M5_I2C_EXT0_CLK_EN	SYSTEM_I2C_EXT0_CLK_EN
#define M5_I2C_EXT0_RST		SYSTEM_I2C_EXT0_RST
#define M5_I2C_EXT1_CLK_EN	SYSTEM_I2C_EXT1_CLK_EN
#define M5_I2C_EXT1_RST		SYSTEM_I2C_EXT1_RST
#define M5_SPI2_CLK_EN		SYSTEM_SPI2_CLK_EN
#define M5_SPI2_RST		SYSTEM_SPI2_RST
#endif

#endif /* M5_PERIPH_CLK_H */
