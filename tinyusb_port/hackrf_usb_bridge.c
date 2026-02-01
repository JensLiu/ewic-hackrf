/*
 * HackRF USB bridge: TinyUSB device <-> existing usb_api_* and streaming
 *
 * - Vendor control: tud_vendor_control_xfer_cb -> usb_vendor_request, schedule_block/ack -> tud_control_xfer/status
 * - Configuration callback when mounted
 * - Bulk: queue API backed by TinyUSB vendor bulk
 */

#include "tusb.h"
#include "tinyusb_port_debug.h"
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include "usb_request.h"
#include "usb_type.h"
#include "usb_device.h"
#include "usb_descriptor.h"
#include "usb_queue.h"
#include "usb_endpoint.h"
#include "usb_standard_request.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* True when handler already sent status/response this request (avoid double-send) */
static bool handler_sent_status;
/* Deferred no-data status when tud_control_status() failed (EP0 busy) */
static bool pending_no_data_status;
static tusb_control_request_t pending_no_data_request;
static uint8_t pending_no_data_rhport;

/* Stubs for usb_device request table (standard/WCID handled by TinyUSB) */
usb_request_status_t usb_vendor_request_read_wcid(
    usb_endpoint_t* const endpoint,
    const usb_transfer_stage_t stage) {
  (void)endpoint;
  (void)stage;
  return USB_REQUEST_STATUS_OK;
}

usb_request_status_t usb_standard_request(
    usb_endpoint_t* const endpoint,
    const usb_transfer_stage_t stage) {
  (void)endpoint;
  (void)stage;
  return USB_REQUEST_STATUS_OK;
}

#define BOARD_TUD_RHPORT 0

/* Store data pointer in td.buffer_pointer_page[0] when using real usb_transfer_t */
#define TRANSFER_DATA(transfer) ((void*)(uintptr_t)((transfer)->td.buffer_pointer_page[0]))

/*---------------------------------------------------------------------------*/
/* Configuration changed callback (replaces usb_standard_request.c)         */
/*---------------------------------------------------------------------------*/

static void (*usb_configuration_changed_cb)(usb_device_t* const) = NULL;

void usb_set_configuration_changed_cb(void (*callback)(usb_device_t* const)) {
  usb_configuration_changed_cb = callback;
}

/*---------------------------------------------------------------------------*/
/* Vendor control: fake endpoint + current request for schedule_block/ack    */
/*---------------------------------------------------------------------------*/

static tusb_control_request_t current_control_request;
static uint8_t current_control_rhport;
static usb_setup_t bridge_setup;
static uint8_t bridge_buffer[32];
/* True while in DATA stage callback; stack sends status on return, so schedule_ack is no-op */
static bool in_data_stage_cb;

static usb_endpoint_t ctrl_ep_in_dummy;
static usb_endpoint_t ctrl_ep_out_dummy;
/* Control endpoint for vendor requests; in/out fixed at init, setup filled per request */
static usb_endpoint_t ctrl_ep_for_vendor = {
  .setup = {0},
  .address = 0,
  .device = &usb_device,
  .in = &ctrl_ep_in_dummy,
  .out = &ctrl_ep_out_dummy,
  .setup_complete = NULL,
  .transfer_complete = NULL,
};

static void bridge_fill_setup(const tusb_control_request_t* req) {
  bridge_setup.request_type = req->bmRequestType;
  bridge_setup.request = req->bRequest;
  bridge_setup.value = req->wValue;
  bridge_setup.index = req->wIndex;
  bridge_setup.length = req->wLength;
}

/* Forward: bulk path used by schedule_block for bulk IN/OUT */
int usb_transfer_schedule(const usb_endpoint_t* const endpoint,
                          void* const data, const uint32_t maximum_length,
                          const transfer_completion_cb completion_cb,
                          void* const user_data);

/* Called by vendor handlers: send IN data or status, or accept OUT data.
 * Also called by transceiver for bulk streaming (bulk IN/OUT) – route to bulk queue. */
