/*
 * Copyright (C) 2010 Samsung Electronics.
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

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/smc.h>
#include <linux/modem_notifier.h>
#include <linux/clk.h>
#include <linux/pci.h>
#include <linux/regulator/consumer.h>
#include <soc/samsung/acpm_mfd.h>
#include <linux/reboot.h>
#include <linux/suspend.h>

#ifdef CONFIG_MUIC_NOTIFIER
#include <linux/muic/muic.h>
#include <linux/muic/muic_notifier.h>
#endif

#include <linux/exynos-pci-ctrl.h>
#include <linux/shm_ipc.h>

#include "modem_prj.h"
#include "modem_utils.h"
#include "modem_ctrl.h"
#include "link_device.h"
#include "link_device_memory.h"
#include "s51xx_pcie.h"

#ifdef CONFIG_EXYNOS_BUSMONITOR
#include <linux/exynos-busmon.h>
#endif

#ifdef CONFIG_SUSPEND_DURING_VOICE_CALL
#include <sound/samsung/abox.h>
#endif

#ifdef CONFIG_LINK_DEVICE_PCIE_S2MPU
#include <soc/samsung/exynos-s2mpu.h>
#endif

#ifdef CONFIG_CP_LCD_NOTIFIER
#include "../../../video/fbdev/exynos/dpu30/decon.h"
static int s5100_lcd_notifier(struct notifier_block *notifier,
		unsigned long event, void *v);
#endif /* CONFIG_CP_LCD_NOTIFIER */

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

#define RUNTIME_PM_AFFINITY_CORE 2

static struct modem_ctl *g_mc;

static int register_phone_active_interrupt(struct modem_ctl *mc);
static int register_cp2ap_wakeup_interrupt(struct modem_ctl *mc);

static int s5100_reboot_handler(struct notifier_block *nb,
				    unsigned long l, void *p)
{
	struct modem_ctl *mc = container_of(nb, struct modem_ctl, reboot_nb);

	mif_info("Now is device rebooting..\n");

	mutex_lock(&mc->pcie_check_lock);
	mc->device_reboot = true;
	mutex_unlock(&mc->pcie_check_lock);

	return 0;
}

static void print_mc_state(struct modem_ctl *mc)
{
	int pwr  = mif_gpio_get_value(mc->s5100_gpio_cp_pwr, false);
	int reset = mif_gpio_get_value(mc->s5100_gpio_cp_reset, false);
	int pshold = mif_gpio_get_value(mc->s5100_gpio_cp_ps_hold, false);

	int ap_wakeup = mif_gpio_get_value(mc->s5100_gpio_ap_wakeup, false);
	int cp_wakeup = mif_gpio_get_value(mc->s5100_gpio_cp_wakeup, false);

	int dump = mif_gpio_get_value(mc->s5100_gpio_cp_dump_noti, false);
	int ap_status = mif_gpio_get_value(mc->s5100_gpio_ap_status, false);
	int phone_active = mif_gpio_get_value(mc->s5100_gpio_phone_active, false);

	mif_info("%s: %pf:GPIO - pwr:%d rst:%d phd:%d aw:%d cw:%d dmp:%d ap_status:%d phone_state:%d\n",
		mc->name, CALLER, pwr, reset, pshold, ap_wakeup, cp_wakeup,
		dump, ap_status, phone_active);
}

static void pcie_clean_dislink(struct modem_ctl *mc)
{
	if (mc->pcie_powered_on)
		s5100_poweroff_pcie(mc, true);

	if (!mc->pcie_powered_on)
		mif_err("Link is disconnected!!!\n");

#if defined(CONFIG_SUSPEND_DURING_VOICE_CALL)
	mc->pcie_voice_call_on = false;
#endif
}

static void cp2ap_wakeup_work(struct work_struct *ws)
{
	struct modem_ctl *mc = container_of(ws, struct modem_ctl, wakeup_work);
	if (mc->phone_state == STATE_CRASH_EXIT)
		return;

	s5100_poweron_pcie(mc);
}

static void cp2ap_suspend_work(struct work_struct *ws)
{
	struct modem_ctl *mc = container_of(ws, struct modem_ctl, suspend_work);
	if (mc->phone_state == STATE_CRASH_EXIT)
		return;

	s5100_poweroff_pcie(mc, false);
}

#if defined(CONFIG_SUSPEND_DURING_VOICE_CALL)
static void voice_call_on_work(struct work_struct *ws)
{
	struct modem_ctl *mc = container_of(ws, struct modem_ctl, call_on_work);

	mutex_lock(&mc->pcie_check_lock);
	if (!mc->pcie_voice_call_on)
		goto exit;

	if (mc->pcie_powered_on &&
			(s51xx_check_pcie_link_status(mc->pcie_ch_num) != 0)) {
		if (wake_lock_active(&mc->mc_wake_lock))
			wake_unlock(&mc->mc_wake_lock);
	}

exit:
	mif_info("wakelock active = %d, voice status = %d\n",
		wake_lock_active(&mc->mc_wake_lock), mc->pcie_voice_call_on);
	mutex_unlock(&mc->pcie_check_lock);
}

static void voice_call_off_work(struct work_struct *ws)
{
	struct modem_ctl *mc = container_of(ws, struct modem_ctl, call_off_work);

	mutex_lock(&mc->pcie_check_lock);
	if (mc->pcie_voice_call_on)
		goto exit;

	if (mc->pcie_powered_on &&
			(s51xx_check_pcie_link_status(mc->pcie_ch_num) != 0)) {
		if (!wake_lock_active(&mc->mc_wake_lock))
			wake_lock(&mc->mc_wake_lock);
	}

exit:
	mif_info("wakelock active = %d, voice status = %d\n",
		wake_lock_active(&mc->mc_wake_lock), mc->pcie_voice_call_on);
	mutex_unlock(&mc->pcie_check_lock);
}
#endif

