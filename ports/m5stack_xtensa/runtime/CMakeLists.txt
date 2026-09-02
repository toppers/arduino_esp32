cmake_minimum_required(VERSION 3.23)

foreach(required
        FMP3_CORE_ROOT
        FMP3_APPLICATION_DIR
        FMP3_APPLICATION_NAME
        ARDUINO_SDK_LD_ROOT
        A1_ESPTOOL_EXECUTABLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Required setting is missing: ${required}")
    endif()
endforeach()

get_filename_component(RUNTIME_ROOT "${CMAKE_CURRENT_LIST_DIR}" REALPATH)

#
#  Keep the builder's directory out of the distributed image.
#
#  `__FILE__` reaches `.rodata` through the kernel's assertion macros, so the
#  absolute path of whoever built the stage was ending up in every sketch a user
#  compiles. Measured on the 0.3.0 package before this: 15 of the 47 shipped
#  objects carried it, and 7 to 9 copies survived into each final `.bin`.
#
#  It must sit HERE, above add_subdirectory(fmp3_core): add_compile_options only
#  reaches targets created after it, and the leak comes mostly from the kernel's
#  own objects (alarm.o, cyclic.o, interrupt.o ...), which do not link
#  seam_common either. Placed after that subdirectory it silently does nothing -
#  measured: the objects still carried the absolute path.
#
#  Side benefit: two machines building the same source no longer differ over
#  where they checked it out. The banner's __DATE__/__TIME__ still differs, so
#  this does not make the build reproducible on its own.
#
get_filename_component(FMP3_REPO_ROOT "${RUNTIME_ROOT}/../../.." REALPATH)
add_compile_options("-ffile-prefix-map=${FMP3_REPO_ROOT}=.")

get_filename_component(FMP3_CORE_ROOT "${FMP3_CORE_ROOT}" REALPATH)
get_filename_component(FMP3_APPLICATION_DIR
    "${FMP3_APPLICATION_DIR}" REALPATH)
get_filename_component(ARDUINO_SDK_LD_ROOT
    "${ARDUINO_SDK_LD_ROOT}" REALPATH)

#
#  XIPリンクのPADDR決定方式。
#  ONなら flash_cache_init.c が bootloader の張ったMMUエントリからPADDRを復元し、
#  リンクは1回で済む（2パスPADDR自己整合を行わない）。
#  OFFなら従来の2パス（link→elf2image→file_offs実測→焼き込んで再link）へ戻る。
#
option(A1_XIP_PADDR_RUNTIME
    "Resolve XIP DROM/IROM flash offsets at boot instead of burning them in" ON)
#  PoC用の観測（既定OFF）。ONにするとPADDR候補をコンソールへ出す。
option(A1_XIP_PADDR_PROBE "Print resolved XIP PADDR values at startup" OFF)
#
#  事前ビルドステージからDWARFを落とす（既定ON）。Arduinoの利用者がFMP3ランタイム側を
#  デバッガで追うことはまずないため、配布サイズを優先する。`elf2image`はloadable
#  セグメントしか見ないので`app_xip.bin`は変わらない（`fmp_xip.elf`のランタイム側
#  デバッグ情報だけが失われる）。
#
option(A1_PREBUILT_STRIP_DEBUG
    "Strip debug info from the prebuilt stage objects" ON)
#  negative control（既定は空）。実行時PADDRを指定ページ数ずらす＝必ず起動失敗する。
set(A1_XIP_POISON_PAGE "" CACHE STRING
    "Shift the runtime-resolved XIP PADDR by N 64KB pages (negative control)")

if(NOT DEFINED FMP3_RUNTIME_PROFILE)
    set(FMP3_RUNTIME_PROFILE minimal)
endif()
#
#  5 profile を 3 へ減らしてある。
#    dual-core … m5-unified が FMP3_PRC_NUM=2 で建つようになったので吸収（D-4c）
#    wifi-scan … 初期化を一本化して scan アダプタを wifi-connect へ入れたので
#                吸収（D-4e。前提は D-4d）
#
#  ★wifi-scan の統合は一度取り止めている。「WiFiScan の例題が wifi-connect でも
#    ビルドできる」ことを根拠にしたのが誤りで、実際は
#    toppers_wifi_optional_stubs.c の weak スタブ（常に ScanFailed）に
#    解決されていただけだった。**リンクが通ることは動くことの証明にならない。**
#    D-4d で初期化を一本化して初めて実物が入り、実機で 18 AP を検出することを
#    確認したうえで畳んだ。
#
if(NOT FMP3_RUNTIME_PROFILE MATCHES "^(minimal|m5-unified|wifi-connect|all-in-one|bt-classic)$")
    message(FATAL_ERROR
        "FMP3_RUNTIME_PROFILE must be minimal, m5-unified, wifi-connect, or all-in-one")
endif()

#
#  チップ選択。既定は esp32s3 で、-DA1_CHIP を渡さない従来の呼び出しは
#  そのまま同じステージを建てる。チップに依らない部分（arch の common、
#  boot/flash_cache_init.c）は TOPPERS_ESP32_LX6 の有無で分かれ、その略称は
#  各 chip_stddef.h が自分で定義する。
#
#  ここは project() より前なので、パス組み立てに要らないものだけ決める。
#
if(NOT DEFINED A1_CHIP OR A1_CHIP STREQUAL "")
    set(A1_CHIP esp32s3)
endif()
if(A1_CHIP STREQUAL "esp32s3")
    set(A1_CHIP_TARGET_NAME esp32s3_devkitc_gcc)
    set(A1_CHIP_ARCH_NAME esp32s3)
    #  S3 は全 profile で同じスクリプト（wifi 専用の派生は無い）。
    set(A1_XIP_LD_NAME esp32s3_xip_m5.ld)
    set(A1_CHIP_ROM_LD esp32s3.rom.ld)
    #  ROM 常駐コンソールは USB-Serial/JTAG。LX6 に USJ は無く UART0 を使う。
    set(A1_CHIP_DEFS TOPPERS_S3_CONSOLE_USJ)
    #  ESP-IDF 由来のコードへ渡すチップ判別と、そのチップの sdkconfig が読む
    #  フラッシュ周波数マクロ。綴りがチップで違う（LX6 は ESPRESSIF_ 系）。
    set(A1_CHIP_IDF_DEFS TOPPERS_ESP32S3 CONFIG_ESP32S3_FLASH_FREQ_80M)
    #  S3 の ld/ に librtc.a は無い。
    set(A1_CHIP_WIFI_LIBS)
    #  2nd-stage bootloader が読む app descriptor。LX6 では要らない。
    set(A1_CHIP_HAS_APPDESC ON)
elseif(A1_CHIP STREQUAL "esp32")
    set(A1_CHIP_TARGET_NAME esp32_devkitc_gcc)
    set(A1_CHIP_ARCH_NAME esp32)
    #  LX6 のリンカスクリプトは 4 種あるが、選ぶ軸は 2 つしかない：
    #  「.ctors を集約するか」と「BT コントローラの EM 領域を空けるか」。
    #
    #  seam/init_array.cpp が __ctors_start/__init_array_start を要求するので
    #  集約版が要る。そして**このポートはどの profile でも BT を使わない**ので、
    #  EM 領域(0x3FFB0000〜、約56KB)を予約する理由が無い。両方を満たすのは
    #  esp32_xip_m5_wifi.ld で、名前の "wifi" は由来であって中身は
    #  「.ctors 集約＋フル DRAM(288KB)」である。
    #
    #  esp32_xip_m5.ld は EM 領域を除いた 232KB で、将来 M5+BT 構成を作る
    #  ときのために上流が残しているもの。BT profile を足すときは
    #  そちらか esp32_xip_btc.ld へ分岐すること。
    #  BT だけは EM 領域を空ける必要があるので別のスクリプトを使う。
    if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
        set(A1_XIP_LD_NAME esp32_xip_btc.ld)
    else()
        set(A1_XIP_LD_NAME esp32_xip_m5_wifi.ld)
    endif()
    set(A1_CHIP_ROM_LD esp32.rom.ld)
    #  LX6 の hardware_init_hook は既定で banner を出したところで無限ループする
    #  （元は QEMU PoC の T1 スコープ）。Arduino ランタイムは sta_ker まで進む。
    #  wifi/ の shim にはカーネルヘッダを読まない TU があり、そこは -D でしか
    #  チップを判別できない（esp_shim_cfg.h の注記）。chip_stddef.h 側は
    #  二重定義にならないようガードしてある。
    set(A1_CHIP_DEFS TOPPERS_LX6_PROCEED_TO_KERNEL TOPPERS_ESP32_LX6)
    set(A1_CHIP_IDF_DEFS TOPPERS_ESP32_LX6 CONFIG_ESPRESSIF_FLASH_FREQ_80M)
    #  temprature_sens_read / force_wifi_mode / unforce_wifi_mode /
    #  coex_bt_high_prio は LX6 では librtc.a が持つ（ld/ に在る。S3 には無い）。
    set(A1_CHIP_WIFI_LIBS "-lrtc@@")
    set(A1_CHIP_HAS_APPDESC OFF)
else()
    message(FATAL_ERROR "A1_CHIP must be esp32s3 or esp32 (got '${A1_CHIP}')")
endif()

foreach(required_path
        "${FMP3_CORE_ROOT}/CMakeLists.txt"
        "${FMP3_APPLICATION_DIR}/${FMP3_APPLICATION_NAME}.c"
        "${FMP3_APPLICATION_DIR}/${FMP3_APPLICATION_NAME}.cfg"
        "${ARDUINO_SDK_LD_ROOT}/${A1_CHIP_ROM_LD}"
        "${A1_ESPTOOL_EXECUTABLE}")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Required file was not found: ${required_path}")
    endif()
endforeach()

set(FMP3_COMPILE_DEFS
    TOPPERS_BOOT_SEAM
    ${A1_CHIP_DEFS})
set(FMP3_INCLUDE_DIRS "${FMP3_APPLICATION_DIR}")
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|wifi-connect|all-in-one|bt-classic)$")
    foreach(required
            ARDUINO_SDK_INCLUDE_ROOT
            ARDUINO_SDK_LIBRARY_ROOT)
        if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
            message(FATAL_ERROR
                "${FMP3_RUNTIME_PROFILE} profile requires setting: ${required}")
        endif()
    endforeach()
    get_filename_component(ARDUINO_SDK_INCLUDE_ROOT
        "${ARDUINO_SDK_INCLUDE_ROOT}" REALPATH)
    get_filename_component(ARDUINO_SDK_LIBRARY_ROOT
        "${ARDUINO_SDK_LIBRARY_ROOT}" REALPATH)
    get_filename_component(ARDUINO_SDK_ROOT
        "${ARDUINO_SDK_INCLUDE_ROOT}" DIRECTORY)
    list(APPEND FMP3_COMPILE_DEFS
        ESP_PLATFORM
        ${A1_CHIP_IDF_DEFS}
        CONFIG_ESP_PHY_ENABLED=1
        TOPPERS_ESPIDF_SUPPLY
        TOPPERS_ESP_WIFI_SYSCLK_PLL)
    list(APPEND FMP3_INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/wifi/shim"
        "${CMAKE_CURRENT_LIST_DIR}/wifi/config/${A1_CHIP_ARCH_NAME}"
        #  attachInterrupt の cfg 片とその定数ヘッダ。
        "${CMAKE_CURRENT_LIST_DIR}/arduino")
    if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
        #  bt.cfg（BT タスク・割込み）の置き場。
        list(APPEND FMP3_INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}/bt")
        #  ★seam_objects 側にも要る。esp_shim_bt_crit_wifi.c は Wi-Fi 専用の
        #    critical section 実装で、bt_shim.c の本実装と二重定義になる。
        #  cfg パスにも要る（esp_shim.cfg の CRE_FLG がこれで出る）。
        list(APPEND FMP3_COMPILE_DEFS
            TOPPERS_BT_CLASSIC TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
    endif()
endif()

#
#  M5Unified アダプタ（toppers_m5_begin() ほか、src/ToppersFMP3_M5UnifiedBridge.h
#  が公開している API の実体）。
#
#  ★かつては `${FMP3_APPLICATION_DIR}/${FMP3_APPLICATION_NAME}_m5.cpp` として
#    アプリ側に置いていたが、これは**製品の構成要素**であってアプリではない
#    （公開ヘッダの実装であり、どのアプリを選んでも必要）。アプリ名に縛られて
#    いると、同じ profile で「配布用アプリ」と「自己診断アプリ」を切り替える
#    ことができない。runtime 側へ移し、パスを名前で持つ。
#
set(M5_ARDUINO_ADAPTER_SOURCE
    "${CMAKE_CURRENT_LIST_DIR}/m5/adapter/m5_arduino_adapter.cpp")

#
#  The combined M5 + Wi-Fi profile.
#
#  M5_USE_ESP_SHIM drops the m5 mini shim's duplicate definitions (guarded with
#  #ifndef in m5/shim/*, m5/compat/esp_shim_m5_ext.c and
#  m5/app_support/m5_display_smoke_heap.c) so wifi/shim provides them instead.
#  That is the reference port's mechanism and its answer to "which side owns the
#  critical-section discipline": the Wi-Fi side does.
#
#  ★Two header sets both offer "freertos/FreeRTOS.h" - m5/compat/freertos and
#    wifi/freertos_stub - and they disagree about portMUX_TYPE: a one-field dummy
#    on the M5 side (the mux is ignored, a global nest counter does the work)
#    against a two-field struct the Wi-Fi side actually uses. Include order
#    decides which a translation unit sees, and with m5/compat first the Wi-Fi
#    HAL fails to compile (phy_init.c gets the M5 type). BEFORE puts the Wi-Fi
#    set first for the combined profile.
#
#  ★M5_USE_ESP_SHIM は seam_common へ載せる（下）。FMP3_COMPILE_DEFS へ足しても
#    m5/seam のソースには届かない——あれは fmp3_core サブディレクトリ用で、
#    消費側ターゲットは seam_common の INTERFACE 定義を見る。
#    実測: FMP3_COMPILE_DEFS へ足した状態で結合を建てたら、ガードした 37 記号が
#    そのまま重複として監査に出た（ガードは正しく、届いていなかった）。

if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    foreach(required
            M5GFX_SOURCE_ROOT
            M5UNIFIED_SOURCE_ROOT
            TOPPERS_LIBRARY_SOURCE_ROOT)
        if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
            message(FATAL_ERROR
                "M5Unified profile requires setting: ${required}")
        endif()
        get_filename_component(${required} "${${required}}" REALPATH)
    endforeach()
    #
    #  M5GFX and M5Unified are compiled from the user's sketchbook, which is
    #  OUTSIDE the repository, so the repo-wide -ffile-prefix-map at the top of
    #  this file does not reach them. Without these two the m5-unified stage
    #  still shipped the builder's home directory - found by running
    #  scripts/check_host_paths.py over all three profiles, where minimal and
    #  wifi-connect passed and this one did not.
    #
    #  These must be added before the M5 targets below, for the same reason as
    #  the repo-wide one: add_compile_options only reaches later targets.
    #
    add_compile_options(
        "-ffile-prefix-map=${M5GFX_SOURCE_ROOT}=M5GFX/src"
        "-ffile-prefix-map=${M5UNIFIED_SOURCE_ROOT}=M5Unified/src")
    foreach(required_path
            "${M5GFX_SOURCE_ROOT}/M5GFX.cpp"
            "${M5UNIFIED_SOURCE_ROOT}/M5Unified.cpp"
            "${TOPPERS_LIBRARY_SOURCE_ROOT}/ToppersFMP3_M5UnifiedBridge.h"
            "${M5_ARDUINO_ADAPTER_SOURCE}")
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR "M5Unified input was not found: ${required_path}")
        endif()
    endforeach()
