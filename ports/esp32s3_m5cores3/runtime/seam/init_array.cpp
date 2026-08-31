typedef void (*toppers_init_function_t)(void);

extern "C" {
extern toppers_init_function_t __init_array_start[];
extern toppers_init_function_t __init_array_end[];
extern toppers_init_function_t __ctors_start[];
extern toppers_init_function_t __ctors_end[];

void esp_run_init_array(void)
{
    for (toppers_init_function_t *entry = __init_array_start;
         entry < __init_array_end; ++entry) {
        if (*entry != nullptr) {
            (*entry)();
        }
    }

    for (toppers_init_function_t *entry = __ctors_end;
         entry-- > __ctors_start;) {
        if (*entry != nullptr) {
            (*entry)();
        }
    }
}
}