/* It means initial GPIO level. */
static int check_link_order = 1;
static irqreturn_t ap_wakeup_handler(int irq, void *data)
{
	struct modem_ctl *mc = (struct modem_ctl *)data;
	int gpio_val = mif_gpio_get_value(mc->s5100_gpio_ap_wakeup, true);
	unsigned long flags;

	mif_disable_irq(&mc->s5100_irq_ap_wakeup);

	if (mc->device_reboot) {
		mif_err("skip : device is rebooting..!!!\n");
		return IRQ_HANDLED;
	}

	if (gpio_val == check_link_order) {
		mif_err("skip : cp2ap_wakeup val is same with before : %d\n", gpio_val);
		mc->apwake_irq_chip->irq_set_type(
			irq_get_irq_data(mc->s5100_irq_ap_wakeup.num),
			(gpio_val == 1 ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH));
		mif_enable_irq(&mc->s5100_irq_ap_wakeup);
		return IRQ_HANDLED;
	}
	check_link_order = gpio_val;

	spin_lock_irqsave(&mc->pcie_pm_lock, flags);
	if (mc->pcie_pm_suspended) {
		if (gpio_val == 1) {
			/* try to block system suspend */
			if (!wake_lock_active(&mc->mc_wake_lock))
				wake_lock(&mc->mc_wake_lock);
		}

		mif_err("cp2ap_wakeup work pending. gpio_val : %d\n", gpio_val);
		mc->pcie_pm_resume_wait = true;
		mc->pcie_pm_resume_gpio_val = gpio_val;

		spin_unlock_irqrestore(&mc->pcie_pm_lock, flags);
		return IRQ_HANDLED;
	}
	spin_unlock_irqrestore(&mc->pcie_pm_lock, flags);

	mc->apwake_irq_chip->irq_set_type(
		irq_get_irq_data(mc->s5100_irq_ap_wakeup.num),
		(gpio_val == 1 ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH));
	mif_enable_irq(&mc->s5100_irq_ap_wakeup);

	queue_work_on(RUNTIME_PM_AFFINITY_CORE, mc->wakeup_wq,
			(gpio_val == 1 ? &mc->wakeup_work : &mc->suspend_work));

	return IRQ_HANDLED;
}

static irqreturn_t cp_active_handler(int irq, void *data)
{
	struct modem_ctl *mc = (struct modem_ctl *)data;
	struct link_device *ld;
	struct mem_link_device *mld;
	struct io_device *iod;
	int cp_active;
	enum modem_state old_state;
	enum modem_state new_state;

	if (mc == NULL) {
		mif_err_limited("modem_ctl is NOT initialized - IGNORING interrupt\n");
		goto irq_done;
	}

	ld = get_current_link(mc->iod);
	mld = to_mem_link_device(ld);

	if (mc->s51xx_pdev == NULL) {
		mif_err_limited("S5100 is NOT initialized - IGNORING interrupt\n");
		goto irq_done;
	}

	if (mc->phone_state != STATE_ONLINE) {
		mif_err_limited("Phone_state is NOT ONLINE - IGNORING interrupt\n");
		goto irq_done;
	}

	cp_active = mif_gpio_get_value(mc->s5100_gpio_phone_active, true);
	mif_err("[PHONE_ACTIVE Handler] state:%s cp_active:%d\n",
			cp_state_str(mc->phone_state), cp_active);

	if (cp_active == 1) {
		mif_err("ERROR - cp_active is not low, state:%s cp_active:%d\n",
				cp_state_str(mc->phone_state), cp_active);
		return IRQ_HANDLED;
	}

	if (timer_pending(&mld->crash_ack_timer))
		del_timer(&mld->crash_ack_timer);

	mif_set_snapshot(false);

	old_state = mc->phone_state;
	new_state = STATE_CRASH_EXIT;

	if (ld->crash_reason.type == CRASH_REASON_NONE)
		ld->crash_reason.type = CRASH_REASON_CP_ACT_CRASH;

	mif_info("Set s5100_cp_reset_required to false\n");
	mc->s5100_cp_reset_required = false;

	if (old_state != new_state) {
		mif_err("new_state = %s\n", cp_state_str(new_state));

		if (old_state == STATE_ONLINE)
			modem_notify_event(MODEM_EVENT_EXIT, mc);

		list_for_each_entry(iod, &mc->modem_state_notify_list, list) {
			if (iod && atomic_read(&iod->opened) > 0)
				iod->modem_state_changed(iod, new_state);
		}
	}

	atomic_set(&mld->forced_cp_crash, 0);

irq_done:
	mif_disable_irq(&mc->s5100_irq_phone_active);

	return IRQ_HANDLED;
}

static int register_phone_active_interrupt(struct modem_ctl *mc)
{
	int ret;

	if (mc == NULL)
		return -EINVAL;

	if (mc->s5100_irq_phone_active.registered == true)
		return 0;

	mif_info("Register PHONE ACTIVE interrupt.\n");
	mif_init_irq(&mc->s5100_irq_phone_active, mc->s5100_irq_phone_active.num,
			"phone_active", IRQF_TRIGGER_LOW);

	ret = mif_request_irq(&mc->s5100_irq_phone_active, cp_active_handler, mc);
	if (ret) {
		mif_err("%s: ERR! request_irq(%s#%d) fail (%d)\n",
			mc->name, mc->s5100_irq_phone_active.name,
			mc->s5100_irq_phone_active.num, ret);
		mif_err("xxx\n");
		return ret;
	}

	return ret;
}

static int register_cp2ap_wakeup_interrupt(struct modem_ctl *mc)
{
	int ret;

	if (mc == NULL)
		return -EINVAL;

	if (mc->s5100_irq_ap_wakeup.registered == true) {
		mif_info("Set IRQF_TRIGGER_LOW to cp2ap_wakeup gpio\n");
		check_link_order = 1;
		ret = mc->apwake_irq_chip->irq_set_type(
				irq_get_irq_data(mc->s5100_irq_ap_wakeup.num),
				IRQF_TRIGGER_LOW);
		return ret;
	}

	mif_info("Register CP2AP WAKEUP interrupt.\n");
	mif_init_irq(&mc->s5100_irq_ap_wakeup, mc->s5100_irq_ap_wakeup.num, "cp2ap_wakeup",
			IRQF_TRIGGER_LOW);

	ret = mif_request_irq(&mc->s5100_irq_ap_wakeup, ap_wakeup_handler, mc);
	if (ret) {
		mif_err("%s: ERR! request_irq(%s#%d) fail (%d)\n",
			mc->name, mc->s5100_irq_ap_wakeup.name,
			mc->s5100_irq_ap_wakeup.num, ret);
		mif_err("xxx\n");
		return ret;
	}

	return ret;
}

static int ds_detect = 2;
module_param(ds_detect, int, S_IRUGO | S_IWUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(ds_detect, "Dual SIM detect");

static ssize_t ds_detect_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", ds_detect);
}

static ssize_t ds_detect_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret;
	int value;

	ret = kstrtoint(buf, 0, &value);
	if (ret != 0) {
		mif_err("invalid value:%d with %d\n", value, ret);
		return -EINVAL;
	}

	ds_detect = value;
	mif_info("set ds_detect: %d\n", ds_detect);

	return count;
}
static DEVICE_ATTR_RW(ds_detect);

static struct attribute *sim_attrs[] = {
	&dev_attr_ds_detect.attr,
	NULL,
};

static const struct attribute_group sim_group = {
	.attrs = sim_attrs,
	.name = "sim",
};

