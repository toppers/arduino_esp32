/*
 *  無印ESP32(LX6) の newlib リエントラント getter
 *
 *  ROM/newlib 経路が呼ぶ。wifi を含む profile では wifi/shim/wifi_stubs.c が
 *  同じ関数を持つので、そちらがリンクされない profile（m5-unified）でだけ
 *  この TU をビルドする。両方入ると多重定義になり、リンクは
 *  --allow-multiple-definition で通ってしまうため、どちらが生き残るかが
 *  オブジェクト名の順序で決まる——CMakeLists.txt 側で入れ分ける理由がこれ。
 *  取りこぼしは scripts/audit_duplicate_symbols.py が止める。
 *
 *  S3 ではビルドしない（arch 側の chip_rom_libc.c が同じ役目を持つ）。
 */

struct _reent;
extern struct _reent *_impure_ptr;

struct _reent *
__getreent(void)
{
	return(_impure_ptr);
}

