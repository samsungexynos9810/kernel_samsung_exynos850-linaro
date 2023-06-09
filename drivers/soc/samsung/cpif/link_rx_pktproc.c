/*
 * Copyright (C) 2018-2019, Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/cacheflush.h>
#include <linux/shm_ipc.h>
#include "modem_prj.h"
#include "modem_utils.h"
#include "link_device_memory.h"

#ifdef CONFIG_CP_PKTPROC_PERF_TEST
struct pktproc_perftest_data perftest_data[PERFTEST_MODE_MAX] = {
	{
		/* empty */
	},
	{
		/* PERFTEST_MODE_IPV4
		 * port: 5000 -> 5001
		 * payload: 1464 (0x5b8)
		 */
		.header = {
			0x45, 0x00, 0x05, 0xB8, 0x00, 0x00, 0x40, 0x00,
			0x80, 0x11, 0x71, 0xDF, 0xC0, 0xA8, 0x01, 0x03,
			0xC0, 0xA8, 0x01, 0x02, 0x13, 0x88, 0x13, 0x89,
			0x05, 0xA4, 0x00, 0x00
		},
		.header_len = 28,
		.dst_port_offset = 22,
		.packet_len = 1464
	},
	{
		/* PERFTEST_MODE_CLAT
		 * port: 5000 -> 5001
		 * payload: 1444 (0x5a4)
		 */
		.header = {
			0x60, 0x0a, 0xf8, 0x0c, 0x05, 0xa4, 0x11, 0x40,
			0x00, 0x64, 0xff, 0x9b, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x02,
			0x20, 0x01, 0x02, 0xd8, 0xe1, 0x43, 0x7b, 0xfb,
			0x1d, 0xda, 0x90, 0x9d, 0x8b, 0x8d, 0x05, 0xe7,
			0x13, 0x88, 0x13, 0x89, 0x05, 0xa4, 0x00, 0x00,
		},
		.header_len = 48,
		.dst_port_offset = 42,
		.packet_len = 1484
	}
};
#endif

static bool pktproc_check_hw_checksum(u8 status)
{
	if (unlikely(status & PKTPROC_STATUS_IGNR))
		return false;
	if (unlikely(!(status & PKTPROC_STATUS_IPCS) || !(status & PKTPROC_STATUS_TCPC)))
		return false;
	if (unlikely((status & PKTPROC_STATUS_IPCSF) || (status & PKTPROC_STATUS_TCPCF)))
		return false;

	return true;
}

/*
 * Get a packet: ringbuf mode
 */
static int pktproc_get_pkt_from_ringbuf_mode(struct pktproc_queue *q, struct sk_buff **new_skb)
{
	int ret = 0;
	u16 len;
	u16 ch_id;
	u8 *src;
	struct sk_buff *skb = NULL;
	struct link_device *ld = &q->mld->link_dev;
	struct pktproc_desc_ringbuf *desc = q->desc_ringbuf;

	if (!pktproc_check_active(q->ppa, q->q_idx)) {
		mif_err_limited("Queue %d not activated\n", q->q_idx);
		return -EACCES;
	}
	if (q->ppa->desc_mode != DESC_MODE_RINGBUF) {
		mif_err_limited("Invalid desc_mode %d\n", q->ppa->desc_mode);
		return -EINVAL;
	}

	/* Get data */
	len = desc[*q->rear_ptr].length;
	if (len > PKTPROC_MAX_PACKET_SIZE) {
		mif_err_limited("Length is invalid:%d\n", len);
		q->stat.err_len++;
		ret = -EPERM;
		goto rx_error_on_desc;
	}
	ch_id = desc[*q->rear_ptr].channel_id;
	if (ch_id == SIPC5_CH_ID_MAX) {
		mif_err_limited("Channel ID is invalid:%d\n", ch_id);
		q->stat.err_chid++;
		ret = -EPERM;
		goto rx_error_on_desc;
	}
	src = desc[*q->rear_ptr].data_addr - q->q_info->data_base + q->data;
	if ((src < q->data) || (src > q->data + q->data_size)) {
		mif_err_limited("Data address is invalid:%pK data:%pK size:0x%08x\n", src, q->data, q->data_size);
		q->stat.err_addr++;
		ret = -EINVAL;
		goto rx_error_on_desc;
	}
	if (!q->ppa->use_hw_iocc)
		__inval_dcache_area(src, len);
	pp_debug("len:%d ch_id:%d src:%pK\n", len, ch_id, src);

	/* Build skb */
	skb = napi_alloc_skb(&q->napi, len);
	if (unlikely(!skb)) {
		mif_err_limited("alloc_skb() error\n");
		q->stat.err_nomem++;
		ret = -ENOMEM;
		goto rx_error;
	}
	skb_put(skb, len);
	skb_copy_to_linear_data(skb, src, len);
#ifdef PKTPROC_DEBUG_PKT
	pr_buffer("pktproc", (char *)skb->data, (size_t)len, (size_t)40);
#endif

	/* Set priv */
	skbpriv(skb)->lnk_hdr = 0;
	skbpriv(skb)->sipc_ch = ch_id;
	skbpriv(skb)->iod = link_get_iod_with_channel(ld, skbpriv(skb)->sipc_ch);
	skbpriv(skb)->ld = ld;
	switch (q->ppa->version) {
	case PKTPROC_V2:
		if (pktproc_check_hw_checksum(desc[*q->rear_ptr].status))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			q->stat.err_csum++;
		break;
	default:
		break;
	}

	*new_skb = skb;
	q->stat.pass_cnt++;
	*q->rear_ptr = circ_new_ptr(q->q_info->num_desc, *q->rear_ptr, 1);

	return 0;

rx_error_on_desc:
	mif_err_limited("Skip invalid descriptor at %d\n", *q->rear_ptr);
	*q->rear_ptr = circ_new_ptr(q->q_info->num_desc, *q->rear_ptr, 1);

rx_error:
	if (skb)
		dev_kfree_skb_any(skb);

	return ret;
}

