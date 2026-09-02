#include <kernel.h>
#include <stdbool.h>
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_shim.h"

void sys_init(void) {}
u32_t sys_now(void) { return (u32_t)(esp_shim_time_us() / 1000); }
u32_t sys_jiffies(void) { return sys_now(); }
sys_prot_t sys_arch_protect(void) { return (sys_prot_t)esp_shim_int_disable(); }
void sys_arch_unprotect(sys_prot_t value) { esp_shim_int_restore((uint32_t)value); }

err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    *sem = esp_shim_sem_create(1U, count != 0U ? 1U : 0U);
    return *sem != NULL ? ERR_OK : ERR_MEM;
}
void sys_sem_free(sys_sem_t *sem) { esp_shim_sem_delete(*sem); *sem = NULL; }
void sys_sem_signal(sys_sem_t *sem) { (void)esp_shim_sem_give(*sem); }
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout_ms)
{
    return esp_shim_sem_take(*sem, timeout_ms == 0U ? 0xffffffffU : timeout_ms)
        ? 1U : SYS_ARCH_TIMEOUT;
}

err_t sys_mutex_new(sys_mutex_t *mutex)
{
    *mutex = esp_shim_sem_create(1U, 1U);
    return *mutex != NULL ? ERR_OK : ERR_MEM;
}
void sys_mutex_free(sys_mutex_t *mutex) { esp_shim_sem_delete(*mutex); *mutex = NULL; }
void sys_mutex_lock(sys_mutex_t *mutex) { (void)esp_shim_sem_take(*mutex, 0xffffffffU); }
void sys_mutex_unlock(sys_mutex_t *mutex) { (void)esp_shim_sem_give(*mutex); }

err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    sys_mbox_t wrapper = esp_shim_calloc(1U, sizeof(*wrapper));
    if (wrapper == NULL) return ERR_MEM;
    wrapper->os_mbox = esp_shim_queue_create((uint32_t)size, sizeof(void *));
    if (wrapper->os_mbox == NULL) { esp_shim_free(wrapper); return ERR_MEM; }
    *mbox = wrapper;
    return ERR_OK;
}
void sys_mbox_free(sys_mbox_t *mbox)
{
    esp_shim_queue_delete((*mbox)->os_mbox); esp_shim_free(*mbox); *mbox = NULL;
}
void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    (void)esp_shim_queue_send((*mbox)->os_mbox, &msg, 0xffffffffU, false);
}
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    return esp_shim_queue_send((*mbox)->os_mbox, &msg, 0U, false) ? ERR_OK : ERR_MEM;
}
err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    return esp_shim_queue_send_from_isr((*mbox)->os_mbox, &msg) ? ERR_OK : ERR_MEM;
}
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout_ms)
{
    void *value = NULL;
    if (!esp_shim_queue_recv((*mbox)->os_mbox, &value,
            timeout_ms == 0U ? 0xffffffffU : timeout_ms)) return SYS_ARCH_TIMEOUT;
    if (msg != NULL) *msg = value;
    return 1U;
}
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    void *value = NULL;
    if (!esp_shim_queue_recv((*mbox)->os_mbox, &value, 0U)) return SYS_MBOX_EMPTY;
    if (msg != NULL) *msg = value;
    return 0U;
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
                            int stacksize, int priority)
{
    void *handle = NULL;
    return esp_shim_task_create(thread, name, (uint32_t)stacksize, arg,
        (uint32_t)priority, &handle) != 0 ? handle : NULL;
}
void sys_delay_ms(uint32_t ms) { esp_shim_task_delay(ms); }

static sys_sem_t thread_sem;
sys_sem_t *sys_thread_sem_init(void)
{
    if (thread_sem == NULL) thread_sem = esp_shim_sem_create(1U, 0U);
    return &thread_sem;
}
void sys_thread_sem_deinit(void) {}
sys_sem_t *sys_thread_sem_get(void) { return sys_thread_sem_init(); }
bool sys_thread_tcpip(int type) { (void)type; return true; }
