#ifndef TOPPERS_STANDALONE_APP_H
#define TOPPERS_STANDALONE_APP_H

#define STANDALONE_TASK_PRIORITY	10
#define STANDALONE_TASK_STACK_SIZE	4096

#if !defined(TOPPERS_MACRO_ONLY)
#include <kernel.h>

extern void standalone_task(EXINF exinf);
#endif

#endif  /* TOPPERS_STANDALONE_APP_H */
