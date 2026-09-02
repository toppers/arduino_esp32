#ifndef TOPPERS_BT_CLASSIC_APP_H
#define TOPPERS_BT_CLASSIC_APP_H

#define ARDUINO_TASK_PRIORITY 10
#define ARDUINO_TASK_STACK_SIZE 8192

#ifndef TOPPERS_MACRO_ONLY
#include <stdint.h>
extern void toppers_arduino_task(intptr_t exinf);
#endif

#endif
