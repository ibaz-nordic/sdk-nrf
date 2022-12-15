/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/__assert.h>
#include "sdc_hci_vs.h"

static struct bt_le_ext_adv *adv_set;

const static struct bt_le_adv_param param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_EXT_ADV |
				     BT_LE_ADV_OPT_USE_NAME,
				     BT_GAP_ADV_FAST_INT_MIN_2,
				     BT_GAP_ADV_FAST_INT_MAX_2,
				     NULL);

static struct bt_le_ext_adv_start_param ext_adv_start_param = {
	.timeout = 0,
	.num_events = 1,
};

static void adv_sent(struct bt_le_ext_adv *instance,
		     struct bt_le_ext_adv_sent_info *info)
{
	printk("Complete adv...\n");
}

static const struct bt_le_ext_adv_cb adv_cb = {
	.sent = adv_sent,
};

static int set_adv_randomness(uint8_t handle, int rand_us)
{
	struct net_buf *buf;
	sdc_hci_cmd_vs_set_adv_randomness_t *cmd_params;

	buf = bt_hci_cmd_create(SDC_HCI_OPCODE_CMD_VS_SET_ADV_RANDOMNESS,
				sizeof(*cmd_params));
	if (!buf) {
		printk("Could not allocate command buffer\n");
		return -ENOMEM;
	}

	cmd_params = net_buf_add(buf, sizeof(*cmd_params));
	cmd_params->adv_handle = handle;
	cmd_params->rand_us = rand_us;

	return bt_hci_cmd_send_sync(SDC_HCI_OPCODE_CMD_VS_SET_ADV_RANDOMNESS,
				    buf, NULL);
}

void main(void)
{
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
	err = bt_le_ext_adv_create(&param, &adv_cb, &adv_set);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
	printk("success\n");

	set_adv_randomness(0xFF, 0);
	set_adv_randomness(0, 0);

	printk("Extended advertising enable...\n");
	err = bt_le_ext_adv_start(adv_set, &ext_adv_start_param);
	if (err) {
		printk("failed (err %d)\n", err);
		return;
	}
}

