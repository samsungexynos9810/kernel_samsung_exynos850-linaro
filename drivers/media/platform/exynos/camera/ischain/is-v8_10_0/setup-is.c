/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Exynos9630  gpio and clock configurations
 *
 * Copyright (c) 2019 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#include <exynos-is.h>
#include <is-config.h>

#if defined(ENABLE_CLOG_RESERVED_MEM)
#include "is-resourcemgr.h"
#endif

/*------------------------------------------------------*/
/*		Common control				*/
/*------------------------------------------------------*/

#define REGISTER_CLK(name) {name, NULL}

struct is_clk is_clk_list[] = {
	REGISTER_CLK("GATE_SIPU_IPP_QCH"),
	REGISTER_CLK("GATE_SIPU_IPP_QCH_C2_STAT"),
	REGISTER_CLK("GATE_SIPU_IPP_QCH_C2_YDS"),

	REGISTER_CLK("GATE_TNR_QCH"),
	REGISTER_CLK("GATE_DNS_QCH"),
	REGISTER_CLK("GATE_ITP_QCH"),
	REGISTER_CLK("GATE_ORBMCH_QCH_ACLK"),
	REGISTER_CLK("GATE_ORBMCH_QCH_C2CLK"),

	REGISTER_CLK("GATE_GDC_QCH"),
	REGISTER_CLK("GATE_MCSC_QCH"),
	REGISTER_CLK("GATE_MCSC_CMU_MCSC_QCH"),
	REGISTER_CLK("GATE_GDC_QCH_C2"),
	REGISTER_CLK("GATE_MCSC_QCH_C2"),
	REGISTER_CLK("GATE_C2AGENT_D0_MCSC_QCH"),
	REGISTER_CLK("GATE_C2AGENT_D1_MCSC_QCH"),
	REGISTER_CLK("GATE_C2AGENT_D2_MCSC_QCH"),

	REGISTER_CLK("GATE_VRA_QCH"),
	REGISTER_CLK("UMUX_CLKCMU_VRA_BUS_USER"),
	REGISTER_CLK("UMUX_CLKCMU_VRA_CLAHE_USER"),
	REGISTER_CLK("GATE_CLAHE_QCH"),
	REGISTER_CLK("DOUT_CLK_VRA_BUSP"),

	REGISTER_CLK("CIS_CLK0"),
	REGISTER_CLK("CIS_CLK1"),
	REGISTER_CLK("CIS_CLK2"),
	REGISTER_CLK("CIS_CLK3"),
	REGISTER_CLK("CIS_CLK4"),

	REGISTER_CLK("GATE_DFTMUX_TOP_QCH_CIS_CLK0"),
	REGISTER_CLK("GATE_DFTMUX_TOP_QCH_CIS_CLK1"),
	REGISTER_CLK("GATE_DFTMUX_TOP_QCH_CIS_CLK2"),
	REGISTER_CLK("GATE_DFTMUX_TOP_QCH_CIS_CLK3"),
	REGISTER_CLK("GATE_DFTMUX_TOP_QCH_CIS_CLK4"),

	REGISTER_CLK("GATE_CSIS_PDP_QCH_C2_CSIS"),
	REGISTER_CLK("GATE_CSIS_PDP_QCH_CSIS0"),
	REGISTER_CLK("GATE_CSIS_PDP_QCH_CSIS1"),
	REGISTER_CLK("GATE_CSIS_PDP_QCH_CSIS2"),
	REGISTER_CLK("GATE_CSIS_PDP_QCH_CSIS3"),
	REGISTER_CLK("GATE_CSIS_PDP_QCH_CSIS4"),
	REGISTER_CLK("GATE_CSIS_PDP_QCH_CSIS_DMA"),
	REGISTER_CLK("GATE_CSIS_PDP_QCH_PDP_TOP"),
};


int is_set_rate(struct device *dev,
	const char *name, ulong frequency)
{
	int ret = 0;
	size_t index;
	struct clk *clk = NULL;

	for (index = 0; index < ARRAY_SIZE(is_clk_list); index++) {
		if (!strcmp(name, is_clk_list[index].name))
			clk = is_clk_list[index].clk;
	}

	if (IS_ERR_OR_NULL(clk)) {
		pr_err("[@][ERR] %s: clk_target_list is NULL : %s\n", __func__, name);
		return -EINVAL;
	}

	ret = clk_set_rate(clk, frequency);
	if (ret) {
		pr_err("[@][ERR] %s: clk_set_rate is fail(%s)(ret: %d)\n", __func__, name, ret);
		return ret;
	}

	/* is_get_rate_dt(dev, name); */

	return ret;
}

