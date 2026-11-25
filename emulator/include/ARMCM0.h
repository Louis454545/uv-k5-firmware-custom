#ifndef HOST_ARMCM0_H
#define HOST_ARMCM0_H

#include <stdint.h>

typedef int IRQn_Type;

static inline void __disable_irq(void) {}
static inline void __enable_irq(void) {}
static inline void __NOP(void) {}
static inline void NVIC_EnableIRQ(IRQn_Type irqn) { (void)irqn; }
static inline void NVIC_DisableIRQ(IRQn_Type irqn) { (void)irqn; }

void HOST_RequestReset(void);
static inline void NVIC_SystemReset(void) { HOST_RequestReset(); }

#endif
