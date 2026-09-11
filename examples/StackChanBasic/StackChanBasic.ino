/*
 *  StackChanBasic - 画面に顔を出して反応させる入門サンプル
 *
 *  CoreS3 / M5Stack Basic / M5StickS3 のどれでも動く。顔の各部は画面の
 *  短い辺に対する割合で決めているので、240x135 の StickS3 でも同じ
 *  見た目になる。
 *
 *  Tools > FMP3 Runtime > M5Unified + Dual Core を選んでビルドする。
 *
 *  やること:
 *    - 顔を表示する（図形だけ。画像ファイルは使わない）
 *    - ときどき自動でまばたきする
 *    - 画面をタッチすると Happy になる
 *    - しばらく触らないと Sleepy になり、タッチで起きる
 *
 *  改造するときに見る場所:
 *    - 色を変える               -> COLOR_ 定数
 *    - 顔の大きさや位置を変える -> EYE_ / MOUTH_ の ..._PCT 定数
 *    - 目や口の形を変える       -> drawOneEye() / drawMouth()
 *    - 眠るまでの時間を変える   -> SLEEP_TIMEOUT_MS
 *    - 表情を増やす             -> FaceExpression に追加して
 *                                  drawOneEye() / drawMouth() に分岐を足す
 */

/*
 *  ★<M5Unified.h> ではなく必ずこちらを include する。
 *    スケッチは -DARDUINO 付きでコンパイルされるが、同梱ランタイムの
 *    M5Unified は ARDUINO 未定義でビルドされているため、素朴に
 *    <M5Unified.h> を読むとクラスのレイアウトが 4 バイトずれて
 *    M5.Display.width() で落ちる。理由はこのヘッダの先頭コメントにある。
 */
#include <ToppersFMP3_M5Unified.h>

/*
 *  M5Unified is linked into the m5-unified runtimes alone. Without this guard
 *  the wrong Tools > FMP3 Runtime option reaches the linker and fails there on
 *  M5 symbols, naming nothing a beginner could act on. The SELECTED define
 *  says the platform passes the selection at all, so a platform older than it
 *  is not rejected by a guard it cannot answer.
 */
#if defined(TOPPERS_FMP3_RUNTIME_SELECTED) \
    && !defined(TOPPERS_FMP3_RUNTIME_M5_UNIFIED) \
    && !defined(TOPPERS_FMP3_RUNTIME_ALL_IN_ONE)
#error "StackChanBasic needs the M5Unified runtime. Select Tools > FMP3 Runtime > M5Unified + Dual Core. M5Unified is linked into that runtime only, so any other option leaves this sketch without it."
#endif

/*
 *  このポートは Arduino コア（core.a）をリンクしないので、
 *  Serial / millis() / delay() / random() は使えない。
 *  代わりにランタイムが出している次の 2 つを使う。
 *    esp_shim_time_us() : 起動からの経過時間（マイクロ秒）
 *    target_fput_log()  : シリアルモニタへ 1 文字出す
 */
extern "C" int64_t esp_shim_time_us(void);
extern "C" void target_fput_log(char character);

/* ------------------------------------------------------------------ */
/*  調整用の定数                                                      */
/* ------------------------------------------------------------------ */

/*  何ミリ秒さわらないと眠るか */
constexpr uint32_t SLEEP_TIMEOUT_MS = 15000;

/*  Happy な顔をしている長さ */
constexpr uint32_t HAPPY_DURATION_MS = 1000;

/*  まばたき：目を閉じている長さと、次に閉じるまでの間隔 */
constexpr uint32_t BLINK_CLOSED_MS = 120;
constexpr uint32_t BLINK_INTERVAL_MIN_MS = 2000;
constexpr uint32_t BLINK_INTERVAL_MAX_MS = 6000;

/*  色（TFT_ で始まる名前は M5GFX が用意している） */
constexpr int COLOR_BACKGROUND = TFT_BLACK;
constexpr int COLOR_EYE = TFT_CYAN;
constexpr int COLOR_MOUTH = TFT_CYAN;
constexpr int COLOR_SLEEP_MARK = TFT_DARKGREY;

