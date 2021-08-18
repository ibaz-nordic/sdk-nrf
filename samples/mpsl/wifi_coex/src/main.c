/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>

#include <stddef.h>
#include <sys/printk.h>
#include <sys/util.h>
#include <sys/byteorder.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/services/lbs.h>
#include <mpsl_coex.h>
#include <hal/nrf_radio.h>
#include <helpers/nrfx_gppi.h>
#include <nrfx_timer.h>
#include <dk_buttons_and_leds.h>
#include <hal/nrf_gpiote.h>
#include <nrfx_gpiote.h>

#if defined(CONFIG_HAS_HW_NRF_PPI)
#include <nrfx_ppi.h>
#define gppi_channel_t        nrf_ppi_channel_t
#define gppi_channel_alloc    nrfx_ppi_channel_alloc
#define gppi_channel_enable   nrfx_ppi_channel_enable
#define gppi_channel_disable  nrfx_ppi_channel_disable
#define COEX_REQ_GPIO_PIN     3  // output from coex
#define COEX_PRIO_GPIO_PIN    4
#define COEX_GRANT_GPIO_PIN   2  // input to coex
#define APP_REQ_GPIO_PIN      28 // input to app
#define APP_GRANT_GPIO_PIN    26 // output from app
#elif defined(CONFIG_HAS_HW_NRF_DPPIC)
#include <nrfx_dppi.h>
#define gppi_channel_t        uint8_t
#define gppi_channel_alloc    nrfx_dppi_channel_alloc
#define gppi_channel_enable   nrfx_dppi_channel_enable
#define gppi_channel_disable  nrfx_dppi_channel_disable
#define COEX_REQ_GPIO_PIN     4  // output from coex
#define COEX_PRIO_GPIO_PIN    27
#define COEX_GRANT_GPIO_PIN   6  // input to coex
#define APP_REQ_GPIO_PIN      5  // input to app
#define APP_GRANT_GPIO_PIN    7  // output from app
//#error "Not supported for DPPI yet"
#else
#error "No PPI or DPPI"
#endif

#define APP_COUNTER_TIMER_ID  1
#define APP_COUNTER_TIMER     NRF_TIMER1
#define COEX_TIMER            NRF_TIMER2

#define INVALID_CHANNEL       0xFF
#define USER_BUTTON           DK_BTN1_MSK
#define TYPE_DELAY_US         8
#define RADIO_DELAY_US        5

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_LBS_VAL),
};

static const nrfx_timer_t counter_timer = NRFX_TIMER_INSTANCE(APP_COUNTER_TIMER_ID);
static bool app_button_state;
uint8_t app_grant_gpiote_ch;
uint8_t app_req_assert_gpiote_ch;
gppi_channel_t app_req_assert_ppi_channel;


static gppi_channel_t allocate_ppi_channel(void)
{
	gppi_channel_t channel;

	if (gppi_channel_alloc(&channel) != NRFX_SUCCESS) {
		printk("(D)PPI channel allocation error\n");
		channel = INVALID_CHANNEL;
	}
	return channel;
}

static void counter_handler(nrf_timer_event_t event_type, void *p_context)
{
}

static int init_counter(void)
{
	/* Use timer as a counter */
	static const nrfx_timer_config_t counter_timer_cfg = {
		.mode = NRF_TIMER_MODE_COUNTER,
		.bit_width = NRF_TIMER_BIT_WIDTH_32,
		.interrupt_priority = NRFX_TIMER_DEFAULT_CONFIG_IRQ_PRIORITY,
		.p_context = NULL
	};

	if (nrfx_timer_init(&counter_timer, &counter_timer_cfg, counter_handler) != NRFX_SUCCESS) {
		printk("Failed to initialise counter timer\n");
		return -ENXIO;
	}

#if defined(CONFIG_HAS_HW_NRF_PPI)
	/* Connect NRF_RADIO->READY to counter */
	gppi_channel_t channel = allocate_ppi_channel();
	if (channel == INVALID_CHANNEL)
		return -ENXIO;

#elif defined(CONFIG_HAS_HW_NRF_DPPIC)
	// HAL_DPPI_RADIO_EVENTS_READY_CHANNEL_IDX
	gppi_channel_t channel = 4;

#endif
	nrfx_gppi_channel_endpoints_setup(channel,
		nrf_radio_event_address_get(NRF_RADIO, NRF_RADIO_EVENT_READY),
		nrf_timer_task_address_get(APP_COUNTER_TIMER, NRF_TIMER_TASK_COUNT));

	gppi_channel_enable(channel);
	return 0;
}

