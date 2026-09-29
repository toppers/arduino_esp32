/*
 *  TOPPERS/FMP3 ESP32-S3/LX6 port - FreeRTOS queue / semaphore / critical
 *  section ABI for prebuilt ESP-IDF archives
 *
 *  Copyright (C) 2026 by Embedded and Real-Time Systems Laboratory
 *              Graduate School of Information Science, Nagoya Univ., JAPAN
 *
 *  上記著作権者は，本ソフトウェアをTOPPERSライセンス（条件は他のソー
 *  スファイルの先頭コメントを参照）の下で利用することを許諾する．本ソ
 *  フトウェアは無保証で提供される．
 */

/*
 *  What this is
 *  ------------
 *  External functions with the names, signatures and semantics of the
 *  ESP-IDF v5.5 FreeRTOS kernel, for code that was compiled against the real
 *  headers and now calls them by symbol: the prebuilt driver archives of the
 *  M5Stack SDK (libesp_driver_i2s.a, the GDMA driver in libesp_hw_support.a)
 *  and anything written the same way on this port.
 *
 *  The compat headers under m5/compat/freertos/ are a different thing: they
 *  are static inline wrappers that M5GFX and M5Unified are compiled against,
 *  and they supply nothing to an archive that is already built. This file
 *  deliberately does not include them - their inline xQueueReceive and ours
 *  would collide - so the ABI types are restated below.
 *
 *  Why a new implementation rather than those wrappers
 *  ---------------------------------------------------
 *  The wrappers map onto esp_shim_queue_* in m5_kernel_shim.c, which is
 *  correct for its first user and wrong for an interrupt handler:
 *
 *   - xQueueReceiveFromISR calls the task-side esp_shim_queue_recv, which on
 *     success writes waiter = TSK_NONE. A task that was waiting on the same
 *     queue loses its registration and is never woken.
 *   - Everything is guarded by loc_cpu, which masks interrupts on the CALLING
 *     core only. The m5 runtime is built with two processors; a task on one
 *     and a handler on the other would edit the ring at the same time.
 *
 *  Here the state lives in one place, a ring and its counters, guarded by an
 *  FMP3 spinlock (M5_ABI_SPN). loc_spn is legal in both task and non-task
 *  context, gives real cross-core exclusion, and puts the local core into
 *  the CPU-locked state, so a handler on the same core cannot interleave.
 *
 *  Rules this file follows, and why
 *  --------------------------------
 *   1. No semaphore is the source of truth. pol_sem and prcv_dtq are
 *      task-context only in FMP3 (CHECK_TSKCTX_UNL), so a non-blocking take
 *      from an ISR cannot be built on them. The ring's count is the truth;
 *      kernel calls are used only to put a task to sleep and wake it.
 *   2. Sleep with tslp_tsk, wake with wup_tsk. wup_tsk is legal from an ISR,
 *      and it latches a wakeup that arrives before the sleep, which closes
 *      the window between "registered as a waiter" and "asleep". A stale
 *      latched wakeup only costs one extra loop: every wait re-checks its
 *      condition, and so does esp_shim_task_notify_take, the other user of
 *      slp_tsk / wup_tsk in the m5 runtime.
 *   3. Never call a kernel wake while holding the spinlock. FMP3 service calls
 *      other than the spinlock ones fail with E_CTX in the CPU-locked state.
 *      Wakeups are queued per core and issued after the outermost unlock.
 *      The same deferral makes a FromISR call made inside an IDF critical
 *      section work, which prebuilt code does.
 *   4. Critical sections nest per core. IDF allows portENTER_CRITICAL inside
 *      portENTER_CRITICAL on the same core; FMP3 spinlocks do not nest.
 *      Every portMUX_TYPE maps to the one ABI spinlock - coarser than IDF,
 *      which has a lock per mux, but it cannot deadlock (there is only one
 *      lock) and code inside a critical section does not block.
 *   5. Fail, never pretend. Pool exhaustion, a call from the wrong context,
 *      and a spinlock that cannot be taken return the FreeRTOS failure value
 *      and bump a counter that m5_abi_stats() reports.
 *
 *  What is knowingly different from FreeRTOS
 *  -----------------------------------------
 *   - Waiters are woken first-come, not highest-priority-first.
 *   - Mutexes do not inherit priority. They exclude correctly; a
 *     low-priority holder can be preempted by a middle-priority task.
 *   - uxMemoryCaps is ignored: the m5 runtime has one internal-SRAM heap.
 *   - The handle is not a FreeRTOS Queue_t. Code that reads a queue's
 *     internals, rather than calling these functions, will not work.
 */

