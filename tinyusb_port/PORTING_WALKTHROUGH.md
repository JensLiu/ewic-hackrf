# HackRF TinyUSB Port: Complete Porting Walkthrough

This document walks through the entire TinyUSB porting process for HackRF, indicating what is modified and how it differs from both upstream TinyUSB and the original hackrf_usb stack.

---

## 1. Overview: What Was Replaced

### Original hackrf_usb Stack (Removed/Replaced)

| Component | Location | Role |
|-----------|----------|------|
| `usb.c` | common/ | USB hardware init, ISR, main USB loop |
| `usb_request.c` | common/ | Control transfer dispatch, setup parsing |
| `usb_standard_request.c` | common/ | Standard USB requests (descriptor, config, etc.) |
| `usb_endpoint.c` | lib/hackrf_usb/ | Endpoint/queue init, tied to LPC43xx USB HW |
| `usb_queue.c` | common/ | Transfer queue, DMA descriptors, completion callbacks |

**Removed from build** when using TinyUSB (see CMakeLists.txt comment):
```
# usb.c, usb_request.c, usb_standard_request.c, usb_endpoint.c, usb_queue.c
```

### What Remains (Unchanged hackrf_usb / common)

- **`usb_descriptor.c`** – Device/configuration/string descriptors
- **`usb_device.c`** – `usb_device_t` and configuration tables
- **`usb_request.h`** – Types: `usb_transfer_stage_t`, `usb_request_status_t`, `usb_request_handler_fn`
- **`usb_queue.h`** – Types: `usb_transfer_t`, `usb_queue_t`, `transfer_completion_cb`; API: `usb_transfer_schedule`, `usb_transfer_schedule_block`, `usb_transfer_schedule_ack`, `usb_queue_init`, `usb_queue_flush_endpoint`, `usb_queue_transfer_complete`
- **`usb_endpoint.h`** – `usb_endpoint_t`, `USB_DECLARE_QUEUE`, extern endpoints
- **`usb_api_*.c`** – All vendor request handlers (e.g. `usb_vendor_request_set_freq`, `usb_vendor_request_set_transceiver_mode`, etc.)

The **API surface** (`usb_transfer_schedule_block`, `usb_transfer_schedule_ack`, `usb_vendor_request`, `usb_endpoint_*`) is preserved so that `usb_api_*` and mode handlers do not need to change.

---

## 2. Original hackrf_usb Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ main.c                                                                       │
│   - Loop: tud_task() equivalent was usb_run() / polling                       │
│   - Reads transceiver_request, calls rx_mode/tx_mode/off_mode/sweep_mode     │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ usb_api_transceiver.c, usb_api_*.c (VENDOR HANDLERS)                         │
│   - usb_vendor_request_set_transceiver_mode, usb_vendor_request_set_freq...  │
│   - Call usb_transfer_schedule_block() for DATA, usb_transfer_schedule_ack() │
│   - rx_mode/tx_mode call usb_transfer_schedule_block() for bulk              │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ usb_request.c + usb_standard_request.c                                       │
│   - usb_vendor_request() dispatches to vendor_request_handler[request]       │
│   - Handles SETUP/DATA/STATUS stages                                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ usb_queue.c + usb_endpoint.c                                                 │
│   - usb_transfer_schedule_block: control → queue DATA/status; bulk → queue   │
│   - usb_transfer_schedule: allocates transfer, programs HW (QTDs)            │
│   - On completion: usb_queue_transfer_complete → completion_cb → next xfer   │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ usb.c                                                                        │
│   - USB0 init (clocks, PHY, reset)                                           │
│   - usb0_isr(): reads ChipIdea registers, processes QTDs, queues events      │
│   - usb_run(): processes queue, invokes callbacks                            │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ LPC43xx USB0 Hardware (ChipIdea High-Speed)                                  │
│   - QHDs, QTDs, endpoint primed via register writes                          │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Key difference from TinyUSB**: hackrf_usb talks directly to the ChipIdea hardware (QHD/QTD structures). There is no layered DCD → USBD → class driver model.

---

## 3. TinyUSB Architecture (Upstream)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ Application (main.c)                                                         │
│   tud_task() - processes event queue                                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ USBD (usbd.c) - Device stack core                                            │
│   - Event loop, class driver registration                                    │
│   - usbd_edpt_xfer() - queues transfers to DCD                               │
└─────────────────────────────────────────────────────────────────────────────┘
         │                                    │
         │ Control                            │ Class (Vendor)
         ▼                                    ▼
