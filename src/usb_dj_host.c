#include "usb_dj_host.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "tusb.h"

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
    // Buttons - Deck A
    { "PitchReset_A",     4, 0x80, DJ_CTRL_BUTTON },
    { "PitchBendMinus_A", 0, 0x02, DJ_CTRL_BUTTON },
    { "PitchBendPlus_A",  0, 0x04, DJ_CTRL_BUTTON },
    { "Sync_A",           4, 0x20, DJ_CTRL_BUTTON },
    { "Shift_A",          0, 0x01, DJ_CTRL_BUTTON },
    { "Shifted_A",        3, 0x10, DJ_CTRL_BUTTON },
    { "N1_A",             4, 0x40, DJ_CTRL_BUTTON },
    { "N2_A",             0, 0x10, DJ_CTRL_BUTTON },
    { "N3_A",             0, 0x20, DJ_CTRL_BUTTON },
    { "N4_A",             0, 0x40, DJ_CTRL_BUTTON },
    { "N5_A",             5, 0x01, DJ_CTRL_BUTTON },
    { "N6_A",             5, 0x02, DJ_CTRL_BUTTON },
    { "N7_A",             5, 0x04, DJ_CTRL_BUTTON },
    { "N8_A",             5, 0x08, DJ_CTRL_BUTTON },
    { "RWD_A",            0, 0x08, DJ_CTRL_BUTTON },
    { "FWD_A",            0, 0x80, DJ_CTRL_BUTTON },
    { "CUE_A",            1, 0x02, DJ_CTRL_BUTTON },
    { "Play_A",           1, 0x04, DJ_CTRL_BUTTON },
    { "Listen_A",         1, 0x01, DJ_CTRL_BUTTON },
    { "Load_A",           1, 0x08, DJ_CTRL_BUTTON },

    // Buttons - Deck B
    { "PitchReset_B",     4, 0x02, DJ_CTRL_BUTTON },
    { "PitchBendMinus_B", 3, 0x02, DJ_CTRL_BUTTON },
    { "PitchBendPlus_B",  3, 0x04, DJ_CTRL_BUTTON },
    { "Sync_B",           4, 0x08, DJ_CTRL_BUTTON },
    { "Shift_B",          3, 0x01, DJ_CTRL_BUTTON },
    { "Shifted_B",        3, 0x20, DJ_CTRL_BUTTON },
    { "N1_B",             4, 0x04, DJ_CTRL_BUTTON },
    { "N2_B",             2, 0x10, DJ_CTRL_BUTTON },
    { "N3_B",             2, 0x20, DJ_CTRL_BUTTON },
    { "N4_B",             2, 0x40, DJ_CTRL_BUTTON },
    { "N5_B",             5, 0x10, DJ_CTRL_BUTTON },
    { "N6_B",             5, 0x20, DJ_CTRL_BUTTON },
    { "N7_B",             5, 0x40, DJ_CTRL_BUTTON },
    { "N8_B",             5, 0x80, DJ_CTRL_BUTTON },
    { "RWD_B",            3, 0x08, DJ_CTRL_BUTTON },
    { "FWD_B",            2, 0x80, DJ_CTRL_BUTTON },
    { "CUE_B",            2, 0x02, DJ_CTRL_BUTTON },
    { "Play_B",           2, 0x04, DJ_CTRL_BUTTON },
    { "Listen_B",         2, 0x01, DJ_CTRL_BUTTON },
    { "Load_B",           2, 0x08, DJ_CTRL_BUTTON },

    // Global buttons
    { "Vinyl",            4, 0x10, DJ_CTRL_BUTTON },
    { "Magic",            4, 0x01, DJ_CTRL_BUTTON },
    { "Up",               1, 0x10, DJ_CTRL_BUTTON },
    { "Down",             1, 0x80, DJ_CTRL_BUTTON },
    { "Folders",          1, 0x20, DJ_CTRL_BUTTON },
    { "Files",            1, 0x40, DJ_CTRL_BUTTON },

    // Dials and sliders
    { "Treble_A",         7,  0xFF, DJ_CTRL_DIAL },
    { "Medium_A",         8,  0xFF, DJ_CTRL_DIAL },
    { "Bass_A",           9,  0xFF, DJ_CTRL_DIAL },
    { "Vol_A",            6,  0xFF, DJ_CTRL_DIAL },
    { "Treble_B",         12, 0xFF, DJ_CTRL_DIAL },
    { "Medium_B",         13, 0xFF, DJ_CTRL_DIAL },
    { "Bass_B",           14, 0xFF, DJ_CTRL_DIAL },
    { "Vol_B",            11, 0xFF, DJ_CTRL_DIAL },
    { "XFader",           10, 0xFF, DJ_CTRL_DIAL },

    // Jog wheels / rotary encoders
    { "Jog_A",            15, 0xFF, DJ_CTRL_ENCODER },
    { "Pitch_A",          17, 0xFF, DJ_CTRL_ENCODER },
    { "Jog_B",            16, 0xFF, DJ_CTRL_ENCODER },
    { "Pitch_B",          18, 0xFF, DJ_CTRL_ENCODER },
};

