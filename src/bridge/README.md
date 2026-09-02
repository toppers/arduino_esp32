# Arduino bridge

The bridge implements the C/C++ boundary that invokes Arduino `setup()` once and then
repeatedly invokes `loop()` from a statically configured FMP3 task.

- `ports/m5stack_xtensa/app/phase3/phase3_arduino_app.cfg` creates
  `ARDUINO_TASK` on `CLS_PRC1` with `TA_ACT | TA_FPU`.
- `ArduinoSketchBridge.cpp` is compiled by Arduino builder and linked into the
  FMP3 ELF together with the generated sketch object.
- `esp_run_init_array()` runs C++ static constructors before `setup()`.
- `dly_tsk(1000)` supplies a 1ms scheduling point after each `loop()`.
- `toppers_arduino_runtime_init()` is a weak extension hook. The bridge does not
  link `initArduino()` or Arduino `core.a`.

FMP3専用アプリは`ports`または`fmp_app`に置く。Arduino builderはライブラリの
`src`配下を再帰的にコンパイルするため、`kernel.h`やFMP3 cfgに依存するソースを
このディレクトリへ追加しない。