┌─────────────────────────┐    ┌─────────────────────────────────────────────┐
│ usbd_control.c          │    │ vendor_device.c (TinyUSB vendor class)       │
│   - SETUP, DATA, STATUS │    │   - tud_vendor_write/read, tx_cb/rx_cb       │
│   - status_stage_xact() │    │   - Buffered stream or direct xfer           │
└─────────────────────────┘    └─────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ DCD (dcd_ci_hs.c) - Device Controller Driver                                 │
│   - dcd_edpt_xfer(), dcd_edpt_open()                                         │
│   - Programs QHD/QTD, handles completion IRQ                                 │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Hardware (ChipIdea registers)                                                │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Key difference**: TinyUSB has a clear separation: DCD (hardware), USBD (core), class drivers (vendor). Control and class transfers go through different paths.

---

## 4. Port Architecture: How They Connect

The port **bridges** TinyUSB to the existing HackRF API:

1. **TinyUSB vendor control** → `tud_vendor_control_xfer_cb` → **hackrf_usb_bridge** → `usb_vendor_request` → existing `usb_api_*` handlers
2. **Bulk** → `usb_transfer_schedule_block` (from rx_mode/tx_mode) → **hackrf_usb_bridge** `usb_transfer_schedule` → `tud_vendor_write` / vendor stream → TinyUSB vendor class → DCD

The bridge implements the **HackRF usb_queue / usb_request API** on top of TinyUSB:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ main.c (UNCHANGED structure)                                                 │
│   - tud_task(), hackrf_usb_bridge_poll()                                     │
│   - Reads transceiver_request, calls off_mode/rx_mode/tx_mode/sweep_mode     │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ lib/hackrf_usb/usb_api_*.c (UNCHANGED)                                       │
│   - usb_transfer_schedule_block(), usb_transfer_schedule_ack()               │
│   - Same vendor_request_handler[] table                                      │
└─────────────────────────────────────────────────────────────────────────────┘
                                      │
         ┌────────────────────────────┴────────────────────────────┐
         │                                                         │
         ▼                                                         ▼
┌─────────────────────────────┐                    ┌──────────────────────────────┐
│ hackrf_usb_bridge.c (NEW)   │                    │ transceiver_mode_tinyusb.c   │
│   - tud_vendor_control_xfer │                    │   (REPLACES usb_api_*.c      │
│     _cb → usb_vendor_request│                    │    mode implementations)     │
│   - usb_transfer_schedule   │                    │   - rx_mode, tx_mode,        │
│     _block routing          │                    │     off_mode use bridge      │
│   - Control: tud_control_   │                    │   - usb_transfer_schedule_   │
│     xfer, tud_control_status│                    │     block for bulk           │
│   - Bulk: tud_vendor_write, │                    │                              │
│     tud_vendor_tx_cb→       │                    │                              │
│     usb_queue_transfer_     │                    │                              │
│     complete                │                    │                              │
└─────────────────────────────┘                    └──────────────────────────────┘
         │                                                         │
         └────────────────────────────┬────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ TinyUSB (lib/tinyusb + tinyusb_port overrides)                               │
│   - tusb.c, usbd.c, usbd_control.c, vendor_device.c, dcd_ci_hs.c             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. Layer-by-Layer Breakdown

### 5.1 Hardware & Platform (tinyusb_port.c, ci_hs_hackrf.h)

| Component | Origin | Modified? | Notes |
|-----------|--------|-----------|-------|
| **tinyusb_port.c** | **New** | N/A | Replaces `usb.c`. HW init (clocks, PHY, reset), `tusb_init()`, `tud_connect()`, descriptor callbacks, `usb0_isr` → `tud_int_handler` + `tud_task()`, SysTick for `board_millis`, debug printf → UART. |
| **ci_hs_hackrf.h** | **New** | N/A | Bridges libopencm3 to TinyUSB. Maps `USB0_BASE`, `NVIC_USB0_IRQ`, provides `NVIC_EnableIRQ` etc. so `dcd_ci_hs.c` works with libopencm3 instead of LPCOpen. |

**hackrf_usb**: `usb.c` did init + ISR + event loop. TinyUSB splits: init in `tinyusb_port.c`, ISR calls `tud_int_handler` + `tud_task`, loop in `main.c` via `tud_task()`.

---

### 5.2 DCD – Device Controller Driver (dcd_ci_hs.c)

| Component | Origin | Modified? | Diff from TinyUSB |
|-----------|--------|-----------|-------------------|
| **tinyusb_port/dcd_ci_hs.c** | lib/tinyusb/.../dcd_ci_hs.c | **Yes** | See below |

**Modifications:**

1. **LPC43xx header** (line ~52):
   - **TinyUSB**: `#include "ci_hs_lpc18_43.h"` for LPC18xx/LPC43xx.
   - **Port**: `#include "ci_hs_hackrf.h"` for LPC43xx so it uses libopencm3.

---

### 5.3 USBD – Device Stack Core (usbd.c)

