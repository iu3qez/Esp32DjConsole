#include "usb_dj_host.h"
#include "status_led.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/usb_types_ch9.h"

static const char *TAG = "usb_dj";

// ---------------------------------------------------------------------------
// Control mapping table (ported from sample.ino)
// ---------------------------------------------------------------------------

typedef struct {
    const char *name;
    uint8_t byte_offset;
    uint8_t byte_mask;
    dj_control_type_t control_type;
} control_mapping_t;

static const control_mapping_t s_mappings[] = {
    // Buttons (type 0) - Deck A
    { "PitchReset_A",    4, 0x80, DJ_CTRL_BUTTON },
    { "PitchBendMinus_A",0, 0x02, DJ_CTRL_BUTTON },
    { "PitchBendPlus_A", 0, 0x04, DJ_CTRL_BUTTON },
    { "Sync_A",          4, 0x20, DJ_CTRL_BUTTON },
    { "Shift_A",         0, 0x01, DJ_CTRL_BUTTON },
    { "Shifted_A",       3, 0x10, DJ_CTRL_BUTTON },
    { "N1_A",            4, 0x40, DJ_CTRL_BUTTON },
    { "N2_A",            0, 0x10, DJ_CTRL_BUTTON },
    { "N3_A",            0, 0x20, DJ_CTRL_BUTTON },
    { "N4_A",            0, 0x40, DJ_CTRL_BUTTON },
    { "N5_A",            5, 0x01, DJ_CTRL_BUTTON },
    { "N6_A",            5, 0x02, DJ_CTRL_BUTTON },
    { "N7_A",            5, 0x04, DJ_CTRL_BUTTON },
    { "N8_A",            5, 0x08, DJ_CTRL_BUTTON },
    { "RWD_A",           0, 0x08, DJ_CTRL_BUTTON },
    { "FWD_A",           0, 0x80, DJ_CTRL_BUTTON },
    { "CUE_A",           1, 0x02, DJ_CTRL_BUTTON },
    { "Play_A",          1, 0x04, DJ_CTRL_BUTTON },
    { "Listen_A",        1, 0x01, DJ_CTRL_BUTTON },
    { "Load_A",          1, 0x08, DJ_CTRL_BUTTON },

    // Buttons - Deck B
    { "PitchReset_B",    4, 0x02, DJ_CTRL_BUTTON },
    { "PitchBendMinus_B",3, 0x02, DJ_CTRL_BUTTON },
    { "PitchBendPlus_B", 3, 0x04, DJ_CTRL_BUTTON },
    { "Sync_B",          4, 0x08, DJ_CTRL_BUTTON },
    { "Shift_B",         3, 0x01, DJ_CTRL_BUTTON },
    { "Shifted_B",       3, 0x20, DJ_CTRL_BUTTON },
    { "N1_B",            4, 0x04, DJ_CTRL_BUTTON },
    { "N2_B",            2, 0x10, DJ_CTRL_BUTTON },
    { "N3_B",            2, 0x20, DJ_CTRL_BUTTON },
    { "N4_B",            2, 0x40, DJ_CTRL_BUTTON },
    { "N5_B",            5, 0x10, DJ_CTRL_BUTTON },
    { "N6_B",            5, 0x20, DJ_CTRL_BUTTON },
    { "N7_B",            5, 0x40, DJ_CTRL_BUTTON },
    { "N8_B",            5, 0x80, DJ_CTRL_BUTTON },
    { "RWD_B",           3, 0x08, DJ_CTRL_BUTTON },
    { "FWD_B",           2, 0x80, DJ_CTRL_BUTTON },
    { "CUE_B",           2, 0x02, DJ_CTRL_BUTTON },
    { "Play_B",          2, 0x04, DJ_CTRL_BUTTON },
    { "Listen_B",        2, 0x01, DJ_CTRL_BUTTON },
    { "Load_B",          2, 0x08, DJ_CTRL_BUTTON },

    // Global buttons
    { "Vinyl",           4, 0x10, DJ_CTRL_BUTTON },
    { "Magic",           4, 0x01, DJ_CTRL_BUTTON },
    { "Up",              1, 0x10, DJ_CTRL_BUTTON },
    { "Down",            1, 0x80, DJ_CTRL_BUTTON },
    { "Folders",         1, 0x20, DJ_CTRL_BUTTON },
    { "Files",           1, 0x40, DJ_CTRL_BUTTON },

    // Dials and sliders (type 1), range 0x00-0xFF
    { "Treble_A",        7,  0xFF, DJ_CTRL_DIAL },
    { "Medium_A",        8,  0xFF, DJ_CTRL_DIAL },
    { "Bass_A",          9,  0xFF, DJ_CTRL_DIAL },
    { "Vol_A",           6,  0xFF, DJ_CTRL_DIAL },
    { "Treble_B",        12, 0xFF, DJ_CTRL_DIAL },
    { "Medium_B",        13, 0xFF, DJ_CTRL_DIAL },
    { "Bass_B",          14, 0xFF, DJ_CTRL_DIAL },
    { "Vol_B",           11, 0xFF, DJ_CTRL_DIAL },
    { "XFader",          10, 0xFF, DJ_CTRL_DIAL },

    // Jog wheels / rotary encoders (type 2), 0x00-0xFF with wrap-around
    { "Jog_A",           15, 0xFF, DJ_CTRL_ENCODER },
    { "Pitch_A",         17, 0xFF, DJ_CTRL_ENCODER },
    { "Jog_B",           16, 0xFF, DJ_CTRL_ENCODER },
    { "Pitch_B",         18, 0xFF, DJ_CTRL_ENCODER },
};

