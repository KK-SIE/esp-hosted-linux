// SPDX-License-Identifier: GPL-2.0-only
/*
 * Espressif Systems Wireless LAN device driver
 *
 * SPDX-FileCopyrightText: 2015-2023 Espressif Systems (Shanghai) CO LTD
 *
 */

#include "utils.h"
#include "esp_stats.h"
#include "esp_kernel_port.h"
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/ktime.h>
#include <linux/spinlock.h>
#include <linux/vmalloc.h>

#define ESP_DIAG_EVENT_CAPACITY 2048

struct esp_diag_event_record {
	u64 sequence;
	u64 time_ns;
	u32 type;
	s32 a;
	s32 b;
	s32 c;
	s32 d;
};

bool esp_diag_enabled;
module_param_named(diag_enabled, esp_diag_enabled, bool, 0644);
MODULE_PARM_DESC(diag_enabled, "Enable packet and transport diagnostic counters");

static atomic64_t esp_diag_counters[ESP_DIAG_COUNT];
static DEFINE_SPINLOCK(esp_diag_event_lock);
static struct esp_diag_event_record esp_diag_events[ESP_DIAG_EVENT_CAPACITY];
static u64 esp_diag_event_sequence;
static u32 esp_diag_event_head;
static u32 esp_diag_event_count;

static const char * const esp_diag_event_names[] = {
	"tx_input", "tx_enqueue", "tx_dequeue", "tx_credit",
	"tx_bus_start", "tx_bus_done", "tx_drop", "tx_driver_error",
	"tx_layout", "irq",
	"rx_length", "rx_cmd53_start", "rx_cmd53_done", "rx_read_request",
	"cmd_wait", "cmd_response", "cmd_timeout"
};
static const char * const esp_diag_names[ESP_DIAG_COUNT] = {
	"tx_input", "tx_input_bytes",
	"tx_len_le128", "tx_len_129_512", "tx_len_gt512",
	"tx_nonlinear", "tx_gso", "tx_checksum_partial",
	"tx_enqueue", "tx_dequeue", "tx_drop", "tx_driver_error",
	"credit_start", "credit_done", "credit_error", "credit_empty",
	"tx_bus_start", "tx_bus_done", "tx_bus_error",
	"tx_wire_bytes", "tx_unsafe_padding",
	"irq_callback", "irq_read_start", "irq_read_done",
	"irq_error", "irq_rx_notify", "irq_clear_start", "irq_clear_done",
	"rx_start", "rx_done", "rx_error",
	"rx_length_error", "rx_data_error", "rx_delta_gt_buffer", "rx_truncated",
	"rx_packet", "rx_bytes",
	"cmd_wait", "cmd_response", "cmd_timeout"
};

void esp_diag_add(enum esp_diag_counter counter, u64 value)
{
	if (esp_diag_enabled)
		atomic64_add(value, &esp_diag_counters[counter]);
}

void esp_diag_event(enum esp_diag_event_type type, s32 a, s32 b, s32 c, s32 d)
{
	struct esp_diag_event_record *event;
	unsigned long flags;

	if (!esp_diag_enabled || (unsigned int)type >= ARRAY_SIZE(esp_diag_event_names))
		return;

	spin_lock_irqsave(&esp_diag_event_lock, flags);
	event = &esp_diag_events[esp_diag_event_head];
	event->sequence = ++esp_diag_event_sequence;
	event->time_ns = ktime_get_ns();
	event->type = type;
	event->a = a;
	event->b = b;
	event->c = c;
	event->d = d;
	esp_diag_event_head = (esp_diag_event_head + 1) % ESP_DIAG_EVENT_CAPACITY;
	if (esp_diag_event_count < ESP_DIAG_EVENT_CAPACITY)
		esp_diag_event_count++;
	spin_unlock_irqrestore(&esp_diag_event_lock, flags);
}