#include <stdint.h>
#include <kernel.h>
#include <t_syslog.h>
#include <string.h>
#include "kernel_cfg.h"

extern void *esp_shim_malloc(size_t size);
extern void  esp_shim_free(void *ptr);

/*
 *  ABI types, as the IDF v5.5 Xtensa port defines them (portmacro.h:
 *  BaseType_t is int, UBaseType_t unsigned int, TickType_t uint32_t;
 *  spinlock.h: spinlock_t is { owner, count }). Values from queue.h.
 */
typedef int				BaseType_t;
typedef unsigned int	UBaseType_t;
typedef uint32_t		TickType_t;
typedef void			*QueueHandle_t;
typedef void			*TaskHandle_t;
typedef struct {
	volatile uint32_t	owner;
	volatile uint32_t	count;
} portMUX_TYPE;

#define ABI_pdTRUE				((BaseType_t) 1)
#define ABI_pdFALSE				((BaseType_t) 0)
#define ABI_portMAX_DELAY		((TickType_t) 0xFFFFFFFFU)
#define ABI_portMUX_NO_TIMEOUT	((BaseType_t) -1)

#define ABI_SEND_TO_BACK		((BaseType_t) 0)
#define ABI_SEND_TO_FRONT		((BaseType_t) 1)
#define ABI_OVERWRITE			((BaseType_t) 2)

#define ABI_TYPE_BASE			((uint8_t) 0U)
#define ABI_TYPE_MUTEX			((uint8_t) 1U)
#define ABI_TYPE_COUNTING		((uint8_t) 2U)
#define ABI_TYPE_BINARY			((uint8_t) 3U)
#define ABI_TYPE_RECURSIVE		((uint8_t) 4U)

/*  CONFIG_FREERTOS_HZ is 1000 in the SDK the archives were built with
 *  (sdkconfig.h), so one tick is one millisecond. RELTIM is microseconds. */
#define ABI_TICK_US				1000U

#define ABI_QUE_MAX				16		/* queues + semaphores alive at once */
#define ABI_WAITERS				4		/* tasks blocked on one queue, each way */
#define ABI_PENDING				8		/* wakeups deferred per core */

typedef struct {
	bool_t		used;
	uint8_t		type;
	UBaseType_t	length;			/* slots; for a semaphore, the maximum count */
	UBaseType_t	item_size;		/* 0 for every kind of semaphore */
	UBaseType_t	head, tail, count;
	uint8_t		*storage;		/* length * item_size bytes, or NULL */
	ID			rx_wait[ABI_WAITERS];	/* waiting for an item */
	ID			tx_wait[ABI_WAITERS];	/* waiting for a free slot */
	uint_t		n_rx, n_tx;
	ID			holder;			/* mutexes: the task that took it */
	UBaseType_t	recursion;		/* recursive mutexes: depth */
} ABI_QUE;

static ABI_QUE	abi_que[ABI_QUE_MAX];

/*  Diagnostics. Read with m5_abi_stats(); nothing here is load-bearing. */
static volatile uint32_t	abi_lock_failures;
static volatile uint32_t	abi_wrong_context;
static volatile uint32_t	abi_pool_exhausted;
static volatile uint32_t	abi_waiter_overflow;
static volatile uint32_t	abi_pending_overflow;

#if defined(M5_ABI_SPN)

/*
 *  The core the caller runs on, without a service call: get_pid is
 *  CHECK_UNL and fails once the spinlock is held. PRID is 0xCDCD on the
 *  PRO CPU and 0xABAB on the APP CPU; bit 13 tells them apart, which is
 *  what IDF's esp_cpu_get_core_id() reads on both the ESP32 and ESP32-S3.
 *  FMP3 tasks do not migrate unless mig_tsk is called, so the answer
 *  cannot change under a task between reading it and using it.
 */
