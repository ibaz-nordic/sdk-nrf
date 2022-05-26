/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <sdc_hci_cmd_le.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define PEER_NAME_LEN_MAX 30
/* BT Core 5.3 specification allows controller to wait 6 periodic advertising events for
 * synchronization establishment, hence timeout must be longer than that.
 */
#define SYNC_TIMEOUT_INTERVAL_NUM 10
/* Maximum length of advertising data represented in hexadecimal format */
#define ADV_DATA_HEX_STR_LEN_MAX (BT_GAP_ADV_MAX_EXT_ADV_DATA_LEN * 2 + 1)

static struct bt_le_per_adv_sync *sync;
static bt_addr_le_t per_addr;
static volatile bool per_adv_found;
static volatile bool peripheral_connected;
static bool scan_enabled;
static uint8_t per_sid;
static uint32_t sync_timeout_ms;
static struct bt_conn *default_conn;

static K_SEM_DEFINE(sem_peripheral, 0, 1);
static K_SEM_DEFINE(sem_per_adv, 0, 1);
static K_SEM_DEFINE(sem_per_sync, 0, 1);
static K_SEM_DEFINE(sem_per_sync_lost, 0, 1);

static void scan_disable(void);


static inline uint32_t adv_interval_to_ms(uint16_t interval)
{
	return interval * 5 / 4;
}

static bool data_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, PEER_NAME_LEN_MAX - 1);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	default:
		return true;
	}
}

static void sync_cb(struct bt_le_per_adv_sync *sync,
		    struct bt_le_per_adv_sync_synced_info *info)
{
	(void) sync;
	(void) info;

	k_sem_give(&sem_per_sync);
}

static void term_cb(struct bt_le_per_adv_sync *sync,
		    const struct bt_le_per_adv_sync_term_info *info)
{
	(void) sync;
	(void) info;

	k_sem_give(&sem_per_sync_lost);
}

static void recv_cb(struct bt_le_per_adv_sync *sync,
		    const struct bt_le_per_adv_sync_recv_info *info,
		    struct net_buf_simple *buf)
{
	(void) buf;

	printk("PER_ADV_SYNC[%u]: tx_power %i,  RSSI %i\n",
	       bt_le_per_adv_sync_get_index(sync), info->tx_power,
	       info->rssi);
}

static void scan_recv(const struct bt_le_scan_recv_info *info,
		      struct net_buf_simple *buf)
{
	if (!peripheral_connected) {
		char peripheral[] = "Nordic PAST peripheral";
		char le_addr[BT_ADDR_LE_STR_LEN];
		char name[PEER_NAME_LEN_MAX];

		(void)memset(name, 0, sizeof(name));
		bt_data_parse(buf, data_cb, name);
		bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

		if (!memcmp(name, peripheral, sizeof(peripheral))) {

			scan_disable();

			struct bt_le_conn_param *param = BT_LE_CONN_PARAM_DEFAULT;
			int err = bt_conn_le_create(info->addr, BT_CONN_LE_CREATE_CONN,
							param, &default_conn);
			if (err) {
				printk("Create conn failed (err %d)\n", err);
			}
			printk("[DEVICE]: %s, %s\n", le_addr, name);
		}
	} else {
		if (!per_adv_found) {
			char beacon[] = "Nordic PAST beacon";
			char le_addr[BT_ADDR_LE_STR_LEN];
			char name[PEER_NAME_LEN_MAX];

			(void)memset(name, 0, sizeof(name));
			bt_data_parse(buf, data_cb, name);
			bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));

			if (!memcmp(name, beacon, sizeof(beacon))) {
				printk("[DEVICE]: %s, %s\n", le_addr, name);
				sync_timeout_ms =adv_interval_to_ms(info->interval);;
				per_adv_found = true;
				per_sid = info->sid;
				bt_addr_le_copy(&per_addr, info->addr);
				k_sem_give(&sem_per_adv);
			}
		}
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		printk("Failed to connect to %s (%u)\n", addr, conn_err);

		bt_conn_unref(default_conn);
		default_conn = NULL;
		return;
	}
	printk("Connected: %s\n", addr);
	peripheral_connected = true;
	k_sem_give(&sem_peripheral);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	peripheral_connected = false;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s (reason 0x%02x)\n", addr, reason);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;
}