/*
 * Get a packet : sktbuf mode on 32bit region
 */
static int pktproc_clear_data_addr(struct pktproc_queue *q)
{
	struct pktproc_desc_sktbuf *desc = q->desc_sktbuf;
	u8 *src;

	if (q->ppa->desc_mode != DESC_MODE_SKTBUF)
		return 0;

	pp_debug("Clean buffer manager from %d to %d\n", q->done_ptr, *q->fore_ptr);
	while (*q->fore_ptr != q->done_ptr) {
		src = desc[q->done_ptr].data_addr - q->q_info->data_base + q->data - (NET_SKB_PAD + NET_IP_ALIGN);

		free_mif_buff(q->manager, src);
		q->done_ptr = circ_new_ptr(q->q_info->num_desc, q->done_ptr, 1);
	}
	memset(desc, 0, q->desc_size);

	return 0;
}

static int pktproc_fill_data_addr(struct pktproc_queue *q)
{
	struct pktproc_desc_sktbuf *desc = q->desc_sktbuf;
	u8 *addr;
	u32 space;
	int i;
	unsigned long flags;

	if (q->ppa->desc_mode != DESC_MODE_SKTBUF)
		return 0;

	spin_lock_irqsave(&q->lock, flags);

	space = circ_get_space(q->q_info->num_desc, *q->fore_ptr, q->done_ptr);
	pp_debug("Space:%d Q%d:%d/%d/%d BM:%d/%d/%d\n",
		space, q->q_idx, *q->fore_ptr, *q->rear_ptr, q->done_ptr,
		q->manager->cell_count, q->manager->used_cell_count, q->manager->free_cell_count);

	for (i = 0; i < space; i++) {
		addr = alloc_mif_buff(q->manager);
		if (!addr) {
			mif_err_limited("alloc_mif_buff() error for space:%d Q%d:%d/%d/%d BM:%d/%d/%d\n",
				space, q->q_idx, *q->fore_ptr, *q->rear_ptr, q->done_ptr,
				q->manager->cell_count, q->manager->used_cell_count, q->manager->free_cell_count);

			q->stat.err_bm_nomem++;
			spin_unlock_irqrestore(&q->lock, flags);
			return -ENOMEM;
		}
		if (!q->ppa->use_hw_iocc)
			__inval_dcache_area(addr, MIF_BUFF_DEFAULT_CELL_SIZE);

		desc[*q->fore_ptr].data_addr = (u32)(addr - q->data) +
						q->q_info->data_base +
						(NET_SKB_PAD + NET_IP_ALIGN);
		desc[*q->fore_ptr].data_addr_msb = 0;

		if (*q->fore_ptr == 0)
			desc[*q->fore_ptr].control |= (1 << 7);	/* HEAD */

		if (*q->fore_ptr == (q->q_info->num_desc - 1))
			desc[*q->fore_ptr].control |= (1 << 3);	/* RINGEND */

		*q->fore_ptr = circ_new_ptr(q->q_info->num_desc, *q->fore_ptr, 1);
	}
	pp_debug("fore/rear/done:%d/%d/%d\n", *q->fore_ptr, *q->rear_ptr, q->done_ptr);

	spin_unlock_irqrestore(&q->lock, flags);

	return 0;
}