| Component | Origin | Modified? | Diff from TinyUSB |
|-----------|--------|-----------|-------------------|
| **tinyusb_port/usbd.c** | lib/tinyusb/.../usbd.c | **Yes** | See below |

**Modifications:**

1. **`usbd_edpt_xfer()` on busy EP** (~lines 1403–1407):
   - **TinyUSB**: `TU_ASSERT(busy == 0)` → crash if EP busy.
   - **Port**: If busy, logs and **returns `false`** so control layer can defer instead of asserting.

**Reason**: With `tud_task()` run from the USB ISR, EP0 can still be busy when the stack tries to queue status. Deferring avoids asserts and host stalls.

---

### 5.4 Control Layer (usbd_control.c)

| Component | Origin | Modified? | Diff from TinyUSB |
|-----------|--------|-----------|-------------------|
| **tinyusb_port/usbd_control.c** | lib/tinyusb/.../usbd_control.c | **Yes** | See below |

**Modifications:**

1. **Deferred status state**:
   - **TinyUSB**: Not present.
   - **Port**: `static bool pending_status`, `static tusb_control_request_t pending_status_request`.

2. **`usbd_control_reset()`**: Clears `pending_status`.

3. **`usbd_control_deferred_status_poll(rhport)`**: Retries `status_stage_xact()` when EP0 is free. Called at the start of each `tud_task_ext()` loop iteration.

