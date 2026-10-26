#include <raw_hid/events.h>

#include <zephyr/sys/atomic.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

ZMK_EVENT_IMPL(raw_hid_received_event);
ZMK_EVENT_IMPL(raw_hid_sent_event);

/*
 * Transport the last report arrived on. Every send goes back there only, so
 * a host talking over BLE never has its replies copied onto USB (or the other
 * way round). Sticky rather than one-shot: a reply raised later from another
 * thread (e.g. a deferred save echo) still follows its request. Until the
 * first report arrives, sends go to every enabled transport (upstream
 * behaviour).
 */
static atomic_t reply_transport = ATOMIC_INIT(RAW_HID_TRANSPORT_ALL);

void raw_hid_set_reply_transport(enum raw_hid_transport transport) {
    atomic_set(&reply_transport, transport);
}

static int raw_hid_sent_event_listener(const zmk_event_t *eh) {
    struct raw_hid_sent_event *event = as_raw_hid_sent_event(eh);
    if (event == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    atomic_val_t transport = atomic_get(&reply_transport);

#if IS_ENABLED(CONFIG_RAW_HID_USB)
    if (transport & RAW_HID_TRANSPORT_USB) {
        raw_hid_usb_send(event->data, event->length);
    }
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (transport & RAW_HID_TRANSPORT_BLE) {
        raw_hid_ble_send(event->data, event->length);
    }
#endif

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(raw_hid_process_sent_event, raw_hid_sent_event_listener);
ZMK_SUBSCRIPTION(raw_hid_process_sent_event, raw_hid_sent_event);
