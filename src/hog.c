#include <raw_hid/raw_hid.h>
#include <raw_hid/events.h>

#include <zmk/ble.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/spinlock.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info {
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code;     /* country HID Device hardware is localized for. */
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id;   /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE | HIDS_REMOTE_WAKE,
};

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_FEATURE = 0x03,
};

static struct hids_report raw_hid_report_output = {
    .id = 0x00,
    .type = HIDS_OUTPUT,
};

static struct hids_report raw_hid_report_input = {
    .id = 0x00,
    .type = HIDS_INPUT,
};

static uint8_t ctrl_point;

/* Host that wrote the last report: replies go back to it, not to whichever
 * host holds the active profile. */
static bt_addr_le_t reply_addr;
static bool reply_addr_set;
/* Written on the BT RX thread, read wherever a reply is raised (a deferred
 * reply can come from another work queue). */
static struct k_spinlock reply_lock;

static ssize_t read_hids_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_info));
}

static ssize_t read_hids_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                    void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static ssize_t read_hids_raw_hid_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                            void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, raw_hid_report_desc,
                             sizeof(raw_hid_report_desc));
}

static ssize_t write_hids_raw_hid_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                         const void *buf, uint16_t len, uint16_t offset,
                                         uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    uint8_t *data = (uint8_t *)buf;
    LOG_INF("BT - Received Raw HID report of length %i", len);
    LOG_HEXDUMP_DBG(data, len, "BT - Received Raw HID report");
    K_SPINLOCK(&reply_lock) {
        bt_addr_le_copy(&reply_addr, bt_conn_get_dst(conn));
        reply_addr_set = true;
    }
    raw_hid_set_reply_transport(RAW_HID_TRANSPORT_BLE);
    raise_raw_hid_received_event((struct raw_hid_received_event){.data = data, .length = len});

    return len;
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    return len;
}

/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(
    raw_hog_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_hids_info,
                           NULL, &info),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
                           read_hids_raw_hid_report_map, NULL, NULL),

    // send to host
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, NULL, NULL, NULL),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &raw_hid_report_input),

    // receive from host
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, NULL,
                           write_hids_raw_hid_report, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_hids_report_ref,
                       NULL, &raw_hid_report_output),

    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_ctrl_point, &ctrl_point));

void raw_hid_ble_send(const uint8_t *data, uint8_t len) {
    bt_addr_le_t addr;
    bool addr_set;

    K_SPINLOCK(&reply_lock) {
        addr_set = reply_addr_set;
        bt_addr_le_copy(&addr, &reply_addr);
    }

    struct bt_conn *conn = addr_set ? bt_conn_lookup_addr_le(BT_ID_DEFAULT, &addr)
                                    : zmk_ble_active_profile_conn();
    if (conn == NULL) {
        LOG_ERR("Raw HID host not connected");
        return;
    }

    LOG_INF("BT - Sending Raw HID report of length %i", len);
    uint8_t report[CONFIG_RAW_HID_REPORT_SIZE] = {0};
    memcpy(report, data, len);
    LOG_HEXDUMP_DBG(report, CONFIG_RAW_HID_REPORT_SIZE, "BT - Sending Raw HID report");

    struct bt_gatt_notify_params notify_params = {
        .attr = &raw_hog_svc.attrs[5],
        .data = &report,
        .len = CONFIG_RAW_HID_REPORT_SIZE,
    };

    int err = bt_gatt_notify_cb(conn, &notify_params);
    if (err == -EPERM) {
        bt_conn_set_security(conn, BT_SECURITY_L2);
    } else if (err) {
        LOG_ERR("Error notifying %d", err);
    }

    bt_conn_unref(conn);
}

K_THREAD_STACK_DEFINE(raw_hog_q_stack, CONFIG_ZMK_BLE_THREAD_STACK_SIZE);

struct k_work_q raw_hog_work_q;

static int raw_hog_init(void) {

    static const struct k_work_queue_config queue_config = {.name = "HID Over GATT Send Work"};
    k_work_queue_start(&raw_hog_work_q, raw_hog_q_stack, K_THREAD_STACK_SIZEOF(raw_hog_q_stack),
                       CONFIG_ZMK_BLE_THREAD_PRIORITY, &queue_config);

    return 0;
}

SYS_INIT(raw_hog_init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);