static inline uint_t
abi_core(void)
{
	uint32_t	prid;

	Asm("rsr %0, prid" : "=r"(prid));
	return((uint_t)((prid >> 13) & 1U));
}

/*  Indexed by abi_core(), which is 0 or 1 from PRID whatever TNUM_PRCID
 *  says, so these are sized for the two cores the chip has. */
#define ABI_NCORE	2
static uint_t	abi_nest[ABI_NCORE];
static ID		abi_pending[ABI_NCORE][ABI_PENDING];
static uint_t	abi_npending[ABI_NCORE];

static bool_t
abi_lock(void)
{
	uint_t	core = abi_core();

	if (abi_nest[core] == 0U) {
		if (loc_spn(M5_ABI_SPN) != E_OK) {
			/*  E_CTX: the CPU is already locked by loc_cpu (not by us), or
			 *  another spinlock is held. Either way there is no exclusion
			 *  to be had, so the caller must fail. */
			abi_lock_failures++;
			return(false);
		}
	}
	abi_nest[core]++;
	return(true);
}

/*  Busy-wait for the lock for at most `cycles` CCOUNT ticks. */
static bool_t
abi_try_lock(uint32_t cycles)
{
	uint_t		core = abi_core();
	uint32_t	start, now;
	ER			ercd;

	if (abi_nest[core] == 0U) {
		Asm("rsr %0, ccount" : "=r"(start));
		for (;;) {
			ercd = try_spn(M5_ABI_SPN);
			if (ercd == E_OK) {
				break;
			}
			if (ercd != E_OBJ) {		/* E_OBJ = held elsewhere; retry */
				abi_lock_failures++;
				return(false);
			}
			Asm("rsr %0, ccount" : "=r"(now));
			if ((uint32_t)(now - start) >= cycles) {
				return(false);
			}
		}
	}
	abi_nest[core]++;
	return(true);
}

static void
abi_unlock(void)
{
	uint_t	core = abi_core();
	ID		wake[ABI_PENDING];
	uint_t	n, i;

	if (abi_nest[core] == 0U) {
		return;		/* unbalanced exit; nothing is held */
	}
	if (--abi_nest[core] != 0U) {
		return;
	}
	n = abi_npending[core];
	for (i = 0U; i < n; i++) {
		wake[i] = abi_pending[core][i];
	}
	abi_npending[core] = 0U;
	(void) unl_spn(M5_ABI_SPN);
	for (i = 0U; i < n; i++) {
		(void) wup_tsk(wake[i]);	/* E_QOVR (already latched) is fine */
	}
}

/*  Queue a wakeup for the outermost unlock. Call with the lock held. */
static void
abi_defer_wake(ID tskid)
{
	uint_t	core = abi_core();

	if (abi_npending[core] < (uint_t) ABI_PENDING) {
		abi_pending[core][abi_npending[core]++] = tskid;
	}
	else {
		/*  Losing a wakeup would leave a task asleep with work queued.
		 *  The pending list is sized for one queue op's worth of wakes
		 *  per nesting level, so reaching here is a sizing bug. */
		abi_pending_overflow++;
	}
}

#define ABI_HAVE_LOCK	1
#else
#define ABI_HAVE_LOCK	0
static bool_t abi_lock(void) { abi_lock_failures++; return(false); }
static bool_t abi_try_lock(uint32_t cycles) { (void) cycles; abi_lock_failures++; return(false); }
static void abi_unlock(void) { }
static void abi_defer_wake(ID tskid) { (void) tskid; }
#endif /* M5_ABI_SPN */

/* ---- waiter lists (call with the lock held) ---- */

static bool_t
abi_add_waiter(ID *list, uint_t *n, ID self)
{
	uint_t	i;

	for (i = 0U; i < *n; i++) {
		if (list[i] == self) {
			return(true);
		}
	}
	if (*n >= (uint_t) ABI_WAITERS) {
		abi_waiter_overflow++;
		return(false);
	}
	list[(*n)++] = self;
	return(true);
}

