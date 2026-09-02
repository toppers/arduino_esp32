# BT Classic の実機確認ツール

- `spp_echo_test.py` — PC から RFCOMM で `examples/BluetoothSPP` へ接続し、
  echo の往復をバイト一致で確かめる。**positive control 付き**（期待値を
  1 バイト壊し、比較器が実際に差を検出することを示す）。root 不要
  （`AF_BLUETOOTH`/`BTPROTO_RFCOMM` を直接使う）。

```sh
bluetoothctl --timeout 15 scan on      # アドレスを見つける
bluetoothctl devices | grep M5Stack-SPP
python3 tools/bt/spp_echo_test.py <BD_ADDR> 1
```

例題は 128 バイトのバッファで行を組み立てて満杯で吐き出すので、
127 バイトを超えるペイロードは**仕様どおり 2 回に分かれて**返る。
テストはその下で測る。

- `../BtRefDump/` — 素の M5Stack コア + BluetoothSerial の参照ダンプ
  （動く構成とレジスタを突き合わせるためのもの）。
