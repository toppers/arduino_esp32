/*
 * The executable task body is compiled by Arduino builder and linked as an
 * external object. This translation unit keeps the FMP3 application source
 * contract explicit and gives the configuration a stable application name.
 */
const char toppers_phase3_application[] = "Arduino sketch bridge";

#if defined(TOPPERS_XIP_PADDR_PROBE)
/*
 *  flash_xip_map()がSRAMへ退避したPADDR候補をコンソールへ出す。
 * -DTOPPERS_XIP_PADDR_PROBEのみ有効。
 *
 *  出力経路は target_fput_log（TOPPERS_S3_CONSOLE_USJ＝USB-Serial-JTAG）。
 *  ArduinoSketchBridge.cppのweakな toppers_arduino_runtime_init() を
 *  こちらの強い定義で上書きし、setup()の直前に一度だけ出力する。
 */
#include <stdint.h>

extern volatile uint32_t toppers_xip_paddr_probe[8];
extern void target_fput_log(char c);

static void
probe_puts(const char *text)
{
	while (*text != '\0') {
		target_fput_log(*text++);
	}
}

static void
probe_puthex(uint32_t value)
{
	int shift;

	probe_puts("0x");
	for (shift = 28; shift >= 0; shift -= 4) {
		uint32_t digit = (value >> shift) & 0xFU;

		target_fput_log((char)((digit < 10U)
			? ('0' + digit) : ('a' + (digit - 10U))));
	}
}

static void
probe_line(const char *label, uint32_t drom, uint32_t irom)
{
	probe_puts(label);
	probe_puthex(drom);
	probe_puts(" / ");
	probe_puthex(irom);
	target_fput_log('\n');
}

void
toppers_arduino_runtime_init(void)
{
	probe_puts("[PADDR-PROBE] begin\n");
	probe_line("[PADDR-PROBE] P0 mapped    : ",
			   toppers_xip_paddr_probe[0], toppers_xip_paddr_probe[1]);
	probe_line("[PADDR-PROBE] M1 mmu-paddr : ",
			   toppers_xip_paddr_probe[4], toppers_xip_paddr_probe[5]);
	probe_line("[PADDR-PROBE] mmu raw entry: ",
			   toppers_xip_paddr_probe[2], toppers_xip_paddr_probe[3]);
	probe_line("[PADDR-PROBE] mmu entry id : ",
			   toppers_xip_paddr_probe[6], toppers_xip_paddr_probe[7]);
	if ((toppers_xip_paddr_probe[4] == toppers_xip_paddr_probe[0])
			&& (toppers_xip_paddr_probe[5] == toppers_xip_paddr_probe[1])) {
		probe_puts("[PADDR-PROBE] result M1 MATCH\n");
	}
	else {
		probe_puts("[PADDR-PROBE] result M1 MISMATCH\n");
	}
}
#endif /* TOPPERS_XIP_PADDR_PROBE */