static struct bt_le_scan_cb scan_callbacks = {
	.recv = scan_recv,
};

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static struct bt_le_per_adv_sync_cb sync_callbacks = {
	.synced = sync_cb,
	.term = term_cb,
	.recv = recv_cb,
};

static void create_sync(void)
{
	struct bt_le_per_adv_sync_param sync_create_param;
	int err;

	printk("Creating Periodic Advertising Sync...\n");
	bt_addr_le_copy(&sync_create_param.addr, &per_addr);
	sync_create_param.options = 0;
	sync_create_param.sid = per_sid;
	sync_create_param.skip = 0;
	sync_create_param.timeout = sync_timeout_ms;
	err = bt_le_per_adv_sync_create(&sync_create_param, &sync);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
}

static int delete_sync(void)
{
	int err;

	printk("Deleting Periodic Advertising Sync...\n");
	err = bt_le_per_adv_sync_delete(sync);
	if (err) {
		printk("failed (err %d)\n", err);
		return err;
	}
	return 0;
}

static int scan_init(void)
{
	printk("Connection callbacks register\n");
	bt_conn_cb_register(&conn_callbacks);

	printk("Scan callbacks register...\n");
	bt_le_scan_cb_register(&scan_callbacks);

	printk("Periodic Advertising callbacks register...\n");
	bt_le_per_adv_sync_cb_register(&sync_callbacks);

	return 0;
}

static int scan_enable(void)
{
	struct bt_le_scan_param param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
		.timeout = 0U,
	};

	int err;

	if (!scan_enabled) {
		printk("Start scanning...\n");
		err = bt_le_scan_start(&param, NULL);
		if (err) {
			printk("failed (err %d)\n", err);
			return err;
		}
		scan_enabled = true;
	}

	return 0;
}

static void scan_disable(void)
{
	int err;

	printk("Scan disable...\n");
	err = bt_le_scan_stop();
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	scan_enabled = false;
}

static void send_past(void)
{
	int err = 0;
	err = bt_le_per_adv_sync_transfer(sync, default_conn, 1);

	if (err) {
		printk("failed (err %d)\n", err);
	}
}

void main(void)
{
	int err;

	printk("Starting PAST Central Demo\n");

	printk("Bluetooth initialization...\n");
	err = bt_enable(NULL);
	if (err) {
		printk("failed (err %d)\n", err);
	}

	peripheral_connected = false;
	scan_init();

	while (true) {
		per_adv_found = false;
		scan_enable();

		printk("Looking for peripheral...\n");
		err = k_sem_take(&sem_peripheral, K_FOREVER);
		if (err) {
			printk("failed (err %d)\n", err);
			return;
		}
		scan_enable();

		printk("Waiting for periodic advertising...\n");
		err = k_sem_take(&sem_per_adv, K_FOREVER);
		if (err) {
			printk("failed (err %d)\n", err);
			return;
		}
		create_sync();

		printk("Waiting for periodic sync...\n");
		err = k_sem_take(&sem_per_sync, K_MSEC(sync_timeout_ms * SYNC_TIMEOUT_INTERVAL_NUM));
		if (err) {
			printk("failed (err %d)\n", err);
			err = delete_sync();
			if (err) {
				return;
			}
			continue;
		}
		printk("Periodic sync established.\n");

		printk("Transferring sync...\n");
 		send_past();

		scan_disable();

		printk("Waiting for periodic sync lost...\n");
		err = k_sem_take(&sem_per_sync_lost, K_FOREVER);
		if (err) {
			printk("failed (err %d)\n", err);
			return;
		}
		printk("Periodic sync lost.\n");
	}
}