static int get_ds_detect(void)
{
	if (ds_detect > 2 || ds_detect < 1)
		ds_detect = 2;

	mif_info("Dual SIM detect = %d\n", ds_detect);
	return ds_detect - 1;
}

static int init_control_messages(struct modem_ctl *mc)
{
	struct link_device *ld = get_current_link(mc->iod);
	struct mem_link_device *mld = to_mem_link_device(ld);
	int ds_det;

	set_ctrl_msg(&mld->ap2cp_united_status, 0);
	set_ctrl_msg(&mld->cp2ap_united_status, 0);

	ds_det = get_ds_detect();
	if (ds_det < 0) {
		mif_err("ds_det error:%d\n", ds_det);
		return -EINVAL;
	}

	update_ctrl_msg(&mld->ap2cp_united_status, ds_det, mc->sbi_ds_det_mask,
			mc->sbi_ds_det_pos);
	mif_info("ds_det:%d\n", ds_det);

	return 0;
}

static int power_on_cp(struct modem_ctl *mc)
{
	struct link_device *ld = get_current_link(mc->iod);
	struct modem_data __maybe_unused *modem = mc->mdm_data;
	struct mem_link_device *mld = to_mem_link_device(ld);

	mif_info("%s: +++\n", mc->name);

	mif_disable_irq(&mc->s5100_irq_phone_active);
	mif_disable_irq(&mc->s5100_irq_ap_wakeup);
	drain_workqueue(mc->wakeup_wq);

	print_mc_state(mc);

	if (!wake_lock_active(&mc->mc_wake_lock))
		wake_lock(&mc->mc_wake_lock);

	mif_set_snapshot(true);

	mc->phone_state = STATE_OFFLINE;
	pcie_clean_dislink(mc);

	mc->pcie_registered = false;

	mif_gpio_set_value(mc->s5100_gpio_ap_status, 1, 0);
	mif_gpio_set_value(mc->s5100_gpio_cp_dump_noti, 0, 0);

	/* Clear shared memory */
	init_ctrl_msg(&mld->ap2cp_msg);
	init_ctrl_msg(&mld->cp2ap_msg);

	print_mc_state(mc);

	mif_gpio_set_value(mc->s5100_gpio_cp_reset, 0, 0);
	mif_gpio_set_value(mc->s5100_gpio_cp_pwr, 0, 50);
	mif_gpio_set_value(mc->s5100_gpio_cp_pwr, 1, 50);
	mif_gpio_set_value(mc->s5100_gpio_cp_reset, 1, 50);

	mif_info("GPIO status after S5100 Power on\n");
	print_mc_state(mc);

	mif_info("---\n");

	return 0;
}

static int power_off_cp(struct modem_ctl *mc)
{
	mif_info("%s: +++\n", mc->name);

	if (mc->phone_state == STATE_OFFLINE)
		goto exit;

	mc->phone_state = STATE_OFFLINE;
	mc->bootd->modem_state_changed(mc->iod, STATE_OFFLINE);
	mc->iod->modem_state_changed(mc->iod, STATE_OFFLINE);

	pcie_clean_dislink(mc);

	mif_gpio_set_value(mc->s5100_gpio_cp_reset, 0, 0);
	mif_gpio_set_value(mc->s5100_gpio_cp_pwr, 0, 0);

	print_mc_state(mc);

exit:
	mif_info("---\n");

	return 0;
}

static int power_shutdown_cp(struct modem_ctl *mc)
{
	int i;

	mif_err("%s: +++\n", mc->name);

	if (mc->phone_state == STATE_OFFLINE)
		goto exit;

	mif_disable_irq(&mc->s5100_irq_phone_active);
	mif_disable_irq(&mc->s5100_irq_ap_wakeup);
	drain_workqueue(mc->wakeup_wq);

	/* wait for cp_active for 3 seconds */
	for (i = 0; i < 150; i++) {
		if (mif_gpio_get_value(mc->s5100_gpio_phone_active, false) == 1) {
			mif_err("PHONE_ACTIVE pin is HIGH...\n");
			break;
		}
		msleep(20);
	}

	mif_gpio_set_value(mc->s5100_gpio_cp_reset, 0, 0);
	mif_gpio_set_value(mc->s5100_gpio_cp_pwr, 0, 0);

	print_mc_state(mc);

	pcie_clean_dislink(mc);

exit:
	mif_err("---\n");
	return 0;
}

static int power_reset_dump_cp(struct modem_ctl *mc)
{
	struct s51xx_pcie *s51xx_pcie = pci_get_drvdata(mc->s51xx_pdev);
#ifdef CONFIG_LINK_DEVICE_PCIE_GPIO_WA
	unsigned long flags;
#endif

	mif_info("%s: +++\n", mc->name);

	mc->phone_state = STATE_OFFLINE;
	mif_disable_irq(&mc->s5100_irq_phone_active);
	mif_disable_irq(&mc->s5100_irq_ap_wakeup);
	drain_workqueue(mc->wakeup_wq);
	pcie_clean_dislink(mc);

	if (s51xx_pcie->link_status == 1) {
		mif_err("link_satus:%d\n", s51xx_pcie->link_status);
		s51xx_pcie_save_state(mc->s51xx_pdev);
		pcie_clean_dislink(mc);
	}

#ifdef CONFIG_LINK_DEVICE_PCIE_GPIO_WA
	spin_lock_irqsave(&mc->gpio_toggle_lock, flags);
	if (mif_gpio_set_value(mc->s5100_gpio_cp_dump_noti, 1, 10))
		mif_gpio_toggle_value(mc->s5100_gpio_ap_status, 50);
	spin_unlock_irqrestore(&mc->gpio_toggle_lock, flags);
#else
	mif_gpio_set_value(mc->s5100_gpio_cp_dump_noti, 1, 0);
#endif

	mif_info("s5100_cp_reset_required:%d\n", mc->s5100_cp_reset_required);
	if (mc->s5100_cp_reset_required == true) {
		mif_gpio_set_value(mc->s5100_gpio_cp_reset, 0, 50);
		print_mc_state(mc);

		mif_gpio_set_value(mc->s5100_gpio_cp_reset, 1, 50);
		print_mc_state(mc);
	}

	mif_gpio_set_value(mc->s5100_gpio_ap_status, 1, 0);

	mif_err("---\n");

	return 0;
}

static int power_reset_cp(struct modem_ctl *mc)
{
	struct s51xx_pcie *s51xx_pcie = pci_get_drvdata(mc->s51xx_pdev);

	mif_info("%s: +++\n", mc->name);

	mc->phone_state = STATE_OFFLINE;
	pcie_clean_dislink(mc);

	if (s51xx_pcie->link_status == 1) {
		/* save_s5100_status(); */
		mif_err("link_satus:%d\n", s51xx_pcie->link_status);
		pcie_clean_dislink(mc);
	}

	mif_gpio_set_value(mc->s5100_gpio_cp_reset, 0, 50);
	print_mc_state(mc);

	mif_gpio_set_value(mc->s5100_gpio_cp_reset, 1, 50);
	print_mc_state(mc);

	mif_err("---\n");

	return 0;
}

