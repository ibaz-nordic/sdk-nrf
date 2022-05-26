/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>

static struct bt_le_ext_adv *adv_set;

const static struct bt_le_adv_param param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_EXT_ADV |
				     BT_LE_ADV_OPT_USE_NAME,
				     BT_GAP_ADV_FAST_INT_MIN_2,
				     BT_GAP_ADV_FAST_INT_MAX_2,
				     NULL);

const static struct bt_le_per_adv_param per_adv_param = {
	.interval_min = BT_GAP_ADV_FAST_INT_MIN_2, //BT_GAP_ADV_SLOW_INT_MIN,
	.interval_max = BT_GAP_ADV_FAST_INT_MAX_2, //BT_GAP_ADV_SLOW_INT_MAX,
	.options = BT_LE_ADV_OPT_USE_TX_POWER,
};

void main(void)
{
	char addr_s[BT_ADDR_LE_STR_LEN];
	struct bt_le_oob oob_local;
	int err;

	printk("Starting PAST Beacon Demo\n");

	/* Initialize the Bluetooth Subsystem */
	printk("Bluetooth initialization...");
	err = bt_enable(NULL);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	printk("success\n");

	printk("Advertising set create...");
	err = bt_le_ext_adv_create(&param, NULL, &adv_set);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	printk("success\n");

	printk("Periodic advertising params set...");
	err = bt_le_per_adv_set_param(adv_set, &per_adv_param);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	printk("success\n");

	printk("Periodic advertising enable...");
	err = bt_le_per_adv_start(adv_set);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	printk("success\n");

	printk("Extended advertising enable...");
	err = bt_le_ext_adv_start(adv_set, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	printk("success\n");

	bt_le_ext_adv_oob_get_local(adv_set, &oob_local);
	bt_addr_le_to_str(&oob_local.addr, addr_s, sizeof(addr_s));

	printk("Started extended advertising as %s\n", addr_s);
}

