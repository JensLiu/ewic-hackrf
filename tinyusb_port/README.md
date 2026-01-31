# TinyUSB USB Device Port for HackRF

This directory contains the port layer that replaces the original HackRF USB stack (`hackrf_usb`) with TinyUSB as a USB **device**, so the HackRF appears as a vendor-class device to the host PC.

## Overview

*Note: Host mode (`tinyusb_host_init`) exists in the API but is not used; the firmware runs as a USB device only.*

The HackRF One uses an NXP LPC43xx microcontroller with two USB controllers:

- **USB0**: High-speed capable (480 Mbps) – used for PC communication
- **USB1**: Full-speed only (12 Mbps) – not easily accessible

The original stack (`usb.c`, `usb_request.c`, `usb_queue.c`, `usb_endpoint.c`) talks directly to the ChipIdea hardware. TinyUSB provides a layered model (DCD → USBD → class drivers). This port bridges TinyUSB to the existing HackRF API (`usb_transfer_schedule_block`, `usb_vendor_request`, etc.) so the rest of the firmware (`usb_api_*`, mode handlers) stays unchanged.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│ Application (main.c)                                             │
│   tud_task(), hackrf_usb_bridge_poll(), mode switches            │
├─────────────────────────────────────────────────────────────────┤
│ lib/hackrf_usb (usb_api_*.c)                                     │
│   Vendor handlers, usb_transfer_schedule_block(), usb_vendor_request│
├─────────────────────────────────────────────────────────────────┤
│ tinyusb_port/hackrf_usb_bridge.c                                 │
│   Tud_vendor_control_xfer_cb → usb_vendor_request                │
│   usb_transfer_schedule → tud_vendor_write / tx_cb               │
├─────────────────────────────────────────────────────────────────┤
│ TinyUSB (lib/tinyusb + tinyusb_port overrides)                   │
│   usbd.c, usbd_control.c, vendor_device.c, dcd_ci_hs.c           │
├─────────────────────────────────────────────────────────────────┤
│ tinyusb_port/ci_hs_hackrf.h, tinyusb_port.c                      │
│   libopencm3 ↔ TinyUSB, USB0 init, ISR, descriptors              │
└─────────────────────────────────────────────────────────────────┘
```

## Files

| File | Role |
|------|------|
| `tusb_config.h` | TinyUSB configuration (MCU, vendor class, buffers) |
| `ci_hs_hackrf.h` | Maps libopencm3 LPC43xx to TinyUSB ChipIdea DCD |
| `ci_hs_lpc18_43.h` | Redirects to `ci_hs_hackrf.h` for LPC43xx |
| `tinyusb_port.c` | USB0 init, ISR (`tud_int_handler` + `tud_task`), descriptors |
| `tinyusb_port.h` | Public API (init, ISR, bridge poll) |
| `hackrf_usb_bridge.c` | HackRF usb_queue / usb_request API on top of TinyUSB |
| `usbd.c`, `usbd_control.c`, `dcd_ci_hs.c` | Modified TinyUSB copies (see TINYUSB_CHANGES.md) |
| `transceiver_mode_tinyusb.c`, `sweep_mode_tinyusb.c`, `cpld_mode_tinyusb.c` | Mode loops using the bridge |

## Build

```bash
mkdir build && cd build
cmake -DBOARD=HACKRF_ONE ..
make
```

Flash:

```bash
hackrf_spiflash -w hackrf_usb.bin
```

## Documentation

- **TINYUSB_CHANGES.md** – Modifications to TinyUSB files (deferred status, busy EP handling, DCD header)
- **PORTING_WALKTHROUGH.md** – Full porting process, architecture comparison, data flows

## License

This port is provided under the same license as the HackRF firmware (GPL v2+). TinyUSB is licensed under the MIT license.
