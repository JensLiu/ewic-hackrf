/*
 * FT4232HQ USB host support for HackRF (TinyUSB bare host API).
 *
 * Stage 1: detect FTDI devices and open a pair of bulk endpoints so the
 * rest of the firmware can send/receive data using a simple C API.
 *
 * Stage 2 (later): layer IQ framing and buffering on top of these basic
 * read/write calls.
 */

#include "tusb.h"
#include "hackrf_core.h"  /* for delay() */

// FT4232HQ default VID/PID (multi-channel USB UART/MPSSE)
#define FTDI_VID         0x0403
#define FTDI_PID_FT4232H 0x6011

typedef struct {
  bool    present;
  uint8_t dev_addr;
  uint8_t ep_in;
  uint8_t ep_out;
} ftdi_host_state_t;

static tusb_desc_device_t   s_ftdi_dev_desc;
static ftdi_host_state_t    s_ftdi;

// Receive buffer for continuous reading
static uint8_t s_rx_buf[512];
static bool    s_rx_pending = false;

/* Small delay to prevent UART corruption during rapid events */
static void dbg_delay(void) {
  for (volatile int i = 0; i < 50000; i++) {}
}

// Forward declaration for RX callback
static void ftdi_rx_complete_cb(tuh_xfer_t *xfer);

// Start a bulk IN transfer (internal)
static bool ftdi_start_rx(void) {
  if (!s_ftdi.present || s_ftdi.ep_in == 0 || s_rx_pending) {
    return false;
  }
  
  tuh_xfer_t xfer = {
    .daddr       = s_ftdi.dev_addr,
    .ep_addr     = s_ftdi.ep_in,
    .buflen      = sizeof(s_rx_buf),
    .buffer      = s_rx_buf,
    .complete_cb = ftdi_rx_complete_cb,
    .user_data   = 0
  };
  
  if (tuh_edpt_xfer(&xfer)) {
    s_rx_pending = true;
    return true;
  }
  return false;
}

// RX complete callback - print data and re-queue
static void ftdi_rx_complete_cb(tuh_xfer_t *xfer) {
  s_rx_pending = false;
  
  if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len > 0) {
    // FTDI bulk IN has 2-byte modem status header, skip it
    uint16_t len = xfer->actual_len;
    uint8_t *data = xfer->buffer;
    
    if (len > 2) {
      // Print modem status + actual data
      tusb_uart_printf("[RX %u] modem=%02x%02x: ", 
                       (unsigned int)(len - 2),
                       (unsigned int)data[0], 
                       (unsigned int)data[1]);
      
      // Print payload as hex
      for (uint16_t i = 2; i < len && i < 34; i++) {  // limit to 32 bytes
        tusb_uart_printf("%02x ", (unsigned int)data[i]);
      }
      if (len > 34) {
        tusb_uart_printf("...");
      }
      tusb_uart_printf("\r\n");
    } else if (len == 2) {
      // Just modem status, no data (common when idle)
      // Don't print to avoid spam
    }
  } else if (xfer->result != XFER_RESULT_SUCCESS) {
    tusb_uart_printf("[RX] Error result=%d\r\n", (int)xfer->result);
  }
  
  // Re-queue another read
  ftdi_start_rx();
}

//--------------------------------------------------------------------+
// Helper: parse configuration and open first bulk IN/OUT pair
//--------------------------------------------------------------------+

static void ftdi_open_bulk_endpoints(uint8_t dev_addr,
                                     tusb_desc_configuration_t const *cfg) {
  uint8_t const *desc_end = ((uint8_t const *)cfg) + tu_le16toh(cfg->wTotalLength);
  uint8_t const *p        = tu_desc_next(cfg);

  uint8_t ep_in  = 0;
  uint8_t ep_out = 0;

  tusb_uart_printf("[FTDI] Parsing config, len=%u\r\n",
                   (unsigned int)tu_le16toh(cfg->wTotalLength));
  dbg_delay();

  while (p < desc_end) {
    uint8_t type = tu_desc_type(p);
    if (type == TUSB_DESC_ENDPOINT) {
      tusb_desc_endpoint_t const *ep = (tusb_desc_endpoint_t const *)p;
      if (ep->bmAttributes.xfer == TUSB_XFER_BULK) {
        tusb_uart_printf("[FTDI] Found BULK EP 0x%02x, max=%u\r\n",
                         (unsigned int)ep->bEndpointAddress,
                         (unsigned int)tu_edpt_packet_size(ep));
        dbg_delay();
        
        if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
          ep_in = ep->bEndpointAddress;
        } else {
          ep_out = ep->bEndpointAddress;
        }

        if (!tuh_edpt_open(dev_addr, ep)) {
          tusb_uart_printf("[FTDI] ERROR: edpt_open failed 0x%02x\r\n",
                           (unsigned int)ep->bEndpointAddress);
          dbg_delay();
        }

        if (ep_in && ep_out) {
          break;
        }
      }
    }
    p = tu_desc_next(p);
  }

  if (ep_in && ep_out) {
    s_ftdi.present  = true;
    s_ftdi.dev_addr = dev_addr;
    s_ftdi.ep_in    = ep_in;
    s_ftdi.ep_out   = ep_out;

    tusb_uart_printf("[FTDI] SUCCESS: IN=0x%02x OUT=0x%02x\r\n",
                     (unsigned int)ep_in, (unsigned int)ep_out);
    dbg_delay();
    
    // Start continuous reading
    tusb_uart_printf("[FTDI] Starting RX...\r\n");
    ftdi_start_rx();
  } else {
    tusb_uart_printf("[FTDI] ERROR: No bulk EP pair found\r\n");
  }
  dbg_delay();
}

