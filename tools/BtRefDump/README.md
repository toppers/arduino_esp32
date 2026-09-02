# BtRefDump — 動く Bluetooth Classic との比較用

素の M5Stack Arduino コア（`m5stack:esp32:m5stack_core`）＋ `BluetoothSerial` を
実機に焼き、TOPPERS/FMP3 の `bt-classic` profile が読むのと**同じレジスタ**を
出力する。両方を同じ実機に焼いて差分を取るためだけのもので、製品には入らない。

```sh
arduino-cli compile -b m5stack:esp32:m5stack_core:PartitionScheme=huge_app \
    -u -p /dev/ttyACM1 tools/BtRefDump
```

`bt-classic` 側の同じ数字は `-DTOPPERS_BT_INTR_PROBE` で入る周期プローブ
（`ports/m5stack_xtensa/runtime/bt/bt_intr_probe.c`）が出す。

## 2026-09-02 の比較結果

| レジスタ | 動く方 | 本ポート | |
| --- | --- | --- | --- |
| `wifi_clk_en` (0x3FF000CC) | `ffff8800` | `ffffebf9` | 合わせても変化なし |
| `core_rst_en` (0x3FF000D0) | `00000000` | `00000000` | 一致 |
| `rtc_clk_conf` (0x3FF48070) | `29580090` | `09580090` | bit29=FAST_CLK_RTC_SEL。合わせても変化なし |
| `map[src 4]` | 8 | 8 | 一致 |
| `map[src 6/7]` | **25** | **5 → 25** | ★HLI 有効化で一致させた |
| `bb[0x3FF510F8]` | `00040109` | `00040109` | 一致 |
| `bb[0x3FF510A0]` | `3ffb0000` | `3ffb0000` | 一致 |
| `bb[0x3FF51040]` | `0000199b` | `0000199b` | 一致 |
| `cpu_per_conf` | 2 (240MHz) | 1 (160MHz) | 意図的な差 |

**比較できるレジスタは全て一致させたが、コントローラは依然として割込みを出さない。**
残る差はソフトウェア側（FreeRTOS と FMP3）にある。