static void
abi_remove_waiter(ID *list, uint_t *n, ID self)
{
	uint_t	i, j;

	for (i = 0U; i < *n; i++) {
		if (list[i] == self) {
			for (j = i + 1U; j < *n; j++) {
				list[j - 1U] = list[j];
			}
			(*n)--;
			return;
		}
	}
}

/*  Wake the first waiter, if any. Returns whether one was woken. */
static bool_t
abi_wake_one(ID *list, uint_t *n)
{
	ID		tskid;
	uint_t	j;

	if (*n == 0U) {
		return(false);
	}
	tskid = list[0];
	for (j = 1U; j < *n; j++) {
		list[j - 1U] = list[j];
	}
	(*n)--;
	abi_defer_wake(tskid);
	return(true);
}

static void
abi_wake_all(ID *list, uint_t *n)
{
	while (abi_wake_one(list, n)) {
	}
}

/* ---- ring operations (call with the lock held) ---- */

static bool_t
abi_is_semaphore(const ABI_QUE *q)
{
	return(q->item_size == 0U);
}

static void
abi_put(ABI_QUE *q, const void *item, BaseType_t position)
{
	if (abi_is_semaphore(q) || (item == NULL)) {
		/*  item is NULL only for a semaphore: the send entries refuse a
		 *  NULL item for a real queue before getting here. */
		q->count++;
		return;
	}
	if ((position == ABI_OVERWRITE) && (q->count >= q->length)) {
		/*  Overwrite is defined for length-1 queues: replace the item. */
		memcpy(&q->storage[q->head * q->item_size], item, q->item_size);
		return;
	}
	if (position == ABI_SEND_TO_FRONT) {
		q->head = (q->head + q->length - 1U) % q->length;
		memcpy(&q->storage[q->head * q->item_size], item, q->item_size);
	}
	else {
		memcpy(&q->storage[q->tail * q->item_size], item, q->item_size);
		q->tail = (q->tail + 1U) % q->length;
	}
	q->count++;
}

static void
abi_get(ABI_QUE *q, void *buffer)
{
	if (!abi_is_semaphore(q)) {
		if (buffer != NULL) {
			memcpy(buffer, &q->storage[q->head * q->item_size], q->item_size);
		}
		q->head = (q->head + 1U) % q->length;
	}
	q->count--;
}

static bool_t
abi_has_room(const ABI_QUE *q, BaseType_t position)
{
	return((q->count < q->length)
		   || ((position == ABI_OVERWRITE) && !abi_is_semaphore(q)));
}

static ABI_QUE *
abi_valid(QueueHandle_t handle)
{
	ABI_QUE	*q = (ABI_QUE *) handle;

	if ((q < &abi_que[0]) || (q >= &abi_que[ABI_QUE_MAX]) || !q->used) {
		return(NULL);
	}
	return(q);
}

/* ---- timing ---- */

/*  Absolute deadline in microseconds of system time, or UINT64_MAX. */
static uint64_t
abi_deadline(TickType_t ticks)
{
	SYSTIM	now;

	if (ticks == ABI_portMAX_DELAY) {
		return(UINT64_MAX);
	}
	(void) get_tim(&now);
	return((uint64_t) now + (uint64_t) ticks * ABI_TICK_US);
}

/*
 *  Sleep until woken or until the deadline. Returns false once the deadline
 *  has passed. The remaining time is recomputed on every call because a
 *  stale wakeup (rule 2 above) returns early and the loop sleeps again.
 */
static bool_t
abi_sleep_until(uint64_t deadline)
{
	SYSTIM		now;
	uint64_t	remaining;
	ER			ercd;

	if (deadline == UINT64_MAX) {
		(void) slp_tsk();
		return(true);
	}
	(void) get_tim(&now);
	if ((uint64_t) now >= deadline) {
		return(false);
	}
	remaining = deadline - (uint64_t) now;
	/*  tslp_tsk takes at most TMAX_RELTIM (66 min 40 s). A longer wait sleeps
	 *  in pieces; the caller loops and this recomputes what is left. */
	if (remaining > (uint64_t) TMAX_RELTIM) {
		remaining = (uint64_t) TMAX_RELTIM;
	}
	ercd = tslp_tsk((RELTIM) remaining);
	if (ercd == E_TMOUT) {
		(void) get_tim(&now);
		return((uint64_t) now < deadline);
	}
	return((ercd == E_OK) || (ercd == E_RLWAI));
}