static int pktproc_get_pkt_from_sktbuf_mode(struct pktproc_queue *q, struct sk_buff **new_skb)
{
	int ret = 0;
	u16 len, tmp_len;
	u16 ch_id;
	u8 *src;
	struct sk_buff *skb = NULL;
	struct pktproc_desc_sktbuf *desc = q->desc_sktbuf;
	struct link_device *ld = &q->mld->link_dev;

	if (!pktproc_check_active(q->ppa, q->q_idx)) {
		mif_err_limited("Queue %d not activated\n", q->q_idx);
		return -EACCES;
	}
	if (q->ppa->desc_mode != DESC_MODE_SKTBUF) {
		mif_err_limited("Invalid desc_mode %d\n", q->ppa->desc_mode);
		return -EINVAL;
	}

	/* Get data */
	len = desc[q->done_ptr].length;
	if (len > PKTPROC_MAX_PACKET_SIZE) {
		mif_err_limited("Length is invalid:%d\n", len);
		q->stat.err_len++;
		ret = -EPERM;
		goto rx_error_on_desc;
	}

	ch_id = desc[q->done_ptr].channel_id;
	if (ch_id == SIPC5_CH_ID_MAX) {
		mif_err_limited("Channel ID is invalid:%d\n", ch_id);
		q->stat.err_chid++;
		ret = -EPERM;
		goto rx_error_on_desc;
	}

	src = desc[q->done_ptr].data_addr - q->q_info->data_base + q->data - (NET_SKB_PAD + NET_IP_ALIGN);
	if ((src < q->data) || (src > q->data + q->data_size)) {
		mif_err_limited("Data address is invalid:%pK data:%pK size:0x%08x\n", src, q->data, q->data_size);
		q->stat.err_addr++;
		ret = -EINVAL;
		goto rx_error_on_desc;
	}
	if (!q->ppa->use_hw_iocc)
		__inval_dcache_area(src, MIF_BUFF_DEFAULT_CELL_SIZE);
	pp_debug("len:%d ch_id:%d src:%pK\n", len, ch_id, src);

	/* Build skb */
	if (q->use_memcpy) {
		skb = napi_alloc_skb(&q->napi, len);
		if (unlikely(!skb)) {
			mif_err_limited("alloc_skb() error\n");
			q->stat.err_nomem++;
			ret = -ENOMEM;
			goto rx_error;
		}
		skb_put(skb, len);
		skb_copy_to_linear_data(skb, src + NET_SKB_PAD + NET_IP_ALIGN, len);
		free_mif_buff(q->manager, src);
	} else {
		tmp_len = SKB_DATA_ALIGN(len + NET_SKB_PAD + NET_IP_ALIGN);
		tmp_len += SKB_DATA_ALIGN(sizeof(struct skb_shared_info));
		skb = __build_skb(src, tmp_len);
		if (unlikely(!skb)) {
			mif_err_limited("__build_skb() error\n");
			q->stat.err_nomem++;
			ret = -ENOMEM;
			goto rx_error;
		}
		skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);
		skb_put(skb, len);
	}

	if (pktproc_check_hw_checksum(desc[q->done_ptr].status))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		q->stat.err_csum++;


#ifdef PKTPROC_DEBUG_PKT
	pr_buffer("pktproc", (char *)skb->data, (size_t)len, (size_t)40);
#endif

	/* Set priv */
	skbpriv(skb)->lnk_hdr = 0;
	skbpriv(skb)->sipc_ch = ch_id;
	skbpriv(skb)->iod = link_get_iod_with_channel(ld, ch_id);
	skbpriv(skb)->ld = ld;

	*new_skb = skb;
	q->stat.pass_cnt++;
	q->done_ptr = circ_new_ptr(q->q_info->num_desc, q->done_ptr, 1);

	return 0;

rx_error_on_desc:
	mif_err_limited("Skip invalid descriptor at %d\n", q->done_ptr);
	q->done_ptr = circ_new_ptr(q->q_info->num_desc, q->done_ptr, 1);

rx_error:
	if (skb)
		dev_kfree_skb_any(skb);

	return ret;
}

/*
 * Clean RX ring
 */
static int pktproc_get_usage(struct pktproc_queue *q)
{
	u32 usage = 0;

	switch (q->ppa->desc_mode) {
	case DESC_MODE_RINGBUF:
		usage = circ_get_usage(q->q_info->num_desc, *q->fore_ptr, *q->rear_ptr);
		break;
	case DESC_MODE_SKTBUF:
		usage = circ_get_usage(q->q_info->num_desc, *q->rear_ptr, q->done_ptr);
		break;
	default:
		usage = 0;
		break;
	}

	return usage;
}

static int pktproc_check_buffer_status(struct pktproc_queue *q, u32 budget)
{
	if (q->ppa->desc_mode != DESC_MODE_SKTBUF)
		return 0;

	if ((q->manager->free_cell_count < budget) ||
		(circ_get_space(q->q_info->num_desc, *q->rear_ptr, *q->fore_ptr) < budget)) {
		q->stat.use_memcpy_cnt++;
		return 1;
	}

	return 0;
}