static int config_gpiote_output(uint32_t pin,
				nrf_gpiote_polarity_t polarity,
				nrf_gpiote_outinit_t  init_val,
				uint8_t *p_channel_gpiote)
{
	if (nrfx_gpiote_channel_alloc(p_channel_gpiote) != NRFX_SUCCESS) {
		printk("GPIOTE channel output allocation error\n");
		return -ENXIO;
	}
	nrf_gpiote_task_configure(NRF_GPIOTE, *p_channel_gpiote, pin,
					polarity, init_val);
	nrf_gpiote_task_enable(NRF_GPIOTE, *p_channel_gpiote);
	return 0;
}

static int config_gpiote_input(uint32_t pin,
				nrf_gpiote_polarity_t polarity,
				uint8_t *p_channel_gpiote)
{
	if (nrfx_gpiote_channel_alloc(p_channel_gpiote) != NRFX_SUCCESS) {
		printk("GPIOTE channel input allocation error\n");
		return -ENXIO;
	}
	nrf_gpiote_event_configure(NRF_GPIOTE, *p_channel_gpiote, pin, polarity);
	nrf_gpiote_event_enable(NRF_GPIOTE, *p_channel_gpiote);
	return 0;
}

static int init_grant(void)
{
	/* Setup grant GPIOTE task for application */
	if (config_gpiote_output(APP_GRANT_GPIO_PIN,
				NRF_GPIOTE_POLARITY_TOGGLE,
				NRF_GPIOTE_INITIAL_VALUE_HIGH,
				&app_grant_gpiote_ch))
		return -ENXIO;

	/* Setup request GPIOTE (assert) events for application */
	if (config_gpiote_input(APP_REQ_GPIO_PIN, NRF_GPIOTE_POLARITY_LOTOHI, &app_req_assert_gpiote_ch))
		return -ENXIO;

	/* Setup request GPIOTE (de-assert) events for application */
	uint8_t app_req_deassert_gpiote_ch;
	if (config_gpiote_input(APP_REQ_GPIO_PIN, NRF_GPIOTE_POLARITY_HITOLO, &app_req_deassert_gpiote_ch))
		return -ENXIO;

	/* Allocate PPI channel to request assert */
	app_req_assert_ppi_channel = allocate_ppi_channel();
	if (app_req_assert_ppi_channel == INVALID_CHANNEL)
		return -ENXIO;

	/* Connect request deassert to make grant return-to-one */
	gppi_channel_t app_req_deassert_ppi_channel = allocate_ppi_channel();
	if (app_req_deassert_ppi_channel == INVALID_CHANNEL)
		return -ENXIO;

	nrfx_gppi_channel_endpoints_setup(app_req_deassert_ppi_channel,
		nrf_gpiote_event_address_get(NRF_GPIOTE, nrf_gpiote_in_event_get(app_req_deassert_gpiote_ch)),
		nrf_gpiote_task_address_get(NRF_GPIOTE, nrf_gpiote_set_task_get(app_grant_gpiote_ch)));
	gppi_channel_enable(app_req_deassert_ppi_channel);

	return 0;
}

static void start_counter(void)
{
	nrfx_timer_clear(&counter_timer);
	nrfx_timer_enable(&counter_timer);
}

static uint32_t stop_counter(void)
{
	uint32_t radio_event_count = nrfx_timer_capture(&counter_timer, 0);
	nrfx_timer_disable(&counter_timer);
	return radio_event_count;
}

