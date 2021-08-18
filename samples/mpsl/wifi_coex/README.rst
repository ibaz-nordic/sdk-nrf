.. _wifi_coex_sample:

MPSL wifi coex
#############

.. contents::
   :local:
   :depth: 2

This sample demonstrates how to use :ref:`nrfxlib:mpsl` and basic MPSL wifi coex functionality.

Overview
********

This sample application demonstrates wifi coexistance with bluetooth LE.

The application sets up an advertiser.

* When wifi coex request from BLE is denied, there will be no packet transmitted.
* When wifi coex request from BLE is granted, packets will be transmitted.
* When **Button 1** is pressed, BLE request will be denied.

Number of packets transmitted is tracked by counting RADIO->READY event.
When **Button 1** toggles, number of packets transmitted will be printed.

The application generates grant signal on pin APP_GRANT_GPIO_PIN.  This pin has to be connected
to pin COEX_GRANT_GPIO_PIN using a jumper.

* On platform nrf52840dk_nrf52840, these are pins P0.26 and P0.02.
* On platform nrf5340dk_nrf5340_cpunet, these are pins P0.06 and P0.07.

The application senses request signal on pin APP_REQ_GPIO_PIN.  This pin has to be connected
to pin COEX_REQ_GPIO_PIN using a jumper.

* On platform nrf52840dk_nrf52840, these are pins P0.28 and P0.03.
* On platform nrf5340dk_nrf5340_cpunet, these are pins P0.04 and P0.05.

Requirements
************

The sample supports any one of the following development kits:

.. table-from-rows:: /includes/sample_board_rows.txt
   :header: heading
   :rows: nrf52840dk_nrf52840, nrf5340dk_nrf5340_cpunet

Building and Running
********************

.. |sample path| replace:: :file:`samples/mpsl/wifi_coex`

Testing
=======

1. Connect pin APP_GRANT_GPIO_PIN to COEX_GRANT_GPIO_PIN on your development kit.
#. Connect pin APP_REQ_GPIO_PIN to COEX_REQ_GPIO_PIN on your development kit.
#. Build and program the development kit.
#. |connect_terminal|
#. Press **Button 1** on the device and monitor counter value on the terminal
#. Release **Button 1** on the device and monitor counter value on the terminal

Dependencies
************

This sample uses the following `sdk-nrfxlib`_ libraries:

* :ref:`nrfxlib:mpsl`
