#ifndef TOPPERS_TARGET_KERNEL_IMPL_H
#define TOPPERS_TARGET_KERNEL_IMPL_H

#include "esp32c5.h"
#include <sil.h>

#ifndef TOPPERS_MACRO_ONLY
extern void	target_mprc_initialize(void);
extern void	target_initialize(PCB *p_my_pcb);
extern void	target_exit(void) NoReturn;
#endif /* TOPPERS_MACRO_ONLY */

#include "chip_kernel_impl.h"

#endif /* TOPPERS_TARGET_KERNEL_IMPL_H */