int usb_transfer_schedule_block(
    const usb_endpoint_t* const endpoint,
    void* const data,
    const uint32_t maximum_length,
    const transfer_completion_cb completion_cb,
    void* const user_data) {
  /* Bulk endpoints: use bulk queue and TinyUSB vendor bulk (tud_vendor_write / read), not control */
  if (endpoint->address == 0x81 || endpoint->address == 0x02) {
    return usb_transfer_schedule(endpoint, data, maximum_length, completion_cb, user_data);
  }
  (void)completion_cb;
  (void)user_data;
  /* Control: OUT (host-to-device) receive into endpoint->buffer for DATA stage */
  bool out_data = (current_control_request.bmRequestType & 0x80) == 0
                  && current_control_request.wLength > 0;
  if (out_data && maximum_length > 0) {
    TUSB_PORT_DBG_BRIDGE("block OUT len=%lu", (unsigned long)maximum_length);
    tud_control_xfer(current_control_rhport, &current_control_request,
                     ctrl_ep_for_vendor.buffer, (uint16_t)maximum_length);
  } else if (data && maximum_length > 0) {
    /* IN: copy to RAM if small (e.g. from flash) */
    if (maximum_length <= sizeof(bridge_buffer)) {
      TUSB_PORT_DBG_BRIDGE("block IN buf len=%lu", (unsigned long)maximum_length);
      memcpy(bridge_buffer, data, maximum_length);
      tud_control_xfer(current_control_rhport, &current_control_request,
                       bridge_buffer, (uint16_t)maximum_length);
    } else {
      TUSB_PORT_DBG_BRIDGE("block IN ptr len=%lu", (unsigned long)maximum_length);
      tud_control_xfer(current_control_rhport, &current_control_request,
                       data, (uint16_t)maximum_length);
    }
  } else {  
    TUSB_PORT_DBG_BRIDGE("block status (no data)");
    handler_sent_status = true;
    tud_control_status(current_control_rhport, &current_control_request);
  }
  return 0;
}

int usb_transfer_schedule_ack(const usb_endpoint_t* const endpoint) {
  (void)endpoint;
  /* In DATA stage the stack sends status when we return true; avoid double-send */
  if (in_data_stage_cb) {
    TUSB_PORT_DBG_BRIDGE("ack skip (in data stage)");
    return 0;
  }
  /* Request has DATA stage: stack will queue status when DATA completes. Do not call
   * tud_control_status here or we overwrite _ctrl_xfer and queue status too early. */
  if (current_control_request.wLength > 0) {
    TUSB_PORT_DBG_BRIDGE("ack skip (has data stage, stack will ack)");
    return 0;
  }
  TUSB_PORT_DBG_BRIDGE("ack -> status");
  handler_sent_status = true;
  tud_control_status(current_control_rhport, &current_control_request);
  return 0;
}

/* Stall control endpoint */
void usb_endpoint_stall(const usb_endpoint_t* const endpoint) {
  (void)endpoint;
  /* Handled by returning false from tud_vendor_control_xfer_cb */
}

void usb_endpoint_flush(const usb_endpoint_t* const endpoint) {
  if (endpoint->address == 0x81 || endpoint->address == 0x02)
    usb_queue_flush_endpoint(endpoint);
}

/* Map TinyUSB control stage to HackRF transfer stage */
static usb_transfer_stage_t bridge_stage(uint8_t stage) {
  if (stage == CONTROL_STAGE_SETUP) return USB_TRANSFER_STAGE_SETUP;
  if (stage == CONTROL_STAGE_DATA)  return USB_TRANSFER_STAGE_DATA;
  /* CONTROL_STAGE_ACK */
  return USB_TRANSFER_STAGE_STATUS;
}