/* ---- creation and deletion ---- */

static QueueHandle_t
abi_create(UBaseType_t length, UBaseType_t item_size, uint8_t type,
		   UBaseType_t initial)
{
	uint8_t	*storage = NULL;
	ABI_QUE	*q = NULL;
	uint_t	i;

	if (sns_ctx()) {
		abi_wrong_context++;		/* FreeRTOS: not from an ISR either */
		return(NULL);
	}
	if ((length == 0U) || (initial > length)) {
		return(NULL);
	}
	if (item_size != 0U) {
		storage = (uint8_t *) esp_shim_malloc((size_t) length * item_size);
		if (storage == NULL) {
			return(NULL);
		}
	}
	if (!abi_lock()) {
		esp_shim_free(storage);
		return(NULL);
	}
	for (i = 0U; i < (uint_t) ABI_QUE_MAX; i++) {
		if (!abi_que[i].used) {
			q = &abi_que[i];
			memset(q, 0, sizeof(*q));
			q->used = true;
			q->type = type;
			q->length = length;
			q->item_size = item_size;
			q->storage = storage;
			q->count = initial;
			q->holder = TSK_NONE;
			break;
		}
	}
	abi_unlock();
	if (q == NULL) {
		abi_pool_exhausted++;
		syslog(LOG_NOTICE, "m5_abi: queue pool exhausted (%d)", ABI_QUE_MAX);
		esp_shim_free(storage);
	}
	return((QueueHandle_t) q);
}

QueueHandle_t
xQueueGenericCreate(const UBaseType_t uxQueueLength,
					const UBaseType_t uxItemSize, const uint8_t ucQueueType)
{
	return(abi_create(uxQueueLength, uxItemSize, ucQueueType, 0U));
}

QueueHandle_t
xQueueCreateWithCaps(UBaseType_t uxQueueLength, UBaseType_t uxItemSize,
					 UBaseType_t uxMemoryCaps)
{
	(void) uxMemoryCaps;
	return(abi_create(uxQueueLength, uxItemSize, ABI_TYPE_BASE, 0U));
}

QueueHandle_t
xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount,
							  const UBaseType_t uxInitialCount)
{
	return(abi_create(uxMaxCount, 0U, ABI_TYPE_COUNTING, uxInitialCount));
}

/*  A mutex starts available: one "item" in a length-1 semaphore. */
QueueHandle_t
xQueueCreateMutex(const uint8_t ucQueueType)
{
	return(abi_create(1U, 0U, ucQueueType, 1U));
}

/*
 *  idf_additions.h routes all three semaphore kinds through here:
 *  binary as (0, 0, BINARY), counting as (max, initial, COUNTING), mutexes
 *  as (0, 0, MUTEX). A binary semaphore starts empty, as xSemaphoreCreateBinary
 *  does.
 */
QueueHandle_t
xSemaphoreCreateGenericWithCaps(UBaseType_t uxMaxCount,
								UBaseType_t uxInitialCount,
								const uint8_t ucQueueType,
								UBaseType_t uxMemoryCaps)
{
	(void) uxMemoryCaps;
	switch (ucQueueType) {
	case ABI_TYPE_BINARY:
		return(abi_create(1U, 0U, ABI_TYPE_BINARY, 0U));
	case ABI_TYPE_COUNTING:
		return(abi_create(uxMaxCount, 0U, ABI_TYPE_COUNTING, uxInitialCount));
	case ABI_TYPE_MUTEX:
	case ABI_TYPE_RECURSIVE:
		return(xQueueCreateMutex(ucQueueType));
	default:
		return(NULL);
	}
}

void
vQueueDelete(QueueHandle_t xQueue)
{
	ABI_QUE	*q = abi_valid(xQueue);
	uint8_t	*storage;

	if ((q == NULL) || !abi_lock()) {
		return;
	}
	/*  FreeRTOS leaves deleting a queue with waiters undefined. Waking them
	 *  lets them see the handle is gone (abi_valid fails) and return. */
	abi_wake_all(q->rx_wait, &q->n_rx);
	abi_wake_all(q->tx_wait, &q->n_tx);
	storage = q->storage;
	q->used = false;
	q->storage = NULL;
	abi_unlock();
	esp_shim_free(storage);
}