static int pktproc_clean_rx_ring(struct pktproc_queue *q, int budget, int *work_done)
{
	int ret = 0;
	u32 num_frames;
	u32 rcvd = 0;

	if (!pktproc_check_active(q->ppa, q->q_idx))
		return -EACCES;

	num_frames = min_t(unsigned int, pktproc_get_usage(q), budget);
	if (!num_frames)
		return 0;

	q->use_memcpy = pktproc_check_buffer_status(q, budget);

	pp_debug("Q%d num_frames:%d memcpy:%d %d/%d/%d\n",
		q->q_idx, num_frames, q->use_memcpy, *q->fore_ptr, *q->rear_ptr, q->done_ptr);

	while (rcvd < num_frames) {
		struct sk_buff *skb = NULL;

		ret = q->get_packet(q, &skb);
		if (unlikely(ret < 0)) {
			mif_err_limited("get_packet() error %d\n", ret);
			break;
		}
		if (unlikely(!skb)) {
			mif_err_limited("skb is null\n");
			ret = -ENOMEM;
			break;
		}

		rcvd++;

		ret = q->mld->pass_skb_to_net(q->mld, skb);
		if (ret < 0)
			break;
	}

	switch (q->ppa->desc_mode) {
	case DESC_MODE_SKTBUF:
		if (q->alloc_rx_buf(q))
			mif_err_limited("alloc_rx_buf() error %d Q%d\n", ret, q->q_idx);
		break;
	default:
		break;
	}

	*work_done = rcvd;
	return ret;
}

#ifdef CONFIG_CP_PKTPROC_PERF_TEST
/* copied from shmem_irq_handler() */
static void pktproc_perftest_napi_schedule(struct mem_link_device *mld)
{
	mld->rx_int_count++;
	if (napi_schedule_prep(&mld->mld_napi)) {
		struct link_device *ld = &mld->link_dev;

		ld->disable_rx_int(ld);
		__napi_schedule(&mld->mld_napi);
	}
}

static void pktproc_perftest_gen_rx_packet_sktbuf_mode(
		struct pktproc_queue *q, int packet_num, int session)
{
	struct pktproc_desc_sktbuf *desc = q->desc_sktbuf;
	struct pktproc_perftest *perf = &q->ppa->perftest;
	u32 header_len;
	u32 rear_ptr;
	u8 *src;
	u32 *seq;
	u16 *dst_port;
	u16 *dst_addr;
	int i, j;

	header_len = perftest_data[perf->mode].header_len;
	rear_ptr = *q->rear_ptr;

	for (i = 0 ; i < packet_num ; i++) {
		/* set desc */
		desc[rear_ptr].status = 0x11;
		desc[rear_ptr].length = perftest_data[perf->mode].packet_len;
		desc[rear_ptr].filter_result = 0x9;
		desc[rear_ptr].channel_id = perf->ch;

		/* set data */
		src = desc[rear_ptr].data_addr - q->q_info->data_base + q->data;
		memset(src, 0x0, desc[rear_ptr].length);
		memcpy(src, perftest_data[perf->mode].header, header_len);
		seq = (u32 *)(src + header_len);
		*seq = htonl(perf->seq_counter[session]++);
		dst_port = (u16 *)(src + perftest_data[perf->mode].dst_port_offset);
		*dst_port = htons(5001 + session);
		if (perf->mode == PERFTEST_MODE_CLAT) {
			for (j = 0 ; j < 8 ; j++) {
				dst_addr = (u16 *)(src + 24 + (j * 2));
				*dst_addr = htons(perf->clat_ipv6[j]);
			}
		}

		rear_ptr = circ_new_ptr(q->q_info->num_desc, rear_ptr, 1);
	}

	*q->rear_ptr = rear_ptr;
}

static int pktproc_perftest_thread(void *arg)
{
	struct mem_link_device *mld = (struct mem_link_device *) arg;
	struct pktproc_adaptor *ppa = &mld->pktproc;
	struct pktproc_queue *q = ppa->q[0];
	struct pktproc_perftest *perf = &ppa->perftest;
	int i, pkts;

	if (perf->session > 2)
		perf->session = 2;

	/* max 1023 packets per 1ms for 12Gbps */
	pkts = (perf->session > 0 ? (1023 / perf->session) : 0);
	do {
		for (i = 0 ; i < perf->session ; i++) {
			pktproc_perftest_gen_rx_packet_sktbuf_mode(q, pkts, i);
		}
		pktproc_perftest_napi_schedule(mld);
		udelay(perf->udelay);

		if (kthread_should_stop())
			break;
	} while (perf->test_run);

	return 0;
}

static ssize_t perftest_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct modem_ctl *mc = dev_get_drvdata(dev);
	struct link_device *ld = get_current_link(mc->iod);
	struct mem_link_device *mld = to_mem_link_device(ld);
	struct pktproc_adaptor *ppa = &mld->pktproc;
	struct pktproc_perftest *perf = &ppa->perftest;

	static struct task_struct *worker_task;
	int ret;
	int cpu = 5;

	ret = sscanf(buf, "%d %d %d %d %d %x:%x:%x:%x:%x:%x:%x:%x",
		&perf->mode, &perf->session, &perf->ch, &cpu, &perf->udelay,
		&perf->clat_ipv6[0], &perf->clat_ipv6[1], &perf->clat_ipv6[2], &perf->clat_ipv6[3],
		&perf->clat_ipv6[4], &perf->clat_ipv6[5], &perf->clat_ipv6[6], &perf->clat_ipv6[7]);

	if (ret < 1)
		return -EINVAL;

	switch (perf->mode) {
	case PERFTEST_MODE_STOP:
		if (perf->test_run)
			kthread_stop(worker_task);

		perf->seq_counter[0] = 0;
		perf->seq_counter[1] = 0;
		perf->test_run = false;
		break;
	case PERFTEST_MODE_IPV4:
	case PERFTEST_MODE_CLAT:
		if (perf->test_run)
			kthread_stop(worker_task);

		perf->test_run = true;
		worker_task = kthread_create_on_cpu(pktproc_perftest_thread,
			mld, cpu, "perftest");
		wake_up_process(worker_task);
		break;
	default:
		break;
	}

	return count;
}
#endif