/*---------------------------------------------------------------------------*/
/* TinyUSB vendor control callback -> usb_vendor_request                     */
/*---------------------------------------------------------------------------*/

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const* request) {
  current_control_rhport = rhport;
  current_control_request = *request;
  bridge_fill_setup(request);
  ctrl_ep_for_vendor.setup = bridge_setup;

  TUSB_PORT_DBG_BRIDGE("xfer_cb stage=%u req=%u wVal=%u wLen=%u", (unsigned)stage, (unsigned)request->bRequest, (unsigned)request->wValue, (unsigned)request->wLength);

  if (stage == CONTROL_STAGE_SETUP)
    pending_no_data_status = false; /* new request; drop any deferred status from previous */
  handler_sent_status = false;
  in_data_stage_cb = (stage == CONTROL_STAGE_DATA);
  usb_transfer_stage_t hstage = bridge_stage(stage);
  usb_request_status_t status = usb_vendor_request(&ctrl_ep_for_vendor, hstage);
  in_data_stage_cb = false;

  TUSB_PORT_DBG_BRIDGE("xfer_cb after handler status=%d sent=%d", (int)status, (int)handler_sent_status);

  /* No-data request: handler must send status; if it didn't (e.g. read_wcid stub), send here */
  if (stage == CONTROL_STAGE_SETUP && request->wLength == 0 && status == USB_REQUEST_STATUS_OK && !handler_sent_status) {
    if (!tud_control_status(rhport, request)) {
      TUSB_PORT_DBG_BRIDGE("no-data status DEFER (EP0 busy)");
      pending_no_data_rhport = rhport;
      pending_no_data_request = *request;
      pending_no_data_status = true;
    } else {
      TUSB_PORT_DBG_BRIDGE("no-data status sent (fallback)");
    }
  }
  return (status == USB_REQUEST_STATUS_OK);
}

/* Call from main loop after tud_task() to send any deferred no-data status */
void hackrf_usb_bridge_poll(void) {
  if (pending_no_data_status && tud_control_status(pending_no_data_rhport, &pending_no_data_request)) {
    TUSB_PORT_DBG_BRIDGE("poll sent deferred status");
    pending_no_data_status = false;
  }
}

/*---------------------------------------------------------------------------*/
/* Mount callback -> configuration changed                                    */
/*---------------------------------------------------------------------------*/

void tud_mount_cb(void) {
  /* Host has set configuration – enumeration succeeded */
  if (usb_configuration_changed_cb)
    usb_configuration_changed_cb(&usb_device);
}

/*---------------------------------------------------------------------------*/
/* Bulk endpoints and queues (TinyUSB vendor bulk) - use real usb_transfer_t  */
/*---------------------------------------------------------------------------*/

#define BULK_POOL_SIZE 2
static usb_transfer_t bulk_in_transfers[BULK_POOL_SIZE];
static usb_transfer_t bulk_out_transfers[BULK_POOL_SIZE];

/* OUT: accumulate 512-byte rx_cb chunks into full transfer before completing */
static uint32_t bulk_out_accumulated;

/* Control queues (dummy - control uses schedule_block/ack) */
static usb_transfer_t control_out_transfers[1];
static usb_transfer_t control_in_transfers[1];

usb_endpoint_t usb_endpoint_control_out = {
  .address = 0x00,
  .device = &usb_device,
  .in = &usb_endpoint_control_in,
  .out = &usb_endpoint_control_out,
  .setup_complete = NULL,
  .transfer_complete = NULL,
};
usb_endpoint_t usb_endpoint_control_in = {
  .address = 0x80,
  .device = &usb_device,
  .in = &usb_endpoint_control_in,
  .out = &usb_endpoint_control_out,
  .setup_complete = NULL,
  .transfer_complete = NULL,
};

usb_endpoint_t usb_endpoint_bulk_in = {
  .address = 0x81,
  .device = &usb_device,
  .in = &usb_endpoint_bulk_in,
  .out = NULL,
  .setup_complete = NULL,
  .transfer_complete = usb_queue_transfer_complete,
};
usb_endpoint_t usb_endpoint_bulk_out = {
  .address = 0x02,
  .device = &usb_device,
  .in = NULL,
  .out = &usb_endpoint_bulk_out,  /* self for out endpoint */
  .setup_complete = NULL,
  .transfer_complete = usb_queue_transfer_complete,
};

/* Queues (must be after endpoints so .endpoint can be set) */
usb_queue_t usb_endpoint_control_out_queue = {
  .endpoint = &usb_endpoint_control_out,
  .pool_size = 1,
  .free_transfers = NULL,
  .active = NULL,
};
usb_queue_t usb_endpoint_control_in_queue = {
  .endpoint = &usb_endpoint_control_in,
  .pool_size = 1,
  .free_transfers = NULL,
  .active = NULL,
};
usb_queue_t usb_endpoint_bulk_in_queue = {
  .endpoint = &usb_endpoint_bulk_in,
  .pool_size = BULK_POOL_SIZE,
  .free_transfers = NULL,
  .active = NULL,
};
usb_queue_t usb_endpoint_bulk_out_queue = {
  .endpoint = &usb_endpoint_bulk_out,
  .pool_size = BULK_POOL_SIZE,
  .free_transfers = NULL,
  .active = NULL,
};