endif()

project(toppers_fmp3_arduino_runtime C CXX ASM)

set(FMP3_TARGET "${A1_CHIP_TARGET_NAME}")
set(FMP3_TARGET_DIR "${RUNTIME_ROOT}/target/${A1_CHIP_TARGET_NAME}")
set(FMP3_CHIP_DIR "${RUNTIME_ROOT}/arch/xtensa_gcc/${A1_CHIP_ARCH_NAME}")
set(A1_XIP_LD "${FMP3_TARGET_DIR}/${A1_XIP_LD_NAME}")
set(FMP3_LIBRARY_ONLY ON)
#
#  m5-unified は SMP（PRC1/PRC2）で建てる。旧 dual-core profile を吸収した
#  ぶんで、これがこの profile と minimal の唯一のカーネル差分である。
#  ★M5Unified が SMP カーネル上で動くことは実機で確認済み
#    （60 秒 self-test PASS）。
#
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    set(FMP3_PRC_NUM 2)
else()
    set(FMP3_PRC_NUM 1)
endif()
set(FMP3_APPLDIR "${FMP3_APPLICATION_DIR}")
set(FMP3_APPLNAME "${FMP3_APPLICATION_NAME}")

add_subdirectory("${FMP3_CORE_ROOT}" fmp3_core)