#define NUM_MAPPINGS (sizeof(s_mappings) / sizeof(s_mappings[0]))

// ---------------------------------------------------------------------------
// USB init sequence (ported from sample.ino send_init_sequence)
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t  bm_request_type;
    uint8_t  b_request;
    uint16_t w_value;
    uint16_t w_index;
    uint16_t w_length;
} init_cmd_t;

static const init_cmd_t s_init_sequence[] = {
    { 0xC0, 0x2C, 0x0000, 0x0000, 2 },  // => 4040
    { 0xC0, 0x29, 0x0300, 0x0000, 2 },  // => 0c0c
    { 0xC0, 0x29, 0x0400, 0x0000, 2 },  // => f2f2
    { 0xC0, 0x29, 0x0500, 0x0000, 2 },  // => eded
    { 0xC0, 0x29, 0x0600, 0x0000, 2 },  // => 7373
    { 0xC0, 0x2C, 0x0000, 0x0000, 2 },  // => 4040
    { 0xC0, 0x2C, 0x0000, 0x0000, 2 },  // => 4040
    { 0xC0, 0x29, 0x0300, 0x0000, 2 },  // => 0c0c
    { 0xC0, 0x29, 0x0400, 0x0000, 2 },  // => f2f2
    { 0xC0, 0x29, 0x0500, 0x0000, 2 },  // => eded
    { 0xC0, 0x29, 0x0600, 0x0000, 2 },  // => 7373
    { 0xC0, 0x29, 0x0200, 0x0000, 2 },  // => 0000
    { 0x02, 0x01, 0x0000, 0x0082, 0 },  // CLEAR_FEATURE(ENDPOINT_HALT) on EP 0x82
    { 0x40, 0x27, 0x0000, 0x0000, 0 },  // Vendor OUT command
};

#define INIT_SEQ_LEN (sizeof(s_init_sequence) / sizeof(s_init_sequence[0]))

// ---------------------------------------------------------------------------
// Device state
// ---------------------------------------------------------------------------

#define DATA_IN_EP_DEFAULT  0x81   // EP1 IN (Teensy reference driver uses endpoint 1)
#define IFACE_NUM    1             // hdjd Linux driver uses interface 1 for MP3e2
#define EP_MPS_DEFAULT  64         // Fallback MPS; actual value read from descriptor

static uint8_t  s_bulk_in_ep  = DATA_IN_EP_DEFAULT;
static uint16_t s_bulk_in_mps = EP_MPS_DEFAULT;
static uint8_t  s_data_ep_iface = 0;  // interface owning the data endpoint

static usb_host_client_handle_t s_client_hdl = NULL;
static usb_device_handle_t s_dev_hdl = NULL;
static usb_transfer_t *s_ctrl_xfer = NULL;
static usb_transfer_t *s_bulk_in_xfer = NULL;
static SemaphoreHandle_t s_ctrl_sem = NULL;
static TaskHandle_t s_setup_task_hdl = NULL;
static uint8_t s_pending_dev_addr = 0;

static uint8_t s_current_state[DJ_STATE_SIZE];
static uint8_t s_old_state[DJ_STATE_SIZE];
static bool s_device_connected = false;
static dj_control_callback_t s_callback = NULL;
static dj_raw_state_callback_t s_raw_callback = NULL;