void esp_diag_tx_skb(struct sk_buff *skb)
{
	struct ethhdr ethernet;
	u16 protocol = 0;

	if (!esp_diag_enabled)
		return;
	if (skb->len >= sizeof(ethernet) &&
	    !skb_copy_bits(skb, 0, &ethernet, sizeof(ethernet)))
		protocol = ntohs(ethernet.h_proto);
	esp_diag_add(ESP_DIAG_TX_INPUT, 1);
	esp_diag_add(ESP_DIAG_TX_BYTES, skb->len);
	esp_diag_add(skb->len <= 128 ? ESP_DIAG_TX_SMALL :
		     skb->len <= 512 ? ESP_DIAG_TX_MEDIUM : ESP_DIAG_TX_LARGE, 1);
	esp_diag_add(ESP_DIAG_TX_NONLINEAR, skb_is_nonlinear(skb));
	esp_diag_add(ESP_DIAG_TX_GSO, skb_is_gso(skb));
	esp_diag_add(ESP_DIAG_TX_PARTIAL, skb->ip_summed == CHECKSUM_PARTIAL);
	esp_diag_event(ESP_DIAG_EV_TX_INPUT, skb->len, skb_headlen(skb),
		       skb->data_len, protocol);
	pr_info_ratelimited("esp32_diag: TX_INPUT len=%u headlen=%u data_len=%u headroom=%u tailroom=%d nonlinear=%u gso=%u gso_size=%u gso_type=0x%x checksum=%u ethertype=0x%04x\n",
		skb->len, skb_headlen(skb), skb->data_len, skb_headroom(skb),
		skb_tailroom(skb), skb_is_nonlinear(skb), skb_is_gso(skb),
		skb_shinfo(skb)->gso_size, skb_shinfo(skb)->gso_type,
		skb->ip_summed, protocol);
}

int esp_diag_show(struct seq_file *output, void *unused)
{
	unsigned int counter;

	seq_printf(output, "enabled %u\njiffies %lu\n", esp_diag_enabled, jiffies);
	for (counter = 0; counter < ESP_DIAG_COUNT; counter++)
		seq_printf(output, "%s %lld\n", esp_diag_names[counter],
			(long long)atomic64_read(&esp_diag_counters[counter]));
	return 0;
}

int esp_diag_events_show(struct seq_file *output, void *unused)
{
	struct esp_diag_event_record *snapshot;
	unsigned long flags;
	u32 count, first, index;

	snapshot = vmalloc(sizeof(*snapshot) * ESP_DIAG_EVENT_CAPACITY);
	if (!snapshot)
		return -ENOMEM;

	spin_lock_irqsave(&esp_diag_event_lock, flags);
	count = esp_diag_event_count;
	first = (esp_diag_event_head + ESP_DIAG_EVENT_CAPACITY - count) %
		ESP_DIAG_EVENT_CAPACITY;
	for (index = 0; index < count; index++)
		snapshot[index] = esp_diag_events[(first + index) %
							 ESP_DIAG_EVENT_CAPACITY];
	spin_unlock_irqrestore(&esp_diag_event_lock, flags);

	seq_puts(output, "time_ns,sequence,event,a,b,c,d\n");
	for (index = 0; index < count; index++) {
		struct esp_diag_event_record *event = &snapshot[index];

		seq_printf(output, "%llu,%llu,%s,%d,%d,%d,%d\n",
			   (unsigned long long)event->time_ns,
			   (unsigned long long)event->sequence,
			   esp_diag_event_names[event->type], event->a, event->b,
			   event->c, event->d);
	}
	vfree(snapshot);
	return 0;
}

#if TEST_RAW_TP

#include "esp_api.h"
#include <linux/timer.h>
#include <linux/kthread.h>

static struct task_struct *raw_tp_tx_thread;
static int test_raw_tp;
static int test_raw_tp__host_to_esp;
static struct timer_list log_raw_tp_stats_timer;
static u8 log_raw_tp_stats_timer_running;
static unsigned long test_raw_tp_len;
static u32 raw_tp_timer_count;
static u8 traffic_open_init_done;
static struct completion traffic_open;

static void log_raw_tp_stats_timer_cb(struct timer_list *timer)
{
	unsigned long actual_bandwidth = 0;

	mod_timer(&log_raw_tp_stats_timer, jiffies + msecs_to_jiffies(1000));
	actual_bandwidth = (test_raw_tp_len*8)/1024;
	esp_info("%u-%u sec       %lu kbits/sec\n\r",
			raw_tp_timer_count,
			raw_tp_timer_count + 1, actual_bandwidth);

	raw_tp_timer_count++;
	test_raw_tp_len = 0;
}