#define NUM_MAPPINGS (sizeof(s_mappings) / sizeof(s_mappings[0]))

// ---------------------------------------------------------------------------
// USB init sequence (vendor-specific control transfers)
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t  bm_request_type;
    uint8_t  b_request;
    uint16_t w_value;
    uint16_t w_index;
    uint16_t w_length;
} init_cmd_t;

static const init_cmd_t s_init_sequence[] = {
    { 0xC0, 0x2C, 0x0000, 0x0000, 2 },
    { 0xC0, 0x29, 0x0300, 0x0000, 2 },
    { 0xC0, 0x29, 0x0400, 0x0000, 2 },
    { 0xC0, 0x29, 0x0500, 0x0000, 2 },
    { 0xC0, 0x29, 0x0600, 0x0000, 2 },
    { 0xC0, 0x2C, 0x0000, 0x0000, 2 },
    { 0xC0, 0x2C, 0x0000, 0x0000, 2 },
    { 0xC0, 0x29, 0x0300, 0x0000, 2 },
    { 0xC0, 0x29, 0x0400, 0x0000, 2 },
    { 0xC0, 0x29, 0x0500, 0x0000, 2 },
    { 0xC0, 0x29, 0x0600, 0x0000, 2 },
    { 0xC0, 0x29, 0x0200, 0x0000, 2 },
    { 0x02, 0x01, 0x0000, 0x0082, 0 },  // CLEAR_FEATURE(ENDPOINT_HALT) on EP 0x82
    { 0x40, 0x27, 0x0000, 0x0000, 0 },  // Vendor OUT command
};

#define INIT_SEQ_LEN (sizeof(s_init_sequence) / sizeof(s_init_sequence[0]))

// ---------------------------------------------------------------------------
// Device state
// ---------------------------------------------------------------------------

static uint8_t s_current_state[DJ_STATE_SIZE];
static uint8_t s_old_state[DJ_STATE_SIZE];
static bool s_device_connected = false;
static dj_control_callback_t s_callback = NULL;

static uint8_t s_dev_addr = 0;
static uint8_t s_bulk_in_ep = 0;
static uint16_t s_bulk_in_mps = 64;

// Buffers for transfers (aligned for DMA)
TU_ATTR_ALIGNED(64) static uint8_t s_bulk_buf[64];
TU_ATTR_ALIGNED(64) static uint8_t s_ctrl_buf[64];

// Init sequence state machine
static volatile int s_init_step = -1;
static SemaphoreHandle_t s_init_sem = NULL;

// ---------------------------------------------------------------------------
// State diffing
// ---------------------------------------------------------------------------

