/*
 * Sweep state shared between usb_api_sweep.c (init_sweep) and
 * tinyusb_port/sweep_mode_tinyusb.c (sweep_mode).
 */

#ifndef __USB_API_SWEEP_INTERNAL_H__
#define __USB_API_SWEEP_INTERNAL_H__

#include "usb_api_sweep.h"
#include <stdint.h>

#define FREQ_GRANULARITY 1000000
#define MAX_RANGES       10
#ifndef PRALINE
#define THROWAWAY_BUFFERS 2
#else
#define THROWAWAY_BUFFERS 1
#endif

extern uint64_t sweep_freq;
extern uint16_t frequencies[MAX_RANGES * 2];
extern uint16_t num_ranges;
extern uint32_t dwell_blocks;
extern uint32_t step_width;
extern uint32_t offset;
extern enum sweep_style style;

#endif /* __USB_API_SWEEP_INTERNAL_H__ */
