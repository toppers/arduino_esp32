/*
 *  M5Unified profile の自己診断
 *
 *  ★これは**テスト**であって配布物の一部ではない。
 *    stage を -SelfTest 付きで建てたときだけリンクされる
 *    （New-Fmp3PrebuiltStages.ps1）。配布する stage は
 *    phase5_m5_app.cfg だけを使い、この監視タスクは存在しない。
 *
 *  分離した理由: 配布するランタイムに「60 秒かけて自己診断し、
 *  条件を満たさないと FAILED と表示するタスク」が常駐しているのは
 *  製品として筋が悪い。利用者のスケッチには関係のない検査である。
 */
#include "phase5_m5_selftest.h"

#include <stdint.h>
#include <target_syssvc.h>

/*
 *  スケッチ側の計装変数
 *
 *  ★weak 定義である。スケッチが強い定義を持てばそちらが勝つ。
 *    かつては `extern` だけだったので、**この 14 個を書き写さない限り
 *    ユーザのスケッチはリンクできなかった**
 *    （undefined reference: phase5_begin_result …）。
 *    M5Unified profile は最も使われる想定の profile なので、
 *    「ボードを選んだだけで普通のスケッチがビルドできない」のは
 *    配布物として成立しない。既定値をここで与える。
 *    計装が無い場合、下の自己診断は何も報告せずに終わる。
 */
__attribute__((weak)) volatile int32_t phase5_begin_result;
__attribute__((weak)) volatile int32_t phase5_board;
__attribute__((weak)) volatile int32_t phase5_display_width;
__attribute__((weak)) volatile int32_t phase5_display_height;
__attribute__((weak)) volatile int32_t phase5_touch_enabled;
__attribute__((weak)) volatile int32_t phase5_imu_enabled;
__attribute__((weak)) volatile int32_t phase5_rtc_enabled;
__attribute__((weak)) volatile int32_t phase5_power_type;
__attribute__((weak)) volatile int32_t phase5_battery_mv;
__attribute__((weak)) volatile uint32_t phase5_updates;
__attribute__((weak)) volatile uint32_t phase5_touch_events;
__attribute__((weak)) volatile uint32_t phase5_liveness_seconds;
__attribute__((weak)) volatile uint32_t phase5_trace_enters;
__attribute__((weak)) volatile uint32_t phase5_trace_leaves;

volatile uint32_t phase5_monitor_pass;
volatile uint32_t phase5_monitor_failures;