static void process_state_update(void) {
    for (int i = 0; i < NUM_MAPPINGS; i++) {
        const control_mapping_t *m = &s_mappings[i];

        uint8_t old_val = s_old_state[m->byte_offset] & m->byte_mask;
        uint8_t new_val = s_current_state[m->byte_offset] & m->byte_mask;

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
// Bulk IN transfer callback (continuous polling)
// ---------------------------------------------------------------------------

static void bulk_in_cb(tuh_xfer_t *xfer) {
    if (xfer->result == XFER_RESULT_SUCCESS && xfer->actual_len >= DJ_STATE_SIZE) {
        memcpy(s_current_state, xfer->buffer, DJ_STATE_SIZE);
        process_state_update();
    } else if (xfer->result != XFER_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Bulk IN error: result=%d", xfer->result);
    }

    // Re-submit for continuous polling
    if (s_device_connected && tuh_mounted(s_dev_addr)) {
        tuh_xfer_t next = {
            .daddr       = s_dev_addr,
            .ep_addr     = s_bulk_in_ep,
            .buffer      = s_bulk_buf,
            .buflen      = sizeof(s_bulk_buf),
            .complete_cb = bulk_in_cb,
        };
        tuh_edpt_xfer(&next);
    }
}

// ---------------------------------------------------------------------------
// Control transfer callback for init sequence
// ---------------------------------------------------------------------------

static void ctrl_xfer_cb(tuh_xfer_t *xfer) {
    if (xfer->result != XFER_RESULT_SUCCESS) {
        ESP_LOGE(TAG, "Init cmd %d failed: result=%d", s_init_step, xfer->result);
    } else if (s_init_step >= 0 && s_init_step < INIT_SEQ_LEN) {
        const init_cmd_t *cmd = &s_init_sequence[s_init_step];
        if ((cmd->bm_request_type & 0x80) && cmd->w_length > 0) {
            ESP_LOGI(TAG, "  Init %d response: %02X %02X", s_init_step,
                     cmd->w_length > 0 ? s_ctrl_buf[0] : 0,
                     cmd->w_length > 1 ? s_ctrl_buf[1] : 0);
        }
    }
    xSemaphoreGive(s_init_sem);
}

// ---------------------------------------------------------------------------
// Init sequence using TinyUSB control transfers
// ---------------------------------------------------------------------------

static esp_err_t run_init_sequence(uint8_t daddr) {
    ESP_LOGI(TAG, "Running init sequence (%d commands)...", INIT_SEQ_LEN);

    for (int i = 0; i < INIT_SEQ_LEN; i++) {
        s_init_step = i;
        const init_cmd_t *cmd = &s_init_sequence[i];

        ESP_LOGI(TAG, "Init cmd %d/%d: type=0x%02X req=0x%02X val=0x%04X idx=0x%04X len=%d",
                 i + 1, INIT_SEQ_LEN,
                 cmd->bm_request_type, cmd->b_request,
                 cmd->w_value, cmd->w_index, cmd->w_length);

        tusb_control_request_t const request = {
            .bmRequestType_bit = {
                .recipient = cmd->bm_request_type & 0x1F,
                .type      = (cmd->bm_request_type >> 5) & 0x03,
                .direction = (cmd->bm_request_type >> 7) & 0x01,
            },
            .bRequest = cmd->b_request,
            .wValue   = cmd->w_value,
            .wIndex   = cmd->w_index,
            .wLength  = cmd->w_length,
        };

        tuh_xfer_t xfer = {
            .daddr       = daddr,
            .ep_addr     = 0,
            .setup       = &request,
            .buffer      = s_ctrl_buf,
            .complete_cb = ctrl_xfer_cb,
        };

        if (!tuh_control_xfer(&xfer)) {
            ESP_LOGE(TAG, "Init sequence failed to submit cmd %d", i + 1);
            return ESP_FAIL;
        }

        if (xSemaphoreTake(s_init_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGE(TAG, "Init sequence timeout at cmd %d", i + 1);
            return ESP_ERR_TIMEOUT;
        }
    }

    ESP_LOGI(TAG, "Init sequence complete");
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Find bulk IN endpoint from device descriptors
// ---------------------------------------------------------------------------

static bool find_bulk_in_ep(uint8_t daddr) {
    // Walk config descriptor to find best bulk/interrupt IN endpoint
    uint8_t desc_buf[256];

    tusb_control_request_t const req = {
        .bmRequestType_bit = {
            .recipient = TUSB_REQ_RCPT_DEVICE,
            .type      = TUSB_REQ_TYPE_STANDARD,
            .direction = TUSB_DIR_IN,
        },
        .bRequest = TUSB_REQ_GET_DESCRIPTOR,
        .wValue   = (TUSB_DESC_CONFIGURATION << 8) | 0,
        .wIndex   = 0,
        .wLength  = sizeof(desc_buf),
    };

    tuh_xfer_t xfer = {
        .daddr       = daddr,
        .ep_addr     = 0,
        .setup       = &req,
        .buffer      = desc_buf,
        .complete_cb = ctrl_xfer_cb,
    };

    s_init_step = -1;
    if (!tuh_control_xfer(&xfer)) return false;
    if (xSemaphoreTake(s_init_sem, pdMS_TO_TICKS(2000)) != pdTRUE) return false;

    // Parse descriptor
    uint16_t total_len = ((tusb_desc_configuration_t *)desc_buf)->wTotalLength;
    if (total_len > sizeof(desc_buf)) total_len = sizeof(desc_buf);

    uint8_t best_ep = 0;
    uint16_t best_mps = 0;
    uint8_t best_type = 0;
    int offset = 0;

    while (offset < total_len) {
        uint8_t bLength = desc_buf[offset];
        uint8_t bDescType = desc_buf[offset + 1];
        if (bLength == 0) break;

        if (bDescType == TUSB_DESC_ENDPOINT && bLength >= 7) {
            uint8_t ep_addr = desc_buf[offset + 2];
            uint8_t ep_attr = desc_buf[offset + 3];
            uint16_t ep_mps = desc_buf[offset + 4] | (desc_buf[offset + 5] << 8);
            uint8_t ep_type = ep_attr & 0x03;

            if ((ep_addr & 0x80) && (ep_type == 2 || ep_type == 3)) {
                bool better = false;
                if (best_ep == 0) better = true;
                else if (ep_type == 2 && best_type == 3) better = true;
                else if (ep_type == best_type && ep_mps > best_mps) better = true;

                if (better) {
                    best_ep = ep_addr;
                    best_mps = ep_mps;
                    best_type = ep_type;
                    ESP_LOGI(TAG, "  Candidate EP 0x%02X (%s, MPS=%d)",
                             ep_addr, ep_type == 2 ? "Bulk" : "Interrupt", ep_mps);
                }
            }
        }

        offset += bLength;
    }

    if (best_ep) {
        s_bulk_in_ep = best_ep;
        s_bulk_in_mps = best_mps;
        ESP_LOGI(TAG, "Selected EP 0x%02X (MPS=%d)", best_ep, best_mps);
        return true;
    }

    ESP_LOGW(TAG, "No bulk/interrupt IN endpoint found, using default 0x81");
    s_bulk_in_ep = 0x81;
    s_bulk_in_mps = 64;
    return true;
}

// ---------------------------------------------------------------------------
// Device setup task (runs init sequence + starts polling)
// ---------------------------------------------------------------------------

static TaskHandle_t s_setup_task_hdl = NULL;
static volatile uint8_t s_pending_addr = 0;

static void device_setup_task(void *arg) {
    (void) arg;
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint8_t daddr = s_pending_addr;
        if (daddr == 0) continue;

        // Check VID/PID
        uint16_t vid, pid;
        tuh_vid_pid_get(daddr, &vid, &pid);
        if (vid != HERCULES_VID || pid != HERCULES_PID) {
            ESP_LOGD(TAG, "Not our device (VID=%04X PID=%04X)", vid, pid);
            continue;
        }

        ESP_LOGI(TAG, "Hercules DJ Console MP3 e2 found! (VID=%04X PID=%04X)", vid, pid);

        // Find best IN endpoint
        if (!find_bulk_in_ep(daddr)) {
            ESP_LOGE(TAG, "Failed to enumerate endpoints");
            continue;
        }

        // Run vendor init sequence
        if (run_init_sequence(daddr) != ESP_OK) {
            ESP_LOGE(TAG, "Init sequence failed");
            continue;
        }

        // Open the endpoint
        tusb_desc_endpoint_t ep_desc = {
            .bLength          = sizeof(tusb_desc_endpoint_t),
            .bDescriptorType  = TUSB_DESC_ENDPOINT,
            .bEndpointAddress = s_bulk_in_ep,
            .bmAttributes     = { .xfer = TUSB_XFER_BULK },
            .wMaxPacketSize   = s_bulk_in_mps,
            .bInterval        = 0,
        };

        if (!tuh_edpt_open(daddr, &ep_desc)) {
            ESP_LOGE(TAG, "Failed to open EP 0x%02X", s_bulk_in_ep);
            continue;
        }

        // Clear state
        memset(s_current_state, 0, DJ_STATE_SIZE);
        memset(s_old_state, 0, DJ_STATE_SIZE);

        s_dev_addr = daddr;
        s_device_connected = true;

        // Start continuous bulk IN polling
        tuh_xfer_t xfer = {
            .daddr       = daddr,
            .ep_addr     = s_bulk_in_ep,
            .buffer      = s_bulk_buf,
            .buflen      = sizeof(s_bulk_buf),
            .complete_cb = bulk_in_cb,
        };
        tuh_edpt_xfer(&xfer);

        ESP_LOGI(TAG, "DJ Console ready! (%d controls mapped)", NUM_MAPPINGS);
    }
}

// ---------------------------------------------------------------------------
// TinyUSB Host callbacks (called from tuh_task context)
// ---------------------------------------------------------------------------

// Override the weak tuh_mount_cb in main.c
void tuh_mount_cb(uint8_t daddr) {
    ESP_LOGI(TAG, "Host: device %u mounted", daddr);
    s_pending_addr = daddr;
    if (s_setup_task_hdl) {
        xTaskNotifyGive(s_setup_task_hdl);
    }
}

void tuh_umount_cb(uint8_t daddr) {
    ESP_LOGW(TAG, "Host: device %u unmounted", daddr);
    if (daddr == s_dev_addr) {
        s_device_connected = false;
        s_dev_addr = 0;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t usb_dj_host_init(dj_control_callback_t callback) {
    s_callback = callback;
    s_init_sem = xSemaphoreCreateBinary();

    xTaskCreatePinnedToCore(device_setup_task, "dj_setup", 4096, NULL, 4, &s_setup_task_hdl, 1);

    ESP_LOGI(TAG, "USB DJ host initialized (%d controls in mapping table)", NUM_MAPPINGS);
    return ESP_OK;
}

bool usb_dj_host_is_connected(void) {
    return s_device_connected;
}

const uint8_t *usb_dj_host_get_state(void) {
    return s_device_connected ? s_current_state : NULL;
}
