#
#		チップ依存部の CMake 定義（ESP32-C6 用・FMP3）
#
#  target.cmake から include される。fmp3/arch/riscv_gcc/esp32p4/chip.cmake と
#  同じ分担: 共通 arch（COREDIR）は fmp3_core 側、チップ依存部（CHIPDIR）は本 repo 側。
#  ARCHDIR / CHIPDIR / TARGETDIR は呼び出し元の target.cmake が設定済み。
#
#  出典: asp3_esp_idf asp3/asp3_core/arch/riscv_gcc/esp32c6/chip.cmake（ASP3 版）。
#  差分: (1) 変数名 ASP3_* -> FMP3_*、(2) -march を rv32imc -> rv32imac
#  （FMP3 共通部の riscv_insn.h:222 / core_sil.h:167 が amoswap.w.aq を使う。
#   ESP-IDF の C6 も rv32imac: tools/cmake/toolchain-clang-esp32c6.cmake:14）、
#  (3) PLIC/mtimer/msi_ipi/clic の共通ソースは一切リンクしない（割込みは
#  intmtx、HRT は target 側 systimer、単一コアのため IPI 無し）。
#
set(COREDIR ${ARCHDIR}/common)

set(FMP3_RISCV_MARCH "rv32imac_zicsr_zifencei" CACHE STRING
    "RISC-V ISA string for ESP32-C6 (A extension is required by FMP3 spinlocks)")
set(FMP3_RISCV_MABI "ilp32" CACHE STRING "RISC-V ABI for ESP32-C6")

if(NOT DEFINED FMP3_RISCV_SPECS)
    set(FMP3_RISCV_SPECS "--specs=nano.specs")
endif()

list(APPEND FMP3_INCLUDE_DIRS ${CHIPDIR})

list(APPEND FMP3_COMPILE_OPTIONS
    -march=${FMP3_RISCV_MARCH}
    -mabi=${FMP3_RISCV_MABI}
    -mcmodel=medany
    -msmall-data-limit=8
    -mstrict-align
    -mno-save-restore
    -fsigned-char
    -ffunction-sections
    -fdata-sections
    ${FMP3_RISCV_SPECS}
)

list(APPEND FMP3_LINK_OPTIONS
    -march=${FMP3_RISCV_MARCH}
    -mabi=${FMP3_RISCV_MABI}
    -mcmodel=medany
    ${FMP3_RISCV_SPECS}
    -nostartfiles
)

#
#  TLS（chip_asm.inc の init_additional_regs_start_r）。常時有効。
#
list(APPEND FMP3_COMPILE_DEFS TOPPERS_SUPPORT_TLS)

list(APPEND FMP3_ARCH_C_FILES
    ${CHIPDIR}/chip_kernel_impl.c
    ${CHIPDIR}/chip_support.S
)

#
#  非TECS版 SIO ドライバ（コンソール実体の選択）
#    ESP32C6_CONSOLE=usbjtag（既定。M5NanoC6 は native USB のみ）| uart0
#  usbjtag の実体は target 側 esp32c6_usbjtag_hal.c（asp3 と同じ分担）なので
#  ここでは chip_serial.c だけを積み、target.cmake が実体を足す。
#
if(NOT DEFINED ESP32C6_CONSOLE)
    set(ESP32C6_CONSOLE usbjtag)
endif()
if(ESP32C6_CONSOLE STREQUAL "usbjtag")
    list(APPEND FMP3_COMPILE_DEFS TOPPERS_ESP32C6_CONSOLE_USBJTAG)
    list(APPEND FMP3_SYSSVC_TARGET_C_FILES ${CHIPDIR}/chip_serial.c)
elseif(ESP32C6_CONSOLE STREQUAL "uart0")
    list(APPEND FMP3_SYSSVC_TARGET_C_FILES
        ${CHIPDIR}/chip_serial.c
        ${CHIPDIR}/esp32c6_uart.c)
else()
    message(FATAL_ERROR "ESP32C6_CONSOLE は usbjtag / uart0 のみ（指定=${ESP32C6_CONSOLE}）")
endif()

#
#  コア依存部（fmp3_core 側・無改変）
#
include(${COREDIR}/arch.cmake)

#
#  スタートアップ（start.S）を C6 専用版へ差し替える（実装計画 Task 5 の configure/build で発覚）。
#  ESP32-C6 は F 拡張を持たない（rv32imac）が、fmp3_core 共通の
#  arch/riscv_gcc/common/start.S は FPU 初期化（fscsr 等）を無条件に
#  発行しており、`-march=rv32imac` ではアセンブラが `fscsr` を拒否する。
#  fmp3_core は改変しないため、FMP3_START_FILES の該当エントリを
#  C6 専用の chip_start.S（FPU 初期化を core_support.S と同じ
#  `#ifdef __riscv_flen` で括っただけの複製）に差し替える。
#  詳細は chip_start.S 冒頭のコメントを参照。
#
list(REMOVE_ITEM FMP3_START_FILES ${COREDIR}/start.S)
list(APPEND FMP3_START_FILES ${CHIPDIR}/chip_start.S)