/*
 * NAPI
 */
static int pktproc_poll(struct napi_struct *napi, int budget)
{
	struct pktproc_queue *q = container_of(napi, struct pktproc_queue, napi);
	struct mem_link_device *mld = q->mld;
	struct link_device *ld = &mld->link_dev;
	struct modem_ctl *mc = ld->mc;
	int ret;
	u32 rcvd = 0;

	if (likely(!cp_online(mc)))
		goto poll_exit;

	if (!pktproc_check_active(q->ppa, q->q_idx))
		goto poll_exit;

	ret = q->clean_rx_ring(q, budget, &rcvd);
	if ((ret == -EBUSY) || (ret == -ENOMEM))
		goto poll_retry;

	if (rcvd < budget) {
		napi_complete_done(napi, rcvd);
		/* TODO : enable interrupt */

		return rcvd;
	}

poll_retry:
	return budget;

poll_exit:
	napi_complete(napi);
	/* TODO : enable interrupt */

	return 0;
}

/*
 * IRQ handler
 */
static irqreturn_t pktproc_irq_handler(int irq, void *arg)
{
	struct pktproc_queue *q = (struct pktproc_queue *)arg;

	if (!pktproc_get_usage(q))
		return IRQ_HANDLED;

	if (napi_schedule_prep(&q->napi)) {
		/* TODO : disable interrupt */
		__napi_schedule(&q->napi);
	}

	return IRQ_HANDLED;
}

/*
 * Debug
 */
static ssize_t region_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct modem_ctl *mc = dev_get_drvdata(dev);
	struct link_device *ld = get_current_link(mc->iod);
	struct mem_link_device *mld = to_mem_link_device(ld);
	struct pktproc_adaptor *ppa = &mld->pktproc;
	struct pktproc_q_info *q_info;
	struct pktproc_info_v2 *info_v2;
	ssize_t count = 0;
	int i;

	count += sprintf(&buf[count], "Version:%d\n", ppa->version);
	count += sprintf(&buf[count], "CP base:0x%08x\n", ppa->cp_base);
	count += sprintf(&buf[count], "Descriptor mode:%d\n", ppa->desc_mode);
	count += sprintf(&buf[count], "Num of queue:%d\n", ppa->num_queue);
	count += sprintf(&buf[count], "Exclusive interrupt:%d\n", ppa->use_exclusive_irq);
	count += sprintf(&buf[count], "HW cache coherency:%d\n", ppa->use_hw_iocc);
	count += sprintf(&buf[count], "\n");

	switch (ppa->desc_mode) {
	case DESC_MODE_SKTBUF:
		count += sprintf(&buf[count], "Buffer manager\n");
		count += sprintf(&buf[count], "  buffer size:0x%08x\n", ppa->manager->buffer_size);
		count += sprintf(&buf[count], "  total cell count:%d\n", ppa->manager->cell_count);
		count += sprintf(&buf[count], "  cell size:%d\n", ppa->manager->cell_size);
		count += sprintf(&buf[count], "\n");
		break;
	default:
		break;
	}

	switch (ppa->version) {
	case PKTPROC_V2:
		info_v2 = (struct pktproc_info_v2 *)ppa->info_base;
		count += sprintf(&buf[count], "Control:0x%08x\n", info_v2->control);
		break;
	default:
		break;
	}
	for (i = 0; i < ppa->num_queue; i++) {
		struct pktproc_queue *q = ppa->q[i];

		if (!pktproc_check_active(ppa, q->q_idx)) {
			count += sprintf(&buf[count], "Queue %d is not active\n", i);
			continue;
		}

		q_info = q->q_info;
		count += sprintf(&buf[count], "Queue%d\n", i);
		count += sprintf(&buf[count], "  num_desc:%d(0x%08x)\n", q_info->num_desc, q_info->num_desc);
		count += sprintf(&buf[count], "  desc_base:0x%08x\n", q_info->desc_base);
		count += sprintf(&buf[count], "  desc_size:0x%08x\n", q->desc_size);
		count += sprintf(&buf[count], "  data_base:0x%08x\n", q_info->data_base);
		count += sprintf(&buf[count], "  data_size:0x%08x\n", q->data_size);
	}

	return count;
}

