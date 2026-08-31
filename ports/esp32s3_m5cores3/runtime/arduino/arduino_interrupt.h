/*
 *  attachInterrupt 用の割込み線とその優先度
 *
 *  cfg（arduino_interrupt.cfg）と C（arduino_interrupt.c）の両方から読む。
 *  片方だけ直して食い違うことがないよう、値はここ 1 か所で決める。
 *
 *  線の選定根拠は arduino_interrupt.cfg のコメントにある。
 */

#ifndef TOPPERS_ARDUINO_INTERRUPT_H
#define TOPPERS_ARDUINO_INTERRUPT_H

/*  CPU 割込み線。EXTERN_LEVEL・レベル1 で、本ポートでは未使用。 */
#define ARD_GPIO_INTNO		18U

/*  CFG_INT の割込み優先度。コンソール（USART_INTPRI）と同じ段にする。
 *  TMAX_INTPRI はカーネルが決める最低優先度（レベル1）。 */
#define ARD_GPIO_INTPRI		(TMAX_INTPRI)

/*  GPIO 割込みを受けるコア。PRC1/CORE0 固定（esp_shim_intr.c と同じ前提）。 */
#define ARD_CORE_ID			0U

#endif /* TOPPERS_ARDUINO_INTERRUPT_H */