set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")

add_library(seam_common INTERFACE)
target_compile_options(seam_common INTERFACE
    -O2 -Wall -g -mlongcalls)
target_compile_definitions(seam_common INTERFACE
    TOPPERS_BOOT_SEAM
    TOPPERS_OMIT_TECS
    OMIT_DATA_INIT
    TNUM_PRCID=${FMP3_PRC_NUM}
    ${A1_CHIP_DEFS}
    #  結合 profile では m5 のミニシムの重複定義を落とし、
    #  wifi/shim に一本化する（各 TU の #ifndef M5_USE_ESP_SHIM）。
    #  seam_objects / seam_cxx / m5_runtime_objects / net_objects はいずれも
    #  seam_common をリンクするので、ここに置けば全部に届く。
    $<$<STREQUAL:${FMP3_RUNTIME_PROFILE},all-in-one>:M5_USE_ESP_SHIM>
    #  PADDRの観測（-DA1_XIP_PADDR_PROBE=ONのときだけ）。
        $<$<BOOL:${A1_XIP_PADDR_PROBE}>:TOPPERS_XIP_PADDR_PROBE>)
target_include_directories(seam_common INTERFACE
    "${FMP3_CORE_ROOT}/include"
    "${FMP3_TARGET_DIR}"
    "${FMP3_CHIP_DIR}"
    "${RUNTIME_ROOT}/arch/xtensa_gcc/common"
    "${FMP3_CORE_ROOT}/arch/gcc"
    "${FMP3_CORE_ROOT}"
    "${FMP3_APPLICATION_DIR}"
    "${GENERATED_DIR}"
    "${FMP3_CORE_ROOT}/tecs_kernel")

add_library(seam_objects OBJECT
    "${FMP3_APPLICATION_DIR}/${FMP3_APPLICATION_NAME}.c"
    "${FMP3_CORE_ROOT}/syssvc/syslog.c"
    "${FMP3_CORE_ROOT}/syssvc/banner.c"
    "${FMP3_CORE_ROOT}/syssvc/serial.c"
    "${FMP3_CORE_ROOT}/syssvc/serial_cfg.c"
    "${FMP3_CORE_ROOT}/syssvc/logtask.c"
    "${FMP3_CORE_ROOT}/library/log_output.c"
    "${FMP3_CORE_ROOT}/library/vasyslog.c"
    "${FMP3_CORE_ROOT}/library/t_perror.c"
    "${FMP3_CORE_ROOT}/library/strerror.c"
    "${FMP3_CHIP_DIR}/chip_serial.c"
    "${FMP3_CHIP_DIR}/chip_serial_sio.c")
if(A1_CHIP_HAS_APPDESC)
    target_sources(seam_objects PRIVATE "${RUNTIME_ROOT}/seam/seam_s3_appdesc.c")
endif()
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    target_sources(seam_objects PRIVATE
        "${RUNTIME_ROOT}/arduino/arduino_interrupt.c"
        "${RUNTIME_ROOT}/m5/app_support/m5_diag.c"
        "${RUNTIME_ROOT}/m5/app_support/m5_display_smoke_heap.c"
        "${RUNTIME_ROOT}/m5/compat/esp_shim_m5_ext.c")
    if(FMP3_RUNTIME_PROFILE STREQUAL "all-in-one")
        #  Ahead of m5/compat: both provide freertos/FreeRTOS.h and the Wi-Fi
        #  HAL must get the Wi-Fi one (see the M5_USE_ESP_SHIM note above).
        target_include_directories(seam_objects BEFORE PRIVATE
            "${RUNTIME_ROOT}/wifi/freertos_stub")
    endif()
    target_include_directories(seam_objects PRIVATE
        "${RUNTIME_ROOT}/m5/app_support"
        "${RUNTIME_ROOT}/m5/compat"
        "${RUNTIME_ROOT}/wifi/shim"
        "${RUNTIME_ROOT}/arduino"
        #  arduino_interrupt.c が使う HAL（ヘッダ内 inline のみ）と
        #  ROM 宣言。ライブラリのリンクは増えない。
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/platform_port/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/${A1_CHIP_ARCH_NAME}/register"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_common/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/include")
endif()
target_link_libraries(seam_objects PRIVATE seam_common)
add_dependencies(seam_objects generate_cfg_gen_files)