/* utility function to get rate with DT */
ulong is_get_rate(struct device *dev,
	const char *name)
{
	ulong frequency;
	size_t index;
	struct clk *clk = NULL;

	for (index = 0; index < ARRAY_SIZE(is_clk_list); index++) {
		if (!strcmp(name, is_clk_list[index].name))
			clk = is_clk_list[index].clk;
	}

	if (IS_ERR_OR_NULL(clk)) {
		pr_err("[@][ERR] %s: clk_target_list is NULL : %s\n", __func__, name);
		return 0;
	}

	frequency = clk_get_rate(clk);

	pr_info("[@] %s : %ldMhz (enable_count : %d)\n", name, frequency/1000000, __clk_get_enable_count(clk));

	return frequency;
}

/* utility function to eable with DT */
int  is_enable(struct device *dev,
	const char *name)
{
	int ret = 0;
	size_t index;
	struct clk *clk = NULL;

	for (index = 0; index < ARRAY_SIZE(is_clk_list); index++) {
		if (!strcmp(name, is_clk_list[index].name))
			clk = is_clk_list[index].clk;
	}

	if (IS_ERR_OR_NULL(clk)) {
		pr_err("[@][ERR] %s: clk_target_list is NULL : %s\n", __func__, name);
		return -EINVAL;
	}

	if (debug_clk > 1)
		pr_info("[@][ENABLE] %s : (enable_count : %d)\n", name, __clk_get_enable_count(clk));

	ret = clk_prepare_enable(clk);
	if (ret) {
		pr_err("[@][ERR] %s: clk_prepare_enable is fail(%s)(ret: %d)\n", __func__, name, ret);
		return ret;
	}

	return ret;
}

/* utility function to disable with DT */
int is_disable(struct device *dev,
	const char *name)
{
	size_t index;
	struct clk *clk = NULL;

	for (index = 0; index < ARRAY_SIZE(is_clk_list); index++) {
		if (!strcmp(name, is_clk_list[index].name))
			clk = is_clk_list[index].clk;
	}

	if (IS_ERR_OR_NULL(clk)) {
		pr_err("[@][ERR] %s: clk_target_list is NULL : %s\n", __func__, name);
		return -EINVAL;
	}

	clk_disable_unprepare(clk);

	if (debug_clk > 1)
		pr_info("[@][DISABLE] %s : (enable_count : %d)\n", name, __clk_get_enable_count(clk));

	return 0;
}

#ifdef CONFIG_SOC_EXYNOS9820
int is_enabled_clk_disable(struct device *dev, const char *name)
{
	int i;
	struct clk *clk = NULL;

	for (i = 0; i < ARRAY_SIZE(is_clk_list); i++) {
		if (!strcmp(name, is_clk_list[i].name))
			clk = is_clk_list[i].clk;
	}

	if (IS_ERR_OR_NULL(clk)) {
		pr_err("%s: failed to find clk: %s\n", __func__, name);
		return -EINVAL;
	}

	if (__clk_get_enable_count(clk)) {
		pr_err("%s: abnormal clock state is detected: %s, %d\n",
				__func__, name,  __clk_get_enable_count(clk));
		clk_disable_unprepare(clk);
	}

	return 0;
}
#endif

/* utility function to set parent with DT */
int is_set_parent_dt(struct device *dev,
	const char *child, const char *parent)
{
	int ret = 0;
	struct clk *p;
	struct clk *c;

	p = clk_get(dev, parent);
	if (IS_ERR_OR_NULL(p)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, parent);
		return -EINVAL;
	}

	c = clk_get(dev, child);
	if (IS_ERR_OR_NULL(c)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, child);
		return -EINVAL;
	}

	ret = clk_set_parent(c, p);
	if (ret) {
		pr_err("%s: clk_set_parent is fail(%s -> %s)(ret: %d)\n", __func__, child, parent, ret);
		return ret;
	}

	return 0;
}

