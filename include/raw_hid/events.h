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
    /* A transport outside this module (e.g. a custom GATT characteristic):
     * it raises raw_hid_received_event itself and implements
     * raw_hid_ext_send(). */
    RAW_HID_TRANSPORT_EXT = BIT(2),
    RAW_HID_TRANSPORT_ALL = RAW_HID_TRANSPORT_USB | RAW_HID_TRANSPORT_BLE | RAW_HID_TRANSPORT_EXT,
};

/* Called by a transport before it raises raw_hid_received_event. */
void raw_hid_set_reply_transport(enum raw_hid_transport transport);

void raw_hid_usb_send(const uint8_t *data, uint8_t len);
void raw_hid_ble_send(const uint8_t *data, uint8_t len);
/* Weak no-op here; an external transport overrides it. */
void raw_hid_ext_send(const uint8_t *data, uint8_t len);