/*
 *  顔の形。**画面の短い辺に対する割合（％）**で決めている。
 *
 *  ピクセルで書くと画面の小さいボードで顔がはみ出す。このパッケージには
 *  320x240 の CoreS3 と M5Stack Basic、240x135 の M5StickS3 が入っており、
 *  たとえば口の位置を中心から 45px 下にすると StickS3（中心から下へ 67px
 *  しかない）では口が画面外に出る。短い辺の割合にしておけば、どのボードでも
 *  同じ見た目になる。
 *
 *  数字を大きくすればその部品が大きく（遠く）なる。実際のピクセル値は
 *  setup() の computeFaceLayout() が一度だけ計算する。
 */
constexpr int EYE_OFFSET_X_PCT = 23;      /* 中心から目までの左右の距離 */
constexpr int EYE_OFFSET_Y_PCT = -8;      /* 中心から目までの上下の距離（負なら上） */
constexpr int EYE_RADIUS_PCT = 11;        /* 目の大きさ */
constexpr int EYE_CLOSED_HEIGHT_PCT = 3;  /* 閉じた目（横棒）の太さ */
constexpr int MOUTH_OFFSET_Y_PCT = 19;    /* 中心から口までの距離 */
constexpr int MOUTH_RADIUS_PCT = 10;      /* 笑った口の大きさ */
constexpr int MOUTH_BAR_WIDTH_PCT = 7;   /* ふつうの口（横棒）の長さの半分 */
constexpr int MOUTH_BAR_HEIGHT_PCT = 4;   /* 同じ横棒の太さ */
constexpr int SLEEP_DOT_RADIUS_PCT = 3;   /* 眠っている口（小さな丸） */

/* ------------------------------------------------------------------ */
/*  表情                                                              */
/* ------------------------------------------------------------------ */

enum class FaceExpression
{
    Normal,
    Happy,
    Sleepy
};

