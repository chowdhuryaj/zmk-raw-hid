#pragma once

#include <zephyr/sys/util.h>
#include <zmk/event_manager.h>

struct raw_hid_received_event {
    uint8_t *data;
    uint8_t length;
};

ZMK_EVENT_DECLARE(raw_hid_received_event);

struct raw_hid_sent_event {
    uint8_t *data;
    uint8_t length;
};

ZMK_EVENT_DECLARE(raw_hid_sent_event);

enum raw_hid_transport {
    RAW_HID_TRANSPORT_USB = BIT(0),
    RAW_HID_TRANSPORT_BLE = BIT(1),
    RAW_HID_TRANSPORT_ALL = RAW_HID_TRANSPORT_USB | RAW_HID_TRANSPORT_BLE,
};

/* Called by a transport before it raises raw_hid_received_event. */
void raw_hid_set_reply_transport(enum raw_hid_transport transport);

void raw_hid_usb_send(const uint8_t *data, uint8_t len);
void raw_hid_ble_send(const uint8_t *data, uint8_t len);
