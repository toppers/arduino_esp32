#include "ToppersFMP3_Console.h"

/*
 *  FMP3 のシリアルインタフェースドライバ（`third_party/fmp3_core/syssvc/serial.h`）。
 *  カーネルのヘッダはスケッチの include パスに無いので、ここで宣言し直します。
 *  型は FMP3 の `ID` / `uint_t` / `ER_UINT` がいずれも `int` / `unsigned int`
 *  であること（`t_stddef.h`）に対応しています。
 */
extern "C" {

struct toppers_fmp3_serial_rpor {
    unsigned int reacnt;    /*  受信バッファ中の文字数  */
    unsigned int wricnt;    /*  送信バッファ中の文字数  */
};

int serial_ref_por(int portid, struct toppers_fmp3_serial_rpor *pk_rpor);
int serial_rea_dat(int portid, char *buf, unsigned int len);

}  // extern "C"

//  T_SERIAL_RPOR と同じ並びであること。増減したらここで気づけるようにする。
static_assert(sizeof(struct toppers_fmp3_serial_rpor) == 2U * sizeof(unsigned int),
              "toppers_fmp3_serial_rpor must match T_SERIAL_RPOR");

ToppersFMP3ConsoleClass FMP3Console;

int ToppersFMP3ConsoleClass::available() const
{
    struct toppers_fmp3_serial_rpor rpor = { 0U, 0U };
    const int ercd = serial_ref_por(TOPPERS_FMP3_CONSOLE_PORT, &rpor);
    if (ercd < 0) {
        return ercd;
    }
    return (int)rpor.reacnt;
}

int ToppersFMP3ConsoleClass::read() const
{
    if (available() <= 0) {
        return -1;
    }
    char character = 0;
    //  1 文字だけ要求する。available() で 1 以上あることを確かめた直後なので
    //  待たない（serial_rea_dat は要求長が揃うまで待つ）。
    if (serial_rea_dat(TOPPERS_FMP3_CONSOLE_PORT, &character, 1U) != 1) {
        return -1;
    }
    return (int)(unsigned char)character;
}