// ---------------------------------------------------------------------------
// State diffing (ported from sample.ino on_state_update)
// ---------------------------------------------------------------------------

static void process_state_update(void)
{
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        const control_mapping_t *m = &s_mappings[i];

        uint8_t old_val = s_old_state[m->byte_offset] & m->byte_mask;
        uint8_t new_val = s_current_state[m->byte_offset] & m->byte_mask;

        // Buttons: normalize to 0/1
        if (m->control_type == DJ_CTRL_BUTTON) {
            old_val = old_val > 0 ? 1 : 0;
            new_val = new_val > 0 ? 1 : 0;
        }

        if (new_val != old_val) {
            ESP_LOGD(TAG, "Control: %s %d -> %d", m->name, old_val, new_val);

            if (s_callback) {
                s_callback(m->name, m->control_type, i, old_val, new_val);
            }
        }
    }

    memcpy(s_old_state, s_current_state, DJ_STATE_SIZE);
}

// ---------------------------------------------------------------------------
// USB transfer callbacks
// ---------------------------------------------------------------------------

static void ctrl_xfer_cb(usb_transfer_t *transfer)
{
    if (transfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "Control transfer failed, status=%d", transfer->status);
    }
    xSemaphoreGive(s_ctrl_sem);
}

static uint32_t s_xfer_count = 0;

static void bulk_in_cb(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        s_xfer_count++;
        if (s_xfer_count <= 3 || (s_xfer_count % 1000) == 0) {
            ESP_LOGI(TAG, "IN xfer #%lu: %d bytes on EP 0x%02X",
                     (unsigned long)s_xfer_count,
                     transfer->actual_num_bytes,
                     transfer->bEndpointAddress);
        }
        if (transfer->actual_num_bytes >= DJ_STATE_SIZE) {
            if (s_raw_callback) {
                s_raw_callback(transfer->data_buffer, transfer->actual_num_bytes);
            }
            memcpy(s_current_state, transfer->data_buffer, DJ_STATE_SIZE);
            process_state_update();
        } else if (transfer->actual_num_bytes > 0) {
            ESP_LOGW(TAG, "Short transfer: %d bytes (need %d)",
                     transfer->actual_num_bytes, DJ_STATE_SIZE);
        }
        // Re-submit for continuous polling
        usb_host_transfer_submit(transfer);
    } else if (transfer->status == USB_TRANSFER_STATUS_CANCELED) {
        ESP_LOGW(TAG, "IN transfer cancelled (device disconnected?)");
    } else {
        ESP_LOGE(TAG, "IN transfer error, status=%d, EP=0x%02X",
                 transfer->status, transfer->bEndpointAddress);
        // Try to re-submit after a short delay
        vTaskDelay(pdMS_TO_TICKS(100));
        usb_host_transfer_submit(transfer);
    }
}

// ---------------------------------------------------------------------------
// Init sequence
// ---------------------------------------------------------------------------

