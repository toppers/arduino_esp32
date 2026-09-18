/*
 *  TOPPERS/FMP3 の sample1 に相当する縮約版
 *
 *  本家（fmp3_core/sample/sample1.c）は 31 タスク・6 周期通知・6 アラーム
 *  通知・6 データキューを cfg で静的に作り、**シリアルから打つコマンド**で
 *  動かします。このポートでは 2 点が成り立たないので、要点だけを残した
 *  縮約版にしてあります。
 *
 *    (1) スケッチから cfg にオブジェクトを足せない。stage の cfg は
 *        「スケッチ非依存で固定」がこのポートの設計だからです。
 *        ⇒ タスクは実行時に作り（`toppers_fmp3_task_create`）、周期通知と
 *          アラーム通知は stage が持つ 1 個ずつを使います。
 *    (2) Serial 入力がありません（M5Stack core のランタイムを
 *        リンクしないため。README の「制約」）。
 *        ⇒ コマンドの代わりに、周期通知が進行を進めます。
 *
 *  残した要点は本家と同じです:
 *    - 優先度の違う 3 つのタスクが順に走ること
 *    - 起床待ち（slp_tsk）と起床（wup_tsk）でタスクが動くこと
 *    - 周期通知が一定間隔で上がること
 *    - アラーム通知が一度だけ上がること
 *    - 優先度を変えると走る順番が変わること
 *
 *  出力は FMP3 の syslog へ出ます（`Serial` ではありません）。
 *
 *  **Minimal 構成専用**です。カーネル API のラッパは minimal stage にしか
 *  入っていません。
 */

#include <ToppersFMP3_ArduinoBridge.h>

#if defined(TOPPERS_FMP3_RUNTIME_SELECTED) \
    && !defined(TOPPERS_FMP3_RUNTIME_MINIMAL)
#error "Fmp3Sample1 needs Tools > FMP3 Runtime > Minimal. The kernel API wrapper it calls is linked into that runtime alone."
#endif

#include <ToppersFMP3_Kernel.h>

//  Minimal 構成には Wi-Fi 側のログ関数がありません。Blink と同じく
//  FMP3 の低レベル出力を直接使います（`Serial` はこのポートにありません）。
extern "C" void target_fput_log(char character);

static void logLine(const char *text)
{
    for (const char *p = text; *p != '\0'; ++p) { target_fput_log(*p); }
    target_fput_log('\n');
}

//  ★バッファを static にしてあるのは趣味ではありません。M5Stack core は
//  スケッチを `-fstack-protector` でコンパイルするので、**ローカルの char
//  配列を持つ関数**は `__stack_chk_fail` を参照し、Minimal 構成ではそこから
//  newlib の `_write` が未定義になってリンクに落ちます（実測）。
//  同梱の `GpioInterrupt` が静的バッファを使っているのも同じ理由です。
//  呼ぶのは setup() / loop()（タスク文脈）だけなので競合しません。
static void logNumber(const char *tag, long value)
{
    static char buffer[96];
    int index = 0;
    while (tag[index] != '\0' && index < 70) { buffer[index] = tag[index]; index++; }
    long rest = value;
    if (rest < 0) { buffer[index++] = '-'; rest = -rest; }
    static char digits[12];
    int count = 0;
    do { digits[count++] = (char)('0' + (rest % 10)); rest /= 10; } while (rest != 0);
    while (count > 0) { buffer[index++] = digits[--count]; }
    buffer[index] = '\0';
    logLine(buffer);
}

//  スタックはスケッチが持ちます。stage 側に置くと、この例題を使わない
//  スケッチまで RAM を払うためです（Minimal は全ボードの既定構成）。
static uint8_t stack1[2048] __attribute__((aligned(16)));
static uint8_t stack2[2048] __attribute__((aligned(16)));
static uint8_t stack3[2048] __attribute__((aligned(16)));

static int32_t taskId[3] = { -1, -1, -1 };
static volatile uint32_t runCount[3];
static volatile uint32_t cyclicCount;
static volatile uint32_t alarmCount;

//  3 つのタスクは同じ本体を共有し、拡張情報で自分が何番かを知ります。
//  本家の task1/task2/task3 と同じ構造です。
static void taskBody(intptr_t exinf)
{
    const int32_t index = (int32_t)exinf;
    for (;;) {
        runCount[index]++;
        logNumber("[Sample1] task ran, index=", (long)index);
        //  起床待ち。周期通知が wup_tsk で起こします。
        (void)toppers_fmp3_task_sleep();
    }
}

static void cyclicCallback(void)
{
    cyclicCount++;
    //  周期通知は非タスク文脈で走ります。ここでやるのは起床だけにして、
    //  実際の仕事はタスク側に置きます（本家の cyclic_handler と同じ考え）。
    for (int i = 0; i < 3; i++) {
        if (taskId[i] > 0) { (void)toppers_fmp3_task_wakeup(taskId[i]); }
    }
}

static void alarmCallback(void)
{
    alarmCount++;
}

void setup()
{
    logLine("[Sample1] start");

    static const int32_t priority[3] = { 11, 12, 13 };
    void *const stacks[3] = { stack1, stack2, stack3 };

    for (int i = 0; i < 3; i++) {
        const int32_t created = toppers_fmp3_task_create(
            taskBody, (intptr_t)i, priority[i], stacks[i], 2048U);
        if (created <= 0) {
            logNumber("[Sample1] task_create failed, error=", (long)created);
            return;
        }
        taskId[i] = created;
        logNumber("[Sample1] created task id=", (long)created);
        const int32_t activated = toppers_fmp3_task_activate(created);
        if (activated != 0) {
            logNumber("[Sample1] activate failed, error=", (long)activated);
            return;
        }
    }

    const int32_t cyclic = toppers_fmp3_cyclic_start(cyclicCallback);
    logNumber("[Sample1] cyclic_start=", (long)cyclic);
    const int32_t alarm = toppers_fmp3_alarm_start(alarmCallback, 3000000U);
    logNumber("[Sample1] alarm_start (3 s)=", (long)alarm);
}

static uint32_t loops;
static bool swapped;

void loop()
{
    //  delay() はありません。loop() は周期的に呼ばれます。
    if (++loops % 50U != 0U) { return; }

    logNumber("[Sample1] cyclic count=", (long)cyclicCount);
    logNumber("[Sample1] alarm count=", (long)alarmCount);
    for (int i = 0; i < 3; i++) {
        logNumber("[Sample1] run count, index=", (long)i);
        logNumber("[Sample1]   count=", (long)runCount[i]);
    }

    //  本家の chg_pri 相当。優先度を入れ替えると、同じ周期で起こされた
    //  3 タスクの走る順番が変わります。
    if (!swapped && cyclicCount >= 5U && taskId[0] > 0 && taskId[2] > 0) {
        const int32_t a = toppers_fmp3_task_change_priority(taskId[0], 13);
        const int32_t b = toppers_fmp3_task_change_priority(taskId[2], 11);
        logNumber("[Sample1] change_priority a=", (long)a);
        logNumber("[Sample1] change_priority b=", (long)b);
        swapped = true;
    }
}