static int check_cp_status(struct modem_ctl *mc, unsigned int count)
{
	int ret = 0;
	int cnt = 0;
	int val;

	while (1) {
		val = mif_gpio_get_value(mc->s5100_gpio_ap_wakeup, false);
		mif_err_limited("CP2AP_WAKEUP == %d (cnt %d)\n", val, cnt);

		if (val != 0) {
			ret = 0;
			break;
		}

		if (++cnt >= count) {
			mif_err("ERR! CP2AP_WAKEUP == 0 (cnt %d)\n", cnt);
			ret = -EFAULT;
			break;
		}

		msleep(20);
	}

	if (ret == 0)
		mif_info("CP2AP_WAKEUP == 1 cnt: %d\n", cnt);
	else
		mif_err("ERR: Checking count after sending bootloader: %d\n", cnt);

	if (cnt == 0)
		msleep(10);

	return ret;
}

static int start_normal_boot(struct modem_ctl *mc)
{
	struct link_device *ld = get_current_link(mc->bootd);
	struct io_device *iod;
	struct mem_link_device *mld = to_mem_link_device(ld);
	int ret = 0;

	mif_info("+++\n");

	if (init_control_messages(mc))
		mif_err("Failed to initialize control messages\n");

	/* 2cp dump WA */
	if (timer_pending(&mld->crash_ack_timer))
		del_timer(&mld->crash_ack_timer);
	atomic_set(&mld->forced_cp_crash, 0);

	mif_info("Set link mode to LINK_MODE_BOOT.\n");

	if (ld->link_prepare_normal_boot)
		ld->link_prepare_normal_boot(ld, mc->bootd);

	list_for_each_entry(iod, &mc->modem_state_notify_list, list) {
		if (iod && atomic_read(&iod->opened) > 0)
			iod->modem_state_changed(iod, STATE_BOOTING);
	}

	mif_info("Disable phone actvie interrupt.\n");
	mif_disable_irq(&mc->s5100_irq_phone_active);

	mif_gpio_set_value(mc->s5100_gpio_ap_status, 1, 0);
	mc->phone_state = STATE_BOOTING;

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
	reset_cp_upload_cnt();
#endif

	if (ld->link_start_normal_boot) {
		mif_info("link_start_normal_boot\n");
		ld->link_start_normal_boot(ld, mc->iod);
	}

	ret = check_cp_status(mc, 200);
	if (ret < 0) {
		mif_err("ERR! check_cp_status fail (err %d)\n", ret);
		return ret;
	}

	if (ld->register_pcie) {
		mif_info("register_pcie\n");
		ld->register_pcie(ld);
	}

	mif_info("---\n");
	return 0;
}

static int complete_normal_boot(struct modem_ctl *mc)
{
	int err = 0;
	unsigned long remain;
	struct io_device *iod;
#ifdef CONFIG_CP_LCD_NOTIFIER
	int __maybe_unused ret;
	struct modem_data __maybe_unused *modem = mc->mdm_data;
	struct mem_link_device __maybe_unused *mld = modem->mld;
#endif

	mif_info("+++\n");

	reinit_completion(&mc->init_cmpl);
	remain = wait_for_completion_timeout(&mc->init_cmpl, MIF_INIT_TIMEOUT);
	if (remain == 0) {
		mif_err("T-I-M-E-O-U-T\n");
		err = -EAGAIN;
		goto exit;
	}

	/* Enable L1.2 after CP boot */
	s51xx_pcie_l1ss_ctrl(1);

	/* Read cp_active before enabling irq */
	mif_gpio_get_value(mc->s5100_gpio_phone_active, true);

	err = register_phone_active_interrupt(mc);
	if (err)
		mif_err("Err: register_phone_active_interrupt:%d\n", err);
	mif_enable_irq(&mc->s5100_irq_phone_active);

	err = register_cp2ap_wakeup_interrupt(mc);
	if (err)
		mif_err("Err: register_cp2ap_wakeup_interrupt:%d\n", err);
	mif_enable_irq(&mc->s5100_irq_ap_wakeup);

	print_mc_state(mc);

	mc->device_reboot = false;

	list_for_each_entry(iod, &mc->modem_state_notify_list, list) {
		if (iod && atomic_read(&iod->opened) > 0)
			iod->modem_state_changed(iod, STATE_ONLINE);
	}

	print_mc_state(mc);

#if defined(CONFIG_CPIF_TP_MONITOR)
	tpmon_start(1);
#endif
#ifdef CONFIG_CP_LCD_NOTIFIER
	if (mc->lcd_notifier.notifier_call == NULL) {
		mif_info("Register lcd notifier\n");
		mc->lcd_notifier.notifier_call = s5100_lcd_notifier;
		ret = register_lcd_status_notifier(&mc->lcd_notifier);
		if (ret) {
			mif_err("failed to register LCD notifier");
			return ret;
		}
	}
#endif /* CONFIG_CP_LCD_NOTIFIER */

	mif_info("---\n");

exit:
	return err;
}

static int trigger_cp_crash(struct modem_ctl *mc)
{
	struct link_device *ld = get_current_link(mc->bootd);
	struct mem_link_device *mld = to_mem_link_device(ld);
	u32 crash_type;
#ifdef CONFIG_LINK_DEVICE_PCIE_GPIO_WA
	unsigned long flags;
#endif

	if (ld->crash_reason.type == CRASH_REASON_NONE)
		ld->crash_reason.type = CRASH_REASON_MIF_FORCED;
	crash_type = ld->crash_reason.type;

	mif_err("+++\n");

	if (mc->device_reboot) {
		mif_err("skip cp crash : device is rebooting..!!!\n");
		goto exit;
	}

	print_mc_state(mc);

	if (mif_gpio_get_value(mc->s5100_gpio_phone_active, true) == 1) {
#ifdef CONFIG_LINK_DEVICE_PCIE_GPIO_WA
		spin_lock_irqsave(&mc->gpio_toggle_lock, flags);
		if (mif_gpio_set_value(mc->s5100_gpio_cp_dump_noti, 1, 10))
			mif_gpio_toggle_value(mc->s5100_gpio_ap_status, 50);
		spin_unlock_irqrestore(&mc->gpio_toggle_lock, flags);
#else
		mif_gpio_set_value(mc->s5100_gpio_cp_dump_noti, 1, 0);
#endif
	} else {
		mif_err("do not need to set dump_noti\n");
	}

	if (ld->protocol == PROTOCOL_SIT &&
			crash_type == CRASH_REASON_RIL_TRIGGER_CP_CRASH)
		ld->link_trigger_cp_crash(mld, crash_type, ld->crash_reason.string);
	else
		ld->link_trigger_cp_crash(mld, crash_type, "Forced crash is called");

exit:
	mif_err("---\n");
	return 0;
}