static esp_err_t send_ctrl_transfer(const init_cmd_t *cmd)
{
    usb_setup_packet_t *setup = (usb_setup_packet_t *)s_ctrl_xfer->data_buffer;
    setup->bmRequestType = cmd->bm_request_type;
    setup->bRequest = cmd->b_request;
    setup->wValue = cmd->w_value;
    setup->wIndex = cmd->w_index;
    setup->wLength = cmd->w_length;

    s_ctrl_xfer->num_bytes = sizeof(usb_setup_packet_t) + cmd->w_length;
    s_ctrl_xfer->device_handle = s_dev_hdl;
    s_ctrl_xfer->bEndpointAddress = 0;
    s_ctrl_xfer->callback = ctrl_xfer_cb;
    s_ctrl_xfer->context = NULL;

    esp_err_t err = usb_host_transfer_submit_control(s_client_hdl, s_ctrl_xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Submit control transfer failed: %s", esp_err_to_name(err));
        return err;
    }

    if (xSemaphoreTake(s_ctrl_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Control transfer timeout");
        return ESP_ERR_TIMEOUT;
    }

    if (s_ctrl_xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        return ESP_FAIL;
    }

    // Log response for IN transfers (debugging)
    if ((cmd->bm_request_type & 0x80) && cmd->w_length > 0) {
        uint8_t *resp = s_ctrl_xfer->data_buffer + sizeof(usb_setup_packet_t);
        ESP_LOGI(TAG, "  Response: %02X %02X",
                 cmd->w_length > 0 ? resp[0] : 0,
                 cmd->w_length > 1 ? resp[1] : 0);
    }

    return ESP_OK;
}

static esp_err_t run_init_sequence(void)
{
    ESP_LOGI(TAG, "Running init sequence (%d commands)...", INIT_SEQ_LEN);

    for (int i = 0; i < INIT_SEQ_LEN; i++) {
        ESP_LOGI(TAG, "Init cmd %d/%d: type=0x%02X req=0x%02X val=0x%04X idx=0x%04X len=%d",
                 i + 1, INIT_SEQ_LEN,
                 s_init_sequence[i].bm_request_type,
                 s_init_sequence[i].b_request,
                 s_init_sequence[i].w_value,
                 s_init_sequence[i].w_index,
                 s_init_sequence[i].w_length);

        esp_err_t err = send_ctrl_transfer(&s_init_sequence[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Init sequence failed at command %d", i + 1);
            return err;
        }
    }

    ESP_LOGI(TAG, "Init sequence complete");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Descriptor enumeration - find bulk IN endpoint on claimed interface
// ---------------------------------------------------------------------------

static uint8_t find_data_in_ep(usb_device_handle_t dev_hdl)
{
    const usb_config_desc_t *config_desc;
    esp_err_t err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Could not get config descriptor: %s", esp_err_to_name(err));
        return 0;
    }

    // Walk the raw descriptor bytes, log ALL endpoints for debugging.
    // Scan ALL interfaces - the Teensy driver reads from EP 0x81 which may
    // be on interface 0, not interface 1. Prefer bulk over interrupt and
    // larger MPS to avoid picking a small status endpoint (like EP 0x82/3-byte).
    const uint8_t *p = (const uint8_t *)config_desc;
    int total_len = config_desc->wTotalLength;
    int offset = 0;
    int current_iface = -1;
    uint8_t best_ep = 0;
    uint16_t best_mps = 0;
    uint8_t best_type = 0;   // 2=bulk, 3=interrupt
    uint8_t best_iface = 0;

    ESP_LOGI(TAG, "Descriptor total length: %d bytes", total_len);

    while (offset < total_len) {
        uint8_t bLength = p[offset];
        uint8_t bDescriptorType = p[offset + 1];

        if (bLength == 0) break;  // prevent infinite loop

        // Interface descriptor (type 4)
        if (bDescriptorType == 4 && bLength >= 9) {
            current_iface = p[offset + 2];  // bInterfaceNumber
            ESP_LOGI(TAG, "  Interface %d: class=%d, subclass=%d, protocol=%d, endpoints=%d",
                     current_iface, p[offset + 5], p[offset + 6], p[offset + 7], p[offset + 4]);
        }

        // Endpoint descriptor (type 5) - log ALL endpoints
        if (bDescriptorType == 5 && bLength >= 7) {
            uint8_t ep_addr = p[offset + 2];
            uint8_t ep_attr = p[offset + 3];  // bmAttributes
            uint16_t ep_mps = p[offset + 4] | (p[offset + 5] << 8);
            uint8_t ep_type = ep_attr & 0x03;

            const char *dir = (ep_addr & 0x80) ? "IN" : "OUT";
            const char *type_str = "?";
            switch (ep_type) {
                case 0: type_str = "Control"; break;
                case 1: type_str = "Isochronous"; break;
                case 2: type_str = "Bulk"; break;
                case 3: type_str = "Interrupt"; break;
            }
            ESP_LOGI(TAG, "    EP 0x%02X: %s %s, MPS=%d (iface %d)",
                     ep_addr, type_str, dir, ep_mps, current_iface);

            // Accept bulk or interrupt IN endpoints on ANY interface
            if ((ep_addr & 0x80) && (ep_type == 2 || ep_type == 3)) {
                bool better = false;
                if (best_ep == 0) {
                    better = true;
                } else if (ep_type == 2 && best_type == 3) {
                    better = true;  // bulk beats interrupt
                } else if (ep_type == best_type && ep_mps > best_mps) {
                    better = true;  // same type, bigger MPS wins
                }

                if (better) {
                    best_ep = ep_addr;
                    best_mps = ep_mps;
                    best_type = ep_type;
                    best_iface = current_iface;
                    ESP_LOGI(TAG, "  -> Best candidate: EP 0x%02X (%s IN, MPS=%d, iface %d)",
                             ep_addr, type_str, ep_mps, current_iface);
                }
            }
        }

        offset += bLength;
    }

    if (best_ep) {
        ESP_LOGI(TAG, "  => Selected EP 0x%02X (MPS=%d) on interface %d", best_ep, best_mps, best_iface);
        s_bulk_in_mps = best_mps;
        s_data_ep_iface = best_iface;
        return best_ep;
    }

    ESP_LOGW(TAG, "No bulk/interrupt IN endpoint found");
    return 0;
}

// ---------------------------------------------------------------------------
// Start bulk IN polling
// ---------------------------------------------------------------------------

static esp_err_t start_bulk_polling(void)
{
    // num_bytes must be an integer multiple of the endpoint's actual MPS.
    // Round up to cover at least DJ_STATE_SIZE (38) bytes.
    uint16_t mps = s_bulk_in_mps;
    uint16_t num_bytes = ((DJ_STATE_SIZE + mps - 1) / mps) * mps;

    s_bulk_in_xfer->device_handle = s_dev_hdl;
    s_bulk_in_xfer->bEndpointAddress = s_bulk_in_ep;
    s_bulk_in_xfer->num_bytes = num_bytes;
    s_bulk_in_xfer->callback = bulk_in_cb;
    s_bulk_in_xfer->context = NULL;

    ESP_LOGI(TAG, "Bulk IN: MPS=%d, num_bytes=%d", mps, num_bytes);

    esp_err_t err = usb_host_transfer_submit(s_bulk_in_xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to submit bulk IN: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Bulk IN polling started on EP 0x%02X", s_bulk_in_ep);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Device setup (called when our device is found)
// ---------------------------------------------------------------------------

static void setup_device(uint8_t dev_addr)
{
    esp_err_t err;

    err = usb_host_device_open(s_client_hdl, dev_addr, &s_dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
        return;
    }

    // Verify VID/PID
    const usb_device_desc_t *desc;
    usb_host_get_device_descriptor(s_dev_hdl, &desc);
    if (desc->idVendor != HERCULES_VID || desc->idProduct != HERCULES_PID) {
        ESP_LOGD(TAG, "Not our device (VID=%04X PID=%04X)", desc->idVendor, desc->idProduct);
        usb_host_device_close(s_client_hdl, s_dev_hdl);
        s_dev_hdl = NULL;
        return;
    }

    ESP_LOGI(TAG, "Hercules DJ Console MP3 e2 found! (VID=%04X PID=%04X)",
             desc->idVendor, desc->idProduct);
    status_led_set(LED_PURPLE);

    // Enumerate descriptors FIRST to find the best data IN endpoint
    // across ALL interfaces. The Teensy driver uses EP 0x81 (endpoint 1 IN)
    // which may be on interface 0, not interface 1.
    ESP_LOGI(TAG, "Enumerating USB descriptors...");
    uint8_t found_ep = find_data_in_ep(s_dev_hdl);
    if (found_ep) {
        s_bulk_in_ep = found_ep;
    } else {
        ESP_LOGW(TAG, "Using default EP 0x%02X", DATA_IN_EP_DEFAULT);
        s_bulk_in_ep = DATA_IN_EP_DEFAULT;
        s_data_ep_iface = 0;
    }

    // Claim the interface that owns the data endpoint
    ESP_LOGI(TAG, "Claiming interface %d for data EP 0x%02X", s_data_ep_iface, s_bulk_in_ep);
    err = usb_host_interface_claim(s_client_hdl, s_dev_hdl, s_data_ep_iface, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to claim interface %d: %s", s_data_ep_iface, esp_err_to_name(err));
        usb_host_device_close(s_client_hdl, s_dev_hdl);
        s_dev_hdl = NULL;
        return;
    }

    // Also claim interface 1 if the data endpoint is on a different interface
    // (interface 1 is used by the hdjd Linux driver for this device)
    if (s_data_ep_iface != IFACE_NUM) {
        err = usb_host_interface_claim(s_client_hdl, s_dev_hdl, IFACE_NUM, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Could not also claim interface %d: %s (non-fatal)", IFACE_NUM, esp_err_to_name(err));
        }
    }

    // Run vendor-specific init sequence
    err = run_init_sequence();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Init sequence failed");
        status_led_set(LED_RED);
        usb_host_interface_release(s_client_hdl, s_dev_hdl, s_data_ep_iface);
        if (s_data_ep_iface != IFACE_NUM) {
            usb_host_interface_release(s_client_hdl, s_dev_hdl, IFACE_NUM);
        }
        usb_host_device_close(s_client_hdl, s_dev_hdl);
        s_dev_hdl = NULL;
        return;
    }

    // Clear state buffers
    memset(s_current_state, 0, DJ_STATE_SIZE);
    memset(s_old_state, 0, DJ_STATE_SIZE);

    // Start bulk IN polling
    err = start_bulk_polling();
    if (err != ESP_OK) {
        status_led_set(LED_RED);
        usb_host_interface_release(s_client_hdl, s_dev_hdl, s_data_ep_iface);
        if (s_data_ep_iface != IFACE_NUM) {
            usb_host_interface_release(s_client_hdl, s_dev_hdl, IFACE_NUM);
        }
        usb_host_device_close(s_client_hdl, s_dev_hdl);
        s_dev_hdl = NULL;
        return;
    }

    s_device_connected = true;
    ESP_LOGI(TAG, "DJ Console ready! (%d controls mapped)", NUM_MAPPINGS);
}

static void teardown_device(void)
{
    ESP_LOGW(TAG, "Device disconnected");
    s_device_connected = false;

    if (s_dev_hdl) {
        usb_host_interface_release(s_client_hdl, s_dev_hdl, s_data_ep_iface);
        if (s_data_ep_iface != IFACE_NUM) {
            usb_host_interface_release(s_client_hdl, s_dev_hdl, IFACE_NUM);
        }
        usb_host_device_close(s_client_hdl, s_dev_hdl);
        s_dev_hdl = NULL;
    }

    status_led_blink(LED_YELLOW, 500);  // Waiting for device
}

// ---------------------------------------------------------------------------
// USB Host client event callback
// ---------------------------------------------------------------------------

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ESP_LOGI(TAG, "New USB device at address %d", event_msg->new_dev.address);
        // Don't call setup_device here — it blocks on control transfer semaphores,
        // but ctrl_xfer_cb is dispatched by usb_host_client_handle_events() which
        // is blocked waiting for this callback to return. Signal a separate task.
        s_pending_dev_addr = event_msg->new_dev.address;
        xTaskNotifyGive(s_setup_task_hdl);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        teardown_device();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// FreeRTOS tasks
// ---------------------------------------------------------------------------

static void device_setup_task(void *arg)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        setup_device(s_pending_dev_addr);
    }
}

static void usb_lib_task(void *arg)
{
    ESP_LOGI(TAG, "USB host library task started");
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGW(TAG, "No USB clients registered");
        }
    }
}