static void
phase5_log(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

static void
phase5_log_u32(uint32_t value)
{
    uint32_t divisor = 1U;

    while ((value / divisor) >= 10U) {
        divisor *= 10U;
    }
    do {
        target_fput_log((char) ('0' + ((value / divisor) % 10U)));
        divisor /= 10U;
    } while (divisor != 0U);
}

static void
phase5_check(int condition, const char *name)
{
    if (!condition) {
        phase5_monitor_failures++;
        phase5_log("[M5] FAIL: ");
        phase5_log(name);
        phase5_log("\n");
    }
}

void
phase5_monitor_task(EXINF exinf)
{
    uint32_t waited = 0U;
    uint32_t last_report = 0U;

    (void) exinf;
    phase5_log("[M5] FMP3 monitor task start\n");

    while ((phase5_begin_result == 0) && (waited < 30U)) {
        (void) dly_tsk(1000000U);
        waited++;
    }

    /*
     *  計装の有無を判定する
     *
     *  スケッチが計装していれば setup() が必ず
     *  `phase5_begin_result = toppers_m5_begin();` を書く。
     *  この関数は成功で正、失敗で -1 を返すので **0 にはならない**。
     *  よって 30 秒待っても 0 のままなら「スケッチが書いていない」と
     *  一意に判定できる。ユーザの普通のスケッチはここへ来るので、
     *  FAIL を並べずに黙って終わる。
     */
    if (phase5_begin_result == 0) {
        phase5_log("[M5] sketch is not instrumented; "
                   "self-test skipped\n");
        ext_tsk();
    }

    phase5_check(phase5_begin_result > 0, "M5.begin and initial draw");
    if (phase5_begin_result < 0) {
        phase5_log("[M5] monitor FAILED before liveness test\n");
        ext_tsk();
    }

    while (phase5_liveness_seconds < 60U) {
        (void) dly_tsk(1000000U);
        if ((phase5_liveness_seconds / 10U) > (last_report / 10U)) {
            last_report = phase5_liveness_seconds;
            phase5_log("[M5] alive ");
            phase5_log_u32(phase5_liveness_seconds);
            phase5_log("s updates=");
            phase5_log_u32(phase5_updates);
            phase5_log(" touches=");
            phase5_log_u32(phase5_touch_events);
            phase5_log("\n");
        }
    }

    phase5_check((phase5_board == 10) || (phase5_board == 17),
                 "CoreS3 board identification");
    phase5_check((phase5_display_width > 0) && (phase5_display_height > 0),
                 "LCD dimensions");
    phase5_check(phase5_touch_enabled != 0, "touch controller enabled");
    /*
     * CoreS3-SE (board 17) intentionally has no BMI270/BMM150. CoreS3
     * (board 10) must initialize the IMU; SE must report it absent.
     */
    phase5_check(((phase5_board == 10) && (phase5_imu_enabled != 0)) ||
                 ((phase5_board == 17) && (phase5_imu_enabled == 0)),
                 "IMU state matches the detected model");
    phase5_check(phase5_rtc_enabled != 0, "RTC enabled");
    phase5_check(phase5_power_type == 4, "AXP2101 PMIC detected");
    phase5_check(phase5_updates >= 500U, "M5.update continued for 60 seconds");
    phase5_check(phase5_trace_enters == phase5_trace_leaves,
                 "M5.begin/update trace balance");

    phase5_log("[M5] board=");
    phase5_log_u32((uint32_t) phase5_board);
    phase5_log(" display=");
    phase5_log_u32((uint32_t) phase5_display_width);
    phase5_log("x");
    phase5_log_u32((uint32_t) phase5_display_height);
    phase5_log(" pmic=");
    phase5_log_u32((uint32_t) phase5_power_type);
    phase5_log(" battery_mV=");
    phase5_log_u32((uint32_t) phase5_battery_mv);
    phase5_log("\n");

    if (phase5_monitor_failures == 0U) {
        phase5_monitor_pass = 1U;
        phase5_log("[M5] 60-second M5Unified integration PASS\n");
    }
    else {
        phase5_log("[M5] 60-second M5Unified integration FAILED\n");
    }
    ext_tsk();
}

/*
 *  ------------------------------------------------------------------
 *  SMP 分離の自己診断
 *  ------------------------------------------------------------------
 *  m5-unified が FMP3_PRC_NUM=2 で建つようになり dual-core profile を
 *  廃止したので、**その検査までは捨てないよう**ここへ移した。
 *  移さなければ「SMP 配置が壊れても誰も気付かない」状態になる。
 *
 *  検査するのは配置と前進だけで、M5 側の 60 秒テストとは独立である。
 *  スケッチ側の計装（DualCore が持つ 3 つのカウンタ）は
 *  weak 定義なので、計装していないスケッチではその分を飛ばす。
 */
__attribute__((weak)) volatile uint32_t phase6_sketch_setup_count;
__attribute__((weak)) volatile uint32_t phase6_sketch_loop_count;
__attribute__((weak)) volatile int32_t phase6_arduino_processor;

volatile uint32_t phase5_prc2_iterations;
volatile int32_t phase5_smp_monitor_processor;
volatile int32_t phase5_prc2_processor;
volatile uint32_t phase5_smp_pass;
volatile uint32_t phase5_smp_failures;

static void
phase5_smp_check(int condition, const char *name)
{
    if (!condition) {
        phase5_smp_failures++;
        phase5_log("[SMP] FAIL: ");
        phase5_log(name);
        phase5_log("\n");
    }
}

void
phase5_prc2_task(EXINF exinf)
{
    ID processorId = 0;

    (void) exinf;
    if (get_pid(&processorId) == E_OK) {
        phase5_prc2_processor = processorId;
    }

    for (;;) {
        ++phase5_prc2_iterations;
        (void) dly_tsk(1000U);
    }
}

void
phase5_smp_monitor_task(EXINF exinf)
{
    ID processorId = 0;
    int instrumented;

    (void) exinf;
    if (get_pid(&processorId) == E_OK) {
        phase5_smp_monitor_processor = processorId;
    }
    phase5_log("[SMP] monitor start\n");

    (void) dly_tsk(5000000U);

    instrumented = (phase6_sketch_setup_count != 0U)
                   || (phase6_sketch_loop_count != 0U);

    phase5_smp_check(phase5_smp_monitor_processor == 1,
                     "monitor is fixed to PRC1");
    phase5_smp_check(phase5_prc2_processor == 2,
                     "independent worker is fixed to PRC2");
    phase5_smp_check(phase5_prc2_iterations > 100U,
                     "independent worker advanced on PRC2");
    if (instrumented) {
        phase5_smp_check(phase6_arduino_processor == 1,
                         "Arduino task is fixed to PRC1");
        phase5_smp_check(phase6_sketch_loop_count > 100U,
                         "Arduino loop advanced on PRC1");
    }
    else {
        phase5_log("[SMP] sketch is not instrumented; "
                   "2 sketch-side checks skipped\n");
    }

    phase5_log("[SMP] Arduino loops=");
    phase5_log_u32(phase6_sketch_loop_count);
    phase5_log(" PRC2 iterations=");
    phase5_log_u32(phase5_prc2_iterations);
    phase5_log("\n");

    if (phase5_smp_failures == 0U) {
        phase5_smp_pass = 1U;
        phase5_log("[SMP] dual-core isolation PASS\n");
    }
    else {
        phase5_log("[SMP] dual-core isolation FAILED\n");
    }
    ext_tsk();
}