void
vQueueDeleteWithCaps(QueueHandle_t xQueue)
{
	vQueueDelete(xQueue);
}

void
vSemaphoreDeleteWithCaps(QueueHandle_t xSemaphore)
{
	vQueueDelete(xSemaphore);
}

BaseType_t
xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue)
{
	ABI_QUE	*q = abi_valid(xQueue);

	(void) xNewQueue;
	if ((q == NULL) || !abi_lock()) {
		return(ABI_pdFALSE);
	}
	q->head = 0U;
	q->tail = 0U;
	q->count = 0U;
	abi_wake_all(q->tx_wait, &q->n_tx);	/* every slot is free now */
	abi_unlock();
	return(ABI_pdTRUE);
}

/* ---- task-context send / receive ---- */

BaseType_t
xQueueGenericSend(QueueHandle_t xQueue, const void * const pvItemToQueue,
				  TickType_t xTicksToWait, const BaseType_t xCopyPosition)
{
	ABI_QUE		*q;
	ID			self;
	uint64_t	deadline;

	if (sns_ctx()) {
		abi_wrong_context++;
		return(ABI_pdFALSE);
	}
	if (get_tid(&self) != E_OK) {
		return(ABI_pdFALSE);
	}
	deadline = abi_deadline(xTicksToWait);
	for (;;) {
		q = abi_valid(xQueue);
		if ((q == NULL) || !abi_lock()) {
			return(ABI_pdFALSE);
		}
		if ((q->type == ABI_TYPE_MUTEX) || (q->type == ABI_TYPE_RECURSIVE)) {
			/*  Giving a mutex: only the holder may, and it never blocks. */
			if ((q->holder != self) || (q->count != 0U)) {
				abi_unlock();
				return(ABI_pdFALSE);
			}
			q->holder = TSK_NONE;
			q->recursion = 0U;
			q->count = 1U;
			(void) abi_wake_one(q->rx_wait, &q->n_rx);
			abi_unlock();
			return(ABI_pdTRUE);
		}
		if ((pvItemToQueue == NULL) && !abi_is_semaphore(q)) {
			abi_unlock();
			return(ABI_pdFALSE);	/* FreeRTOS asserts: no item for a queue */
		}
		if (abi_has_room(q, xCopyPosition)) {
			abi_put(q, pvItemToQueue, xCopyPosition);
			(void) abi_wake_one(q->rx_wait, &q->n_rx);
			abi_unlock();
			return(ABI_pdTRUE);
		}
		if ((xTicksToWait == 0U) || !abi_add_waiter(q->tx_wait, &q->n_tx, self)) {
			abi_unlock();
			return(ABI_pdFALSE);			/* errQUEUE_FULL */
		}
		abi_unlock();
		if (!abi_sleep_until(deadline)) {
			if ((q = abi_valid(xQueue)) != NULL && abi_lock()) {
				abi_remove_waiter(q->tx_wait, &q->n_tx, self);
				abi_unlock();
			}
			return(ABI_pdFALSE);
		}
	}
}

/*  Shared by xQueueReceive and xQueueSemaphoreTake. */
static BaseType_t
abi_receive(QueueHandle_t xQueue, void *buffer, TickType_t xTicksToWait)
{
	ABI_QUE		*q;
	ID			self;
	uint64_t	deadline;

	if (sns_ctx()) {
		abi_wrong_context++;
		return(ABI_pdFALSE);
	}
	if (get_tid(&self) != E_OK) {
		return(ABI_pdFALSE);
	}
	deadline = abi_deadline(xTicksToWait);
	for (;;) {
		q = abi_valid(xQueue);
		if ((q == NULL) || !abi_lock()) {
			return(ABI_pdFALSE);
		}
		if (q->count > 0U) {
			abi_get(q, buffer);
			if ((q->type == ABI_TYPE_MUTEX) || (q->type == ABI_TYPE_RECURSIVE)) {
				q->holder = self;
				q->recursion = 1U;
			}
			(void) abi_wake_one(q->tx_wait, &q->n_tx);
			abi_unlock();
			return(ABI_pdTRUE);
		}
		if ((xTicksToWait == 0U) || !abi_add_waiter(q->rx_wait, &q->n_rx, self)) {
			abi_unlock();
			return(ABI_pdFALSE);			/* errQUEUE_EMPTY */
		}
		abi_unlock();
		if (!abi_sleep_until(deadline)) {
			if ((q = abi_valid(xQueue)) != NULL && abi_lock()) {
				abi_remove_waiter(q->rx_wait, &q->n_rx, self);
				abi_unlock();
			}
			return(ABI_pdFALSE);
		}
	}
}