static ssize_t status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct modem_ctl *mc = dev_get_drvdata(dev);
	struct link_device *ld = get_current_link(mc->iod);
	struct mem_link_device *mld = to_mem_link_device(ld);
	struct pktproc_adaptor *ppa = &mld->pktproc;
	struct pktproc_q_info *q_info;
	ssize_t count = 0;
	int i;

	switch (ppa->desc_mode) {
	case DESC_MODE_SKTBUF:
		count += sprintf(&buf[count], "Buffer manager total/use/free:%d/%d/%d\n",
			ppa->manager->cell_count, ppa->manager->used_cell_count, ppa->manager->free_cell_count);
		count += sprintf(&buf[count], "\n");
		break;
	default:
		break;
	}

	for (i = 0; i < ppa->num_queue; i++) {
		struct pktproc_queue *q = ppa->q[i];

		if (!pktproc_check_active(ppa, q->q_idx)) {
			count += sprintf(&buf[count], "Queue %d is not active\n", i);
			continue;
		}

		q_info = q->q_info;
		count += sprintf(&buf[count], "Queue%d\n", i);
		count += sprintf(&buf[count], "  num_desc:%d\n", q_info->num_desc);
		switch (ppa->desc_mode) {
		case DESC_MODE_RINGBUF:
			count += sprintf(&buf[count], "  fore/rear:%d/%d\n",
							q_info->fore_ptr, q_info->rear_ptr);
			break;
		case DESC_MODE_SKTBUF:
			count += sprintf(&buf[count], "  fore/rear/done:%d/%d/%d\n",
							q_info->fore_ptr, q_info->rear_ptr, q->done_ptr);
			count += sprintf(&buf[count], "  use memcpy:%d count:%lld\n",
							q->use_memcpy, q->stat.use_memcpy_cnt);
			break;
		default:
			break;
		}
		count += sprintf(&buf[count], "  pass:%lld\n", q->stat.pass_cnt);
		count += sprintf(&buf[count], "  fail:len%lld chid%lld addr%lld nomem%lld bmnomem%lld csum%lld\n",
				q->stat.err_len, q->stat.err_chid, q->stat.err_addr,
				q->stat.err_nomem, q->stat.err_bm_nomem, q->stat.err_csum);
	}

	return count;
}

static DEVICE_ATTR_RO(region);
static DEVICE_ATTR_RO(status);
#ifdef CONFIG_CP_PKTPROC_PERF_TEST
static DEVICE_ATTR_WO(perftest);
#endif

static struct attribute *pktproc_attrs[] = {
	&dev_attr_region.attr,
	&dev_attr_status.attr,
#ifdef CONFIG_CP_PKTPROC_PERF_TEST
	&dev_attr_perftest.attr,
#endif
	NULL,
};

static const struct attribute_group pktproc_group = {
	.attrs = pktproc_attrs,
	.name = "pktproc",
};

/*
 * Initialize PktProc
 */
int pktproc_init(struct pktproc_adaptor *ppa)
{
	int i;
	int ret = 0;

	if (!ppa) {
		mif_err("ppa is null\n");
		return -EPERM;
	}

	if (!pktproc_check_support(ppa))
		return 0;

	for (i = 0; i < ppa->num_queue; i++) {
		struct pktproc_queue *q = ppa->q[i];
		struct pktproc_q_info *q_info = q->q_info;

		mif_info("Q%d\n", i);

		if (ppa->use_exclusive_irq)
			napi_synchronize(&q->napi);

		switch (ppa->desc_mode) {
		case DESC_MODE_SKTBUF:
			if (pktproc_check_active(q->ppa, q->q_idx))
				pktproc_clear_data_addr(q);

			q->use_memcpy = 0;
			q->stat.use_memcpy_cnt = 0;
			break;
		default:
			break;
		}

		*q->fore_ptr = 0;
		*q->rear_ptr = 0;
		q->done_ptr = 0;

		memset(&q->stat, 0, sizeof(struct pktproc_statistics));

		switch (ppa->desc_mode) {
		case DESC_MODE_SKTBUF:
			ret = q->alloc_rx_buf(q);
			if (ret) {
				mif_err_limited("alloc_rx_buf() error %d Q%d\n", ret, q->q_idx);
				continue;
			}
			break;
		default:
			break;
		}

		atomic_set(&q->active, 1);

		mif_info("num_desc:0x%08x desc_base:0x%08x data_base:0x%08x fore:%d rear:%d done:%d\n",
			q_info->num_desc, q_info->desc_base, q_info->data_base,
			q_info->fore_ptr, q_info->rear_ptr, q->done_ptr);
	}

	return 0;
}

/*
 * Create PktProc
 */
