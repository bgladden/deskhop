/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 * Copyright (c) 2025 Hrvoje Cavrak
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * See the file LICENSE for the full license text.
 */

#include "usb_descriptors.h"
#include "main.h"
#include "tusb.h"

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+

                                        // https://github.com/raspberrypi/usb-pid
tusb_desc_device_t const desc_device_config = DEVICE_DESCRIPTOR(0x2e8a, 0x107c);

                                        // https://pid.codes/1209/C000/
tusb_desc_device_t const desc_device = DEVICE_DESCRIPTOR(0x1209, 0xc000);

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
    if (global_state.config_mode_active)
        return (uint8_t const *)&desc_device_config;

    /* Passing a Magic Trackpad through: we are that trackpad now */
    if (tp_active()) {
        static tusb_desc_device_t desc_device_tp;

        desc_device_tp                    = tp_profile()->dev;
        desc_device_tp.bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE;
        desc_device_tp.iManufacturer      = 0x01;
        desc_device_tp.iProduct           = 0x02;
        desc_device_tp.iSerialNumber      = 0x03;
        desc_device_tp.bNumConfigurations = 0x01;
        return (uint8_t const *)&desc_device_tp;
    }

    return (uint8_t const *)&desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

// Relative mouse is used to overcome limitations of multiple desktops on MacOS and Windows

uint8_t const desc_hid_report[] = {TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
                                   TUD_HID_REPORT_DESC_ABS_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE)),
                                   TUD_HID_REPORT_DESC_CONSUMER_CTRL(HID_REPORT_ID(REPORT_ID_CONSUMER)),
                                   TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(REPORT_ID_SYSTEM))
                                   };

uint8_t const desc_hid_report_relmouse[] = {TUD_HID_REPORT_DESC_MOUSEHELP(HID_REPORT_ID(REPORT_ID_RELMOUSE))};

uint8_t const desc_hid_report_vendor[] = {TUD_HID_REPORT_DESC_VENDOR_CTRL(HID_REPORT_ID(REPORT_ID_VENDOR))};


// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    if (global_state.config_mode_active)
        if (instance == ITF_NUM_HID_VENDOR)
            return desc_hid_report_vendor;

    /* Cloned trackpad interfaces come first, ours follow */
    if (tp_active() && instance < tp_itf_base) {
        const tp_profile_t *tp = tp_profile();
        return &tp->pool[tp->itf[instance].rdesc_off];
    }

    switch(instance - tp_itf_base) {
        case ITF_IDX_HID:
            return desc_hid_report;
        case ITF_IDX_HID_REL_M:
            return desc_hid_report_relmouse;
        default:
            return desc_hid_report;
    }
}

bool tud_mouse_report(uint8_t mode, uint8_t buttons, int16_t x, int16_t y, int8_t wheel, int8_t pan) {
    mouse_report_t report = {.buttons = buttons, .wheel = wheel, .x = x, .y = y, .mode = mode, .pan = pan};
    uint8_t instance = ITF_NUM_HID;
    uint8_t report_id = REPORT_ID_MOUSE;

    if (mode == RELATIVE) {
        instance = ITF_NUM_HID_REL_M;
        report_id = REPORT_ID_RELMOUSE;
    }

    return tud_hid_n_report(instance, report_id, &report, sizeof(report));
}


//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const *string_desc_arr[] = {
    (const char[]){0x09, 0x04}, // 0: is supported language is English (0x0409)
    "Hrvoje Cavrak",            // 1: Manufacturer
    "DeskHop Switch",           // 2: Product
    "0",                        // 3: Serials, should use chip ID
    "DeskHop Helper",           // 4: Mouse Helper Interface
    "DeskHop Config",           // 5: Vendor Interface
    "DeskHop Disk",             // 6: Disk Interface
#ifdef DH_DEBUG
    "DeskHop Debug",            // 7: Debug Interface
#endif
};

// String Descriptor Index
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MOUSE,
    STRID_VENDOR,
    STRID_DISK,
    STRID_DEBUG,
};