usb_queue_t* endpoint_queues[12] = { NULL };

#define USB_ENDPOINT_INDEX(addr) \
  (((addr & 0xF) * 2) + ((addr >> 7) & 1))

static usb_queue_t* endpoint_queue(const usb_endpoint_t* ep) {
  uint32_t idx = USB_ENDPOINT_INDEX(ep->address);
  return endpoint_queues[idx];
}

void usb_endpoint_init(const usb_endpoint_t* const endpoint, const bool enable_zlp) {
  (void)endpoint;
  (void)enable_zlp;
  /* TinyUSB configures endpoints on set_configuration */
}

void usb_queue_init(usb_queue_t* const queue) {
  uint32_t idx = USB_ENDPOINT_INDEX(queue->endpoint->address);
  endpoint_queues[idx] = queue;
  usb_transfer_t* pool = (queue->endpoint->address == 0x81) ? bulk_in_transfers :
                         (queue->endpoint->address == 0x02) ? bulk_out_transfers :
                         (queue->endpoint->address == 0x00) ? control_out_transfers : control_in_transfers;
  unsigned int n = (queue->endpoint->address == 0x81 || queue->endpoint->address == 0x02) ? BULK_POOL_SIZE : 1;
  for (unsigned int i = 0; i < n - 1; i++) {
    pool[i].next = &pool[i + 1];
    pool[i].queue = queue;
  }
  pool[n - 1].next = NULL;
  pool[n - 1].queue = queue;
  queue->free_transfers = pool;
  queue->active = NULL;
}

int usb_transfer_schedule(const usb_endpoint_t* const endpoint,
                          void* const data, const uint32_t maximum_length,
                          const transfer_completion_cb completion_cb,
                          void* const user_data) {
  usb_queue_t* queue = endpoint_queue(endpoint);
  if (!queue || !queue->free_transfers)
    return -1;

  usb_transfer_t* t = queue->free_transfers;
  queue->free_transfers = t->next;
  t->next = NULL;
  t->queue = queue;
  t->td.buffer_pointer_page[0] = (uint32_t)(uintptr_t)data;
  t->maximum_length = maximum_length;
  t->completion_cb = completion_cb;
  t->user_data = user_data;
  t->td.total_bytes = 0;

  if (!queue->active) {
    queue->active = t;
    if (endpoint->address & 0x80) {
      /* IN: write to stream. Flush explicitly since vendor won't auto-flush outside xfer_cb. */
      uint32_t written = tud_vendor_write(data, maximum_length);
      uint32_t flushed = tud_vendor_write_flush();
      TUSB_PORT_DBG_BRIDGE("sched IN written=%lu flushed=%lu (auto-flush in tx_cb)", (unsigned long)written, (unsigned long)flushed);
      if (written == 0)
        TUSB_PORT_DBG_BRIDGE("sched IN WARN: written=0 (stream full?) max_len=%lu", (unsigned long)maximum_length);
    } else {
      bulk_out_accumulated = 0;
      tud_vendor_read_xfer();
    }
  } else {
    /* Queued transfer: write to stream, vendor class will auto-flush after current xfer_cb */
    usb_transfer_t* tail = queue->active;
    while (tail->next) tail = tail->next;
    tail->next = t;
    if (endpoint->address & 0x80) {
      uint32_t written = tud_vendor_write(TRANSFER_DATA(t), t->maximum_length);
      TUSB_PORT_DBG_BRIDGE("queue IN written=%lu (will auto-flush)", (unsigned long)written);
    }
  }
  return 0;
}