static int pktproc_get_info(struct pktproc_adaptor *ppa, struct device_node *np)
{
	mif_dt_read_u32(np, "pktproc_version", ppa->version);
	mif_dt_read_u32(np, "pktproc_cp_base", ppa->cp_base);
	switch (ppa->version) {
	case PKTPROC_V1:
		ppa->desc_mode = DESC_MODE_RINGBUF;
		ppa->num_queue = 1;
		ppa->use_exclusive_irq = 0;
		break;
	case PKTPROC_V2:
		mif_dt_read_u32(np, "pktproc_desc_mode", ppa->desc_mode);
		mif_dt_read_u32(np, "pktproc_desc_num_ratio_percent", ppa->desc_num_ratio_percent);
		mif_dt_read_u32(np, "pktproc_num_queue", ppa->num_queue);
		mif_dt_read_u32(np, "pktproc_use_exclusive_irq", ppa->use_exclusive_irq);
		break;
	default:
		mif_err("Unsupported version:%d\n", ppa->version);
		return -EINVAL;
	}
	mif_dt_read_u32(np, "pktproc_use_hw_iocc", ppa->use_hw_iocc);
	mif_info("version:%d cp_base:0x%08x mode:%d num_queue:%d exclusive_irq:%d iocc:%d\n",
		ppa->version, ppa->cp_base, ppa->desc_mode, ppa->num_queue, ppa->use_exclusive_irq, ppa->use_hw_iocc);

	mif_dt_read_u32(np, "pktproc_info_rgn_offset", ppa->info_rgn_offset);
	mif_dt_read_u32(np, "pktproc_info_rgn_size", ppa->info_rgn_size);
	mif_dt_read_u32(np, "pktproc_desc_rgn_offset", ppa->desc_rgn_offset);
	mif_dt_read_u32(np, "pktproc_desc_rgn_size", ppa->desc_rgn_size);
	mif_dt_read_u32(np, "pktproc_data_rgn_offset", ppa->data_rgn_offset);
	mif_info("info_rgn 0x%08x 0x%08x desc_rgn 0x%08x 0x%08x %u data_rgn 0x%08x\n",
		ppa->info_rgn_offset, ppa->info_rgn_size,
		ppa->desc_rgn_offset, ppa->desc_rgn_size,
		ppa->desc_num_ratio_percent, ppa->data_rgn_offset);

	return 0;
}