int s5100_force_crash_exit_ext(void)
{
	if (g_mc)
		g_mc->ops.trigger_cp_crash(g_mc);

	return 0;
}

#if !(defined(CONFIG_SEC_MODEM_S5100) && defined(CONFIG_SEC_MODEM_S5000AP))
int modem_force_crash_exit_ext(void)
{
	return s5100_force_crash_exit_ext();
}
EXPORT_SYMBOL(modem_force_crash_exit_ext);
#endif

int s5100_send_panic_noti_ext(void)
{
	struct modem_data *modem;

	if (g_mc) {
		modem = g_mc->mdm_data;
		if (modem->mld) {
			mif_err("Send CMD_KERNEL_PANIC message to CP\n");
			send_ipc_irq(modem->mld, cmd2int(CMD_KERNEL_PANIC));
		}
	}

	return 0;
}

#if !(defined(CONFIG_SEC_MODEM_S5100) && defined(CONFIG_SEC_MODEM_S5000AP))
int modem_send_panic_noti_ext(void)
{
	return s5100_send_panic_noti_ext();
}
EXPORT_SYMBOL(modem_send_panic_noti_ext);
#endif

static int start_dump_boot(struct modem_ctl *mc)
{
	int err;
	struct link_device *ld = get_current_link(mc->bootd);
	mif_err("+++\n");

	/* Change phone state to CRASH_EXIT */
	mc->phone_state = STATE_CRASH_EXIT;

	if (!ld->link_start_dump_boot) {
		mif_err("%s: link_start_dump_boot is null\n", ld->name);
		return -EFAULT;
	}
	err = ld->link_start_dump_boot(ld, mc->bootd);
	if (err)
		return err;

	mif_gpio_set_value(mc->s5100_gpio_ap_status, 1, 0);

	err = check_cp_status(mc, 200);
	if (err < 0) {
		mif_err("ERR! check_cp_status fail (err %d)\n", err);
		return err;
	}

	/* do not handle cp2ap_wakeup irq during dump process */
	mif_disable_irq(&mc->s5100_irq_ap_wakeup);

	if (ld->register_pcie) {
		mif_info("register_pcie\n");
		ld->register_pcie(ld);
	}

	mif_err("---\n");
	return err;
}

int s5100_poweroff_pcie(struct modem_ctl *mc, bool force_off)
{
	struct link_device *ld = get_current_link(mc->iod);
	struct mem_link_device *mld = to_mem_link_device(ld);
	unsigned long flags;

	mutex_lock(&mc->pcie_onoff_lock);
	mutex_lock(&mc->pcie_check_lock);
	mif_info("+++\n");

	if (!mc->pcie_powered_on &&
			(s51xx_check_pcie_link_status(mc->pcie_ch_num) == 0)) {
		mif_err("skip pci power off : already powered off\n");
		goto exit;
	}

	/* CP reads Tx RP (or tail) after CP2AP_WAKEUP = 1.
	 * skip pci power off if CP2AP_WAKEUP = 1 or Tx pending.
	 */
	if (!force_off) {
		spin_lock_irqsave(&mc->pcie_tx_lock, flags);
		/* wait Tx done if it is running */
		spin_unlock_irqrestore(&mc->pcie_tx_lock, flags);
		msleep(30);
		if (check_mem_link_tx_pending(mld) ||
			mif_gpio_get_value(mc->s5100_gpio_ap_wakeup, true) == 1) {
			mif_err("skip pci power off : condition not met\n");
			goto exit;
		}
	}

	if (mld->msi_irq_base_enabled == 1) {
		disable_irq(mld->msi_irq_base);
		mld->msi_irq_base_enabled = 0;
	}

	if (mc->device_reboot) {
		mif_err("skip pci power off : device is rebooting..!!!\n");
		goto exit;
	}

	mc->pcie_powered_on = false;

	if (mc->s51xx_pdev != NULL && (mc->phone_state == STATE_ONLINE ||
				mc->phone_state == STATE_BOOTING)) {
		mif_info("save s5100_status - phone_state:%d\n",
				mc->phone_state);
		s51xx_pcie_save_state(mc->s51xx_pdev);
	} else
		mif_info("ignore save_s5100_status - phone_state:%d\n",
				mc->phone_state);

	mif_gpio_set_value(mc->s5100_gpio_cp_wakeup, 0, 5);
	print_mc_state(mc);

	exynos_pcie_host_v1_poweroff(mc->pcie_ch_num);

	if (wake_lock_active(&mc->mc_wake_lock))
		wake_unlock(&mc->mc_wake_lock);

exit:
	mif_info("---\n");
	mutex_unlock(&mc->pcie_check_lock);
	mutex_unlock(&mc->pcie_onoff_lock);

	spin_lock_irqsave(&mc->pcie_tx_lock, flags);
	if ((mc->s51xx_pdev != NULL) && !mc->device_reboot && mc->reserve_doorbell_int) {
		mif_info("DBG: doorbell_reserved = %d\n", mc->reserve_doorbell_int);
		if (mc->pcie_powered_on) {
			mc->reserve_doorbell_int = false;
			if (s51xx_pcie_send_doorbell_int(mc->s51xx_pdev, mld->intval_ap2cp_msg) != 0)
				s5100_force_crash_exit_ext();
		} else
			s5100_try_gpio_cp_wakeup(mc);
	}
	spin_unlock_irqrestore(&mc->pcie_tx_lock, flags);

	return 0;
}

