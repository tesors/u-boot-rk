/*
 * Copyright (c) 2013 Google, Inc
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <clk.h>
#include <dm.h>
#include <dt-structs.h>
#include <dwmmc.h>
#include <errno.h>
#include <mapmem.h>
#include <pwrseq.h>
#include <syscon.h>
#include <asm/gpio.h>
#include <asm/arch/clock.h>
#include <asm/arch/periph.h>
#include <linux/err.h>

DECLARE_GLOBAL_DATA_PTR;

struct rockchip_mmc_plat {
#if CONFIG_IS_ENABLED(OF_PLATDATA)
	struct dtd_rockchip_rk3288_dw_mshc dtplat;
#endif
	struct mmc_config cfg;
	struct mmc mmc;
};

struct rockchip_dwmmc_priv {
	struct clk clk;
	struct clk sample_clk;
	struct dwmci_host host;
	int fifo_depth;
	bool fifo_mode;
	u32 minmax[2];
};

#ifdef CONFIG_USING_KERNEL_DTB
int board_mmc_dm_reinit(struct udevice *dev)
{
	struct rockchip_dwmmc_priv *priv = dev_get_priv(dev);

	if (!priv || !&priv->clk)
		return 0;

	if (!memcmp(dev->name, "dwmmc", strlen("dwmmc")))
		return clk_get_by_index(dev, 0, &priv->clk);
	else
		return 0;
}
#endif

#ifdef CONFIG_SPL_BUILD
__weak void mmc_gpio_init_direct(void) {}
#endif

static uint rockchip_dwmmc_get_mmc_clk(struct dwmci_host *host, uint freq)
{
	struct udevice *dev = host->priv;
	struct rockchip_dwmmc_priv *priv = dev_get_priv(dev);
	int ret;

	/*
	 * If DDR52 8bit mode(only emmc work in 8bit mode),
	 * divider must be set 1
	 */
	if (mmc_card_ddr52(host->mmc) && host->mmc->bus_width == 8)
		freq *= 2;

	ret = clk_set_rate(&priv->clk, freq);
	if (ret < 0) {
		debug("%s: err=%d\n", __func__, ret);
		return 0;
	}

	return freq;
}

static int rockchip_dwmmc_ofdata_to_platdata(struct udevice *dev)
{
#if !CONFIG_IS_ENABLED(OF_PLATDATA)
	struct rockchip_dwmmc_priv *priv = dev_get_priv(dev);
	struct dwmci_host *host = &priv->host;

	host->name = dev->name;
	host->ioaddr = dev_read_addr_ptr(dev);
	host->buswidth = dev_read_u32_default(dev, "bus-width", 4);
	host->get_mmc_clk = rockchip_dwmmc_get_mmc_clk;
	host->priv = dev;

	/* use non-removeable as sdcard and emmc as judgement */
	if (dev_read_bool(dev, "non-removable"))
		host->dev_index = 0;
	else
		host->dev_index = 1;

	priv->fifo_depth = dev_read_u32_default(dev, "fifo-depth", 0);

	if (priv->fifo_depth < 0)
		return -EINVAL;
	priv->fifo_mode = dev_read_bool(dev, "fifo-mode");

	/*
	 * 'clock-freq-min-max' is deprecated
	 * (see https://github.com/torvalds/linux/commit/b023030f10573de738bbe8df63d43acab64c9f7b)
	 */
	if (dev_read_u32_array(dev, "clock-freq-min-max", priv->minmax, 2)) {
		int val = dev_read_u32_default(dev, "max-frequency", -EINVAL);

		if (val < 0)
			return val;

		priv->minmax[0] = 400000;  /* 400 kHz */
		priv->minmax[1] = val;
	} else {
		debug("%s: 'clock-freq-min-max' property was deprecated.\n",
		      __func__);
	}
#endif
	return 0;
}

#ifndef CONFIG_MMC_SIMPLE
#define NUM_PHASES	32
#define TUNING_ITERATION_TO_PHASE(i, num_phases) (DIV_ROUND_UP((i) * 360, num_phases))