static uint16_t _desc_str[32];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to
// complete
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    uint8_t chr_count;

    // 2 (hex) characters for every byte + 1 '\0' for string end
    static char serial_number[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1] = {0};

    if (!serial_number[0]) {
       pico_get_unique_board_id_string(serial_number, sizeof(serial_number));
    }

    /* Use the trackpad's own manufacturer, product and serial strings when impersonating it */
    if (tp_active() && !global_state.config_mode_active && index >= STRID_MANUFACTURER && index <= STRID_SERIAL) {
        const uint16_t *cloned = tp_profile()->str[index - STRID_MANUFACTURER];

        if ((cloned[0] & 0xff) >= 2)
            return cloned;
    }

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

        if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
            return NULL;

        const char *str = (index == STRID_SERIAL) ? serial_number : string_desc_arr[index];

        // Cap at max char
        chr_count = strlen(str);
        if (chr_count > 31)
            chr_count = 31;

        // Convert ASCII string into UTF-16
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // first byte is length (including header), second byte is string type
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);

    return _desc_str;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

#define EPNUM_HID        0x81
#define EPNUM_HID_REL_M  0x82
#define EPNUM_HID_VENDOR 0x83

#define EPNUM_MSC_OUT    0x04
#define EPNUM_MSC_IN     0x84

#ifndef DH_DEBUG

#define ITF_NUM_TOTAL 2
#define ITF_NUM_TOTAL_CONFIG 4
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)
#define CONFIG_TOTAL_LEN_CFG (TUD_CONFIG_DESC_LEN + 3 * TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN)

#else
/* CDC uses 2 interfaces (control + data). In normal mode, place it right after
   the 2 HID interfaces (at 2, 3). In config mode, place it after HID_VENDOR (2)
   and MSC (3), so at 4, 5. */
#define ITF_NUM_CDC 2
#define ITF_NUM_CDC_CONFIG 4
#define ITF_NUM_TOTAL 4
#define ITF_NUM_TOTAL_CONFIG 6

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)
#define CONFIG_TOTAL_LEN_CFG (TUD_CONFIG_DESC_LEN + 3 * TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN + TUD_CDC_DESC_LEN)

#define EPNUM_CDC_NOTIF  0x85
#define EPNUM_CDC_OUT    0x06
#define EPNUM_CDC_IN     0x86

#endif


uint8_t const desc_configuration[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_IDX_HID,
                       STRID_PRODUCT,
                       HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report),
                       EPNUM_HID,
                       DH_HID_EP_SIZE,
                       1),

    TUD_HID_DESCRIPTOR(ITF_IDX_HID_REL_M,
                       STRID_MOUSE,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report_relmouse),
                       EPNUM_HID_REL_M,
                       DH_HID_EP_SIZE,
                       1),
#ifdef DH_DEBUG
    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC, STRID_DEBUG, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
#endif
};

uint8_t const desc_configuration_config[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_CONFIG, 0, CONFIG_TOTAL_LEN_CFG, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface number, string index, protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(ITF_IDX_HID,
                       STRID_PRODUCT,
                       HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report),
                       EPNUM_HID,
                       DH_HID_EP_SIZE,
                       1),

    TUD_HID_DESCRIPTOR(ITF_IDX_HID_REL_M,
                       STRID_MOUSE,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report_relmouse),
                       EPNUM_HID_REL_M,
                       DH_HID_EP_SIZE,
                       1),

    TUD_HID_DESCRIPTOR(ITF_IDX_HID_VENDOR,
                       STRID_VENDOR,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(desc_hid_report_vendor),
                       EPNUM_HID_VENDOR,
                       DH_HID_EP_SIZE,
                       1),

    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC,
                       STRID_DISK,
                       EPNUM_MSC_OUT,
                       EPNUM_MSC_IN,
                       64),
#ifdef DH_DEBUG
    // Interface number, string index, EP notification address and size, EP data address (out, in) and size.
    TUD_CDC_DESCRIPTOR(
        ITF_NUM_CDC_CONFIG, STRID_DEBUG, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, CFG_TUD_CDC_EP_BUFSIZE),
#endif
};


//--------------------------------------------------------------------+
// Magic Trackpad passthrough: configuration built at runtime
//--------------------------------------------------------------------+

