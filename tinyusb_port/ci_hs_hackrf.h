/*
 * ChipIdea High-Speed USB driver bridge for HackRF (libopencm3)
 *
 * This file bridges TinyUSB's ChipIdea driver to libopencm3's definitions
 * for the LPC43xx USB controllers.
 *
 * The LPC4320/4330 has two USB controllers:
 *   - USB0: High-speed capable (EHCI), at 0x40006000
 *   - USB1: Full-speed only, at 0x40007000
 */

#ifndef CI_HS_HACKRF_H_
#define CI_HS_HACKRF_H_

// Include libopencm3 headers for LPC43xx
#include <libopencm3/lpc43xx/memorymap.h>
#include <libopencm3/lpc43xx/m4/nvic.h>

// Include the ChipIdea type definitions from TinyUSB
#include "ci_hs_type.h"

//--------------------------------------------------------------------
// Bridge libopencm3 definitions to TinyUSB/LPCOpen expectations
//--------------------------------------------------------------------

// LPCOpen-style base address macros (expected by TinyUSB)
// libopencm3 defines USB0_BASE and USB1_BASE in memorymap.h
#ifndef LPC_USB0_BASE
#define LPC_USB0_BASE   USB0_BASE
#endif

#ifndef LPC_USB1_BASE
#define LPC_USB1_BASE   USB1_BASE
#endif

// IRQ numbers - bridge libopencm3 names to TinyUSB/CMSIS names
#ifndef USB0_IRQn
#define USB0_IRQn       NVIC_USB0_IRQ
#endif

#ifndef USB1_IRQn
#define USB1_IRQn       NVIC_USB1_IRQ
#endif

//--------------------------------------------------------------------
// NVIC functions - bridge libopencm3 to CMSIS-style names
//--------------------------------------------------------------------

// TinyUSB expects CMSIS-style NVIC functions
// libopencm3 uses lowercase nvic_* functions

static inline void NVIC_EnableIRQ(int irq) {
  nvic_enable_irq(irq);
}

static inline void NVIC_DisableIRQ(int irq) {
  nvic_disable_irq(irq);
}

static inline void NVIC_ClearPendingIRQ(int irq) {
  nvic_clear_pending_irq(irq);
}

static inline void NVIC_SetPriority(int irq, uint32_t priority) {
  nvic_set_priority(irq, priority);
}

//--------------------------------------------------------------------
// Controller Configuration Array
//--------------------------------------------------------------------

// This array tells TinyUSB about the available USB controllers
static const ci_hs_controller_t _ci_controller[] = {
  { .reg_base = LPC_USB0_BASE, .irqnum = USB0_IRQn },  // USB0: High-speed
  { .reg_base = LPC_USB1_BASE, .irqnum = USB1_IRQn }   // USB1: Full-speed only
};

//--------------------------------------------------------------------
// Macros used by TinyUSB ChipIdea driver
//--------------------------------------------------------------------

// Get the register base for a given port
#define CI_HS_REG(_port)        ((ci_hs_regs_t*) _ci_controller[_port].reg_base)

// Device Controller Driver (DCD) interrupt enable/disable
#define CI_DCD_INT_ENABLE(_p)   NVIC_EnableIRQ(_ci_controller[_p].irqnum)
#define CI_DCD_INT_DISABLE(_p)  NVIC_DisableIRQ(_ci_controller[_p].irqnum)

// Host Controller Driver (HCD) interrupt enable/disable
#define CI_HCD_INT_ENABLE(_p)   NVIC_EnableIRQ(_ci_controller[_p].irqnum)
#define CI_HCD_INT_DISABLE(_p)  NVIC_DisableIRQ(_ci_controller[_p].irqnum)

#endif /* CI_HS_HACKRF_H_ */