static int raw_tp_tx_process(void *data)
{
	int ret = 0;
	struct sk_buff *tx_skb = NULL;
	struct esp_payload_header *payload_header = NULL;
	struct esp_adapter *adapter = NULL;
	struct esp_wifi_device *priv = NULL;
	struct esp_skb_cb *cb = NULL;
	u8 pad_len = 0;
	u16 total_len = 0;

	pad_len = sizeof(struct esp_payload_header);
	total_len = TEST_RAW_TP__BUF_SIZE + pad_len;
	pad_len += SKB_DATA_ADDR_ALIGNMENT - (total_len % SKB_DATA_ADDR_ALIGNMENT);

	msleep(2000);
	adapter = esp_get_adapter();
	priv = adapter->priv[0];

	while (!kthread_should_stop()) {

		if (esp_is_tx_queue_paused(priv)) {

			tx_skb = esp_alloc_skb(TEST_RAW_TP__BUF_SIZE);
			if (!tx_skb) {
				esp_info("%u esp_alloc_skb failed\n", __LINE__);
				msleep(10);
				continue;
			}
			memset(tx_skb->data, 0, TEST_RAW_TP__BUF_SIZE);
			tx_skb->len = TEST_RAW_TP__BUF_SIZE;
			cb = (struct esp_skb_cb *) tx_skb->cb;
			cb->priv = priv;

			payload_header = (struct esp_payload_header *) tx_skb->data;
			memset(payload_header, 0, pad_len);

			payload_header->if_type = ESP_TEST_IF;
			payload_header->if_num = 0;
			payload_header->len = cpu_to_le16(TEST_RAW_TP__BUF_SIZE);
			payload_header->offset = cpu_to_le16(pad_len);

			if (adapter->capabilities & ESP_CHECKSUM_ENABLED) {
				payload_header->checksum =
					cpu_to_le16(compute_checksum(tx_skb->data,
								(TEST_RAW_TP__BUF_SIZE + pad_len)));
			}
			ret = esp_send_packet(esp_get_adapter(), tx_skb);
			if (!ret)
				test_raw_tp_len += TEST_RAW_TP__BUF_SIZE;

		} else {
			if (traffic_open_init_done)
				wait_for_completion_interruptible(&traffic_open);
		}
	}
	esp_info("raw tp tx thrd stopped\n");
	return 0;
}

static void process_raw_tp_flags(void)
{
	test_raw_tp_cleanup();

	if (test_raw_tp) {

		timer_setup(&log_raw_tp_stats_timer, log_raw_tp_stats_timer_cb, 0);
		mod_timer(&log_raw_tp_stats_timer, jiffies + msecs_to_jiffies(1000));
		log_raw_tp_stats_timer_running = 1;

		if (test_raw_tp__host_to_esp) {

			raw_tp_tx_thread = kthread_run(raw_tp_tx_process, NULL, "raw tp thrd");
			if (!raw_tp_tx_thread)
				esp_err("Failed to create send traffic thread\n");

		}
		if (!traffic_open_init_done) {
			init_completion(&traffic_open);
			traffic_open_init_done = 1;
		}
	}
}


static void start_test_raw_tp(int raw_tp__host_to_esp)
{
	test_raw_tp = 1;
	test_raw_tp__host_to_esp = raw_tp__host_to_esp;
}

static void stop_test_raw_tp(void)
{
	test_raw_tp = 0;
	test_raw_tp__host_to_esp = 0;
}

void esp_raw_tp_queue_resume(void)
{
	if (traffic_open_init_done)
		if (!completion_done(&traffic_open))
			complete_all(&traffic_open);
}

void test_raw_tp_cleanup(void)
{
	int ret = 0;

	if (log_raw_tp_stats_timer_running) {
		ret = del_timer(&log_raw_tp_stats_timer);
		if (!ret) {
			log_raw_tp_stats_timer_running = 0;
		}
		raw_tp_timer_count = 0;
	}

	if (traffic_open_init_done)
		if (!completion_done(&traffic_open))
			complete_all(&traffic_open);

	if (raw_tp_tx_thread) {
		ret = kthread_stop(raw_tp_tx_thread);
		if (ret) {
			msleep(10);
			ret = kthread_stop(raw_tp_tx_thread);
		}
		if (ret)
			esp_err("Kthread stop error\n");

		raw_tp_tx_thread = 0;
	}
}

void update_test_raw_tp_rx_stats(u16 len)
{
	/* if traffic dir is esp to host, increment stats */
	if (!test_raw_tp__host_to_esp)
		test_raw_tp_len += len;
}
#endif

void process_test_capabilities(u32 raw_tp_mode)
{
#if TEST_RAW_TP
	stop_test_raw_tp();
	if (raw_tp_mode == ESP_TEST_RAW_TP_ESP_TO_HOST) {
		start_test_raw_tp(ESP_TEST_RAW_TP__RX);
		esp_info("start testing of ESP->Host raw throughput\n");
	} else if (raw_tp_mode == ESP_TEST_RAW_TP_HOST_TO_ESP) {
		start_test_raw_tp(ESP_TEST_RAW_TP__TX);
		esp_info("start testing of Host->ESP raw throughput\n");
	}
	process_raw_tp_flags();
#endif
}