/*
 * Each fine delay is between 44ps-77ps. Assume each fine delay is 60ps to
 * simplify calculations. So 45degs could be anywhere between 33deg and 57.8deg.
 */
static int rockchip_mmc_get_phase(struct dwmci_host *host, bool sample)
{
	struct udevice *dev = host->priv;
	struct rockchip_dwmmc_priv *priv = dev_get_priv(dev);
	unsigned long rate = clk_get_rate(&priv->clk);
	u32 raw_value;
	u16 degrees;
	u32 delay_num = 0;

	/* Constant signal, no measurable phase shift */
	if (!rate)
		return 0;

	if (sample)
		raw_value = dwmci_readl(host, SDMMC_TIMING_CON1) >> 1;
	else
		raw_value = dwmci_readl(host, SDMMC_TIMING_CON0) >> 1;

	degrees = (raw_value & ROCKCHIP_MMC_DEGREE_MASK) * 90;
	if (raw_value & ROCKCHIP_MMC_DELAY_SEL) {
		/* degrees/delaynum * 1000000 */
		unsigned long factor = (ROCKCHIP_MMC_DELAY_ELEMENT_PSEC / 10) * 36 * (rate / 10000);

		delay_num = (raw_value & ROCKCHIP_MMC_DELAYNUM_MASK);
		delay_num >>= ROCKCHIP_MMC_DELAYNUM_OFFSET;
		degrees += DIV_ROUND_CLOSEST(delay_num * factor, 1000000);
	}
	return degrees % 360;
}

static int rockchip_mmc_set_phase(struct dwmci_host *host, bool sample, int degrees)
{
	struct udevice *dev = host->priv;
	struct rockchip_dwmmc_priv *priv = dev_get_priv(dev);
	unsigned long rate = clk_get_rate(&priv->clk);
	u8 nineties, remainder;
	u8 delay_num;
	u32 raw_value;
	u32 delay;

	/*
	 * The below calculation is based on the output clock from
	 * MMC host to the card, which expects the phase clock inherits
	 * the clock rate from its parent, namely the output clock
	 * provider of MMC host. However, things may go wrong if
	 * (1) It is orphan.
	 * (2) It is assigned to the wrong parent.
	 *
	 * This check help debug the case (1), which seems to be the
	 * most likely problem we often face and which makes it difficult
	 * for people to debug unstable mmc tuning results.
	 */
	if (!rate) {
		printf("%s: invalid clk rate\n", __func__);
		return -EINVAL;
	}

	nineties = degrees / 90;
	remainder = (degrees % 90);

	/*
	 * Due to the inexact nature of the "fine" delay, we might
	 * actually go non-monotonic.  We don't go _too_ monotonic
	 * though, so we should be OK.  Here are options of how we may
	 * work:
	 *
	 * Ideally we end up with:
	 *   1.0, 2.0, ..., 69.0, 70.0, ...,  89.0, 90.0
	 *
	 * On one extreme (if delay is actually 44ps):
	 *   .73, 1.5, ..., 50.6, 51.3, ...,  65.3, 90.0
	 * The other (if delay is actually 77ps):
	 *   1.3, 2.6, ..., 88.6. 89.8, ..., 114.0, 90
	 *
	 * It's possible we might make a delay that is up to 25
	 * degrees off from what we think we're making.  That's OK
	 * though because we should be REALLY far from any bad range.
	 */

	/*
	 * Convert to delay; do a little extra work to make sure we
	 * don't overflow 32-bit / 64-bit numbers.
	 */
	delay = 10000000; /* PSECS_PER_SEC / 10000 / 10 */
	delay *= remainder;
	delay = DIV_ROUND_CLOSEST(delay,
			(rate / 1000) * 36 *
				(ROCKCHIP_MMC_DELAY_ELEMENT_PSEC / 10));

	delay_num = (u8) min_t(u32, delay, 255);

	raw_value = delay_num ? ROCKCHIP_MMC_DELAY_SEL : 0;
	raw_value |= delay_num << ROCKCHIP_MMC_DELAYNUM_OFFSET;
	raw_value |= nineties;

	if (sample)
		dwmci_writel(host, SDMMC_TIMING_CON1, HIWORD_UPDATE(raw_value, 0x07ff, 1));
	else
		dwmci_writel(host, SDMMC_TIMING_CON1, HIWORD_UPDATE(raw_value, 0x07ff, 1));

	debug("set %s_phase(%d) delay_nums=%u actual_degrees=%d\n",
		sample ? "sample" : "drv", degrees, delay_num,
		rockchip_mmc_get_phase(host, sample)
	);

	return 0;
}

