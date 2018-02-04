/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *	Based on arch/arm/plat-omap/cpu-omap.c, (C) 2005 Nokia Corporation
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>


static struct cpufreq_frequency_table freq_table[] = {
	{ .frequency = 408000,  .driver_data = 800000 },
	{ .frequency = 550000,  .driver_data = 800000 },
	{ .frequency = 770000,  .driver_data = 850000 },
	{ .frequency = 910000,  .driver_data = 900000 },
	{ .frequency = 1150000, .driver_data = 975000 },
	{ .frequency = 1230000, .driver_data = 1000000 },
	{ .frequency = 1280000, .driver_data = 1012000 },
	{ .frequency = 1330000, .driver_data = 1050000 },
	/*{ .frequency = 1370000, .driver_data = 1075000 },
	{ .frequency = 1400000, .driver_data = 1100000 },*/
	{ .frequency = CPUFREQ_TABLE_END },
};

/*static struct cpufreq_frequency_table freq_table[] = {
	{ .frequency = 408000,  .driver_data = 800000 },
	{ .frequency = 460000,  .driver_data = 800000 },
	{ .frequency = 550000,  .driver_data = 850000 },
	{ .frequency = 680000,  .driver_data = 900000 },
	{ .frequency = 8200000, .driver_data = 975000 },
	{ .frequency = 1000000, .driver_data = 1000000 },
	{ .frequency = CPUFREQ_TABLE_END },
};*/

/*static struct cpufreq_frequency_table freq_table[] = {
	{ .frequency = 408000,  .driver_data = 1000000 },
	{ .frequency = 460000,  .driver_data = 1000000 },
	{ .frequency = 550000,  .driver_data = 1000000 },
	{ .frequency = 680000,  .driver_data = 1000000 },
	{ .frequency = 8200000, .driver_data = 1000000 },
	{ .frequency = 1000000, .driver_data = 1000000 },
	{ .frequency = CPUFREQ_TABLE_END },
};*/

#define NUM_CPUS	4
#define MAX_UV		1000000

static struct clk *cpu_clk;
static struct clk *pll_x_clk;
static struct clk *pll_p_clk;
static struct clk *emc_clk;
static struct regulator *vdd_cpu_reg;
static bool pll_x_prepared;
static int current_voltage = 1000000;


static unsigned int tegra_get_intermediate(struct cpufreq_policy *policy,
					   unsigned int index)
{
	unsigned int ifreq = clk_get_rate(pll_p_clk) / 1000;

	/*
	 * Don't switch to intermediate freq if:
	 * - we are already at it, i.e. policy->cur == ifreq
	 * - index corresponds to ifreq
	 */
	if ((freq_table[index].frequency == ifreq) || (policy->cur == ifreq))
		return 0;

	return ifreq;
}

static int tegra_target_intermediate(struct cpufreq_policy *policy,
				     unsigned int index)
{
	int ret;

	/*
	 * Take an extra reference to the main pll so it doesn't turn
	 * off when we move the cpu off of it as enabling it again while we
	 * switch to it from tegra_target() would take additional time.
	 *
	 * When target-freq is equal to intermediate freq we don't need to
	 * switch to an intermediate freq and so this routine isn't called.
	 * Also, we wouldn't be using pll_x anymore and must not take extra
	 * reference to it, as it can be disabled now to save some power.
	 */
	clk_prepare_enable(pll_x_clk);

	ret = clk_set_parent(cpu_clk, pll_p_clk);
	if (ret) {
		pr_err("tegra30-cpufreq: unable to set clock parent: %i\n", ret);
		clk_disable_unprepare(pll_x_clk);
	} else
		pll_x_prepared = true;

	return ret;
}