void usb_queue_transfer_complete(usb_endpoint_t* const endpoint) {
  usb_queue_t* queue = endpoint_queue(endpoint);
  if (!queue || !queue->active)
    return;

  usb_transfer_t* t = queue->active;
  uint32_t xferred = t->td.total_bytes;
  if (t->completion_cb)
    t->completion_cb(t->user_data, xferred);

  queue->active = t->next;
  t->next = queue->free_transfers;
  queue->free_transfers = t;

  if (queue->active) {
    usb_transfer_t* next = queue->active;
    void* next_data = TRANSFER_DATA(next);
    if (endpoint->address & 0x80)
      tud_vendor_write(next_data, next->maximum_length);
    else
      tud_vendor_read_xfer();
  }
}

void usb_queue_flush_endpoint(const usb_endpoint_t* const endpoint) {
  usb_queue_t* queue = endpoint_queue(endpoint);
  if (!queue) return;
  if (endpoint->address == 0x02)
    bulk_out_accumulated = 0;
  while (queue->active) {
    usb_transfer_t* t = queue->active;
    queue->active = t->next;
    t->next = queue->free_transfers;
    queue->free_transfers = t;
  }
  tud_vendor_n_read_flush(0);
  tud_vendor_n_write_clear(0);
}

/*---------------------------------------------------------------------------*/
/* TinyUSB vendor bulk callbacks -> queue completion                         */
/*---------------------------------------------------------------------------*/

void tud_vendor_rx_cb(uint8_t idx, const uint8_t* buffer, uint32_t bufsize) {
  (void)idx;
  usb_queue_t* queue = endpoint_queues[USB_ENDPOINT_INDEX(0x02)];
  TUSB_PORT_DBG_BRIDGE("rx_cb bufsize=%lu active=%p", (unsigned long)bufsize, queue ? (void*)queue->active : NULL);
  if (!queue || !queue->active) {
    /* Data arrived before we scheduled (e.g. from flush's read); drain FIFO and re-prime */
    uint32_t total = 0;
    uint8_t discard[64];
    for (;;) {
      uint32_t n = tud_vendor_n_available(0);
      if (n == 0) break;
      uint32_t r = tud_vendor_n_read(0, discard, n < sizeof(discard) ? n : sizeof(discard));
      if (r == 0) break;
      total += r;
    }
    if (total) TUSB_PORT_DBG_BRIDGE("rx_cb drained %lu bytes (no active xfer)", (unsigned long)total);
    tud_vendor_read_xfer();
    return;
  }
  usb_transfer_t* t = queue->active;
  uint32_t n;
  if (bufsize > 0 && buffer) {
    n = bufsize <= (t->maximum_length - bulk_out_accumulated) ? bufsize : (t->maximum_length - bulk_out_accumulated);
    memcpy((uint8_t*)TRANSFER_DATA(t) + bulk_out_accumulated, buffer, n);
  } else {
    n = tud_vendor_n_available(0);
    if (n > (t->maximum_length - bulk_out_accumulated)) n = t->maximum_length - bulk_out_accumulated;
    if (n) tud_vendor_n_read(0, (uint8_t*)TRANSFER_DATA(t) + bulk_out_accumulated, n);
  }
  bulk_out_accumulated += n;
  if (bulk_out_accumulated >= t->maximum_length) {
    t->td.total_bytes = t->maximum_length;
    bulk_out_accumulated = 0;
    usb_queue_transfer_complete(&usb_endpoint_bulk_out);
    if (queue->active)
      bulk_out_accumulated = 0;
  } else {
    tud_vendor_read_xfer(); /* chain next read */
  }
}

void tud_vendor_tx_cb(uint8_t idx, uint32_t sent_bytes) {
  (void)idx;
  usb_queue_t* queue = endpoint_queues[USB_ENDPOINT_INDEX(0x81)];
  TUSB_PORT_DBG_BRIDGE("tx_cb sent=%lu active=%p", (unsigned long)sent_bytes, (void*)queue ? (void*)queue->active : NULL);
  if (sent_bytes == 0)
    TUSB_PORT_DBG_BRIDGE("tx_cb WARN: sent_bytes=0");
  if (queue && queue->active) {
    queue->active->td.total_bytes = sent_bytes;
    usb_queue_transfer_complete(&usb_endpoint_bulk_in);
    TUSB_PORT_DBG_BRIDGE("tx_cb: after complete, next=%p", (void*)(queue->active));
  }
}