static int rockchip_dwmmc_execute_tuning(struct dwmci_host *host, u32 opcode)
{
	int i = 0;
	int ret = -1;
	struct mmc *mmc = host->mmc;
	struct udevice *dev = host->priv;
	struct rockchip_dwmmc_priv *priv = dev_get_priv(dev);

	if (IS_ERR(&priv->sample_clk))
		return -EIO;

	if (mmc->default_phase > 0 && mmc->default_phase < 360) {
		ret = clk_set_phase(&priv->sample_clk, mmc->default_phase);
		if (ret)
			printf("set clk phase fail\n");
		else
			ret = mmc_send_tuning(mmc, opcode);
		mmc->default_phase = 0;
	}
	/*
	 * If use default_phase to tune successfully, return.
	 * Otherwise, use the othe phase to tune.
	 */
	if (!ret)
		return ret;

	for (i = 0; i < 5; i++) {
		/* mmc->init_retry must be 0, 1, 2, 3 */
		if (mmc->init_retry == 4)
			mmc->init_retry = 0;

		ret = clk_set_phase(&priv->sample_clk, 90 * mmc->init_retry);
		if (ret) {
			printf("set clk phase fail\n");
			break;
		}
		ret = mmc_send_tuning(mmc, opcode);
		debug("Tuning phase is %d, ret is %d\n", mmc->init_retry * 90, ret);
		mmc->init_retry++;
		if (!ret)
			break;
	}

	return ret;
}
#else
static int rockchip_dwmmc_execute_tuning(struct dwmci_host *host, u32 opcode) { return 0; }
#endif