BaseType_t
xQueueReceive(QueueHandle_t xQueue, void * const pvBuffer,
			  TickType_t xTicksToWait)
{
	return(abi_receive(xQueue, pvBuffer, xTicksToWait));
}

BaseType_t
xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
	return(abi_receive(xQueue, NULL, xTicksToWait));
}

BaseType_t
xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t xTicksToWait)
{
	ABI_QUE	*q = abi_valid(xMutex);
	ID		self;

	if ((q == NULL) || (q->type != ABI_TYPE_RECURSIVE)
			|| (get_tid(&self) != E_OK)) {
		return(ABI_pdFALSE);
	}
	if (!abi_lock()) {
		return(ABI_pdFALSE);
	}
	if (q->holder == self) {
		q->recursion++;
		abi_unlock();
		return(ABI_pdTRUE);
	}
	abi_unlock();
	return(abi_receive(xMutex, NULL, xTicksToWait));
}

BaseType_t
xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
	ABI_QUE	*q = abi_valid(xMutex);
	ID		self;

	if ((q == NULL) || (q->type != ABI_TYPE_RECURSIVE)
			|| (get_tid(&self) != E_OK)) {
		return(ABI_pdFALSE);
	}
	if (!abi_lock()) {
		return(ABI_pdFALSE);
	}
	if (q->holder != self) {
		abi_unlock();
		return(ABI_pdFALSE);
	}
	if (q->recursion > 1U) {
		q->recursion--;
		abi_unlock();
		return(ABI_pdTRUE);
	}
	abi_unlock();
	return(xQueueGenericSend(xMutex, NULL, 0U, ABI_SEND_TO_BACK));
}

/* ---- interrupt-context send / receive ---- */

BaseType_t
xQueueGenericSendFromISR(QueueHandle_t xQueue,
						 const void * const pvItemToQueue,
						 BaseType_t * const pxHigherPriorityTaskWoken,
						 const BaseType_t xCopyPosition)
{
	ABI_QUE		*q = abi_valid(xQueue);
	BaseType_t	result = ABI_pdFALSE;

	if ((q == NULL) || !abi_lock()) {
		return(ABI_pdFALSE);
	}
	if ((pvItemToQueue == NULL) && !abi_is_semaphore(q)) {
		abi_unlock();
		return(ABI_pdFALSE);	/* e.g. xQueueGiveFromISR on a real queue */
	}
	if (abi_has_room(q, xCopyPosition)
			&& (q->type != ABI_TYPE_MUTEX) && (q->type != ABI_TYPE_RECURSIVE)) {
		abi_put(q, pvItemToQueue, xCopyPosition);
		if (abi_wake_one(q->rx_wait, &q->n_rx)
				&& (pxHigherPriorityTaskWoken != NULL)) {
			/*  Only informative here: FMP3 dispatches at interrupt exit by
			 *  itself when wup_tsk readies a task (see _frxt_setup_switch). */
			*pxHigherPriorityTaskWoken = ABI_pdTRUE;
		}
		result = ABI_pdTRUE;
	}
	abi_unlock();
	return(result);
}

BaseType_t
xQueueGiveFromISR(QueueHandle_t xQueue,
				  BaseType_t * const pxHigherPriorityTaskWoken)
{
	return(xQueueGenericSendFromISR(xQueue, NULL, pxHigherPriorityTaskWoken,
									ABI_SEND_TO_BACK));
}