static void usb_client_task(void *arg)
{
    ESP_LOGI(TAG, "USB client task started");

    // Register client
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = NULL,
        },
    };

    esp_err_t err = usb_host_client_register(&client_config, &s_client_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register USB client: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Waiting for Hercules DJ Console...");
    status_led_blink(LED_YELLOW, 500);

    while (1) {
        usb_host_client_handle_events(s_client_hdl, portMAX_DELAY);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t usb_dj_host_init(dj_control_callback_t callback)
{
    s_callback = callback;
    s_ctrl_sem = xSemaphoreCreateBinary();

    // Install USB Host Library
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB host install failed: %s", esp_err_to_name(err));
        return err;
    }

    // Allocate transfer buffers (DMA-capable memory)
    err = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + EP_MPS_DEFAULT, 0, &s_ctrl_xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate control transfer");
        return err;
    }

    err = usb_host_transfer_alloc(EP_MPS_DEFAULT, 0, &s_bulk_in_xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to allocate bulk IN transfer");
        return err;
    }

    // Create tasks on core 0 (USB peripheral is on core 0)
    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(usb_client_task, "usb_client", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(device_setup_task, "dj_setup", 4096, NULL, 2, &s_setup_task_hdl, 0);

    ESP_LOGI(TAG, "USB DJ host initialized (%d controls in mapping table)", NUM_MAPPINGS);
    return ESP_OK;
}

bool usb_dj_host_is_connected(void)
{
    return s_device_connected;
}

const uint8_t *usb_dj_host_get_state(void)
{
    return s_device_connected ? s_current_state : NULL;
}

void usb_dj_host_set_raw_callback(dj_raw_state_callback_t cb)
{
    s_raw_callback = cb;
}