int pktproc_create(struct platform_device *pdev, struct mem_link_device *mld, u32 memaddr, u32 memsize)
{
	struct device_node *np = pdev->dev.of_node;
	struct pktproc_adaptor *ppa = &mld->pktproc;
	u32 data_size, data_size_by_q;
	int i;
	int ret;

	if (!np) {
		mif_err("of_node is null\n");
		return -EINVAL;
	}
	if (!ppa) {
		mif_err("ppa is null\n");
		return -EINVAL;
	}

	mif_dt_read_u32_noerr(np, "pktproc_support", ppa->support);
	if (!ppa->support) {
		mif_info("pktproc_support is 0. Just return\n");
		return 0;
	}

	/* Get info */
	ret = pktproc_get_info(ppa, np);
	if (ret != 0) {
		mif_err("pktproc_get_dt() error %d\n", ret);
		return ret;
	}

	/* Get base addr */
	mif_info("memaddr:0x%08x memsize:0x%08x\n", memaddr, memsize);
	if (ppa->use_hw_iocc)
		ppa->info_base = phys_to_virt(memaddr);
	else
		ppa->info_base = cp_shmem_get_nc_region(memaddr, ppa->info_rgn_size + ppa->desc_rgn_size);
	if (!ppa->info_base) {
		mif_err("ppa->info_base error\n");
		return -ENOMEM;
	}
	ppa->desc_base = ppa->info_base + ppa->info_rgn_size;
	memset(ppa->info_base, 0, ppa->info_rgn_size + ppa->desc_rgn_size);
	mif_info("info + desc size:0x%08x\n", ppa->info_rgn_size + ppa->desc_rgn_size);

	ppa->data_base = phys_to_virt(memaddr + ppa->data_rgn_offset);
	data_size = memsize - (ppa->info_rgn_size + ppa->desc_rgn_size);
	data_size_by_q = data_size / ppa->num_queue;
	if (!ppa->use_hw_iocc)
		__inval_dcache_area(ppa->data_base, data_size);
	mif_info("Total data buffer size:0x%08x Queue:%d Size by queue:0x%08x\n",
					data_size, ppa->num_queue, data_size_by_q);

	/* Create queue */
	for (i = 0; i < ppa->num_queue; i++) {
		struct pktproc_queue *q;

		mif_info("Queue %d\n", i);

		ppa->q[i] = kzalloc(sizeof(struct pktproc_queue), GFP_ATOMIC);
		if (ppa->q[i] == NULL) {
			mif_err_limited("kzalloc() error %d\n", i);
			ret = -ENOMEM;
			goto create_error;
		}
		q = ppa->q[i];

		atomic_set(&q->active, 0);

		/* Info region */
		switch (ppa->version) {
		case PKTPROC_V1:
			q->info_v1 = (struct pktproc_info_v1 *)ppa->info_base;
			q->q_info = &q->info_v1->q_info;
			break;
		case PKTPROC_V2:
			q->info_v2 = (struct pktproc_info_v2 *)ppa->info_base;
			q->info_v2->control = ((ppa->desc_mode & 0x1) << 4) | (ppa->num_queue & 0xF);
			q->q_info = &q->info_v2->q_info[i];
			break;
		default:
			mif_err("Unsupported version:%d\n", ppa->version);
			ret = -EINVAL;
			goto create_error;
		}

		/* Descriptor, data buffer region */
		switch (ppa->desc_mode) {
		case DESC_MODE_RINGBUF:
			q->data = ppa->data_base + (i * data_size_by_q);
			q->q_info->data_base = ppa->cp_base + ppa->data_rgn_offset + (i * data_size_by_q);
			q->data_size = data_size_by_q;

			q->q_info->num_desc = data_size_by_q / PKTPROC_MAX_PACKET_SIZE;

			q->desc_ringbuf = ppa->desc_base +
					(i * sizeof(struct pktproc_desc_ringbuf) * q->q_info->num_desc);
			q->q_info->desc_base = ppa->cp_base + ppa->desc_rgn_offset +
					(i * sizeof(struct pktproc_desc_ringbuf) * q->q_info->num_desc);
			q->desc_size = sizeof(struct pktproc_desc_ringbuf) * q->q_info->num_desc;

			q->get_packet = pktproc_get_pkt_from_ringbuf_mode;
			break;
		case DESC_MODE_SKTBUF:
			/* Use only one buffer manager for all queues */
			if (ppa->manager == NULL) {
				ppa->manager = init_mif_buff_mng(ppa->data_base, data_size, MIF_BUFF_DEFAULT_CELL_SIZE);
				if (ppa->manager == NULL) {
					mif_err("init_mif_buff_mng() error\n");
					ret = -ENOMEM;
					goto create_error;
				}
			}
			q->manager = ppa->manager;
			q->data = ppa->data_base;
			q->q_info->data_base = ppa->cp_base + ppa->data_rgn_offset;
			q->data_size = data_size;

			q->use_memcpy = 0;
			q->stat.use_memcpy_cnt = 0;

			q->q_info->num_desc = (data_size_by_q * ppa->desc_num_ratio_percent) / (q->manager->cell_size * 100);

			q->desc_sktbuf = ppa->desc_base +
					(i * sizeof(struct pktproc_desc_sktbuf) * q->q_info->num_desc);
			q->q_info->desc_base = ppa->cp_base + ppa->desc_rgn_offset +
					(i * sizeof(struct pktproc_desc_sktbuf) * q->q_info->num_desc);
			q->desc_size = sizeof(struct pktproc_desc_sktbuf) * q->q_info->num_desc;

			q->get_packet = pktproc_get_pkt_from_sktbuf_mode;
			q->alloc_rx_buf = pktproc_fill_data_addr;
			break;
		default:
			mif_err("Unsupported version:%d\n", ppa->version);
			ret = -EINVAL;
			goto create_error;
		}

		if ((q->q_info->desc_base + q->desc_size) > q->q_info->data_base) {
			mif_err("Descriptor overflow:0x%08x 0x%08x 0x%08x\n",
				q->q_info->desc_base, q->desc_size, q->q_info->data_base);
			goto create_error;
		}

		if (ppa->use_exclusive_irq) {
			q->irq_handler = pktproc_irq_handler;

			init_dummy_netdev(&q->netdev);
			netif_napi_add(&q->netdev, &q->napi, pktproc_poll, 64);
			napi_enable(&q->napi);
		} else {
			q->netdev = mld->dummy_net;
			q->napi = mld->mld_napi;
		}

		spin_lock_init(&q->lock);

		q->clean_rx_ring = pktproc_clean_rx_ring;

		q->q_idx = i;
		q->mld = mld;
		q->ppa = ppa;

		q->q_info->fore_ptr = 0;
		q->q_info->rear_ptr = 0;

		q->fore_ptr = &q->q_info->fore_ptr;
		q->rear_ptr = &q->q_info->rear_ptr;
		q->done_ptr = *q->rear_ptr;

		mif_info("num_desc:%d desc_offset:0x%08x desc_size:0x%08x data_offset:0x%08x data_size:0x%08x\n",
			q->q_info->num_desc, q->q_info->desc_base, q->desc_size,
			q->q_info->data_base, q->data_size);
	}

	/* Debug */
	ret = sysfs_create_group(&pdev->dev.kobj, &pktproc_group);
	if (ret != 0) {
		mif_err("sysfs_create_group() error %d\n", ret);
		goto create_error;
	}

	return 0;

create_error:
	for (i = 0; i < ppa->num_queue; i++)
		kfree(ppa->q[i]);

	if (ppa->manager)
		exit_mif_buff_mng(ppa->manager);

	if (!ppa->use_hw_iocc && ppa->info_base)
		vunmap(ppa->info_base);

	return ret;
}
