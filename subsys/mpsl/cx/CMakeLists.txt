#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_MPSL_CX mpsl_cx.c)
zephyr_library_sources_ifdef(CONFIG_MPSL_CX_THREAD thread/mpsl_cx_thread.c)
zephyr_library_sources_ifdef(CONFIG_MPSL_CX_BT_1WIRE bluetooth/mpsl_cx_1w_bluetooth.c)
zephyr_library_sources_ifdef(CONFIG_MPSL_CX_BT_3WIRE bluetooth/mpsl_cx_3w_bluetooth.c)
