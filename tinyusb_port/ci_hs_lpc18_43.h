/*
 * Override for TinyUSB's ci_hs_lpc18_43.h
 *
 * This file redirects to our libopencm3-compatible implementation.
 * It must be found BEFORE TinyUSB's version via include path ordering.
 */

#ifndef CI_HS_LPC18_43_H_
#define CI_HS_LPC18_43_H_

// Include our libopencm3-compatible bridge header
#include "ci_hs_hackrf.h"

#endif /* CI_HS_LPC18_43_H_ */