#  bt-classic joins this list because it compiles against the same ESP-IDF
#  headers; the Wi-Fi specific pieces below are harmless to it and the shim
#  headers it does need are here.
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one|bt-classic)$")
    set(WIFI_INCLUDE_DIRS
        #  attachInterrupt の定数ヘッダ。
        "${RUNTIME_ROOT}/arduino"
        "${RUNTIME_ROOT}/wifi/freertos_stub"
        "${RUNTIME_ROOT}/wifi/config/hal_stub_include"
        "${RUNTIME_ROOT}/wifi/config/${A1_CHIP_ARCH_NAME}"
        "${RUNTIME_ROOT}/wifi/shim"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_wifi/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_wifi/wifi_apps/roaming_app/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_coex/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/include/soc"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/include/esp_private"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/port/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/heap/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/efuse/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/efuse/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_event/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/log/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_phy/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_phy/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_pm/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_common/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/platform_port/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/${A1_CHIP_ARCH_NAME}/include/${A1_CHIP_ARCH_NAME}"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/${A1_CHIP_ARCH_NAME}/register"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_system/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/xtensa/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/xtensa/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_timer/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_security/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_mm/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/newlib/platform_include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/wpa_supplicant/esp_supplicant/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/wpa_supplicant/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/mbedtls/port/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/mbedtls/mbedtls/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/lwip/port/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/lwip/port/freertos/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/lwip/port/esp32xx/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/lwip/lwip/src/include")
    if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
        list(PREPEND WIFI_INCLUDE_DIRS
            "${RUNTIME_ROOT}/wifi/net/include"
            "${RUNTIME_ROOT}/wifi/net")
    endif()
    add_library(wifi_common INTERFACE)
    target_compile_definitions(wifi_common INTERFACE
        ESP_PLATFORM
        ${A1_CHIP_IDF_DEFS}
        CONFIG_ESP_PHY_ENABLED=1
        TOPPERS_ESPIDF_SUPPLY
        TOPPERS_ESP_WIFI_SYSCLK_PLL)
    target_compile_options(wifi_common INTERFACE
        -Wno-error=implicit-function-declaration)
    target_include_directories(wifi_common INTERFACE ${WIFI_INCLUDE_DIRS})
    #
    #  Scan and connect share one bring-up (toppers_wifi_core.c), so
    #  both adapters live in this one profile. wifi_scan_only_stubs.c is gone
    #  with the wifi-scan profile: it stood in for the supplicant that profile
    #  did not link, and this one links the real thing.
    #
    #
    #  ここから下は 2 段に分かれる。
    #
    #    (a) ESP-IDF 供給の共通土台（shim と PHY）。bt-classic にも要る——
    #        BlueDroid と BT コントローラは Wi-Fi と同じ FreeRTOS shim / PHY を使う。
    #    (b) Wi-Fi 本体（アダプタ・coex・wifi_init）。bt-classic では
    #        **リンクしてはいけない**。Wi-Fi アーカイブを引かない profile に
    #        これらが混ざると esp_wifi_* / coex_* が丸ごと未定義になる（実測 59 件）。
    #
    target_sources(seam_objects PRIVATE
        "${RUNTIME_ROOT}/arduino/arduino_interrupt.c"
        "${RUNTIME_ROOT}/wifi/shim/esp_shim.c"
        "${RUNTIME_ROOT}/wifi/shim/esp_shim_mtx.c"
        "${RUNTIME_ROOT}/wifi/shim/esp_shim_sem.c"
        "${RUNTIME_ROOT}/wifi/shim/esp_shim_tsk.c"
        "${RUNTIME_ROOT}/wifi/shim/esp_shim_libc.c"
        "${RUNTIME_ROOT}/wifi/shim/esp_shim_blobglue.c"
        "${RUNTIME_ROOT}/wifi/shim/esp_event_shim.c"
        "${RUNTIME_ROOT}/wifi/hal_src/periph_ctrl.c"
        "${RUNTIME_ROOT}/wifi/hal_src/phy_common.c"
        "${RUNTIME_ROOT}/wifi/hal_src/phy_init.c"
        "${RUNTIME_ROOT}/wifi/hal_src/phy_init_data.c"
        "${RUNTIME_ROOT}/wifi/hal_src/phy_lib_printf.c"
        #  名前に反して PHY/RTC 用のスタブ集（esp_timer_*・gpio_config・
        #  rtc_init_clk ほか）。phy_common.c が要求するので Wi-Fi 無しでも要る。
        "${RUNTIME_ROOT}/wifi/shim/wifi_stubs.c"
        #  BT コントローラも coex の調停 API を無条件で呼ぶので Wi-Fi 無しでも要る。
        "${RUNTIME_ROOT}/wifi/shim/esp_coex_adapter.c"
        )
    if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
        #  BlueDroid の FreeRTOS キューの実体。Wi-Fi は osi テーブル経由で
        #  キューを使うため引かない（他 profile の stage を変えないよう限定する）。
        target_sources(seam_objects PRIVATE
            "${RUNTIME_ROOT}/wifi/shim/esp_shim_ring.c"
            #  本物の esp_timer。wifi_stubs.c の no-op は畳まれる。
            "${RUNTIME_ROOT}/wifi/shim/esp_timer_shim.c")
        #  seam_objects 側にも要る（wifi_stubs.c が BT ブロブと衝突する
        #  no-op を畳むのに見ている）。bt_common の同名定義とは別経路。
        target_compile_definitions(wifi_common INTERFACE
            TOPPERS_BT_CLASSIC TOPPERS_ESP32_BT_BLUEDROID_CLASSIC)
    endif()
    if(NOT FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
        target_compile_definitions(wifi_common INTERFACE
            TOPPERS_ESP_WIFI_WPA2)
        target_sources(seam_objects PRIVATE
            #  Scan and connect share one bring-up (toppers_wifi_core.c), so
            #  both adapters live in this one profile. Both used to own a
            #  private esp_wifi_init()/esp_wifi_start() and a private copy of
            #  the pinned WPA callback table, which is what stopped them from
            #  being linked into one profile.
            "${RUNTIME_ROOT}/wifi/adapter/toppers_wifi_scan.c"
            "${RUNTIME_ROOT}/wifi/adapter/toppers_wifi_core.c"
            "${RUNTIME_ROOT}/wifi/adapter/toppers_wifi_optional_stubs.c"
            #  Wi-Fi 専用の critical section 実装。bt-classic では bt_shim.c が
            #  per-core mux つきの本実装を持つので外す（二重定義になる）。
            "${RUNTIME_ROOT}/wifi/shim/esp_shim_bt_crit_wifi.c"
            "${RUNTIME_ROOT}/wifi/shim/esp_wifi_adapter.c"
            "${RUNTIME_ROOT}/wifi/hal_src/esp_wifi_regulatory.c"
            "${RUNTIME_ROOT}/wifi/hal_src/wifi_init.c"
            "${RUNTIME_ROOT}/wifi/hal_src/wifi_lib_printf.c"
            )
    endif()
    target_link_libraries(seam_objects PRIVATE wifi_common)

    if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
        # Wi-Fi initialization emits a burst larger than the FMP3 default
        # 32-entry buffer. Keep the larger buffer local to this profile.
        target_compile_definitions(seam_objects PRIVATE
            TCNT_SYSLOG_BUFFER=128)
        set(NET_INCLUDE_DIRS ${WIFI_INCLUDE_DIRS})
        list(REMOVE_ITEM NET_INCLUDE_DIRS
            "${RUNTIME_ROOT}/wifi/config/hal_stub_include")
        list(PREPEND NET_INCLUDE_DIRS
            "${ARDUINO_SDK_ROOT}/qio_qspi/include")
        add_library(net_common INTERFACE)
        target_compile_definitions(net_common INTERFACE
            ESP_PLATFORM ${A1_CHIP_IDF_DEFS}
            CONFIG_ESP_PHY_ENABLED=1 TOPPERS_ESPIDF_SUPPLY
            TOPPERS_ESP_WIFI_SYSCLK_PLL)
        target_compile_options(net_common INTERFACE
            -Wno-error=implicit-function-declaration)
        target_include_directories(net_common INTERFACE ${NET_INCLUDE_DIRS})
        add_library(net_objects OBJECT
            "${RUNTIME_ROOT}/wifi/adapter/toppers_wifi_connect.c"
            "${RUNTIME_ROOT}/wifi/net/toppers_netif.c"
            "${RUNTIME_ROOT}/wifi/net/sys_arch.c"
            "${RUNTIME_ROOT}/wifi/net/toppers_lwip_compat.c")
        target_link_libraries(net_objects PRIVATE seam_common net_common)
        add_dependencies(net_objects generate_cfg_gen_files)
    endif()
endif()

add_library(seam_start OBJECT
    "${RUNTIME_ROOT}/arch/xtensa_gcc/common/start.S")
target_link_libraries(seam_start PRIVATE seam_common)
target_compile_definitions(seam_start PRIVATE
    ALLFUNC TOPPERS_FLASH_XIP)
target_include_directories(seam_start PRIVATE
    "${FMP3_CORE_ROOT}/kernel")
add_dependencies(seam_start generate_cfg_gen_files)

if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    set(SEAM_CXX_SOURCES
        "${RUNTIME_ROOT}/m5/compat/cxx_runtime.cpp")
else()
    set(SEAM_CXX_SOURCES
        "${RUNTIME_ROOT}/seam/init_array.cpp")
endif()
add_library(seam_cxx OBJECT ${SEAM_CXX_SOURCES})
target_link_libraries(seam_cxx PRIVATE seam_common)
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    target_include_directories(seam_cxx PRIVATE
        "${RUNTIME_ROOT}/m5/compat"
        "${RUNTIME_ROOT}/wifi/shim")
endif()
target_compile_options(seam_cxx PRIVATE
    -fno-exceptions
    -fno-rtti
    -fno-threadsafe-statics
    -std=gnu++17)
add_dependencies(seam_cxx generate_cfg_gen_files)

if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    set(M5_IDF_INCLUDE_DIRS
        "${RUNTIME_ROOT}/m5/compat/freertos"
        "${RUNTIME_ROOT}/m5/compat"
        "${RUNTIME_ROOT}/m5/idf_compat"
        "${RUNTIME_ROOT}/m5/shim"
        "${RUNTIME_ROOT}/wifi/shim"
        "${RUNTIME_ROOT}/wifi/config/${A1_CHIP_ARCH_NAME}"
        "${FMP3_CORE_ROOT}/kernel"
        "${M5GFX_SOURCE_ROOT}"
        "${M5UNIFIED_SOURCE_ROOT}"
        "${TOPPERS_LIBRARY_SOURCE_ROOT}"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_wifi/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_coex/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/include/soc"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/include/esp_private"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/port/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_hw_support/dma/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/heap/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/efuse/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/efuse/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_event/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/log/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_phy/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_phy/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_pm/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_common/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/platform_port/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/hal/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_rom/${A1_CHIP_ARCH_NAME}/include/${A1_CHIP_ARCH_NAME}"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/soc/${A1_CHIP_ARCH_NAME}/register"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_system/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/xtensa/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/xtensa/${A1_CHIP_ARCH_NAME}/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_timer/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_security/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_mm/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/mbedtls/port/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/mbedtls/mbedtls/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/lwip/lwip/src/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_driver_i2c/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/driver/i2c/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_driver_spi/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_driver_gpio/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/nvs_flash/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_driver_ledc/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_driver_i2s/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_driver_rmt/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/driver/deprecated"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_adc/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_adc/deprecated/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_adc/interface"
        "${ARDUINO_SDK_INCLUDE_ROOT}/esp_adc/${A1_CHIP_ARCH_NAME}/include")

    #  platforms/esp32 は共通コードで、その上にチップ別のディレクトリが重なる。
    #  LX6 では両者が同じディレクトリなので二重に並べない。
    #
    #  ★ここにジェネレータ式を書いてはならない。file(GLOB) は configure 時に
    #  評価されるので `$<...>` は展開されず、**文字列のまま glob に渡って何にも
    #  一致しない**。実際 S3 で platforms/esp32s3/*.cpp が丸ごと落ち、
    #  ステージが 138→135 objects になっていた（2026-09-02）。分岐は if で書く。
    if(A1_CHIP_ARCH_NAME STREQUAL "esp32")
        set(M5GFX_CHIP_PLATFORM_GLOB "")
    else()
        set(M5GFX_CHIP_PLATFORM_GLOB
            "${M5GFX_SOURCE_ROOT}/lgfx/v1/platforms/${A1_CHIP_ARCH_NAME}/*.cpp")
    endif()
    file(GLOB M5GFX_CPP_SOURCES
        "${M5GFX_SOURCE_ROOT}/*.cpp"
        "${M5GFX_SOURCE_ROOT}/lgfx/v1/*.cpp"
        "${M5GFX_SOURCE_ROOT}/lgfx/v1/misc/*.cpp"
        "${M5GFX_SOURCE_ROOT}/lgfx/v1/panel/*.cpp"
        "${M5GFX_SOURCE_ROOT}/lgfx/v1/platforms/esp32/*.cpp"
        ${M5GFX_CHIP_PLATFORM_GLOB}
        "${M5GFX_SOURCE_ROOT}/lgfx/v1/touch/*.cpp")
    list(FILTER M5GFX_CPP_SOURCES EXCLUDE REGEX
        "platforms/esp32/(Bus_EPD|Panel_EPD|Bus_Parallel8)\\.cpp$")
    if(A1_CHIP_ARCH_NAME STREQUAL "esp32")
        #  Panel_CVBS はコンポジット映像出力で、FreeRTOS の xTaskNotify /
        #  xPortGetCoreID を直に使う。M5Stack Basic の画面は SPI の ILI9342C
        #  なので要らない。
        #  ★S3 では除外しない。CONFIG_IDF_TARGET_ESP32 が立たず中身は空に
        #  なるが、**オブジェクトは配布物に入っている**（release の
        #  m5-unified ステージに Panel_CVBS.o がある）。除外すると
        #  objectCount と objectOrder が変わり、S3 の成果物が動く。
        list(FILTER M5GFX_CPP_SOURCES EXCLUDE REGEX
            "platforms/esp32/Panel_CVBS\\.cpp$")
    endif()
    #
    #  CJKフォント（lgfx/Fonts/efont・IPA）は既定で外す。1つでappパーティション
    #  (3MB)を超えるため到達不能で、事前ビルドステージに42MB積むだけになっていた
    #  （lgfx_efont_tw 11.6MB / lgfx_font_japan 10.1MB / lgfx_efont_ja 8.8MB の
    #  .rodata。2026-08-22実測）。小さいフォント（古典＋Free Fonts、計70種）は
    #  lgfx_fonts.cpp に入っており、scripts/gen_m5_fonts.py が生成する対応表から
    #  toppers_m5_set_font() で選べる。
    #  パーティションを広げてCJKを載せる場合は A1_M5_CJK_FONTS=ON にする。
    #
    option(A1_M5_CJK_FONTS
        "Include M5GFX CJK fonts (needs an app partition larger than 3MB)" OFF)
    set(M5GFX_C_FONT_GLOBS "")
    if(A1_M5_CJK_FONTS)
        list(APPEND M5GFX_C_FONT_GLOBS
            "${M5GFX_SOURCE_ROOT}/lgfx/Fonts/efont/*.c"
            "${M5GFX_SOURCE_ROOT}/lgfx/Fonts/IPA/*.c")
    endif()
    file(GLOB M5GFX_C_SOURCES
        ${M5GFX_C_FONT_GLOBS}
        "${M5GFX_SOURCE_ROOT}/lgfx/utility/*.c")
    file(GLOB M5UNIFIED_CPP_SOURCES
        "${M5UNIFIED_SOURCE_ROOT}/*.cpp"
        "${M5UNIFIED_SOURCE_ROOT}/utility/*.cpp"
        "${M5UNIFIED_SOURCE_ROOT}/utility/imu/*.cpp"
        "${M5UNIFIED_SOURCE_ROOT}/utility/led/*.cpp"
        "${M5UNIFIED_SOURCE_ROOT}/utility/power/*.cpp"
        "${M5UNIFIED_SOURCE_ROOT}/utility/rtc/*.cpp")
    list(FILTER M5UNIFIED_CPP_SOURCES EXCLUDE REGEX
        "utility/(Speaker|Mic)_Class\\.cpp$")

    add_library(m5_runtime_objects OBJECT
        ${M5GFX_CPP_SOURCES}
        ${M5GFX_C_SOURCES}
        ${M5UNIFIED_CPP_SOURCES}
        "${M5_ARDUINO_ADAPTER_SOURCE}"
        "${RUNTIME_ROOT}/m5/shim/m5_begin_trace.c"
        "${RUNTIME_ROOT}/m5/shim/m5_gpio_stub.c"
        "${RUNTIME_ROOT}/m5/shim/m5_idf_stubs.c"
        "${RUNTIME_ROOT}/m5/shim/m5_kernel_shim.c"
        #  wifi プロファイルと同じ動的セマフォ実装。
        #  ★結合 profile では seam_objects（wifi ブロック）が同じ TU を
        #    コンパイルするので、ここで足すと同じソースが 2 つの .o になり
        #    stage の basename 衝突検査に当たる。下で条件付きに足す。
        "${RUNTIME_ROOT}/m5/shim/m5_libc_alloc_bridge.c"
        "${RUNTIME_ROOT}/m5/shim/m5_libcxx_glue.c"
        "${RUNTIME_ROOT}/m5/shim/m5_spi_bus_stub.c"
        #  LX6 の m5 構成でだけ要るスタブ（gpio / rtc_gpio / SPI DMA）。
        $<$<STREQUAL:${A1_CHIP},esp32>:${RUNTIME_ROOT}/m5/shim/m5_lx6_stubs.c>
        #  LEDC の実装（バックライト）。S3 は m5_idf_stubs.c のスタブのまま。
        $<$<STREQUAL:${A1_CHIP},esp32>:${RUNTIME_ROOT}/m5/shim/m5_lx6_ledc.c>
        #  __getreent は wifi_stubs.c も持つので、wifi を含まない profile
        #  （m5-unified）のときだけ足す。詳細は m5_lx6_reent.c の冒頭。
        $<$<AND:$<STREQUAL:${A1_CHIP},esp32>,$<STREQUAL:${FMP3_RUNTIME_PROFILE},m5-unified>>:${RUNTIME_ROOT}/m5/shim/m5_lx6_reent.c>
        "${RUNTIME_ROOT}/m5/shim/m5_stub_trace.c"
        "${RUNTIME_ROOT}/m5/shim/m5_unified_stubs.c"
        "${RUNTIME_ROOT}/m5/shim/esp_shim_intr.c")
    if(NOT FMP3_RUNTIME_PROFILE STREQUAL "all-in-one")
        #  Only when seam_objects is not already compiling it (see above).
        target_sources(m5_runtime_objects PRIVATE
            "${RUNTIME_ROOT}/wifi/shim/esp_shim_sem.c")
    endif()
    target_link_libraries(m5_runtime_objects PRIVATE seam_common)
    target_include_directories(m5_runtime_objects PRIVATE ${M5_IDF_INCLUDE_DIRS})
    target_compile_definitions(m5_runtime_objects PRIVATE
        ESP_PLATFORM
        ${A1_CHIP_IDF_DEFS}
        CONFIG_ESP_PHY_ENABLED=1
        TOPPERS_ESPIDF_SUPPLY
        TOPPERS_ESP_WIFI_SYSCLK_PLL)
    target_compile_options(m5_runtime_objects PRIVATE
        -ffunction-sections
        -fdata-sections
        "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include ${RUNTIME_ROOT}/m5/idf_compat/m5_idf_prelude.h>"
        "SHELL:$<$<COMPILE_LANGUAGE:CXX>:-include esp_system.h>"
        $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
        $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
        $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
    add_dependencies(m5_runtime_objects generate_cfg_gen_files)
endif()

#
#  Bluetooth Classic (SPP). LX6 only - the ESP32-S3 has no BT Classic - and
#  built from the tree imported from upstream esp/bt.
#
#  The two *_diag.c files are upstream's tracing hooks, behind options there
#  and not built here; they are kept so the option can be added back without
#  another import.
#
#  sdkconfig_override/ comes FIRST on the include path: BlueDroid needs a
#  different configuration from the Wi-Fi side, and whichever sdkconfig.h is
#  found first wins. libc_override/ supplies the few headers the BlueDroid
#  sources expect and this port does not otherwise have.
#
if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
    if(NOT A1_CHIP STREQUAL "esp32")
        message(FATAL_ERROR
            "the bt-classic profile is esp32 (LX6) only; the ESP32-S3 has no "
            "Bluetooth Classic (got A1_CHIP=${A1_CHIP})")
    endif()
    add_library(bt_common INTERFACE)
    target_compile_definitions(bt_common INTERFACE
        ESP_PLATFORM
        ${A1_CHIP_IDF_DEFS}
        CONFIG_ESP_PHY_ENABLED=1
        TOPPERS_ESPIDF_SUPPLY
        #  ★名前に反して「Bluetooth を有効にする」スイッチである。この sdkconfig.h
        #    では CONFIG_BT_ENABLED / CONFIG_BTDM_* が丸ごとこの #ifdef の配下に
        #    あり、立てないと esp_bt.h が「bluetooth is not enabled」分岐に落ちる。
        #    BLE か BR/EDR かは bt/classic/sdkconfig_override が
        #    CONFIG_BTDM_CTRL_MODE_BLE_ONLY を外して BR_EDR_ONLY に付け替える。
        CONFIG_ESPRESSIF_BLE
        #  上流のゲート名。esp_shim_cfg.h の ESP_SHIM_NUM_FLG と esp_shim.cfg の
        #  CRE_FLG（SPP の tx_event_group 用イベントフラグプール）がこれを見る。
        TOPPERS_ESP32_BT_BLUEDROID_CLASSIC
        TOPPERS_BT_CLASSIC)
    target_compile_options(bt_common INTERFACE
        -Wno-error=implicit-function-declaration)
    target_include_directories(bt_common INTERFACE
        #  kernel_cfg.h (cfg が吐く) はここ。
        "${GENERATED_DIR}"
        "${RUNTIME_ROOT}/bt/classic/sdkconfig_override"
        "${RUNTIME_ROOT}/bt/classic/libc_override"
        "${RUNTIME_ROOT}/bt/classic"
        "${RUNTIME_ROOT}/bt/adapter"
        "${RUNTIME_ROOT}/bt"
        "${RUNTIME_ROOT}/bt/stub/include"
        #  BlueDroid's own headers.  The stack itself is not built from source:
        #  the M5Stack core ships it compiled, as libbt.a (host) beside
        #  libbtdm_app.a (controller), so this runtime only supplies the shims
        #  those two archives call into.
        "${ARDUINO_SDK_INCLUDE_ROOT}/bt/common/osi/include"
        "${ARDUINO_SDK_INCLUDE_ROOT}/bt/common/api/include/api"
        "${ARDUINO_SDK_INCLUDE_ROOT}/bt/host/bluedroid/api/include/api"
        "${ARDUINO_SDK_INCLUDE_ROOT}/bt/include/esp32/include"
        ${WIFI_INCLUDE_DIRS})

    file(GLOB BT_SOURCES
        "${RUNTIME_ROOT}/bt/*.c"
        "${RUNTIME_ROOT}/bt/classic/*.c"
        "${RUNTIME_ROOT}/bt/adapter/*.c"
        "${RUNTIME_ROOT}/bt/shim/*.c")
    list(FILTER BT_SOURCES EXCLUDE REGEX "_diag\\.c$")
    add_library(bt_objects OBJECT ${BT_SOURCES})
    target_link_libraries(bt_objects PRIVATE seam_common bt_common)
    add_dependencies(bt_objects generate_cfg_gen_files)
endif()

set(CONSUMER_OBJECTS
    "$<TARGET_OBJECTS:seam_objects>;"
    "$<TARGET_OBJECTS:seam_start>;"
    "$<TARGET_OBJECTS:seam_cxx>")
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    list(APPEND CONSUMER_OBJECTS "$<TARGET_OBJECTS:m5_runtime_objects>")
endif()
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
    list(APPEND CONSUMER_OBJECTS "$<TARGET_OBJECTS:net_objects>")
endif()
if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
    list(APPEND CONSUMER_OBJECTS "$<TARGET_OBJECTS:bt_objects>")
endif()
if(DEFINED ARDUINO_OBJECTS AND NOT "${ARDUINO_OBJECTS}" STREQUAL "")
    foreach(arduino_object IN LISTS ARDUINO_OBJECTS)
        if(NOT EXISTS "${arduino_object}")
            message(FATAL_ERROR
                "Arduino object was not found: ${arduino_object}")
        endif()
        list(APPEND CONSUMER_OBJECTS "${arduino_object}")
    endforeach()
endif()

set(XIP_DIRECTORY "${CMAKE_BINARY_DIR}/xip")
set(XIP_BINARY "${XIP_DIRECTORY}/app_xip.bin")
set(XIP_ELF "${XIP_DIRECTORY}/fmp_xip.elf")
file(MAKE_DIRECTORY "${XIP_DIRECTORY}")

set(XIP_LINK_UFLAGS "")
set(XIP_LINK_LIBGROUP "-lstdc++@@-lm")
set(XIP_EXTRA_TSCRIPTS "")
set(XIP_EXTRA_DEPENDS "")
if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
    #  BlueDroid はソースからは組まない。M5Stack コアが host を libbt.a (lib/)、
    #  controller を libbtdm_app.a (ld/) として同梱しており、この runtime は
    #  その2つが呼ぶ FreeRTOS/ESP-IDF 側の shim だけを供給する。
    set(BT_ARCHIVES
        "${ARDUINO_SDK_LIBRARY_ROOT}/libbt.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libcoexist.a"
        "${ARDUINO_SDK_LD_ROOT}/libbtdm_app.a"
        "${ARDUINO_SDK_LD_ROOT}/libphy.a"
        "${ARDUINO_SDK_LD_ROOT}/librtc.a")
    foreach(archive IN LISTS BT_ARCHIVES)
        if(NOT EXISTS "${archive}")
            message(FATAL_ERROR "Arduino Bluetooth archive not found: ${archive}")
        endif()
    endforeach()
    set(XIP_LINK_LIBGROUP
        "-L${ARDUINO_SDK_LIBRARY_ROOT}@@"
        "-L${ARDUINO_SDK_LD_ROOT}@@"
        "-Wl,--start-group@@"
        "-lbt@@-lbtdm_app@@-lcoexist@@-lphy@@-lrtc@@"
        "-lstdc++@@-lm@@-Wl,--end-group")
    string(JOIN "" XIP_LINK_LIBGROUP ${XIP_LINK_LIBGROUP})
    #  ROM の printf 系は使えない（理由は esp_shim_libc.c の __wrap_ 群の注記）。
    #  呼び出し側ごと自前実装へ寄せる。
    set(XIP_LINK_UFLAGS
        "-Wl,--wrap=sprintf@@-Wl,--wrap=snprintf@@-Wl,--wrap=vsnprintf")
    list(APPEND XIP_EXTRA_DEPENDS ${BT_ARCHIVES})
    #  arduino_interrupt.c が参照する GPIO 等のレジスタ番地定義。
    set(XIP_EXTRA_TSCRIPTS "${ARDUINO_SDK_LD_ROOT}/${A1_CHIP_ARCH_NAME}.peripherals.ld")
endif()
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
    set(WIFI_ARCHIVES
        "${ARDUINO_SDK_LIBRARY_ROOT}/libnet80211.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libpp.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libcore.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libcoexist.a"
        "${ARDUINO_SDK_LIBRARY_ROOT}/libmesh.a"
        "${ARDUINO_SDK_LD_ROOT}/libphy.a")
    foreach(archive IN LISTS WIFI_ARCHIVES)
        if(NOT EXISTS "${archive}")
            message(FATAL_ERROR "Arduino Wi-Fi archive not found: ${archive}")
        endif()
    endforeach()
    set(XIP_LINK_LIBGROUP
        "-L${ARDUINO_SDK_LIBRARY_ROOT}@@"
        "-L${ARDUINO_SDK_LD_ROOT}@@"
        "-Wl,--start-group@@"
        "-lnet80211@@-lpp@@-lcore@@-lcoexist@@-lmesh@@-lphy@@"
        "${A1_CHIP_WIFI_LIBS}"
        "-lstdc++@@-lm@@-Wl,--end-group")
    if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
        set(XIP_LINK_UFLAGS "-Wl,--wrap=esp_supplicant_init")
        #  チップごとに別物である。ソフト暗号でビルドした版が要る理由は
        #  wifi/prebuilt/wpa2/README.md を参照。
        set(WPA2_PREBUILT_ROOT
            "${RUNTIME_ROOT}/wifi/prebuilt/wpa2/${A1_CHIP_ARCH_NAME}")
        list(APPEND WIFI_ARCHIVES
            "${ARDUINO_SDK_LIBRARY_ROOT}/liblwip.a"
            "${WPA2_PREBUILT_ROOT}/libsupplicant.a"
            "${WPA2_PREBUILT_ROOT}/libmbedcrypto.a")
        foreach(archive IN ITEMS
                "${WPA2_PREBUILT_ROOT}/libsupplicant.a"
                "${WPA2_PREBUILT_ROOT}/libmbedcrypto.a")
            if(NOT EXISTS "${archive}")
                message(FATAL_ERROR
                    "FMP3-compatible WPA2 archive not found: ${archive}")
            endif()
        endforeach()
        set(XIP_LINK_LIBGROUP
            "-L${WPA2_PREBUILT_ROOT}@@"
            "-L${ARDUINO_SDK_LIBRARY_ROOT}@@"
            "-L${ARDUINO_SDK_LD_ROOT}@@"
            "-Wl,--start-group@@"
            "-lnet80211@@-lpp@@-lcore@@-lcoexist@@-lmesh@@-lphy@@"
            "${A1_CHIP_WIFI_LIBS}"
            "-lsupplicant@@-lmbedcrypto@@-llwip@@"
            "-lstdc++@@-lm@@-Wl,--end-group")
    endif()
    string(JOIN "" XIP_LINK_LIBGROUP ${XIP_LINK_LIBGROUP})
    list(APPEND XIP_EXTRA_DEPENDS ${WIFI_ARCHIVES})
endif()
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    set(M5_SOC_ARCHIVE "${ARDUINO_SDK_LIBRARY_ROOT}/libsoc.a")
    set(M5_PERIPHERALS_LD "${ARDUINO_SDK_LD_ROOT}/${A1_CHIP_ARCH_NAME}.peripherals.ld")
    foreach(required_path "${M5_SOC_ARCHIVE}" "${M5_PERIPHERALS_LD}")
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR "Arduino M5 runtime input not found: ${required_path}")
        endif()
    endforeach()
    set(M5_BEGIN_WRAPS
        "-Wl,--wrap=_ZN5m5gfx5M5GFX9init_implEbb"
        "-Wl,--wrap=_ZN2m59M5Unified16_check_boardtypeEN4lgfx6boards7board_tE"
        "-Wl,--wrap=_ZN2m59M5Unified13_setup_pinmapEN4lgfx6boards7board_tE"
        "-Wl,--wrap=_ZN2m59M5Unified10_setup_i2cEN4lgfx6boards7board_tE"
        "-Wl,--wrap=_ZN2m59M5Unified10_setup_ledEN4lgfx6boards7board_tE"
        "-Wl,--wrap=_ZN2m59M5Unified6_beginERKNS0_8config_tE"
        "-Wl,--wrap=_ZN2m511Power_Class5beginEv"
        "-Wl,--wrap=_ZN2m59M5Unified12_begin_audioERNS0_8config_tE"
        "-Wl,--wrap=_ZN2m59M5Unified6updateEv"
        "-Wl,--wrap=_ZN2m59M5Unified14_begin_rtc_imuERKNS0_8config_tE"
        "-Wl,--wrap=_ZN2m59RTC_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE"
        "-Wl,--wrap=_ZN2m59IMU_Class5beginEPNS_9I2C_ClassEN4lgfx6boards7board_tE"
        "-Wl,--wrap=_ZN4lgfx2v112Touch_FT5x064initEv")
    #
    #  ★結合 profile では上書きせず合成する。
    #
    #  ここは元々 XIP_LINK_UFLAGS と XIP_LINK_LIBGROUP を **set() で置換**していた。
    #  all-in-one では Wi-Fi ブロックが先に走って両方を設定しているので、置換すると
    #    (a) Wi-Fi の blob 群（-lnet80211/-lpp/-lphy/-lcoexist/…）が消えて
    #        `phy_wifi_enable_set` 等が未定義になる（実測）
    #    (b) **`-Wl,--wrap=esp_supplicant_init` が消える**——これは黙って通り、
    #        OPEN／WPA 初期化分離が壊れて**実機のオープンAP接続でしか露見しない**
    #  ⇒ どちらも append にする。
    #
    if(FMP3_RUNTIME_PROFILE STREQUAL "all-in-one")
        #  Wi-Fi 側の wrap を保ったまま M5 の wrap を足す。
        string(JOIN "@@" _m5_wraps ${M5_BEGIN_WRAPS})
        set(XIP_LINK_UFLAGS "${XIP_LINK_UFLAGS}@@${_m5_wraps}")
        #  Wi-Fi の群へ -lsoc を足す（M5 が要求する）。
        string(REPLACE "-Wl,--start-group@@" "-Wl,--start-group@@-lsoc@@"
               XIP_LINK_LIBGROUP "${XIP_LINK_LIBGROUP}")
    else()
        string(JOIN "@@" XIP_LINK_UFLAGS ${M5_BEGIN_WRAPS})
        set(XIP_LINK_LIBGROUP
            "-L${ARDUINO_SDK_LIBRARY_ROOT}@@-Wl,--start-group@@"
            "-lsoc@@-lstdc++@@-lm@@-Wl,--end-group")
        string(JOIN "" XIP_LINK_LIBGROUP ${XIP_LINK_LIBGROUP})
    endif()
    set(XIP_EXTRA_TSCRIPTS "${M5_PERIPHERALS_LD}")
    list(APPEND XIP_EXTRA_DEPENDS
        m5_runtime_objects "$<TARGET_OBJECTS:m5_runtime_objects>"
        "${M5_SOC_ARCHIVE}" "${M5_PERIPHERALS_LD}")
endif()
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
    #  net_objects は CONSUMER_OBJECTS には入っていたが依存に無く、
    #  lwip互換層を変更してもリンクがやり直されなかった（2026-08-22）。
    list(APPEND XIP_EXTRA_DEPENDS
        net_objects "$<TARGET_OBJECTS:net_objects>")
endif()

add_custom_command(
    OUTPUT "${XIP_BINARY}" "${XIP_ELF}"
    COMMAND "${CMAKE_COMMAND}"
        -DA1_CHIP=${A1_CHIP}
        -DGCC=${CMAKE_C_COMPILER}
        -DAR=${CMAKE_C_COMPILER_AR}
        -DESPTOOL=${A1_ESPTOOL_EXECUTABLE}
        -DSTAGE=${XIP_DIRECTORY}
        -DLIBFMP3=$<TARGET_FILE:fmp3>
        "-DCONSUMER_OBJS=${CONSUMER_OBJECTS}"
        -DFLASHCACHE_SRC=${RUNTIME_ROOT}/boot/flash_cache_init.c
        -DXIP_LD=${A1_XIP_LD}
        -DROMLD_ROOT=${ARDUINO_SDK_LD_ROOT}
        "-DA1_LINK_UFLAGS=${XIP_LINK_UFLAGS}"
        "-DA1_LINK_LIBGROUP=${XIP_LINK_LIBGROUP}"
        "-DA1_EXTRA_TSCRIPTS=${XIP_EXTRA_TSCRIPTS}"
        "-DA1_XIP_PADDR_RUNTIME=${A1_XIP_PADDR_RUNTIME}"
        "-DA1_XIP_PADDR_PROBE=${A1_XIP_PADDR_PROBE}"
        "-DA1_XIP_POISON_PAGE=${A1_XIP_POISON_PAGE}"
        -P "${RUNTIME_ROOT}/cmake/xip_build.cmake"
    DEPENDS
        #  ★ターゲット名だけをDEPENDSへ書くと「順序のみ」の依存になり、そのターゲットの
        #    オブジェクトが再コンパイルされてもリンクがやり直されない。実際に
        #    2026-08-22、fmp_app/phase5 のソースを変更したのに古いオブジェクトのままの
        #    イメージが残り、ソースと一致しないバイナリができていた。
        #    ファイル依存（$<TARGET_OBJECTS:...>／$<TARGET_FILE:...>）も併記する。
        fmp3
        "$<TARGET_FILE:fmp3>"
        "$<TARGET_OBJECTS:seam_objects>"
        "$<TARGET_OBJECTS:seam_start>"
        "$<TARGET_OBJECTS:seam_cxx>"
        seam_objects
        seam_start
        seam_cxx
        ${ARDUINO_OBJECTS}
        "${RUNTIME_ROOT}/boot/flash_cache_init.c"
        "${RUNTIME_ROOT}/cmake/xip_build.cmake"
        "${A1_XIP_LD}"
        ${XIP_EXTRA_DEPENDS}
    COMMENT "Linking ${FMP3_RUNTIME_PROFILE} Arduino/TOPPERS FMP3 XIP image"
    VERBATIM)

add_custom_target(fmp3_arduino_image ALL
    DEPENDS "${XIP_BINARY}" "${XIP_ELF}")

#
#  スケッチに依存しない成果物のステージング。
#
#  cfgはprofile固定でスケッチに依存しないので、カーネルとruntimeはリリース時に
#  固めて配布できる。ARDUINO_OBJECTS を渡さずに
#      cmake --build <dir> --target fmp3_prebuilt
#  で ${CMAKE_BINARY_DIR}/prebuilt/ へ出力する（ALLには入れない）。
#
set(PREBUILT_DIRECTORY "${CMAKE_BINARY_DIR}/prebuilt")
set(PREBUILT_MANIFEST "${PREBUILT_DIRECTORY}/link-manifest.json")

#  利用者環境のドライバが用意しなければならないArduino側オブジェクト。
set(PREBUILT_ARDUINO_OBJECTS "<sketch>.cpp.o@@ArduinoSketchBridge.cpp.o")
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
    set(PREBUILT_ARDUINO_OBJECTS
        "${PREBUILT_ARDUINO_OBJECTS}@@ToppersFMP3_WiFi.cpp.o")
endif()
if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
    set(PREBUILT_ARDUINO_OBJECTS
        "${PREBUILT_ARDUINO_OBJECTS}@@ToppersFMP3_BT.cpp.o")
endif()

set(PREBUILT_STAGE_DEPENDS fmp3 seam_objects seam_start seam_cxx)
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    list(APPEND PREBUILT_STAGE_DEPENDS m5_runtime_objects)
endif()
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
    list(APPEND PREBUILT_STAGE_DEPENDS net_objects)
endif()

#  消費側オブジェクトのみ（ARDUINO_OBJECTS は含めない）
set(PREBUILT_CONSUMER_OBJECTS
    "$<TARGET_OBJECTS:seam_objects>;"
    "$<TARGET_OBJECTS:seam_start>;"
    "$<TARGET_OBJECTS:seam_cxx>")
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    list(APPEND PREBUILT_CONSUMER_OBJECTS "$<TARGET_OBJECTS:m5_runtime_objects>")
endif()
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
    list(APPEND PREBUILT_CONSUMER_OBJECTS "$<TARGET_OBJECTS:net_objects>")
endif()
if(FMP3_RUNTIME_PROFILE STREQUAL "bt-classic")
    list(APPEND PREBUILT_CONSUMER_OBJECTS "$<TARGET_OBJECTS:bt_objects>")
endif()

#  消費側オブジェクトはコマンドラインではなくファイルで渡す。
#  m5-unified は約200個・22,000文字あり、-D で渡すと生成コマンドが
#  Windowsのバッチ行の限界に触れる。実測では 24,859 文字で
#  「指定されたファイルが見つかりません。」だけを出して cmake が起動せず、
#  RC=0 のまま stage が作られないという分かりにくい失敗になった
#  （引数を67文字削ると通る、という閾値の振る舞いだった）。
#  file(GENERATE) はジェネレータ式を展開できるので $<TARGET_OBJECTS:...> が使える。
set(PREBUILT_CONSUMER_OBJS_FILE
    "${CMAKE_CURRENT_BINARY_DIR}/prebuilt-consumer-objects.txt")
file(GENERATE OUTPUT "${PREBUILT_CONSUMER_OBJS_FILE}"
     CONTENT "$<JOIN:${PREBUILT_CONSUMER_OBJECTS},\n>\n")

set(PREBUILT_PERIPHERALS_LD "")
if(FMP3_RUNTIME_PROFILE MATCHES "^(m5-unified|all-in-one)$")
    set(PREBUILT_PERIPHERALS_LD "${M5_PERIPHERALS_LD}")
endif()

#  wifi-connect が -L で参照するリポジトリ内のWPA2アーカイブ。ステージへ同梱し、
#  manifest では @STAGE@/lib として記録する（配布物の中で自己完結させる）。
set(PREBUILT_WPA_LIB_DIR "")
if(FMP3_RUNTIME_PROFILE MATCHES "^(wifi-connect|all-in-one)$")
    set(PREBUILT_WPA_LIB_DIR "${WPA2_PREBUILT_ROOT}")
endif()

add_custom_command(
    OUTPUT "${PREBUILT_MANIFEST}"
    COMMAND "${CMAKE_COMMAND}"
        -DSTAGE_DIR=${PREBUILT_DIRECTORY}
        -DAR=${CMAKE_C_COMPILER_AR}
        -DGCC=${CMAKE_C_COMPILER}
        -DLIBFMP3=$<TARGET_FILE:fmp3>
        "-DCONSUMER_OBJS_FILE=${PREBUILT_CONSUMER_OBJS_FILE}"
        -DFLASHCACHE_SRC=${RUNTIME_ROOT}/boot/flash_cache_init.c
        -DPROFILE=${FMP3_RUNTIME_PROFILE}
        -DA1_CHIP=${A1_CHIP}
        -DXIP_LD=${A1_XIP_LD}
        -DROMLD_ROOT=${ARDUINO_SDK_LD_ROOT}
        "-DSDK_LIBRARY_ROOT=${ARDUINO_SDK_LIBRARY_ROOT}"
        "-DPERIPHERALS_LD=${PREBUILT_PERIPHERALS_LD}"
        "-DWPA_LIB_DIR=${PREBUILT_WPA_LIB_DIR}"
        "-DOBJCOPY=${CMAKE_OBJCOPY}"
        "-DSTRIP_DEBUG=${A1_PREBUILT_STRIP_DEBUG}"
        "-DLINK_UFLAGS=${XIP_LINK_UFLAGS}"
        "-DLINK_LIBGROUP=${XIP_LINK_LIBGROUP}"
        "-DEXTRA_TSCRIPTS=${XIP_EXTRA_TSCRIPTS}"
        "-DXIP_PADDR_RUNTIME=${A1_XIP_PADDR_RUNTIME}"
        "-DARDUINO_OBJECT_NAMES=${PREBUILT_ARDUINO_OBJECTS}"
        -P "${RUNTIME_ROOT}/cmake/prebuilt_stage.cmake"
    DEPENDS
        #  ターゲット名は順序依存にしかならないので、オブジェクトも列挙する
        #  （fmp3_arduino_image と同じ理由。上の注記参照）。
        ${PREBUILT_STAGE_DEPENDS}
        "$<TARGET_FILE:fmp3>"
        ${PREBUILT_CONSUMER_OBJECTS}
        "${RUNTIME_ROOT}/boot/flash_cache_init.c"
        "${RUNTIME_ROOT}/cmake/prebuilt_stage.cmake"
        "${A1_XIP_LD}"
    COMMENT "Staging sketch-independent ${FMP3_RUNTIME_PROFILE} FMP3 artifacts"
    VERBATIM)

add_custom_target(fmp3_prebuilt DEPENDS "${PREBUILT_MANIFEST}")