//--------------------------------------------------------------------+
// Public API
//--------------------------------------------------------------------+

bool ftdi_host_ready(void) {
  return s_ftdi.present;
}

bool ftdi_host_write_async(void const *buffer, uint16_t len,
                           tuh_xfer_cb_t complete_cb,
                           uintptr_t user_data) {
  if (!s_ftdi.present || (s_ftdi.ep_out == 0)) {
    return false;
  }

  tuh_xfer_t xfer = {
    .daddr       = s_ftdi.dev_addr,
    .ep_addr     = s_ftdi.ep_out,
    .buflen      = len,
    .buffer      = (void *)buffer,
    .complete_cb = complete_cb,
    .user_data   = user_data
  };

  return tuh_edpt_xfer(&xfer);
}

bool ftdi_host_read_async(void *buffer, uint16_t len,
                          tuh_xfer_cb_t complete_cb,
                          uintptr_t user_data) {
  if (!s_ftdi.present || (s_ftdi.ep_in == 0)) {
    return false;
  }

  tuh_xfer_t xfer = {
    .daddr       = s_ftdi.dev_addr,
    .ep_addr     = s_ftdi.ep_in,
    .buflen      = len,
    .buffer      = buffer,
    .complete_cb = complete_cb,
    .user_data   = user_data
  };

  return tuh_edpt_xfer(&xfer);
}

//--------------------------------------------------------------------+
// TinyUSB host callbacks
//--------------------------------------------------------------------+

// Forward declaration
static void on_device_descriptor_complete(tuh_xfer_t *xfer);

// Invoked when device is mounted (configured)
void tuh_mount_cb(uint8_t dev_addr) {
  tusb_uart_printf("[USB] MOUNT addr=%u\r\n", (unsigned int)dev_addr);
  dbg_delay();
  
  // Request the device descriptor with callback
  bool ok = tuh_descriptor_get_device(dev_addr, &s_ftdi_dev_desc,
                                      sizeof(s_ftdi_dev_desc), 
                                      on_device_descriptor_complete, 0);
  tusb_uart_printf("[USB] get_dev_desc started=%d\r\n", ok ? 1 : 0);
  dbg_delay();
  if (!ok) {
    tusb_uart_printf("[USB] ERROR: get_device_desc failed to start\r\n");
    dbg_delay();
  }
}

// Invoked when device is unmounted (bus reset/unplugged)
void tuh_umount_cb(uint8_t dev_addr) {
  tusb_uart_printf("[USB] UNMOUNT addr=%u\r\n", (unsigned int)dev_addr);
  dbg_delay();

  if (s_ftdi.present && (s_ftdi.dev_addr == dev_addr)) {
    s_ftdi.present  = false;
    s_ftdi.dev_addr = 0;
    s_ftdi.ep_in    = 0;
    s_ftdi.ep_out   = 0;
    tusb_uart_printf("[FTDI] Disconnected\r\n");
    dbg_delay();
  }
}

// Called when the control transfer started in tuh_mount_cb completes.
static void on_device_descriptor_complete(tuh_xfer_t *xfer) {
  tusb_uart_printf("[USB] DevDesc cb result=%d\r\n", (int)xfer->result);
  dbg_delay();
  
  if (xfer->result != XFER_RESULT_SUCCESS) {
    tusb_uart_printf("[USB] ERROR: DevDesc xfer failed\r\n");
    dbg_delay();
    return;
  }

  uint16_t vid = s_ftdi_dev_desc.idVendor;
  uint16_t pid = s_ftdi_dev_desc.idProduct;

  tusb_uart_printf("[USB] VID=%04x PID=%04x\r\n",
                   (unsigned int)vid, (unsigned int)pid);
  dbg_delay();

  if (vid == FTDI_VID && pid == FTDI_PID_FT4232H) {
    tusb_uart_printf("[FTDI] FT4232H detected!\r\n");
    dbg_delay();

    // Get configuration descriptor synchronously and open bulk endpoints.
    uint8_t cfg_buf[256];
    xfer_result_t res = tuh_descriptor_get_configuration_sync(xfer->daddr, 0,
                                                               cfg_buf, sizeof(cfg_buf));
    tusb_uart_printf("[USB] ConfigDesc result=%d\r\n", (int)res);
    dbg_delay();
    
    if (res == XFER_RESULT_SUCCESS) {
      ftdi_open_bulk_endpoints(xfer->daddr,
                               (tusb_desc_configuration_t const *)cfg_buf);
    } else {
      tusb_uart_printf("[USB] ERROR: ConfigDesc failed\r\n");
      dbg_delay();
    }
  } else {
    tusb_uart_printf("[USB] Non-FTDI device (ignored)\r\n");
    dbg_delay();
  }
}

