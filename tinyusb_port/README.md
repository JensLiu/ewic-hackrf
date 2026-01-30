# TinyUSB USB Host Port for HackRF

This directory contains the port layer to use TinyUSB as a USB Host on HackRF One.

## Overview

The HackRF One uses an NXP LPC4320 microcontroller with two USB controllers:
- **USB0**: High-speed capable (480 Mbps) - Used for USB Host
- **USB1**: Full-speed only (12 Mbps) - Not easily accessible

TinyUSB's ChipIdea EHCI driver supports the LPC43xx USB controllers, but the
official TinyUSB BSP expects NXP's LPCOpen SDK. This port provides a bridge
to work with HackRF's libopencm3-based firmware.

## Files

- `tusb_config.h` - TinyUSB configuration (MCU type, classes enabled, etc.)
- `ci_hs_hackrf.h` - Bridge header that maps libopencm3 to TinyUSB expectations
- `ci_hs_lpc18_43.h` - Override header (redirects to ci_hs_hackrf.h)
- `tinyusb_port.c` - USB initialization, interrupt handling, and callbacks
- `tinyusb_port.h` - Public API header

## Usage

### Important: USB0 device vs host

**USB0 can operate as either device (HackRF↔PC) or host (keyboard, etc.), not both.**

- **Device mode** (default): The existing hackrf_usb stack uses USB0 so the HackRF appears as a USB device to the PC. Do **not** call `tinyusb_host_init()` or you will break PC communication.
- **Host mode**: To use TinyUSB host (keyboards, serial adapters, mass storage), you must **not** initialise the existing USB device stack on USB0. Use a build-time switch (e.g. `ENABLE_USB_HOST_MODE`) to compile either device-only or host-only and call either `usb_run()` or `tinyusb_host_init()` from `main.c`, and wire `usb0_isr()` to either the device handler or `tinyusb_usb0_isr()`.

### 1. (Optional) Enable USB Host Mode

If you want USB host instead of device, in `main.c`:
- Define `ENABLE_USB_HOST_MODE 1` and gate the existing USB device init (e.g. `usb_run()`, `usb_device_init()`) with `#if !ENABLE_USB_HOST_MODE`.
- After `cpu_clock_init()`, call `tinyusb_host_init()` when host mode is enabled.
- In the vector table / ISR, call `tinyusb_usb0_isr()` instead of the device `usb0_isr()` when host mode is enabled.

### 2. Build the Firmware

```bash
mkdir build && cd build
cmake -DBOARD=HACKRF_ONE ..
make
```

### 3. Flash to HackRF

```bash
hackrf_spiflash -w hackrf_usb.bin
```

### 4. Connect UART for Debug Output

The USB Host status is output via UART. Connect a USB-to-serial adapter:
- UART TX: Pin P2_0 (check HackRF schematic for exact location)
- Baud rate: 115200, 8N1

### 5. Power Considerations

**IMPORTANT**: USB Host mode requires 5V VBUS power to be supplied to connected
devices. The HackRF does NOT have built-in VBUS power control for host mode.

Options:
1. Use a powered USB hub connected to HackRF
2. Use self-powered USB devices
3. Add external 5V power injection to the USB port

## Supported USB Device Classes

The following USB device classes are enabled by default:

- **HID**: Keyboards, mice, gamepads
- **CDC**: Serial adapters (FTDI, CP210x, CH340)
- **MSC**: USB mass storage (flash drives)
- **HUB**: USB hubs (for connecting multiple devices)

## Customization

### Changing USB Classes

Edit `tusb_config.h` to enable/disable device classes:
```c
#define CFG_TUH_HID    4  // Number of HID devices
#define CFG_TUH_CDC    2  // Number of CDC devices
#define CFG_TUH_MSC    1  // Number of MSC devices
```

### Adding Custom Device Handling

Implement TinyUSB callbacks in `tinyusb_port.c`:
- `tuh_mount_cb()` - Device connected
- `tuh_umount_cb()` - Device disconnected
- `tuh_hid_report_received_cb()` - HID data received
- `tuh_cdc_rx_cb()` - CDC data received
- `tuh_msc_mount_cb()` - Mass storage mounted

## Troubleshooting

### USB Device Not Detected

1. Check UART output for initialization messages
2. Verify 5V VBUS power is present on USB port
3. Try a different USB device (some may have compatibility issues)

### Build Errors

1. Ensure TinyUSB submodule is present in `lib/tinyusb/`
2. Check include path order in CMakeLists.txt (tinyusb_port must come before tinyusb/src)
3. Verify `CFG_TUSB_MCU=OPT_MCU_LPC43XX` is defined

### Initialization Fails

1. USB0 clock must be enabled (PLL0USB at 480MHz)
2. USB PHY must be powered on (CREG0 bit 5)
3. Check for conflicting USB0 usage in other code

## Architecture

```
┌─────────────────────────────────────────────────┐
│                  Application                     │
│              (main.c / callbacks)                │
├─────────────────────────────────────────────────┤
│                   TinyUSB                        │
│    ┌─────────┬─────────┬─────────┬─────────┐    │
│    │   HID   │   CDC   │   MSC   │   HUB   │    │
│    └────┬────┴────┬────┴────┬────┴────┬────┘    │
│         └─────────┴─────────┴─────────┘         │
│                   USBH Core                      │
├─────────────────────────────────────────────────┤
│              ChipIdea EHCI HCD                   │
│           (hcd_ci_hs.c / ehci.c)                 │
├─────────────────────────────────────────────────┤
│          TinyUSB Port (this directory)           │
│    ci_hs_hackrf.h - libopencm3 bridge           │
│    tinyusb_port.c - init, ISR, callbacks        │
├─────────────────────────────────────────────────┤
│               libopencm3 / LPC43xx              │
│                 USB0 Hardware                    │
└─────────────────────────────────────────────────┘
```

## License

This port is provided under the same license as the HackRF firmware (GPL v2+).
TinyUSB is licensed under the MIT license.