static int rockchip_dwmmc_probe(struct udevice *dev)
{
	struct rockchip_mmc_plat *plat = dev_get_platdata(dev);
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct rockchip_dwmmc_priv *priv = dev_get_priv(dev);
	struct dwmci_host *host = &priv->host;
	struct udevice *pwr_dev __maybe_unused;
	int ret;

#ifdef CONFIG_SPL_BUILD
	mmc_gpio_init_direct();
#endif
#if CONFIG_IS_ENABLED(OF_PLATDATA)
	struct dtd_rockchip_rk3288_dw_mshc *dtplat = &plat->dtplat;

	host->name = dev->name;
	host->ioaddr = map_sysmem(dtplat->reg[0], dtplat->reg[1]);
	host->buswidth = dtplat->bus_width;
	host->get_mmc_clk = rockchip_dwmmc_get_mmc_clk;
	host->execute_tuning = rockchip_dwmmc_execute_tuning;
	host->priv = dev;
	host->dev_index = 0;
	priv->fifo_depth = dtplat->fifo_depth;
	priv->fifo_mode = 0;
	priv->minmax[0] = 400000;  /*  400 kHz */
	priv->minmax[1] = dtplat->max_frequency;

	ret = clk_get_by_index_platdata(dev, 0, dtplat->clocks, &priv->clk);
	if (ret < 0)
		return ret;
#else
	ret = clk_get_by_index(dev, 0, &priv->clk);
	if (ret < 0)
		return ret;

	ret = clk_get_by_name(dev, "ciu-sample", &priv->sample_clk);
	if (ret < 0)
		debug("MMC: sample clock not found, not support hs200!\n");
	host->execute_tuning = rockchip_dwmmc_execute_tuning;
#endif
	host->fifoth_val = MSIZE(DWMCI_MSIZE) |
		RX_WMARK(priv->fifo_depth / 2 - 1) |
		TX_WMARK(priv->fifo_depth / 2);

	host->fifo_mode = priv->fifo_mode;

#ifdef CONFIG_ROCKCHIP_RK3128
	host->stride_pio = true;
#else
	host->stride_pio = false;
#endif

#ifdef CONFIG_PWRSEQ
	/* Enable power if needed */
	ret = uclass_get_device_by_phandle(UCLASS_PWRSEQ, dev, "mmc-pwrseq",
					   &pwr_dev);
	if (!ret) {
		ret = pwrseq_set_power(pwr_dev, true);
		if (ret)
			return ret;
	}
#endif
	dwmci_setup_cfg(&plat->cfg, host, priv->minmax[1], priv->minmax[0]);
	if (dev_read_bool(dev, "mmc-hs200-1_8v"))
		plat->cfg.host_caps |= MMC_MODE_HS200;
	plat->mmc.default_phase =
		dev_read_u32_default(dev, "default-sample-phase", 0);
#ifdef CONFIG_ROCKCHIP_RV1106
	if (!(ret < 0) && (&priv->sample_clk)) {
		ret = clk_set_phase(&priv->sample_clk, plat->mmc.default_phase);
		if (ret < 0)
			debug("MMC: can not set default phase!\n");
	}
#endif

	plat->mmc.init_retry = 0;
	host->mmc = &plat->mmc;
	host->mmc->priv = &priv->host;
	host->mmc->dev = dev;
	upriv->mmc = host->mmc;

	return dwmci_probe(dev);
}

static int rockchip_dwmmc_bind(struct udevice *dev)
{
	struct rockchip_mmc_plat *plat = dev_get_platdata(dev);

	return dwmci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id rockchip_dwmmc_ids[] = {
	{ .compatible = "rockchip,rk3288-dw-mshc" },
	{ .compatible = "rockchip,rk2928-dw-mshc" },
	{ }
};

U_BOOT_DRIVER(rockchip_dwmmc_drv) = {
	.name		= "rockchip_rk3288_dw_mshc",
	.id		= UCLASS_MMC,
	.of_match	= rockchip_dwmmc_ids,
	.ofdata_to_platdata = rockchip_dwmmc_ofdata_to_platdata,
	.ops		= &dm_dwmci_ops,
	.bind		= rockchip_dwmmc_bind,
	.probe		= rockchip_dwmmc_probe,
	.priv_auto_alloc_size = sizeof(struct rockchip_dwmmc_priv),
	.platdata_auto_alloc_size = sizeof(struct rockchip_mmc_plat),
};

#ifdef CONFIG_PWRSEQ
static int rockchip_dwmmc_pwrseq_set_power(struct udevice *dev, bool enable)
{
	struct gpio_desc reset;
	int ret;

	ret = gpio_request_by_name(dev, "reset-gpios", 0, &reset, GPIOD_IS_OUT);
	if (ret)
		return ret;
	dm_gpio_set_value(&reset, 1);
	udelay(1);
	dm_gpio_set_value(&reset, 0);
	udelay(200);

	return 0;
}

static const struct pwrseq_ops rockchip_dwmmc_pwrseq_ops = {
	.set_power	= rockchip_dwmmc_pwrseq_set_power,
};

static const struct udevice_id rockchip_dwmmc_pwrseq_ids[] = {
	{ .compatible = "mmc-pwrseq-emmc" },
	{ }
};

U_BOOT_DRIVER(rockchip_dwmmc_pwrseq_drv) = {
	.name		= "mmc_pwrseq_emmc",
	.id		= UCLASS_PWRSEQ,
	.of_match	= rockchip_dwmmc_pwrseq_ids,
	.ops		= &rockchip_dwmmc_pwrseq_ops,
};
#endif