namespace {

/* ------------------------------------------------------------------ */
/*  いまの状態                                                        */
/* ------------------------------------------------------------------ */

int screenWidth;
int screenHeight;

/*
 *  上の ..._PCT から計算した実際のピクセル値。setup() で一度だけ入る。
 *  ここを直接いじるのではなく、上の ..._PCT を変えること。
 */
int eyeOffsetX;
int eyeOffsetY;
int eyeRadius;
int eyeClosedHeight;
int mouthOffsetY;
int mouthRadius;
int mouthBarWidth;
int mouthBarHeight;
int sleepDotRadius;
int sleepTextSize;

FaceExpression currentExpression = FaceExpression::Normal;
bool blinkClosed = false;      /* まばたきで目を閉じている */

uint32_t lastActivityMs;       /* 最後にタッチされた時刻 */
uint32_t happyStartedMs;       /* Happy になった時刻 */
uint32_t nextBlinkMs;          /* 次にまばたきする時刻 */
uint32_t blinkOpenMs;          /* まばたきを終える時刻 */

/*  いま画面に描いてある内容。これと違うときだけ描き直す（ちらつき防止） */
FaceExpression drawnExpression = FaceExpression::Normal;
bool drawnEyesClosed = false;

uint32_t randomState = 12345U; /* 簡易乱数の種 */

/* ------------------------------------------------------------------ */
/*  時間・ログ・乱数                                                  */
/*  （Arduino の millis() / Serial / random() の代わり）              */
/* ------------------------------------------------------------------ */

/*  起動からの経過ミリ秒。Arduino の millis() と同じつもりで使える。 */
uint32_t nowMs()
{
    return static_cast<uint32_t>(esp_shim_time_us() / 1000);
}

/*  since から duration ミリ秒たったか。桁あふれしても正しく比べられる書き方。 */
bool elapsed(uint32_t since, uint32_t duration)
{
    return static_cast<uint32_t>(nowMs() - since) >= duration;
}

/*  予定の時刻 deadline になったか。 */
bool reached(uint32_t deadline)
{
    return static_cast<int32_t>(nowMs() - deadline) >= 0;
}

/*  シリアルモニタへ 1 行出す。Serial.println() の代わり。 */
void logLine(const char *text)
{
    while (*text != '\0') {
        target_fput_log(*text);
        ++text;
    }
    target_fput_log('\n');
}

/*  min 以上 max 未満の値を返す簡易乱数（xorshift）。 */
uint32_t randomBetween(uint32_t min, uint32_t max)
{
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return min + (randomState % (max - min));
}

/* ------------------------------------------------------------------ */
/*  顔を描く                                                          */
/* ------------------------------------------------------------------ */

int faceCenterX()
{
    return screenWidth / 2;
}

int faceCenterY()
{
    return screenHeight / 2;
}

/*
 *  ..._PCT を実際のピクセル数へ直す。基準は**短い辺**。
 *
 *  長い辺を基準にすると、横長の画面で顔が縦にはみ出す。短い辺なら、
 *  縦にも横にも収まることが最初から保証される。
 */
int fromShortEdge(int percent)
{
    const int shortEdge = (screenWidth < screenHeight) ? screenWidth
                                                       : screenHeight;
    return (shortEdge * percent) / 100;
}

void computeFaceLayout()
{
    eyeOffsetX = fromShortEdge(EYE_OFFSET_X_PCT);
    eyeOffsetY = fromShortEdge(EYE_OFFSET_Y_PCT);
    eyeRadius = fromShortEdge(EYE_RADIUS_PCT);
    eyeClosedHeight = fromShortEdge(EYE_CLOSED_HEIGHT_PCT);
    mouthOffsetY = fromShortEdge(MOUTH_OFFSET_Y_PCT);
    mouthRadius = fromShortEdge(MOUTH_RADIUS_PCT);
    mouthBarWidth = fromShortEdge(MOUTH_BAR_WIDTH_PCT);
    mouthBarHeight = fromShortEdge(MOUTH_BAR_HEIGHT_PCT);
    sleepDotRadius = fromShortEdge(SLEEP_DOT_RADIUS_PCT);

    /*  どれも 1px より細くはしない（0 を渡すと何も描かれない） */
    if (eyeClosedHeight < 2) { eyeClosedHeight = 2; }
    if (mouthBarHeight < 2) { mouthBarHeight = 2; }
    if (sleepDotRadius < 2) { sleepDotRadius = 2; }

    /*
     *  zzz の文字サイズ。M5GFX の 1 は 6x8px なので、短い辺の 1/80 を
     *  目安にすると CoreS3 で 3、StickS3 で 1 になる。
     */
    sleepTextSize = fromShortEdge(100) / 80;
    if (sleepTextSize < 1) { sleepTextSize = 1; }
}

int eyeCenterY()
{
    return faceCenterY() + eyeOffsetY;
}

int mouthCenterY()
{
    return faceCenterY() + mouthOffsetY;
}

/*  片方の目を描く。x, y はその目の中心。 */
void drawOneEye(int x, int y, FaceExpression expression, bool closed)
{
    if (closed || (expression == FaceExpression::Sleepy)) {
        /*  閉じた目：横棒 */
        M5.Display.fillRoundRect(x - eyeRadius,
                                 y - (eyeClosedHeight / 2),
                                 eyeRadius * 2,
                                 eyeClosedHeight,
                                 eyeClosedHeight / 2,
                                 COLOR_EYE);
        return;
    }

    if (expression == FaceExpression::Happy) {
        /*
         *  笑った目：丸を描いて、少し下にずらした丸を背景色で重ねる。
         *  残った上側の三日月が「∩」の形になる。
         */
        M5.Display.fillCircle(x, y, eyeRadius, COLOR_EYE);
        M5.Display.fillCircle(x, y + (eyeRadius / 2), eyeRadius,
                              COLOR_BACKGROUND);
        return;
    }

    /*  ふつうの目：丸 */
    M5.Display.fillCircle(x, y, eyeRadius, COLOR_EYE);
}

/*  両目を描く。先に目のあたりだけ消してから描く。 */
void drawEyes(FaceExpression expression, bool closed)
{
    const int y = eyeCenterY();

    M5.Display.startWrite();
    M5.Display.fillRect(0, y - eyeRadius - 2,
                        screenWidth, (eyeRadius * 2) + 4,
                        COLOR_BACKGROUND);
    drawOneEye(faceCenterX() - eyeOffsetX, y, expression, closed);
    drawOneEye(faceCenterX() + eyeOffsetX, y, expression, closed);
    M5.Display.endWrite();
}

/*  口を描く。先に口のあたりだけ消してから描く。 */
void drawMouth(FaceExpression expression)
{
    const int x = faceCenterX();
    const int y = mouthCenterY();

    M5.Display.startWrite();
    M5.Display.fillRect(0, y - mouthRadius - 2,
                        screenWidth, (mouthRadius * 2) + 4,
                        COLOR_BACKGROUND);

    switch (expression) {
    case FaceExpression::Happy:
        /*
         *  笑った口：丸を描いて上半分を背景色で消す。
         *  残った下半分が大きく開いた口になる。
         */
        M5.Display.fillCircle(x, y, mouthRadius, COLOR_MOUTH);
        M5.Display.fillRect(x - mouthRadius - 1,
                            y - mouthRadius - 1,
                            (mouthRadius * 2) + 2,
                            mouthRadius + 1,
                            COLOR_BACKGROUND);
        break;

    case FaceExpression::Sleepy:
        /*  眠っている口：小さな丸 */
        M5.Display.fillCircle(x, y, sleepDotRadius, COLOR_MOUTH);
        break;

    case FaceExpression::Normal:
    default:
        /*  ふつうの口：小さな横棒 */
        M5.Display.fillRoundRect(x - mouthBarWidth, y - (mouthBarHeight / 2),
                                 mouthBarWidth * 2, mouthBarHeight,
                                 mouthBarHeight / 2, COLOR_MOUTH);
        break;
    }

    M5.Display.endWrite();
}

/*  眠っているときだけ右上に zzz を出す。 */
void drawSleepMark(FaceExpression expression)
{
    /*  "zzz" は 3 文字。M5GFX の 1 文字は 6x8px を setTextSize 倍したもの。 */
    const int textWidth = 3 * 6 * sleepTextSize;
    const int textHeight = 8 * sleepTextSize;
    const int left = screenWidth - textWidth - (textHeight / 2);
    const int top = textHeight / 2;

    M5.Display.startWrite();
    M5.Display.fillRect(left, top, textWidth, textHeight, COLOR_BACKGROUND);
    if (expression == FaceExpression::Sleepy) {
        M5.Display.setTextColor(COLOR_SLEEP_MARK, COLOR_BACKGROUND);
        M5.Display.setTextSize(sleepTextSize);
        M5.Display.setCursor(left, top);
        M5.Display.print("zzz");
    }
    M5.Display.endWrite();
}

/*  顔をぜんぶ描き直す。 */
void drawFace(FaceExpression expression, bool closed)
{
    drawEyes(expression, closed);
    drawMouth(expression);
    drawSleepMark(expression);
}

/* ------------------------------------------------------------------ */
/*  音                                                                */
/* ------------------------------------------------------------------ */

/*
 *  ★このランタイムではまだ音が鳴らせない。
 *
 *  M5Unified + Dual Core プロファイルは Speaker_Class.cpp / Mic_Class.cpp を
 *  ビルドから外しており（ports/m5stack_xtensa/runtime/CMakeLists.txt）、
 *  M5.begin() も internal_spk = false で呼ばれている。CoreS3 のスピーカーは
 *  I2S + AW88298 だが、このポートに I2S ドライバは入っていない。
 *
 *  スピーカーが載ったら、この関数の中を次の 1 行にすれば音が出る。
 *
 *      M5.Speaker.tone(2000, 80);   // 2000Hz を 80ms
 *
 *  それまでは、鳴らそうとしたことをログに残すだけにしておく。
 */
void playHappySound()
{
    logLine("Sound: happy (speaker not available in this runtime)");
}

/* ------------------------------------------------------------------ */
/*  状態を変える                                                      */
/* ------------------------------------------------------------------ */

void scheduleNextBlink()
{
    nextBlinkMs = nowMs() +
                  randomBetween(BLINK_INTERVAL_MIN_MS, BLINK_INTERVAL_MAX_MS);
}

void setExpression(FaceExpression expression)
{
    if (currentExpression == expression) {
        return;
    }

    currentExpression = expression;
    blinkClosed = false;

    switch (expression) {
    case FaceExpression::Normal:
        logLine("Expression: NORMAL");
        scheduleNextBlink();
        break;
    case FaceExpression::Happy:
        logLine("Expression: HAPPY");
        happyStartedMs = nowMs();
        break;
    case FaceExpression::Sleepy:
        logLine("Expression: SLEEPY");
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  毎回やること                                                      */
/* ------------------------------------------------------------------ */

/*  タッチされたら Happy にする。 */
void updateTouch()
{
    if (M5.Touch.getCount() == 0) {
        return;
    }

    const auto &touch = M5.Touch.getDetail(0);
    if (!touch.wasPressed()) {
        return;
    }

    /*
     *  触られた場所は touch.x / touch.y で分かる。
     *  「左半分だけ別の顔にする」ような改造はここに書く。
     */
    logLine("Touch detected");

    lastActivityMs = nowMs();
    setExpression(FaceExpression::Happy);
    playHappySound();
}

/*  Normal のときだけ、ときどき目を閉じる。 */
void updateBlink()
{
    if (currentExpression != FaceExpression::Normal) {
        return;
    }

    if (blinkClosed) {
        if (reached(blinkOpenMs)) {
            blinkClosed = false;
            scheduleNextBlink();
        }
        return;
    }

    if (reached(nextBlinkMs)) {
        logLine("Blink");
        blinkClosed = true;
        blinkOpenMs = nowMs() + BLINK_CLOSED_MS;
    }
}

/*  時間がたったら表情を戻したり眠らせたりする。 */
void updateExpression()
{
    switch (currentExpression) {
    case FaceExpression::Happy:
        if (elapsed(happyStartedMs, HAPPY_DURATION_MS)) {
            setExpression(FaceExpression::Normal);
        }
        break;

    case FaceExpression::Normal:
        if (elapsed(lastActivityMs, SLEEP_TIMEOUT_MS)) {
            setExpression(FaceExpression::Sleepy);
        }
        break;

    case FaceExpression::Sleepy:
    default:
        break;
    }
}

/*  画面の内容が古くなったときだけ描き直す。 */
void updateDisplay()
{
    const bool eyesClosed = blinkClosed ||
                            (currentExpression == FaceExpression::Sleepy);

    if (currentExpression != drawnExpression) {
        drawFace(currentExpression, eyesClosed);
    }
    else if (eyesClosed != drawnEyesClosed) {
        drawEyes(currentExpression, eyesClosed);
    }
    else {
        return;
    }

    drawnExpression = currentExpression;
    drawnEyesClosed = eyesClosed;
}

}  // namespace

/* ------------------------------------------------------------------ */
/*  setup / loop                                                      */
/* ------------------------------------------------------------------ */

void setup()
{
    /*
     *  ★M5.begin() ではなく toppers_m5_begin() を呼ぶ。
     *
     *  このポートは LCD の SPI をレジスタ直で叩いており GDMA を実装して
     *  いないため、M5GFX に DMA を使わせてはいけない。toppers_m5_begin() は
     *  M5.begin() のあとに DMA チャネルを 0 へ落とす後始末までやってくれる。
     *  戻り値が 1 なら初期化成功。
     */
    if (toppers_m5_begin() != 1) {
        logLine("StackChanBasic: M5 init failed");
        return;
    }

    screenWidth = M5.Display.width();
    screenHeight = M5.Display.height();

    /*  画面の大きさが分かってから、顔の各部のピクセル数を決める。 */
    computeFaceLayout();

    /*  乱数の種を起動時刻からもらう（毎回同じ間隔でまばたきしないように） */
    randomState ^= static_cast<uint32_t>(esp_shim_time_us());

    M5.Display.fillScreen(COLOR_BACKGROUND);

    lastActivityMs = nowMs();
    currentExpression = FaceExpression::Normal;
    blinkClosed = false;
    scheduleNextBlink();

    drawFace(currentExpression, false);
    drawnExpression = currentExpression;
    drawnEyesClosed = false;

    logLine("StackChanBasic started");
    logLine("Expression: NORMAL");
}

void loop()
{
    /*
     *  delay() は使えないが、FMP3 のブリッジが loop() を約 1ms ごとに
     *  呼んでくれるので、ここで待つ必要はない。
     */
    M5.update();

    updateTouch();
    updateBlink();
    updateExpression();
    updateDisplay();
}
