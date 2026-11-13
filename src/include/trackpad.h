/*
 * This file is part of DeskHop (https://github.com/hrvach/deskhop).
 *
 * Apple Magic Trackpad passthrough ("solution A").
 *
 * When a Magic Trackpad is plugged into either board, both boards re-enumerate
 * towards their computers as a clone of that trackpad (same VID/PID, strings,
 * HID interfaces and report descriptors), with the usual DeskHop keyboard and
 * mouse interfaces appended after the cloned ones. Raw reports from the
 * trackpad are forwarded unmodified to the active output only, and HID
 * GET_REPORT / SET_REPORT requests from the computers are relayed to the real
 * trackpad, so the OS multitouch driver talks to the genuine device.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "structs.h"

/* How many HID interfaces of the trackpad we are able to clone */
#define TP_MAX_ITF      4
/* Shared storage for all cloned HID report descriptors */
#define TP_RDESC_POOL   1536
/* Longest string descriptor we clone, in UTF-16 units including the header */
#define TP_STR_UNITS    33
/* Largest GET_REPORT / SET_REPORT payload we relay */
#define TP_CTRL_MAX     256
/* Largest interrupt packet (full speed) */
#define TP_PKT_MAX      64

#define TP_MAGIC        0x54504d32 /* "TPM2" */

typedef struct TU_ATTR_PACKED {
    uint8_t  itf_num;         /* bInterfaceNumber on the real trackpad */
    uint8_t  subclass;
    uint8_t  protocol;
    uint8_t  country;
    uint16_t bcd_hid;
    uint16_t rdesc_len;
    uint16_t rdesc_off;       /* Offset into the descriptor pool */
    uint8_t  ep_in;           /* 0 if absent */
    uint8_t  ep_in_interval;
    uint16_t ep_in_size;
    uint8_t  ep_out;          /* 0 if absent */
    uint8_t  ep_out_interval;
    uint16_t ep_out_size;
} tp_itf_t;

/* Everything needed to impersonate the trackpad. Sent over UART as-is, both
   boards run the same firmware so the layout matches. */
typedef struct {
    uint32_t magic;
    uint8_t  n_itf;
    uint8_t  cfg_attr;
    uint8_t  cfg_power;
    uint8_t  _pad;
    tusb_desc_device_t dev;
    tp_itf_t itf[TP_MAX_ITF];
    uint16_t str[3][TP_STR_UNITS]; /* Raw manufacturer / product / serial string descriptors */
    uint16_t pool_len;
    uint8_t  pool[TP_RDESC_POOL];
} tp_profile_t;

/* Number of HID instances occupied by the cloned trackpad (0 when inactive).
   DeskHop's own HID instances are shifted up by this much. */
extern volatile uint8_t tp_itf_base;

void tp_init(void);

/* ---- Device side (core 0) ---- */
bool tp_active(void);
const tp_profile_t *tp_profile(void);
void tp_device_task(device_t *state);
uint16_t tp_dev_get_report(uint8_t slot, uint8_t report_id, uint8_t report_type, uint8_t *buffer, uint16_t reqlen);
void tp_dev_set_report(uint8_t slot, uint8_t report_id, uint8_t report_type, const uint8_t *buffer, uint16_t len);

/* ---- Host side (core 1) ---- */
bool tp_host_is_trackpad(uint8_t dev_addr);
void tp_host_itf_mounted(uint8_t dev_addr, uint8_t instance);
bool tp_host_unmounted(uint8_t dev_addr);
void tp_host_input(uint8_t dev_addr, uint8_t instance, const uint8_t *report, uint16_t len);
void tp_host_task(device_t *state);

/* ---- UART handlers ---- */
void handle_tp_frame_begin_msg(uart_packet_t *packet, device_t *state);
void handle_tp_frame_data_msg(uart_packet_t *packet, device_t *state);
void handle_tp_get_req_msg(uart_packet_t *packet, device_t *state);
void handle_tp_sync_req_msg(uart_packet_t *packet, device_t *state);
