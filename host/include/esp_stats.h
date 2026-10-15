// SPDX-License-Identifier: GPL-2.0-only
/*
 * Espressif Systems Wireless LAN device driver
 *
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 */
#ifndef __ESP_STAT__H__
#define __ESP_STAT__H__

#include "esp.h"

enum esp_diag_counter {
	ESP_DIAG_TX_INPUT, ESP_DIAG_TX_BYTES,
	ESP_DIAG_TX_SMALL, ESP_DIAG_TX_MEDIUM, ESP_DIAG_TX_LARGE,
	ESP_DIAG_TX_NONLINEAR, ESP_DIAG_TX_GSO, ESP_DIAG_TX_PARTIAL,
	ESP_DIAG_TX_ENQUEUE, ESP_DIAG_TX_DEQUEUE, ESP_DIAG_TX_DROP,
	ESP_DIAG_TX_DRIVER_ERROR,
	ESP_DIAG_CREDIT_START, ESP_DIAG_CREDIT_DONE,
	ESP_DIAG_CREDIT_ERROR, ESP_DIAG_CREDIT_EMPTY,
	ESP_DIAG_TX_START, ESP_DIAG_TX_DONE, ESP_DIAG_TX_ERROR,
	ESP_DIAG_TX_WIRE_BYTES, ESP_DIAG_TX_UNSAFE_PADDING,
	ESP_DIAG_IRQ, ESP_DIAG_IRQ_READ_START, ESP_DIAG_IRQ_READ_DONE,
	ESP_DIAG_IRQ_ERROR, ESP_DIAG_IRQ_NOTIFY,
	ESP_DIAG_IRQ_CLEAR_START, ESP_DIAG_IRQ_CLEAR_DONE,
	ESP_DIAG_RX_START, ESP_DIAG_RX_DONE, ESP_DIAG_RX_ERROR,
	ESP_DIAG_RX_LENGTH_ERROR, ESP_DIAG_RX_DATA_ERROR,
	ESP_DIAG_RX_DELTA_GT_BUFFER,
	ESP_DIAG_RX_TRUNCATED,
	ESP_DIAG_RX_PACKET, ESP_DIAG_RX_BYTES,
	ESP_DIAG_CMD_WAIT, ESP_DIAG_CMD_RESPONSE, ESP_DIAG_CMD_TIMEOUT,
	ESP_DIAG_COUNT
};

enum esp_diag_event_type {
	ESP_DIAG_EV_TX_INPUT,
	ESP_DIAG_EV_TX_ENQUEUE,
	ESP_DIAG_EV_TX_DEQUEUE,
	ESP_DIAG_EV_TX_CREDIT,
	ESP_DIAG_EV_TX_BUS_START,
	ESP_DIAG_EV_TX_BUS_DONE,
	ESP_DIAG_EV_TX_DROP,
	ESP_DIAG_EV_TX_DRIVER_ERROR,
	ESP_DIAG_EV_TX_LAYOUT,
	ESP_DIAG_EV_IRQ,
	ESP_DIAG_EV_RX_LENGTH,
	ESP_DIAG_EV_RX_CMD53_START,
	ESP_DIAG_EV_RX_CMD53_DONE,
	ESP_DIAG_EV_RX_READ_REQUEST,
	ESP_DIAG_EV_CMD_WAIT,
	ESP_DIAG_EV_CMD_RESPONSE,
	ESP_DIAG_EV_CMD_TIMEOUT,
};

extern bool esp_diag_enabled;
struct seq_file;
void esp_diag_add(enum esp_diag_counter counter, u64 value);
void esp_diag_event(enum esp_diag_event_type type, s32 a, s32 b, s32 c, s32 d);
void esp_diag_tx_skb(struct sk_buff *skb);
int esp_diag_show(struct seq_file *output, void *unused);
int esp_diag_events_show(struct seq_file *output, void *unused);

#define TEST_RAW_TP 1

#if TEST_RAW_TP

#define TEST_RAW_TP__BUF_SIZE    1460

#define ESP_TEST_RAW_TP__RX      0
#define ESP_TEST_RAW_TP__TX      1

void esp_raw_tp_queue_resume(void);
#endif

void test_raw_tp_cleanup(void);
void update_test_raw_tp_rx_stats(u16 len);

#endif