/* Find an endpoint number nobody is using yet (bit n set = endpoint n taken) */
static uint8_t tp_alloc_ep(uint16_t *used) {
    for (uint8_t ep = 1; ep < 16; ep++) {
        if (!(*used & (1u << ep))) {
            *used |= (1u << ep);
            return ep;
        }
    }
    return 0;
}

/* The trackpad's HID interfaces, exactly as it describes them (same interface
   numbers and endpoint addresses), followed by DeskHop's keyboard and mouse. */
static uint8_t const *tp_build_configuration(void) {
    static uint8_t desc[TUD_CONFIG_DESC_LEN + TP_MAX_ITF * TUD_HID_INOUT_DESC_LEN + 2 * TUD_HID_DESC_LEN + 80];
    const tp_profile_t *tp = tp_profile();
    uint16_t in_used = 0, out_used = 0;
    uint8_t *p = desc + TUD_CONFIG_DESC_LEN;
    uint8_t itf_count = 0;

    for (int i = 0; i < tp->n_itf; i++) {
        const tp_itf_t *itf = &tp->itf[i];
        uint8_t cloned[] = {
            /* Interface */
            9, TUSB_DESC_INTERFACE, itf->itf_num, 0, (uint8_t)(itf->ep_out ? 2 : 1), TUSB_CLASS_HID, itf->subclass, itf->protocol, 0,
            /* HID */
            9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(itf->bcd_hid), itf->country, 1, HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(itf->rdesc_len),
            /* Endpoint in */
            7, TUSB_DESC_ENDPOINT, itf->ep_in, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(itf->ep_in_size), itf->ep_in_interval,
            /* Endpoint out, if the trackpad has one */
            7, TUSB_DESC_ENDPOINT, itf->ep_out, TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(itf->ep_out_size), itf->ep_out_interval,
        };
        uint8_t len = itf->ep_out ? sizeof(cloned) : sizeof(cloned) - 7;

        memcpy(p, cloned, len);
        p += len;

        in_used |= 1u << (itf->ep_in & 0x0f);
        if (itf->ep_out)
            out_used |= 1u << (itf->ep_out & 0x0f);

        /* Our own interfaces are numbered after the highest one the trackpad uses */
        if (itf->itf_num >= itf_count)
            itf_count = itf->itf_num + 1;
    }

    uint8_t ep_kbd = 0x80 | tp_alloc_ep(&in_used);
    uint8_t ep_rel = 0x80 | tp_alloc_ep(&in_used);

    uint8_t own[] = {
        TUD_HID_DESCRIPTOR(itf_count + ITF_IDX_HID, STRID_PRODUCT, HID_ITF_PROTOCOL_KEYBOARD,
                           sizeof(desc_hid_report), ep_kbd, DH_HID_EP_SIZE, 1),
        TUD_HID_DESCRIPTOR(itf_count + ITF_IDX_HID_REL_M, STRID_MOUSE, HID_ITF_PROTOCOL_NONE,
                           sizeof(desc_hid_report_relmouse), ep_rel, DH_HID_EP_SIZE, 1),
    };
    memcpy(p, own, sizeof(own));
    p += sizeof(own);
    itf_count += 2;

#ifdef DH_DEBUG
    uint8_t ep_notif = 0x80 | tp_alloc_ep(&in_used);
    uint8_t ep_cdc   = tp_alloc_ep(&in_used);
    out_used |= 1u << ep_cdc;

    uint8_t cdc[] = {
        TUD_CDC_DESCRIPTOR(itf_count, STRID_DEBUG, ep_notif, 8, ep_cdc, 0x80 | ep_cdc, CFG_TUD_CDC_EP_BUFSIZE),
    };
    memcpy(p, cdc, sizeof(cdc));
    p += sizeof(cdc);
    itf_count += 2;
#endif
    (void)out_used;

    uint16_t total = (uint16_t)(p - desc);
    uint8_t header[] = {
        TUD_CONFIG_DESCRIPTOR(1, itf_count, 0, total, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    };
    memcpy(desc, header, sizeof(header));

    return desc;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index; // for multiple configurations

    if (global_state.config_mode_active)
        return desc_configuration_config;

    if (tp_active())
        return tp_build_configuration();

    return desc_configuration;
}