static void grant_request(void)
{
	gppi_channel_disable(app_req_assert_ppi_channel);
	nrfx_gppi_channel_endpoints_setup(app_req_assert_ppi_channel,
		nrf_gpiote_event_address_get(NRF_GPIOTE, nrf_gpiote_in_event_get(app_req_assert_gpiote_ch)),
		nrf_gpiote_task_address_get(NRF_GPIOTE, nrf_gpiote_set_task_get(app_grant_gpiote_ch)));
	gppi_channel_enable(app_req_assert_ppi_channel);
}

static void deny_request(void)
{
	gppi_channel_disable(app_req_assert_ppi_channel);
	nrfx_gppi_channel_endpoints_setup(app_req_assert_ppi_channel,
		nrf_gpiote_event_address_get(NRF_GPIOTE, nrf_gpiote_in_event_get(app_req_assert_gpiote_ch)),
		nrf_gpiote_task_address_get(NRF_GPIOTE, nrf_gpiote_clr_task_get(app_grant_gpiote_ch)));
	gppi_channel_enable(app_req_assert_ppi_channel);
}

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & USER_BUTTON) {
		uint32_t user_button_state = button_state & USER_BUTTON;
		printk("radio_event_count %d\n", stop_counter());
		start_counter();
		app_button_state = user_button_state ? true : false;

		if (app_button_state)
			deny_request();
		else
			grant_request();
	}
}

static void coex_enable_callback(void)
{
}

static int allocate_coex_gpiote_cfg(mpsl_coex_gpiote_cfg_t *cfg)
{
	uint8_t channel_gpiote;
	if (nrfx_gpiote_channel_alloc(&channel_gpiote) != NRFX_SUCCESS) {
		printk("GPIOTE channel allocation error\n");
		return -ENXIO;
	}
	cfg->gpiote_ch_id = channel_gpiote;
	return 0;
}

static int allocate_coex_pin_cfg(mpsl_coex_gpiote_cfg_t *cfg)
{
	gppi_channel_t ppi_channel = allocate_ppi_channel();
	if (ppi_channel == INVALID_CHANNEL)
		return -ENXIO;
	cfg->ppi_ch_id = ppi_channel;

	if (allocate_coex_gpiote_cfg(cfg))
		return -ENXIO;

	cfg->active_high = true;
	return 0;
}