4. **Status stage complete path**: If `pending_status` is set, retries `status_stage_xact()` (backup when another transfer's status completion runs first).

5. **DATA stage complete** (when queueing status):
   - **TinyUSB**: Assumes `status_stage_xact()` succeeds.
   - **Port**: If `status_stage_xact()` returns false (EP0 busy), stores request, sets `pending_status`, logs. Status is retried by the poll (next tud_task) or status-complete path (or via `hackrf_usb_bridge_poll` for no-data requests).

---

### 5.5 Bridge (hackrf_usb_bridge.c)

| Component | Origin | Modified? | Notes |
|-----------|--------|-----------|-------|
| **hackrf_usb_bridge.c** | **New** | N/A | Implements HackRF usb_queue/usb_request API on top of TinyUSB. |

**Control path**

- `tud_vendor_control_xfer_cb` receives SETUP/DATA/STATUS.
- Maps to `usb_transfer_stage_t` (SETUP/DATA/STATUS).
- Calls `usb_vendor_request(&ctrl_ep_for_vendor, stage)`.
- For DATA: `usb_transfer_schedule_block` → `tud_control_xfer` or `tud_control_status`.
- For no-data: `usb_transfer_schedule_ack` → `tud_control_status` (with EP0-busy deferral).
- `hackrf_usb_bridge_poll()` retries deferred no-data status after `tud_task()`.

**Bulk path**

- `usb_transfer_schedule_block` with EP 0x81 or 0x02 → `usb_transfer_schedule`.
- IN: `tud_vendor_write` + `tud_vendor_write_flush`.
- On completion: `tud_vendor_tx_cb` → `usb_queue_transfer_complete` → completion callback, then next transfer.

**Difference from hackrf_usb**: hackrf_usb programmed QTDs directly. The bridge uses TinyUSB vendor class (`tud_vendor_write`, stream, `tx_cb`).

---

### 5.6 Mode Implementations (transceiver_mode_tinyusb.c, sweep_mode_tinyusb.c, cpld_mode_tinyusb.c)

| Component | Origin | Modified? | Notes |
|-----------|--------|-----------|-------|
| **transceiver_mode_tinyusb.c** | **New** (replaces impl in usb_api_transceiver.c) | N/A | `off_mode`, `rx_mode`, `tx_mode` using `usb_transfer_schedule_block`, `usb_queue_transfer_complete`. Same logic as original, different backend. |
| **sweep_mode_tinyusb.c** | **New** (replaces impl in usb_api_sweep.c) | N/A | `sweep_mode` using bridge. |
| **cpld_mode_tinyusb.c** | **New** (replaces impl in usb_api_cpld.c) | N/A | `cpld_update` using bridge. |

**hackrf_usb**: Mode loops lived in `usb_api_transceiver.c`, `usb_api_sweep.c`, `usb_api_cpld.c` and called `usb_transfer_schedule_block` backed by `usb_queue.c` + HW. **Port**: Same API, implementations moved to `tinyusb_port` and use the bridge + TinyUSB.

---

## 6. Configuration (tusb_config.h)

| Option | Value | Purpose |
|--------|-------|---------|
| `CFG_TUSB_MCU` | `OPT_MCU_LPC43XX` | Select LPC43xx. |
| `CFG_TUD_VENDOR` | 1 | Enable vendor class. |
| `CFG_TUD_VENDOR_EPSIZE` | 512 | Bulk max packet size. |
| `CFG_TUD_VENDOR_TXRX_BUFFERED` | 1 | Use buffered (stream) mode. |
| `CFG_TUD_VENDOR_RX_MANUAL_XFER` | 1 | App schedules RX. |
| `CFG_TUD_VENDOR_TX_ZLP_AFTER_FULL_PACKET` | 0 | Disable auto-ZLP for streaming. |

---

## 7. Summary Table: Modified vs Original

| Layer | TinyUSB (upstream) | hackrf_usb (original) | Port (tinyusb_port) |
|-------|--------------------|------------------------|----------------------|
| **HW init** | BSP/board | `usb.c` | `tinyusb_port.c` |
| **DCD** | `dcd_ci_hs.c` (LPCOpen) | Direct QHD/QTD in `usb.c`/`usb_queue.c` | `dcd_ci_hs.c` + `ci_hs_hackrf.h` |
| **Vendor class** | N/A | N/A | `tinyusb_port` copy with ZLP option disabled |
| **USBD** | Asserts on busy EP | N/A | Return false on busy, deferred status |
| **Control** | No deferral | `usb_request.c` | Deferred status when EP0 busy |
| **Vendor class** | Unmodified | N/A | Use lib/tinyusb upstream |
| **Queue/transfer API** | N/A | `usb_queue.c` | `hackrf_usb_bridge.c` |
| **Mode loops** | N/A | In `usb_api_*` | `transceiver_mode_tinyusb.c`, etc. |

---

## 8. Data Flow: Control Request Example

1. Host sends SETUP.
2. DCD raises IRQ → `tud_int_handler` → event queued.
3. `tud_task()` (from ISR or main) processes event.
4. TinyUSB parses SETUP, calls `tud_vendor_control_xfer_cb(SETUP)`.
5. Bridge maps to `usb_vendor_request(ctrl_ep, SETUP)`.
6. `usb_vendor_request` → `vendor_request_handler[request]` (e.g. `usb_vendor_request_set_freq`).
7. Handler may call `usb_transfer_schedule_block` for DATA, or `usb_transfer_schedule_ack` for status.
8. Bridge: DATA → `tud_control_xfer`, status → `tud_control_status` (or deferred).
9. When DATA completes, `usbd_control_xfer_cb` runs; if status could not be queued, it is deferred.
10. `usbd_control_deferred_status_poll()` (each tud_task loop) or status-stage complete callback or `hackrf_usb_bridge_poll` sends deferred status when EP0 is free.

---

## 9. Data Flow: Bulk RX (hackrf_transfer)

1. Host sends SET_TRANSCEIVER_MODE(RX).
2. `request_transceiver_mode(RX)` sets `transceiver_request.mode=RX`, increments `seq`.
3. Main loop sees new mode, calls `rx_mode(seq)`.
4. `rx_mode` enables streaming, loops while `transceiver_request.seq == seq`.
5. When `m0_count - usb_count >= 16KB`, calls `usb_transfer_schedule_block(bulk_in, buffer, 16K, complete_cb)`.
6. Bridge: `usb_transfer_schedule` → `tud_vendor_write` + `tud_vendor_write_flush`.
7. TinyUSB vendor class sends data via DCD.
8. On completion, DCD fires, `vendord_xfer_cb` calls `tud_vendor_tx_cb`.
9. Bridge `tud_vendor_tx_cb` calls `usb_queue_transfer_complete` → `transceiver_bulk_transfer_complete`.
10. `rx_mode` loop continues, schedules next 16K when M0 has produced more data.

---

## 10. Files Reference

| File | Role |
|------|------|
| `tinyusb_port/tinyusb_port.c` | HW init, ISR, descriptors, SysTick, debug |
| `tinyusb_port/ci_hs_hackrf.h` | libopencm3 ↔ TinyUSB DCD |
| `tinyusb_port/dcd_ci_hs.c` | **Modified** DCD (ci_hs_hackrf.h) |
| `tinyusb_port/vendor_device.c`, `.h` | **Modified** vendor (ZLP option for streaming) |
| `tinyusb_port/usbd.c` | **Modified** USBD (busy EP returns false) |
| `tinyusb_port/usbd_control.c` | **Modified** control (deferred status) |
| `tinyusb_port/hackrf_usb_bridge.c` | **New** bridge (control + bulk) |
| `tinyusb_port/transceiver_mode_tinyusb.c` | **New** off/rx/tx modes |
| `tinyusb_port/sweep_mode_tinyusb.c` | **New** sweep mode |
| `tinyusb_port/cpld_mode_tinyusb.c` | **New** CPLD update |
| `tinyusb_port/tusb_config.h` | TinyUSB configuration |
| `lib/tinyusb/` | Unmodified upstream (tusb.c, tusb_fifo.c, etc.) |
