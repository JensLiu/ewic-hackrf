# Copyright 2012 Jared Boone <jared@sharebrained.com>
#
# This file is part of HackRF.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.  If not, write to
# the Free Software Foundation, Inc., 51 Franklin Street,
# Boston, MA 02110-1301, USA.
#

cmake_minimum_required(VERSION 3.10.0)
set(CMAKE_TOOLCHAIN_FILE toolchain-arm-cortex-m.cmake)

# Generate compile_commands.json in build dir for clangd/editor (use build/ as compile-commands-dir)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(hackrf_usb C)

include(hackrf-common.cmake)

add_custom_command(
	OUTPUT ${PATH_HACKRF_CPLD_DATA_C}
	COMMAND ${PATH_CPLD_BITSTREAM_TOOL} --xsvf ${PATH_HACKRF_CPLD_XSVF} --hackrf-data ${PATH_HACKRF_CPLD_DATA_C}
	DEPENDS ${PATH_CPLD_BITSTREAM_TOOL} ${PATH_HACKRF_CPLD_XSVF}
)

# TinyUSB directory
set(TINYUSB_DIR ${PATH_HACKRF_FIRMWARE}/lib/tinyusb)

# Include directories
include_directories(
	${PATH_HACKRF_FIRMWARE_COMMON}
	${PATH_HACKRF_FIRMWARE}/lib/hackrf_usb
	${PATH_HACKRF_FIRMWARE}/lib/libopencm3/include
	${PATH_HACKRF_FIRMWARE}/uart_support
	${PATH_HACKRF_FIRMWARE}/tinyusb_port
	${PATH_HACKRF_FIRMWARE}/transceiver
	${TINYUSB_DIR}/src
	${TINYUSB_DIR}/src/common
	${TINYUSB_DIR}/src/host
	${TINYUSB_DIR}/src/portable/chipidea/ci_hs
	${TINYUSB_DIR}/src/portable/ehci
)

# Core firmware (USB host mode only - see TINYUSB_SRC for host stack)
set(SRC_M4
	main.c
	"${PATH_HACKRF_FIRMWARE_COMMON}/tuning.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/streaming.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/m0_state.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/radio.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/hackrf_core.c"
	# USB device mode removed - using USB host mode only
	# Removed: USB device mode sources (usb_api_*.c, usb_descriptor.c, usb_device.c)
	"${PATH_HACKRF_FIRMWARE_COMMON}/selftest.c"
	"${PATH_HACKRF_FIRMWARE}/uart_support/uart.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/fault_handler.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/cpld_jtag.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/cpld_xc2c.c"
	"${PATH_HACKRF_CPLD_DATA_C}"
	"${PATH_HACKRF_FIRMWARE_COMMON}/xapp058/lenval.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/xapp058/micro.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/xapp058/ports.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/crc.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/rom_iap.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/operacake.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/operacake_sctimer.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/delay.c"
	"${PATH_HACKRF_FIRMWARE_COMMON}/adc.c"
)

set(SRC_M0 sgpio_m0.s)

#--------------------------------------------------------------------
# TinyUSB USB Host Sources (for FTDI communication)
#--------------------------------------------------------------------
set(TINYUSB_SRC
	# TinyUSB core
	${TINYUSB_DIR}/src/tusb.c
	${TINYUSB_DIR}/src/common/tusb_fifo.c
	# Host stack
	${TINYUSB_DIR}/src/host/usbh.c
	${TINYUSB_DIR}/src/host/hub.c
# CDC host class (includes built-in FTDI serial sub-driver)
	${TINYUSB_DIR}/src/class/cdc/cdc_host.c
	# EHCI host controller
	${TINYUSB_DIR}/src/portable/ehci/ehci.c
	# ChipIdea host controller driver (HackRF copy: uses ci_hs_hackrf.h for LPC43xx)
	${PATH_HACKRF_FIRMWARE}/tinyusb_port/hcd_ci_hs.c
	# HackRF TinyUSB port: init, ISR
	${PATH_HACKRF_FIRMWARE}/tinyusb_port/tinyusb_port.c
	# FTDI host: CDC callbacks + public API
	${PATH_HACKRF_FIRMWARE}/tinyusb_port/ftdi_host.c
	# transceiver code
	${PATH_HACKRF_FIRMWARE}/transceiver/custom_transceiver.c
	${PATH_HACKRF_FIRMWARE}/transceiver/gr_lib.c
	${PATH_HACKRF_FIRMWARE}/transceiver/sine_table.c
)

# Add TinyUSB sources to M4 build
set(SRC_M4
	${SRC_M4}
	${TINYUSB_SRC}
)

if(BOARD STREQUAL "HACKRF_ONE")
	SET(SRC_M4
		${SRC_M4}
		"${PATH_HACKRF_FIRMWARE_COMMON}/portapack.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/ui_portapack.c"
	)
endif()

if(BOARD STREQUAL "RAD1O")
	SET(SRC_M4
		${SRC_M4}
		"${PATH_HACKRF_FIRMWARE_COMMON}/rad1o/display.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/rad1o/print.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/rad1o/render.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/rad1o/decoder.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/rad1o/smallfonts.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/rad1o/draw.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/rad1o/ubuntu18.c"
		"${PATH_HACKRF_FIRMWARE_COMMON}/ui_rad1o.c"
	)
endif()

# TinyUSB compile definitions (OPT_MCU_LPC43XX = 8 from tusb_option.h)
set(HACKRF_OPTS "${HACKRF_OPTS} -DCFG_TUSB_MCU=OPT_MCU_LPC43XX")

DeclareTargets()