static int tegra_target(struct cpufreq_policy *policy, unsigned int index)
{
	unsigned long rate = freq_table[index].frequency;
	unsigned long uv = freq_table[index].driver_data;
	unsigned int ifreq = clk_get_rate(pll_p_clk) / 1000;
	int ret = 0;

	//pr_err("switching uv: %lu\n", uv);

	if (uv > current_voltage)
		regulator_set_voltage(vdd_cpu_reg, uv, MAX_UV);

	/*
	 * Vote on memory bus frequency based on cpu frequency
	 * This sets the minimum frequency, display or avp may request higher
	 */
	if (rate >= 925000)
		clk_set_rate(emc_clk, 667000000); /* cpu 816 MHz, emc max */
	else /*if (rate >= 450000)*/
		clk_set_rate(emc_clk, 667000000 / 2); /* cpu 456 MHz, emc 150Mhz */
	//else
	//	clk_set_rate(emc_clk, 100000000);  /* emc 50Mhz */

	/*
	 * target freq == pll_p, don't need to take extra reference to pll_x_clk
	 * as it isn't used anymore.
	 */
	if (rate == ifreq)
		return clk_set_parent(cpu_clk, pll_p_clk);

	ret = clk_set_rate(pll_x_clk, rate * 1000);
	/* Restore to earlier frequency on error, i.e. pll_x */
	if (ret)
	{
		pr_err("Failed to change pll_x to %lu\n", rate);
		regulator_set_voltage(vdd_cpu_reg, current_voltage, MAX_UV);
	}
	else
	{
		regulator_set_voltage(vdd_cpu_reg, uv, MAX_UV);
		current_voltage = uv;
	}

	ret = clk_set_parent(cpu_clk, pll_x_clk);
	/* This shouldn't fail while changing or restoring */
	WARN_ON(ret);

	/*
	 * Drop count to pll_x clock only if we switched to intermediate freq
	 * earlier while transitioning to a target frequency.
	 */
	if (pll_x_prepared) {
		clk_disable_unprepare(pll_x_clk);
		pll_x_prepared = false;
	}

	return ret;
}

static int tegra_cpu_init(struct cpufreq_policy *policy)
{
	int ret;

	if (policy->cpu >= NUM_CPUS)
		return -EINVAL;

	clk_prepare_enable(emc_clk);
	clk_prepare_enable(cpu_clk);

	/* FIXME: what's the actual transition time? */
	ret = cpufreq_generic_init(policy, freq_table, 300 * 1000);
	if (ret) {
		clk_disable_unprepare(cpu_clk);
		clk_disable_unprepare(emc_clk);
		return ret;
	}

	policy->clk = cpu_clk;
	policy->suspend_freq = freq_table[0].frequency;
	return 0;
}

static int tegra_cpu_exit(struct cpufreq_policy *policy)
{
	clk_disable_unprepare(cpu_clk);
	clk_disable_unprepare(emc_clk);
	return 0;
}

static struct cpufreq_driver tegra_cpufreq_driver = {
	.flags			= CPUFREQ_NEED_INITIAL_FREQ_CHECK,
	.verify			= cpufreq_generic_frequency_table_verify,
	.get_intermediate	= tegra_get_intermediate,
	.target_intermediate	= tegra_target_intermediate,
	.target_index		= tegra_target,
	.get			= cpufreq_generic_get,
	.init			= tegra_cpu_init,
	.exit			= tegra_cpu_exit,
	.name			= "tegra3",
	.attr			= cpufreq_generic_attr,
	.suspend		= cpufreq_generic_suspend,
};

static int __init tegra_cpufreq_init(void)
{
	pr_err("TEGRA CPUFREQ INIT\n");
	vdd_cpu_reg = regulator_get(NULL, "vdd_cpu");
	if (IS_ERR(vdd_cpu_reg))
		return PTR_ERR(vdd_cpu_reg);
	regulator_set_voltage(vdd_cpu_reg, MAX_UV, MAX_UV);
	pr_err("SET VOLTAGE\n");

	cpu_clk = clk_get_sys(NULL, "cclk_g");
	if (IS_ERR(cpu_clk))
		return PTR_ERR(cpu_clk);

	pll_x_clk = clk_get_sys(NULL, "pll_x");
	if (IS_ERR(pll_x_clk))
		return PTR_ERR(pll_x_clk);

	pll_p_clk = clk_get_sys(NULL, "pll_p_cclkg");
	if (IS_ERR(pll_p_clk))
		return PTR_ERR(pll_p_clk);

	emc_clk = clk_get_sys("cpu", "emc");
	if (IS_ERR(emc_clk)) {
		clk_put(cpu_clk);
		return PTR_ERR(emc_clk);
	}

	pr_info("pll_p clock: %lu\n", clk_get_rate(pll_p_clk));
	pr_info("pll_x clock: %lu\n", clk_get_rate(pll_x_clk));
	pr_info("cpu clock: %lu\n", clk_get_rate(cpu_clk));
	pr_info("emc clock: %lu\n", clk_get_rate(emc_clk));

	return cpufreq_register_driver(&tegra_cpufreq_driver);
}

static void __exit tegra_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&tegra_cpufreq_driver);
	clk_put(emc_clk);
	clk_put(cpu_clk);
	regulator_put(vdd_cpu_reg);
}


MODULE_AUTHOR("Colin Cross <ccross@android.com>");
MODULE_DESCRIPTION("cpufreq driver for Nvidia Tegra3");
MODULE_LICENSE("GPL");
module_init(tegra_cpufreq_init);
module_exit(tegra_cpufreq_exit);