#if defined(CONFIG_HAS_HW_NRF_PPI)
static int coex_enable(void)
{
	mpsl_coex_if_t interface_cfg;
	mpsl_coex_gpiote_cfg_t *cfg;

	interface_cfg.if_id = MPSL_COEX_802152_3WIRE_GPIOTE_ID;

	cfg = &interface_cfg.interfaces.coex_3wire_gpiote.request_cfg;
	if (allocate_coex_pin_cfg(cfg))
		return -ENXIO;
	cfg->gpio_pin = COEX_REQ_GPIO_PIN;

	cfg = &interface_cfg.interfaces.coex_3wire_gpiote.priority_cfg;
	if (allocate_coex_pin_cfg(cfg))
		return -ENXIO;
	cfg->gpio_pin = COEX_PRIO_GPIO_PIN;

	cfg = &interface_cfg.interfaces.coex_3wire_gpiote.grant_cfg;
	if (allocate_coex_pin_cfg(cfg))
		return -ENXIO;
	cfg->gpio_pin = COEX_GRANT_GPIO_PIN;

	gppi_channel_t ppi_channel = allocate_ppi_channel();
	if (ppi_channel == INVALID_CHANNEL)
		return -ENXIO;

	interface_cfg.interfaces.coex_3wire_gpiote.radio_ppi_ch_id.radio_events_disabled = ppi_channel;
	interface_cfg.interfaces.coex_3wire_gpiote.radio_ppi_ch_id.radio_events_end = ppi_channel;
	interface_cfg.interfaces.coex_3wire_gpiote.radio_ppi_ch_id.radio_events_address = ppi_channel;
	interface_cfg.interfaces.coex_3wire_gpiote.is_rx_active_level = 1;
	interface_cfg.interfaces.coex_3wire_gpiote.type_delay_us = TYPE_DELAY_US;
	interface_cfg.interfaces.coex_3wire_gpiote.radio_delay_us = RADIO_DELAY_US;
	interface_cfg.interfaces.coex_3wire_gpiote.p_timer_instance = COEX_TIMER;

	mpsl_coex_support_802152_3wire_gpiote_if();

	return (int)mpsl_coex_enable(&interface_cfg, coex_enable_callback);
}
#elif defined(CONFIG_HAS_HW_NRF_DPPIC)
static int coex_enable(void)
{
	mpsl_coex_if_t interface_cfg;
	mpsl_coex_gpiote_cfg_t *cfg;

	interface_cfg.if_id = MPSL_COEX_802152_3WIRE_GPIOTE_ID;

	cfg = &interface_cfg.interfaces.coex_3wire_gpiote.request_cfg;
	if (allocate_coex_pin_cfg(cfg))
		return -ENXIO;
	cfg->gpio_pin = COEX_REQ_GPIO_PIN;

	cfg = &interface_cfg.interfaces.coex_3wire_gpiote.priority_cfg;
	if (allocate_coex_pin_cfg(cfg))
		return -ENXIO;
	cfg->gpio_pin = COEX_PRIO_GPIO_PIN;

	cfg = &interface_cfg.interfaces.coex_3wire_gpiote.grant_cfg;
	if (allocate_coex_gpiote_cfg(cfg))
		return -ENXIO;
	cfg->active_high = true;
	cfg->gpio_pin = COEX_GRANT_GPIO_PIN;

	// HAL_DPPI_RADIO_TASKS_DISABLED_CHANNEL_IDX
	cfg->ppi_ch_id = 11;

	// HAL_DPPI_RADIO_EVENTS_DISABLED_CH_IDX
	interface_cfg.interfaces.coex_3wire_gpiote.radio_ppi_ch_id.radio_events_disabled = 7;
	// HAL_DPPI_RADIO_EVENTS_END_CHANNEL_IDX
	interface_cfg.interfaces.coex_3wire_gpiote.radio_ppi_ch_id.radio_events_end = 6;
	// HAL_DPPI_RADIO_EVENTS_ADDRESS_CHANNEL_IDX
	interface_cfg.interfaces.coex_3wire_gpiote.radio_ppi_ch_id.radio_events_address = 5;
	interface_cfg.interfaces.coex_3wire_gpiote.is_rx_active_level = 1;
	interface_cfg.interfaces.coex_3wire_gpiote.type_delay_us = TYPE_DELAY_US;
	interface_cfg.interfaces.coex_3wire_gpiote.radio_delay_us = RADIO_DELAY_US;
	interface_cfg.interfaces.coex_3wire_gpiote.p_timer_instance = COEX_TIMER;

	mpsl_coex_support_802152_3wire_gpiote_if();

	return (int)mpsl_coex_enable(&interface_cfg, coex_enable_callback);
}
#endif


void main(void)
{
	printk("Starting Wifi Coex Demo on board %s\n", CONFIG_BOARD);

	if (bt_enable(NULL)) {
		printk("Bluetooth init failed\n");
	}
	printk("Bluetooth initialized\n");

	if (init_counter()) {
		printk("Failed to initialise counter\n");
		return;
	}
	start_counter();

	if (dk_buttons_init(button_changed)) {
		printk("Failed to initialise button\n");
		return;
	}

	if (coex_enable()) {
		printk("Failed to enable coex\n");
		return;
	}

	if (init_grant()) {
		printk("Failed to initialise grant pin\n");
		return;
	}
	grant_request();

	if (bt_le_adv_start(BT_LE_ADV_NCONN_NAME, ad, ARRAY_SIZE(ad), NULL, 0)) {
		printk("Advertising failed to start\n");
		return;
	}
	printk("Advertising started\n");

	while (1) {
		k_sleep(K_SECONDS(2));
	}
}