int s5100_poweron_pcie(struct modem_ctl *mc)
{
	struct link_device *ld;
	struct mem_link_device *mld;
	unsigned long flags;
#if defined(CONFIG_LINK_DEVICE_PCIE_S2MPU)
	int ret;
	u32 cp_num;
	u32 shmem_idx;
#endif

	if (mc == NULL) {
		mif_info("Skip pci power on : mc is NULL\n");
		return 0;
	}

	ld = get_current_link(mc->iod);
	mld = to_mem_link_device(ld);

	if (mc->phone_state == STATE_OFFLINE) {
		mif_info("Skip pci power on : phone_state is OFFLINE\n");
		return 0;
	}

	mutex_lock(&mc->pcie_onoff_lock);
	mutex_lock(&mc->pcie_check_lock);
	mif_info("+++\n");
	if (mc->pcie_powered_on &&
			(s51xx_check_pcie_link_status(mc->pcie_ch_num) != 0)) {
		mif_err("skip pci power on : already powered on\n");
		goto exit;
	}

	if (mif_gpio_get_value(mc->s5100_gpio_ap_wakeup, true) == 0) {
		mif_err("skip pci power on : condition not met\n");
		goto exit;
	}

	if (mc->device_reboot) {
		mif_err("skip pci power on : device is rebooting..!!!\n");
		goto exit;
	}

	if (!wake_lock_active(&mc->mc_wake_lock))
		wake_lock(&mc->mc_wake_lock);

	mif_gpio_set_value(mc->s5100_gpio_cp_wakeup, 1, 5);
	print_mc_state(mc);

	if (exynos_pcie_host_v1_poweron(mc->pcie_ch_num) != 0)
		goto exit;

	mc->pcie_powered_on = true;

#if defined(CONFIG_LINK_DEVICE_PCIE_S2MPU)
	if (!mc->s5100_s2mpu_enabled) {
		mc->s5100_s2mpu_enabled = true;
		cp_num = ld->mdm_data->cp_num;

		for (shmem_idx = 0 ; shmem_idx < MAX_CP_SHMEM ; shmem_idx++) {
			if (shmem_idx == SHMEM_MSI)
				continue;

			if (cp_shmem_get_base(cp_num, shmem_idx)) {
				ret = (int) exynos_set_dev_stage2_ap("hsi2", 0,
					cp_shmem_get_base(cp_num, shmem_idx),
					cp_shmem_get_size(cp_num, shmem_idx), ATTR_RW);
				mif_info("pcie s2mpu idx:%d - addr:0x%08lx size:0x%08x ret:%d\n",
					shmem_idx,
					cp_shmem_get_base(cp_num, shmem_idx),
					cp_shmem_get_size(cp_num, shmem_idx), ret);
			}
		}
	}
#endif

	if (mc->s51xx_pdev != NULL) {
		s51xx_pcie_restore_state(mc->s51xx_pdev);

		/* DBG: check MSI sfr setting values */
		print_msi_register(mc->s51xx_pdev);
	} else {
		mif_err("DBG: MSI sfr not set up, yet(s5100_pdev is NULL)");
	}

	if (mld->msi_irq_base_enabled == 0) {
		enable_irq(mld->msi_irq_base);
		mld->msi_irq_base_enabled = 1;
	}

	if ((mc->s51xx_pdev != NULL) && mc->pcie_registered) {
		/* DBG */
		mif_info("DBG: doorbell: pcie_registered = %d\n", mc->pcie_registered);
		if (s51xx_pcie_send_doorbell_int(mc->s51xx_pdev, mc->int_pcie_link_ack) != 0) {
			/* DBG */
			mif_err("DBG: s5100pcie_send_doorbell_int() func. is failed !!!\n");
			s5100_force_crash_exit_ext();
		}
	}

#if defined(CONFIG_SUSPEND_DURING_VOICE_CALL)
	if (mc->pcie_voice_call_on && (mc->phone_state != STATE_CRASH_EXIT)) {
		if (wake_lock_active(&mc->mc_wake_lock))
			wake_unlock(&mc->mc_wake_lock);

		mif_info("wakelock active = %d, voice status = %d\n",
			wake_lock_active(&mc->mc_wake_lock), mc->pcie_voice_call_on);
	}
#endif

exit:
	mif_info("---\n");
	mutex_unlock(&mc->pcie_check_lock);
	mutex_unlock(&mc->pcie_onoff_lock);

	spin_lock_irqsave(&mc->pcie_tx_lock, flags);
	if ((mc->s51xx_pdev != NULL) && mc->pcie_powered_on && mc->reserve_doorbell_int) {
		mif_info("DBG: doorbell: doorbell_reserved = %d\n", mc->reserve_doorbell_int);
		mc->reserve_doorbell_int = false;
		if (s51xx_pcie_send_doorbell_int(mc->s51xx_pdev, mld->intval_ap2cp_msg) != 0)
			s5100_force_crash_exit_ext();
	}
	spin_unlock_irqrestore(&mc->pcie_tx_lock, flags);

	return 0;
}

int s5100_set_outbound_atu(struct modem_ctl *mc, struct cp_btl *btl, loff_t *pos, u32 map_size)
{
	int ret = 0;
	u32 atu_grp = (*pos) / map_size;

	if (atu_grp != btl->last_pcie_atu_grp) {
		ret = exynos_pcie_rc_set_outbound_atu(
			mc->pcie_ch_num, btl->mem.cp_p_base, (atu_grp * map_size), map_size);
		btl->last_pcie_atu_grp = atu_grp;
	}

	return ret;
}

static int suspend_cp(struct modem_ctl *mc)
{
	if (!mc)
		return 0;

	do {
#if defined(CONFIG_SUSPEND_DURING_VOICE_CALL)
		if (mc->pcie_voice_call_on)
			break;
#endif

		if (mif_gpio_get_value(mc->s5100_gpio_ap_wakeup, true) == 1) {
			mif_err("abort suspend");
			return -EBUSY;
		}
	} while (0);

#ifndef CONFIG_CP_LCD_NOTIFIER
	modem_ctrl_set_kerneltime(mc);
	mif_gpio_set_value(mc->s5100_gpio_ap_status, 0, 0);
	mif_gpio_get_value(mc->s5100_gpio_ap_status, true);
#endif

	return 0;
}

static int resume_cp(struct modem_ctl *mc)
{
	if (!mc)
		return 0;

#ifndef CONFIG_CP_LCD_NOTIFIER
	modem_ctrl_set_kerneltime(mc);
	mif_gpio_set_value(mc->s5100_gpio_ap_status, 1, 0);
	mif_gpio_get_value(mc->s5100_gpio_ap_status, true);
#endif
	return 0;
}

static int s5100_pm_notifier(struct notifier_block *notifier,
				       unsigned long pm_event, void *v)
{
	struct modem_ctl *mc;
	unsigned long flags;
	int gpio_val;

	mc = container_of(notifier, struct modem_ctl, pm_notifier);

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		mif_info("Suspend prepare\n");

		spin_lock_irqsave(&mc->pcie_pm_lock, flags);
		mc->pcie_pm_suspended = true;
		spin_unlock_irqrestore(&mc->pcie_pm_lock, flags);
		break;
	case PM_POST_SUSPEND:
		mif_info("Resume done\n");

		spin_lock_irqsave(&mc->pcie_pm_lock, flags);
		mc->pcie_pm_suspended = false;
		if (mc->pcie_pm_resume_wait) {
			mc->pcie_pm_resume_wait = false;
			gpio_val = mc->pcie_pm_resume_gpio_val;

			mif_err("cp2ap_wakeup work resume. gpio_val : %d\n", gpio_val);

			mc->apwake_irq_chip->irq_set_type(
				irq_get_irq_data(mc->s5100_irq_ap_wakeup.num),
				(gpio_val == 1 ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH));
			mif_enable_irq(&mc->s5100_irq_ap_wakeup);

			queue_work_on(RUNTIME_PM_AFFINITY_CORE, mc->wakeup_wq,
				(gpio_val == 1 ? &mc->wakeup_work : &mc->suspend_work));
		}
		spin_unlock_irqrestore(&mc->pcie_pm_lock, flags);
		break;
	default:
		mif_info("pm_event %lu\n", pm_event);
		break;
	}

	return NOTIFY_OK;
}

