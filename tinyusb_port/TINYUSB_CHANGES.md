# Changes from upstream TinyUSB (device stack + DCD)

This document lists modifications made for the HackRF/LPC43xx TinyUSB device port. All **modified TinyUSB files** are kept in `tinyusb_port/` (not in `lib/tinyusb/`) so upstream TinyUSB under `lib/tinyusb/` remains unmodified.

---

## 1. `tinyusb_port/usbd_control.c`

(Copy of `lib/tinyusb/src/device/usbd_control.c` with the following changes.)

### 1.1 Deferred control status (EP0 busy)

- **Added** static state for deferring the status stage when EP0 is busy:
  - `static bool pending_status;`
  - `static tusb_control_request_t pending_status_request;`
- **Comment** (around line 62): when `status_stage_xact()` fails (EP0 busy), the request is deferred so the host eventually gets an ACK.

### 1.2 `usbd_control_reset()`

- **Added** `pending_status = false;` so deferred state is cleared on reset.

### 1.3 `usbd_control_deferred_status_poll(rhport)`

- **Added**: retries sending the deferred status stage when EP0 becomes free. Called at the start of the `tud_task_ext()` loop so deferred status is sent promptly without waiting for another control transfer.

### 1.4 `usbd_control_xfer_cb()` – status stage complete path

- **Added**: when a status stage completes, if `pending_status` is set, the code retries `status_stage_xact()` to send the deferred status. Complements the poll for cases where another transfer's status completion runs first.

### 1.5 `usbd_control_xfer_cb()` – DATA stage complete path

- **Changed**: when DATA stage is complete and the stack tries to queue the status stage via `status_stage_xact()`, failure (EP0 busy) is no longer fatal.
- **New behaviour**: if `status_stage_xact(rhport, &_ctrl_xfer.request)` returns false:
  - Store the request in `pending_status_request` and set `pending_status = true`.
  - **Added** debug log: `TU_LOG_USBD("  control: deferred status (EP0 busy), req=%u\r\n", ...)`.
- **Unchanged**: when `status_stage_xact()` succeeds, behaviour is as before (status queued immediately).
- Deferred status is retried by `usbd_control_deferred_status_poll()` (each tud_task loop) and by the status-stage complete path (or via `hackrf_usb_bridge_poll()` for no-data requests).

**Reason**: With `tud_task()` run from the USB ISR, EP0 can still be busy when DATA completes. Deferring and retrying when EP0 is free avoids host blocking and prevents asserts.

---

## 2. `tinyusb_port/usbd.c`

(Copy of `lib/tinyusb/src/device/usbd.c` with the following changes.)

### 2.1 `tud_task_ext()` – task loop

- **Added** at the start of the `while (1)` loop (before `osal_queue_receive`): call `usbd_control_deferred_status_poll(_usbd_rhport)` so deferred control status is retried on each task iteration.

### 2.2 `usbd_edpt_xfer()` – busy endpoint handling

- **Changed** (around lines 1400–1404): when the target endpoint is already busy (`_usbd_dev.ep_status[epnum][dir].busy != 0`), the code no longer asserts.
- **New behaviour**:
  - Log: `TU_LOG_USBD("  EP %02X busy, skip\r\n", ep_addr);`
  - **Return `false`** so the caller can defer (e.g. control status in `usbd_control.c`).
- **Reason**: Under ISR-driven `tud_task()`, EP0 can still be busy when the stack tries to queue the next transfer; returning false allows the control layer to defer and retry instead of crashing.

---

## Summary table

| File                       | Change                                                                 |
|----------------------------|-----------------------------------------------------------------------|
| `tinyusb_port/usbd_control.c` | Deferred status state; `usbd_control_deferred_status_poll()`; defer on EP0 busy in DATA complete path; retry in status-complete path and poll; clear deferred in reset; extra TU_LOG when deferring. |
| `tinyusb_port/usbd.c`        | Call `usbd_control_deferred_status_poll()` at start of `tud_task_ext()` loop; on busy EP in `usbd_edpt_xfer()`: log and return false instead of asserting. |
| `tinyusb_port/dcd_ci_hs.c`   | For `OPT_MCU_LPC43XX`, include `ci_hs_hackrf.h`. |
| `tinyusb_port/vendor_device.c`, `.h` | Add `CFG_TUD_VENDOR_TX_ZLP_AFTER_FULL_PACKET` (0 = no auto-ZLP for streaming). |

---

## 3. `tinyusb_port/dcd_ci_hs.c`

(Copy of `lib/tinyusb/src/portable/chipidea/ci_hs/dcd_ci_hs.c` with the following changes.)

### 3.1 LPC43xx: use HackRF port header

- **Changed** (around lines 50–56): for `OPT_MCU_LPC43XX`, the file includes `ci_hs_hackrf.h` instead of `ci_hs_lpc18_43.h`. Upstream uses `ci_hs_lpc18_43.h` for both LPC18xx and LPC43xx; the HackRF port uses `ci_hs_hackrf.h`, which bridges libopencm3 LPC43xx definitions to the ChipIdea driver.

---

## 4. `tinyusb_port/vendor_device.c` and `tinyusb_port/vendor_device.h`

(Copies of lib/tinyusb vendor class with ZLP option.)

### 4.1 CFG_TUD_VENDOR_TX_ZLP_AFTER_FULL_PACKET

- **Added** config option (default 1): when 1, send ZLP after full packet if FIFO empty (signals end of transfer). When 0, do not send ZLP.
- **tusb_config.h** sets it to 0 for HackRF streaming: the app feeds data in `tx_cb`; auto-ZLP would terminate the stream and make the host stop.

---

## 5. `tinyusb_port_debug.h` (unified port debug)

Single switch `TUSB_PORT_DEBUG` (in tusb_config.h) controls all port debug output. Does not alter TinyUSB's `CFG_TUSB_DEBUG` / `CFG_TUD_LOG_LEVEL`.

- **TUSB_PORT_DBG(fmt, ...)**: descriptor callbacks, init (tinyusb_port.c)
- **TUSB_PORT_DBG_BRIDGE(fmt, ...)**: control/bulk path (hackrf_usb_bridge.c)
- **TUSB_PORT_DBG_EVT(eventid)**: event hook (evt6, evt7, etc.)

Set `TUSB_PORT_DEBUG 1` in tusb_config.h for bring-up; 0 for production (UART in ISR blocks streaming).

---

Upstream files under `lib/tinyusb/` are **not modified**. The build uses the copies in `tinyusb_port/` (see `CMakeLists.txt`). You can swap `lib/tinyusb/` with a fresh upstream when updating. The rest of the port layer (`tinyusb_port/`), bridge (`hackrf_usb_bridge.c`), and `main.c` are outside the TinyUSB tree and are not listed here.
