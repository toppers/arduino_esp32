#include <stddef.h>
#include <stdint.h>
#include "esp_shim.h"

extern void target_fput_log(char character);

/*
 * Newlib's default guard initializer calls the ESP-IDF random subsystem from
 * a global constructor.  That subsystem is not part of the FMP3 startup path,
 * so provide a process-lifetime canary and a fail-stop handler locally.
 */
uintptr_t __stack_chk_guard = UINT32_C(0x9e3779b9);

void __stack_chk_fail(void)
{
    static const char message[] = "[WiFiConnect] stack protector failure\n";
    size_t index;

    for (index = 0; index < sizeof(message) - 1U; ++index)
        target_fput_log(message[index]);
    for (;;) {}
}

void *heap_caps_malloc_prefer(size_t size, size_t num, ...)
{
    (void)num;
    return esp_shim_malloc(size);
}

void vTaskDelay(uint32_t ticks)
{
    esp_shim_task_delay(ticks);
}

long _write(int file, const void *buffer, size_t length)
{
    const char *text = (const char *)buffer;
    size_t index;
    (void)file;
    for (index = 0; index < length; ++index) target_fput_log(text[index]);
    return (long)length;
}

/*
 * _getpid, _kill and _exit used to be defined here as well. They are provided
 * by arch/xtensa_gcc/esp32s3/chip_rom_libc.c, which every profile links, and
 * duplicating them here was only ever resolved by accident: the link carries
 * -Wl,--allow-multiple-definition, so ld silently took whichever object sorted
 * first, and chip_rom_libc.o wins over toppers_lwip_compat.o on the letter 'c'.
 *
 * Removing them changes no behaviour - the chip_rom_libc versions were already
 * the ones linked - and it makes that choice deliberate. Those versions are
 * also the better ones: they report through syslog before halting, where the
 * copies here returned -1 or spun silently.
 *
 * Found by scripts/audit_duplicate_symbols.py.
 */