int s5100_try_gpio_cp_wakeup(struct modem_ctl *mc)
{
	if ((mif_gpio_get_value(mc->s5100_gpio_cp_wakeup, false) == 0) &&
			(mif_gpio_get_value(mc->s5100_gpio_ap_wakeup, false) == 0) &&
			(s51xx_check_pcie_link_status(mc->pcie_ch_num) == 0)) {
		mif_gpio_set_value(mc->s5100_gpio_cp_wakeup, 1, 0);
		return 0;
	}
	return -EPERM;
}

static void s5100_get_ops(struct modem_ctl *mc)
{
	mc->ops.power_on = power_on_cp;
	mc->ops.power_off = power_off_cp;
	mc->ops.power_shutdown = power_shutdown_cp;
	mc->ops.power_reset = power_reset_cp;
	mc->ops.power_reset_dump = power_reset_dump_cp;

	mc->ops.start_normal_boot = start_normal_boot;
	mc->ops.complete_normal_boot = complete_normal_boot;

	mc->ops.start_dump_boot = start_dump_boot;
	mc->ops.trigger_cp_crash = trigger_cp_crash;

	mc->ops.suspend = suspend_cp;
	mc->ops.resume = resume_cp;
}

static void s5100_get_pdata(struct modem_ctl *mc, struct modem_data *pdata)
{
	struct platform_device *pdev = to_platform_device(mc->dev);
	struct device_node *np = pdev->dev.of_node;
	int ret = 0;

	/* CP Power */
	mc->s5100_gpio_cp_pwr = of_get_named_gpio(np, "gpio_ap2cp_cp_pwr_on", 0);
	if (mc->s5100_gpio_cp_pwr < 0) {
		mif_err("Can't Get s5100_gpio_cp_pwr!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_cp_pwr, "AP2CP_CP_PWR_ON");
	gpio_direction_output(mc->s5100_gpio_cp_pwr, 0);

	/* CP Reset */
	mc->s5100_gpio_cp_reset = of_get_named_gpio(np, "gpio_ap2cp_nreset_n", 0);
	if (mc->s5100_gpio_cp_reset < 0) {
		mif_err("Can't Get gpio_cp_nreset_n!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_cp_reset, "AP2CP_NRESET_N");
	gpio_direction_output(mc->s5100_gpio_cp_reset, 0);

	/* CP PS HOLD */
	mc->s5100_gpio_cp_ps_hold = of_get_named_gpio(np, "gpio_cp2ap_cp_ps_hold", 0);
	if (mc->s5100_gpio_cp_ps_hold < 0) {
		mif_err("Can't Get s5100_gpio_cp_ps_hold!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_cp_ps_hold, "CP2AP_CP_PS_HOLD");
	gpio_direction_input(mc->s5100_gpio_cp_ps_hold);

	/* AP2CP WAKE UP */
	mc->s5100_gpio_cp_wakeup = of_get_named_gpio(np, "gpio_ap2cp_wake_up", 0);
	if (mc->s5100_gpio_cp_wakeup < 0) {
		mif_err("Can't Get s5100_gpio_cp_wakeup!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_cp_wakeup, "AP2CP_WAKE_UP");
	gpio_direction_output(mc->s5100_gpio_cp_wakeup, 0);

	/* CP2AP WAKE UP */
	mc->s5100_gpio_ap_wakeup = of_get_named_gpio(np, "gpio_cp2ap_wake_up", 0);
	if (mc->s5100_gpio_ap_wakeup < 0) {
		mif_err("Can't Get gpio_cp2ap_wake_up!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_ap_wakeup, "CP2AP_WAKE_UP");
	mc->s5100_irq_ap_wakeup.num = gpio_to_irq(mc->s5100_gpio_ap_wakeup);

	/* DUMP NOTI */
	mc->s5100_gpio_cp_dump_noti = of_get_named_gpio(np, "gpio_ap2cp_dump_noti", 0);
	if (mc->s5100_gpio_cp_dump_noti < 0) {
		mif_err("Can't Get gpio_ap2cp_dump_noti!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_cp_dump_noti, "AP2CP_DUMP_NOTI");
	gpio_direction_output(mc->s5100_gpio_cp_dump_noti, 0);

	/* PDA ACTIVE */
	mc->s5100_gpio_ap_status = of_get_named_gpio(np, "gpio_ap2cp_pda_active", 0);
	if (mc->s5100_gpio_ap_status < 0) {
		mif_err("Can't Get s5100_gpio_ap_status!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_ap_status, "AP2CP_PDA_ACTIVE");
	gpio_direction_output(mc->s5100_gpio_ap_status, 0);

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
	/* 2CP UART SEL */
	mc->s5100_gpio_2cp_uart_sel = of_get_named_gpio(np, "gpio_2cp_uart_sel", 0);
	if (mc->s5100_gpio_2cp_uart_sel < 0) {
		mif_err("Can't Get s5100_gpio_2cp_uart_sel!\n");
	} else {
		gpio_request(mc->s5100_gpio_2cp_uart_sel, "AP2CP_UART_SEL");
		gpio_direction_output(mc->s5100_gpio_2cp_uart_sel, 0);
	}
#endif

	/* PHONE ACTIVE */
	mc->s5100_gpio_phone_active = of_get_named_gpio(np, "gpio_cp2ap_phone_active", 0);
	if (mc->s5100_gpio_phone_active < 0) {
		mif_err("Can't Get s5100_gpio_phone_active!\n");
		return;
	}
	gpio_request(mc->s5100_gpio_phone_active, "CP2AP_PHONE_ACTIVE");
	mc->s5100_irq_phone_active.num = gpio_to_irq(mc->s5100_gpio_phone_active);

	ret = of_property_read_u32(np, "mif,int_ap2cp_pcie_link_ack",
				&mc->int_pcie_link_ack);
	if (ret) {
		mif_err("Can't Get PCIe Link ACK interrupt number!!!\n");
		return;
	}
	mc->int_pcie_link_ack += DOORBELL_INT_ADD;

	/* Get PCIe Channel Number */
	ret = of_property_read_u32(np, "pci_ch_num",
				&mc->pcie_ch_num);
	if (ret) {
		mif_err("Can't Get PCIe channel!!!\n");
		return;
	}
	mif_info("S5100 PCIe Channel Number : %d\n", mc->pcie_ch_num);

	mc->sbi_crash_type_mask = pdata->sbi_crash_type_mask;
	mc->sbi_crash_type_pos = pdata->sbi_crash_type_pos;

	mc->sbi_ds_det_mask = pdata->sbi_ds_det_mask;
	mc->sbi_ds_det_pos = pdata->sbi_ds_det_pos;
}

#ifdef CONFIG_EXYNOS_BUSMONITOR
static int s5100_busmon_notifier(struct notifier_block *nb,
						unsigned long event, void *data)
{
	struct busmon_notifier *info = (struct busmon_notifier *)data;
	char *init_desc = info->init_desc;

	if (init_desc != NULL &&
		(strncmp(init_desc, "CP", strlen(init_desc)) == 0 ||
		strncmp(init_desc, "APB_CORE_CP", strlen(init_desc)) == 0 ||
		strncmp(init_desc, "MIF_CP", strlen(init_desc)) == 0)) {
		struct modem_ctl *mc =
			container_of(nb, struct modem_ctl, busmon_nfb);

		mc->ops.trigger_cp_crash(mc);
	}
	return 0;
}
#endif

#if defined(CONFIG_SUSPEND_DURING_VOICE_CALL)
static int s5100_abox_call_state_notifier(struct notifier_block *nb,
		unsigned long action, void *nb_data)
{
	struct modem_ctl *mc = container_of(nb, struct modem_ctl, abox_call_state_nb);

	mif_info("call event = %lu\n", action);

	switch (action) {
	case ABOX_CALL_EVENT_OFF:
		mc->pcie_voice_call_on = false;
		queue_work_on(RUNTIME_PM_AFFINITY_CORE, mc->wakeup_wq,
			&mc->call_off_work);
		break;
	case ABOX_CALL_EVENT_ON:
		mc->pcie_voice_call_on = true;
		queue_work_on(RUNTIME_PM_AFFINITY_CORE, mc->wakeup_wq,
			&mc->call_on_work);
		break;
	default:
		mif_err("undefined call event = %lu\n", action);
		break;
	}

	return NOTIFY_DONE;
}
#endif

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
static int s5100_modem_notifier(struct notifier_block *nb,
		unsigned long action, void *nb_data)
{
	struct modem_ctl *mc = container_of(nb, struct modem_ctl, modem_nb);
	struct modem_ctl *origin_mc = nb_data;

	mif_info("action:%lu\n", action);

	switch (action) {
	case MODEM_EVENT_EXIT:
	case MODEM_EVENT_WATCHDOG:
		if (origin_mc != mc) {
			s5100_force_crash_exit_ext();
			break;
		}
		break;
	}

	return NOTIFY_DONE;
}
#endif

#ifdef CONFIG_CP_LCD_NOTIFIER
static int s5100_lcd_notifier(struct notifier_block *notifier,
		unsigned long event, void *v)
{
	struct modem_ctl *mc =
		container_of(notifier, struct modem_ctl, lcd_notifier);

	switch (event) {
	case LCD_OFF:
		mif_info("LCD_OFF Notification\n");
		modem_ctrl_set_kerneltime(mc);
		mif_gpio_set_value(mc->s5100_gpio_ap_status, 0, 0);
		mif_gpio_get_value(mc->s5100_gpio_ap_status, true);
		break;

	case LCD_ON:
		mif_info("LCD_ON Notification\n");
		modem_ctrl_set_kerneltime(mc);
		mif_gpio_set_value(mc->s5100_gpio_ap_status, 1, 0);
		mif_gpio_get_value(mc->s5100_gpio_ap_status, true);
		break;

	default:
		mif_info("lcd_event %ld\n", event);
		break;
	}

	return NOTIFY_OK;
}
#endif /* CONFIG_CP_LCD_NOTIFIER */

int s5100_init_modemctl_device(struct modem_ctl *mc, struct modem_data *pdata)
{
	int ret = 0;
	struct platform_device *pdev = to_platform_device(mc->dev);
	struct resource __maybe_unused *sysram_alive;

	mif_err("+++\n");

	g_mc = mc;

	s5100_get_ops(mc);
	s5100_get_pdata(mc, pdata);
	dev_set_drvdata(mc->dev, mc);

	wake_lock_init(&mc->mc_wake_lock, WAKE_LOCK_SUSPEND, "s5100_wake_lock");
	mutex_init(&mc->pcie_onoff_lock);
	mutex_init(&mc->pcie_check_lock);
	spin_lock_init(&mc->pcie_tx_lock);
	spin_lock_init(&mc->pcie_pm_lock);
#if defined(CONFIG_LINK_DEVICE_PCIE_GPIO_WA)
	spin_lock_init(&mc->gpio_toggle_lock);
#endif

	mif_gpio_set_value(mc->s5100_gpio_cp_reset, 0, 0);

	mif_err("Register GPIO interrupts\n");
	mc->apwake_irq_chip = irq_get_chip(mc->s5100_irq_ap_wakeup.num);
	if (mc->apwake_irq_chip == NULL) {
		mif_err("Can't get irq_chip structure!!!!\n");
		return -EINVAL;
	}

	mc->wakeup_wq = create_singlethread_workqueue("cp2ap_wakeup_wq");
	if (!mc->wakeup_wq) {
		mif_err("%s: ERR! fail to create wakeup_wq\n", mc->name);
		return -EINVAL;
	}

	INIT_WORK(&mc->wakeup_work, cp2ap_wakeup_work);
	INIT_WORK(&mc->suspend_work, cp2ap_suspend_work);

	mc->reboot_nb.notifier_call = s5100_reboot_handler;
	register_reboot_notifier(&mc->reboot_nb);

	/* Register PM notifier_call */
	mc->pm_notifier.notifier_call = s5100_pm_notifier;
	ret = register_pm_notifier(&mc->pm_notifier);
	if (ret) {
		mif_err("failed to register PM notifier_call\n");
		return ret;
	}

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
	mc->modem_nb.notifier_call = s5100_modem_notifier;
	register_modem_event_notifier(&mc->modem_nb);
#endif

#if defined(CONFIG_SUSPEND_DURING_VOICE_CALL)
	INIT_WORK(&mc->call_on_work, voice_call_on_work);
	INIT_WORK(&mc->call_off_work, voice_call_off_work);

	mc->abox_call_state_nb.notifier_call = s5100_abox_call_state_notifier;
	register_abox_call_event_notifier(&mc->abox_call_state_nb);
#endif

	if (sysfs_create_group(&pdev->dev.kobj, &sim_group))
		mif_err("failed to create sysfs node related sim\n");

	mif_err("---\n");

	return 0;
}