/* utility function to set rate with DT */
int is_set_rate_dt(struct device *dev,
	const char *conid, unsigned int rate)
{
	int ret = 0;
	struct clk *target;

	target = clk_get(dev, conid);
	if (IS_ERR_OR_NULL(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return -EINVAL;
	}

	ret = clk_set_rate(target, rate);
	if (ret) {
		pr_err("%s: clk_set_rate is fail(%s)(ret: %d)\n", __func__, conid, ret);
		return ret;
	}

	/* is_get_rate_dt(dev, conid); */

	return 0;
}

/* utility function to get rate with DT */
ulong is_get_rate_dt(struct device *dev,
	const char *conid)
{
	struct clk *target;
	ulong rate_target;

	target = clk_get(dev, conid);
	if (IS_ERR_OR_NULL(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return 0;
	}

	rate_target = clk_get_rate(target);
	pr_info("[@] %s : %ldMhz\n", conid, rate_target/1000000);

	return rate_target;
}

/* utility function to eable with DT */
int is_enable_dt(struct device *dev,
	const char *conid)
{
	int ret;
	struct clk *target;

	target = clk_get(dev, conid);
	if (IS_ERR_OR_NULL(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return -EINVAL;
	}

	ret = clk_prepare(target);
	if (ret) {
		pr_err("%s: clk_prepare is fail(%s)(ret: %d)\n", __func__, conid, ret);
		return ret;
	}

	ret = clk_enable(target);
	if (ret) {
		pr_err("%s: clk_enable is fail(%s)(ret: %d)\n", __func__, conid, ret);
		return ret;
	}

	return 0;
}

/* utility function to disable with DT */
int is_disable_dt(struct device *dev,
	const char *conid)
{
	struct clk *target;

	target = clk_get(dev, conid);
	if (IS_ERR_OR_NULL(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return -EINVAL;
	}

	clk_disable(target);
	clk_unprepare(target);

	return 0;
}

static void exynos9630_is_print_clk_reg(void)
{

}

static int exynos9630_is_print_clk(struct device *dev)
{
	pr_info("FIMC-IS CLOCK DUMP\n");

	is_get_rate_dt(dev, "GATE_SIPU_IPP_QCH");
	is_get_rate_dt(dev, "GATE_SIPU_IPP_QCH_C2_STAT");
	is_get_rate_dt(dev, "GATE_SIPU_IPP_QCH_C2_YDS");

	is_get_rate_dt(dev, "GATE_TNR_QCH");
	is_get_rate_dt(dev, "GATE_DNS_QCH");
	is_get_rate_dt(dev, "GATE_ITP_QCH");
	is_get_rate_dt(dev, "GATE_ORBMCH_QCH_ACLK");
	is_get_rate_dt(dev, "GATE_ORBMCH_QCH_C2CLK");

	is_get_rate_dt(dev, "GATE_GDC_QCH");
	is_get_rate_dt(dev, "GATE_MCSC_QCH");
	is_get_rate_dt(dev, "GATE_MCSC_CMU_MCSC_QCH");
	is_get_rate_dt(dev, "GATE_GDC_QCH_C2");
	is_get_rate_dt(dev, "GATE_MCSC_QCH_C2");
	is_get_rate_dt(dev, "GATE_C2AGENT_D0_MCSC_QCH");
	is_get_rate_dt(dev, "GATE_C2AGENT_D1_MCSC_QCH");
	is_get_rate_dt(dev, "GATE_C2AGENT_D2_MCSC_QCH");

	is_get_rate_dt(dev, "GATE_VRA_QCH");
	is_get_rate_dt(dev, "UMUX_CLKCMU_VRA_BUS_USER");
	is_get_rate_dt(dev, "UMUX_CLKCMU_VRA_CLAHE_USER");
	is_get_rate_dt(dev, "GATE_CLAHE_QCH");
	is_get_rate_dt(dev, "DOUT_CLK_VRA_BUSP");

	is_get_rate_dt(dev, "CIS_CLK0");
	is_get_rate_dt(dev, "CIS_CLK1");
	is_get_rate_dt(dev, "CIS_CLK2");
	is_get_rate_dt(dev, "CIS_CLK3");
	is_get_rate_dt(dev, "CIS_CLK4");

	is_get_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK0");
	is_get_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK1");
	is_get_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK2");
	is_get_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK3");
	is_get_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK4");

	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_C2_CSIS");
	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS0");
	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS1");
	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS2");
	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS3");
	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS4");
	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS_DMA");
	is_get_rate_dt(dev, "GATE_CSIS_PDP_QCH_PDP_TOP");

	exynos9630_is_print_clk_reg();

	return 0;
}

#if defined(ENABLE_CLOG_RESERVED_MEM)
/* utility function to get rate with DT */
ulong is_dump_rate_dt(struct device *dev,
	const char *conid)
{
	struct clk *target;
	ulong rate_target;

	target = clk_get(dev, conid);
	if (IS_ERR_OR_NULL(target)) {
		pr_err("%s: could not lookup clock : %s\n", __func__, conid);
		return -EINVAL;
	}

	rate_target = clk_get_rate(target);
	cinfo("[@] %s : %ldMhz\n", conid, rate_target/1000000);
	return rate_target;
}

int exynos9630_is_dump_clk(struct device *dev)
{

	cinfo("### FIMC-IS CLOCK DUMP ###\n");

	is_dump_rate_dt(dev, "GATE_SIPU_IPP_QCH");
	is_dump_rate_dt(dev, "GATE_SIPU_IPP_QCH_C2_STAT");
	is_dump_rate_dt(dev, "GATE_SIPU_IPP_QCH_C2_YDS");

	is_dump_rate_dt(dev, "GATE_TNR_QCH");
	is_dump_rate_dt(dev, "GATE_DNS_QCH");
	is_dump_rate_dt(dev, "GATE_ITP_QCH");
	is_dump_rate_dt(dev, "GATE_ORBMCH_QCH_ACLK");
	is_dump_rate_dt(dev, "GATE_ORBMCH_QCH_C2CLK");

	is_dump_rate_dt(dev, "GATE_GDC_QCH");
	is_dump_rate_dt(dev, "GATE_MCSC_QCH");
	is_dump_rate_dt(dev, "GATE_MCSC_CMU_MCSC_QCH");
	is_dump_rate_dt(dev, "GATE_GDC_QCH_C2");
	is_dump_rate_dt(dev, "GATE_MCSC_QCH_C2");
	is_dump_rate_dt(dev, "GATE_C2AGENT_D0_MCSC_QCH");
	is_dump_rate_dt(dev, "GATE_C2AGENT_D1_MCSC_QCH");
	is_dump_rate_dt(dev, "GATE_C2AGENT_D2_MCSC_QCH");

	is_dump_rate_dt(dev, "GATE_VRA_QCH");
	is_dump_rate_dt(dev, "UMUX_CLKCMU_VRA_BUS_USER");
	is_dump_rate_dt(dev, "UMUX_CLKCMU_VRA_CLAHE_USER");
	is_dump_rate_dt(dev, "GATE_CLAHE_QCH");
	is_dump_rate_dt(dev, "DOUT_CLK_VRA_BUSP");

	is_dump_rate_dt(dev, "CIS_CLK0");
	is_dump_rate_dt(dev, "CIS_CLK1");
	is_dump_rate_dt(dev, "CIS_CLK2");
	is_dump_rate_dt(dev, "CIS_CLK3");
	is_dump_rate_dt(dev, "CIS_CLK4");

	is_dump_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK0");
	is_dump_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK1");
	is_dump_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK2");
	is_dump_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK3");
	is_dump_rate_dt(dev, "GATE_DFTMUX_TOP_QCH_CIS_CLK4");

	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_C2_CSIS");
	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS0");
	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS1");
	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS2");
	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS3");
	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS4");
	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_CSIS_DMA");
	is_dump_rate_dt(dev, "GATE_CSIS_PDP_QCH_PDP_TOP");

	return 0;
}
#endif

int exynos9630_is_clk_gate(u32 clk_gate_id, bool is_on)
{
	return 0;
}

int exynos9630_is_uart_gate(struct device *dev, bool mask)
{
	return 0;
}

int exynos9630_is_get_clk(struct device *dev)
{
	struct clk *clk;
	size_t i;

	for (i = 0; i < ARRAY_SIZE(is_clk_list); i++) {
		clk = devm_clk_get(dev, is_clk_list[i].name);
		if (IS_ERR_OR_NULL(clk)) {
			pr_err("[@][ERR] %s: could not lookup clock : %s\n",
				__func__, is_clk_list[i].name);
			return -EINVAL;
		}

		is_clk_list[i].clk = clk;
	}

	return 0;
}

int exynos9630_is_cfg_clk(struct device *dev)
{
	return 0;
}

int exynos9630_is_clk_on(struct device *dev)
{
	int ret = 0;
	struct exynos_platform_is *pdata;

	pdata = dev_get_platdata(dev);
	if (pdata->clock_on) {
		ret = pdata->clk_off(dev);
		if (ret) {
			pr_err("clk_off is fail(%d)\n", ret);
			goto p_err;
		}
	}
	is_enable(dev, "GATE_SIPU_IPP_QCH");
	is_enable(dev, "GATE_SIPU_IPP_QCH_C2_STAT");
	is_enable(dev, "GATE_SIPU_IPP_QCH_C2_YDS");

	is_enable(dev, "GATE_TNR_QCH");
	is_enable(dev, "GATE_DNS_QCH");
	is_enable(dev, "GATE_ITP_QCH");
	is_enable(dev, "GATE_ORBMCH_QCH_ACLK");
	is_enable(dev, "GATE_ORBMCH_QCH_C2CLK");

	is_enable(dev, "GATE_GDC_QCH");
	is_enable(dev, "GATE_MCSC_QCH");
	is_enable(dev, "GATE_MCSC_CMU_MCSC_QCH");
	is_enable(dev, "GATE_GDC_QCH_C2");
	is_enable(dev, "GATE_MCSC_QCH_C2");
	is_enable(dev, "GATE_C2AGENT_D0_MCSC_QCH");
	is_enable(dev, "GATE_C2AGENT_D1_MCSC_QCH");
	is_enable(dev, "GATE_C2AGENT_D2_MCSC_QCH");

	is_enable(dev, "GATE_VRA_QCH");
	is_enable(dev, "UMUX_CLKCMU_VRA_BUS_USER");
	is_enable(dev, "UMUX_CLKCMU_VRA_CLAHE_USER");
	is_enable(dev, "GATE_CLAHE_QCH");
	is_enable(dev, "DOUT_CLK_VRA_BUSP");

	if (debug_clk > 1)
		exynos9630_is_print_clk(dev);

	pdata->clock_on = true;

p_err:
	return 0;
}

int exynos9630_is_clk_off(struct device *dev)
{
	int ret = 0;
	struct exynos_platform_is *pdata;

	pdata = dev_get_platdata(dev);
	if (!pdata->clock_on) {
		pr_err("clk_off is fail(already off)\n");
		ret = -EINVAL;
		goto p_err;
	}

	is_disable(dev, "GATE_SIPU_IPP_QCH");
	is_disable(dev, "GATE_SIPU_IPP_QCH_C2_STAT");
	is_disable(dev, "GATE_SIPU_IPP_QCH_C2_YDS");

	is_disable(dev, "GATE_TNR_QCH");
	is_disable(dev, "GATE_DNS_QCH");
	is_disable(dev, "GATE_ITP_QCH");
	is_disable(dev, "GATE_ORBMCH_QCH_ACLK");
	is_disable(dev, "GATE_ORBMCH_QCH_C2CLK");

	is_disable(dev, "GATE_GDC_QCH");
	is_disable(dev, "GATE_MCSC_QCH");
	is_disable(dev, "GATE_MCSC_CMU_MCSC_QCH");
	is_disable(dev, "GATE_GDC_QCH_C2");
	is_disable(dev, "GATE_MCSC_QCH_C2");
	is_disable(dev, "GATE_C2AGENT_D0_MCSC_QCH");
	is_disable(dev, "GATE_C2AGENT_D1_MCSC_QCH");
	is_disable(dev, "GATE_C2AGENT_D2_MCSC_QCH");

	is_disable(dev, "GATE_VRA_QCH");
	is_disable(dev, "UMUX_CLKCMU_VRA_BUS_USER");
	is_disable(dev, "UMUX_CLKCMU_VRA_CLAHE_USER");
	is_disable(dev, "GATE_CLAHE_QCH");
	is_disable(dev, "DOUT_CLK_VRA_BUSP");

	pdata->clock_on = false;

p_err:
	return 0;
}

/* Wrapper functions */
int exynos_is_clk_get(struct device *dev)
{
	exynos9630_is_get_clk(dev);
	return 0;
}

int exynos_is_clk_cfg(struct device *dev)
{
	exynos9630_is_cfg_clk(dev);
	return 0;
}

int exynos_is_clk_on(struct device *dev)
{
	exynos9630_is_clk_on(dev);
	return 0;
}

int exynos_is_clk_off(struct device *dev)
{
	exynos9630_is_clk_off(dev);
	return 0;
}

int exynos_is_print_clk(struct device *dev)
{
	exynos9630_is_print_clk(dev);
	return 0;
}

int exynos_is_dump_clk(struct device *dev)
{
#if defined(ENABLE_CLOG_RESERVED_MEM)
	exynos9630_is_dump_clk(dev);
#endif
	return 0;
}

int exynos_is_set_user_clk_gate(u32 group_id, bool is_on,
	u32 user_scenario_id,
	unsigned long msk_state,
	struct exynos_is_clk_gate_info *gate_info)
{
	return 0;
}

int exynos_is_clk_gate(u32 clk_gate_id, bool is_on)
{
	exynos9630_is_clk_gate(clk_gate_id, is_on);
	return 0;
}