BaseType_t
xQueueReceiveFromISR(QueueHandle_t xQueue, void * const pvBuffer,
					 BaseType_t * const pxHigherPriorityTaskWoken)
{
	ABI_QUE		*q = abi_valid(xQueue);
	BaseType_t	result = ABI_pdFALSE;

	if ((q == NULL) || !abi_lock()) {
		return(ABI_pdFALSE);
	}
	if ((q->count > 0U)
			&& (q->type != ABI_TYPE_MUTEX) && (q->type != ABI_TYPE_RECURSIVE)) {
		abi_get(q, pvBuffer);
		if (abi_wake_one(q->tx_wait, &q->n_tx)
				&& (pxHigherPriorityTaskWoken != NULL)) {
			*pxHigherPriorityTaskWoken = ABI_pdTRUE;
		}
		result = ABI_pdTRUE;
	}
	abi_unlock();
	return(result);
}

/* ---- queries ---- */

static UBaseType_t
abi_count(QueueHandle_t xQueue, bool_t spaces)
{
	ABI_QUE		*q = abi_valid(xQueue);
	UBaseType_t	n;

	if ((q == NULL) || !abi_lock()) {
		return(0U);
	}
	n = spaces ? (q->length - q->count) : q->count;
	abi_unlock();
	return(n);
}

UBaseType_t
uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
	return(abi_count(xQueue, false));
}

UBaseType_t
uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue)
{
	return(abi_count(xQueue, false));
}

UBaseType_t
uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
	return(abi_count(xQueue, true));
}

BaseType_t
xQueueIsQueueFullFromISR(const QueueHandle_t xQueue)
{
	/*  An invalid handle has no room either; report it full so a caller
	 *  that skips sending on "full" does not write into nothing. */
	return((abi_valid(xQueue) == NULL) || (abi_count(xQueue, true) == 0U)
		   ? ABI_pdTRUE : ABI_pdFALSE);
}

BaseType_t
xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue)
{
	return((abi_count(xQueue, false) == 0U) ? ABI_pdTRUE : ABI_pdFALSE);
}

/* ---- port layer ---- */

BaseType_t
xPortEnterCriticalTimeout(portMUX_TYPE *mux, BaseType_t timeout)
{
	(void) mux;		/* every mux is the one ABI spinlock (rule 4) */
	if (timeout == ABI_portMUX_NO_TIMEOUT) {
		return(abi_lock() ? ABI_pdTRUE : ABI_pdFALSE);
	}
	return(abi_try_lock((uint32_t) timeout) ? ABI_pdTRUE : ABI_pdFALSE);
}

void
vPortExitCritical(portMUX_TYPE *mux)
{
	(void) mux;
	abi_unlock();
}

BaseType_t
xPortInIsrContext(void)
{
	return(sns_ctx() ? ABI_pdTRUE : ABI_pdFALSE);
}

/*
 *  The Xtensa port's portYIELD_FROM_ISR calls this to ask for a context
 *  switch on the way out of the interrupt. There is nothing to ask for: the
 *  only way this ABI readies a task from an ISR is wup_tsk, and FMP3 already
 *  schedules the dispatch at interrupt exit when a service call made in
 *  non-task context readies a task (on another core, by IPI). The handlers
 *  that reach here run as FMP3 ISRs (CRE_ISR, see esp_shim_intr.c).
 */
void
_frxt_setup_switch(void)
{
}

/*  Counters for a test or a diagnostic dump. Any argument may be NULL. */
void
m5_abi_stats(uint32_t *lock_failures, uint32_t *wrong_context,
			 uint32_t *pool_exhausted, uint32_t *waiter_overflow,
			 uint32_t *pending_overflow)
{
	if (lock_failures != NULL) { *lock_failures = abi_lock_failures; }
	if (wrong_context != NULL) { *wrong_context = abi_wrong_context; }
	if (pool_exhausted != NULL) { *pool_exhausted = abi_pool_exhausted; }
	if (waiter_overflow != NULL) { *waiter_overflow = abi_waiter_overflow; }
	if (pending_overflow != NULL) { *pending_overflow = abi_pending_overflow; }
}

/*  Whether this build has the spinlock at all (a cfg without M5_ABI_SPN
 *  gets an ABI in which every call fails). */
int32_t
m5_abi_available(void)
{
	return((int32_t) ABI_HAVE_LOCK);
}
