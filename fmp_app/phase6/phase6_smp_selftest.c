/*
 *  dual-core profile の自己診断
 *
 *  ★これは**テスト**であって配布物の一部ではない。
 *    stage を -SelfTest 付きで建てたときだけリンクされる
 *    （New-Fmp3PrebuiltStages.ps1）。
 *
 *  ここに在る PRC2 のワーカタスクも「2 個目のコアが独立に走ることを
 *  示すための実演」であって製品機能ではない。配布 stage では PRC2 は
 *  空いたままになる（スケッチから PRC2 を使う手段はまだ無い——
 *  cfg が stage 側で固定されているため。これは今後の課題である）。
 */
#include "phase6_smp_selftest.h"

#include <stdint.h>
#include <target_syssvc.h>

/*
 *  スケッチ側の計装カウンタ
 *
 *  ★weak 定義である。スケッチが強い定義を持てばそちらが勝つ。
 *    かつては `extern` だけだったので、**計装していない普通のスケッチは
 *    リンクできなかった**（undefined reference: phase6_sketch_setup_count …）。
 *    profile を選んだだけでユーザのスケッチがビルドできないのは配布物として
 *    成立しないので、既定値をここで与える。
 *    計装が無い場合は下の自己診断がスケッチ依存の検査を飛ばす。
 */
__attribute__((weak)) volatile uint32_t phase6_sketch_setup_count;
__attribute__((weak)) volatile uint32_t phase6_sketch_loop_count;
__attribute__((weak)) volatile int32_t phase6_arduino_processor;

volatile uint32_t phase6_prc2_iterations;
volatile int32_t phase6_prc2_processor;
volatile int32_t phase6_monitor_processor;
volatile uint32_t phase6_monitor_pass;
volatile uint32_t phase6_monitor_failures;

static void
phase6_log(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text++);
    }
}

static void
phase6_log_u32(uint32_t value)
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
phase6_check(int condition, const char *name)
{
    if (!condition) {
        phase6_monitor_failures++;
        phase6_log("[SMP] FAIL: ");
        phase6_log(name);
        phase6_log("\n");
    }
}

void
phase6_prc2_task(EXINF exinf)
{
    ID processorId = 0;

    (void) exinf;
    if (get_pid(&processorId) == E_OK) {
        phase6_prc2_processor = processorId;
    }

    for (;;) {
        ++phase6_prc2_iterations;
        (void) dly_tsk(1000U);
    }
}

void
phase6_monitor_task(EXINF exinf)
{
    ID processorId = 0;
    int instrumented;

    (void) exinf;
    if (get_pid(&processorId) == E_OK) {
        phase6_monitor_processor = processorId;
    }
    phase6_log("[SMP] SMP monitor start\n");

    (void) dly_tsk(5000000U);

    /*
     *  スケッチが計装しているか
     *
     *  例題 DualCore は setup() で setup_count を、loop() で
     *  loop_count を必ず加算する。loop() は 1ms ごとに呼ばれるので、
     *  5 秒待って両方 0 なら計装が無い＝ユーザの普通のスケッチである。
     *  そこへ FAIL を並べるのは誤報なので、**スケッチ依存の 3 件だけ**を
     *  飛ばす。カーネル側の配置と PRC2 の前進は計装と無関係に検査できるので
     *  そのまま回す（自己診断を丸ごと無効化はしない）。
     *  ★計装済みなのに setup() が一度も走らなかった場合もここへ入る。
     *    区別できないので、生の数値を必ず印字して読み手に判断させる。
     */
    instrumented = (phase6_sketch_setup_count != 0U)
                   || (phase6_sketch_loop_count != 0U);

    phase6_check(phase6_monitor_processor == 1, "monitor is fixed to PRC1");
    phase6_check(phase6_prc2_processor == 2,
                 "independent worker is fixed to PRC2");
    phase6_check(phase6_prc2_iterations > 100U,
                 "independent worker advanced on PRC2");
    if (instrumented) {
        phase6_check(phase6_arduino_processor == 1,
                     "Arduino task is fixed to PRC1");
        phase6_check(phase6_sketch_setup_count == 1U,
                     "Arduino setup ran exactly once");
        phase6_check(phase6_sketch_loop_count > 100U,
                     "Arduino loop advanced on PRC1");
    }
    else {
        phase6_log("[SMP] sketch is not instrumented; "
                   "3 sketch-side checks skipped\n");
    }

    phase6_log("[SMP] Arduino loops=");
    phase6_log_u32(phase6_sketch_loop_count);
    phase6_log(" PRC2 iterations=");
    phase6_log_u32(phase6_prc2_iterations);
    phase6_log("\n");

    if (phase6_monitor_failures == 0U) {
        phase6_monitor_pass = 1U;
        if (instrumented) {
            phase6_log("[SMP] dual-core isolation PASS\n");
        }
        else {
            phase6_log("[SMP] dual-core isolation PASS "
                       "(kernel-side checks only)\n");
        }
    }
    else {
        phase6_log("[SMP] dual-core isolation FAILED\n");
    }
    ext_tsk();
}
