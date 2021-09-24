// SPDX-License-Identifier: GPL-2.0
/*
 * StarFive JH7100 Clock Generator Driver
 *
 * Copyright 2021 Ahmad Fatoum, Pengutronix
 * Copyright (C) 2021 Glider bv
 * Copyright (C) 2021 Starfive
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>

#include <dt-bindings/clock/starfive-jh7100.h>

#define STARFIVE_CLK_ENABLE_SHIFT	31
#define STARFIVE_CLK_INVERT_SHIFT	30
#define STARFIVE_CLK_MUX_SHIFT		24


static const char *cpundbus_root_sels[4] = {
	[0] = "osc_sys",
	[1] = "pll0_out",
	[2] = "pll1_out",
	[3] = "pll2_out",
};

static const char *dla_root_sels[4] = {
	[0] = "osc_sys",
	[1] = "pll1_out",
	[2] = "pll2_out",
	[3] = "dummy",
};

static const char *dsp_root_sels[4] = {
	[0] = "osc_sys",
	[1] = "pll0_out",
	[2] = "pll1_out",
	[3] = "pll2_out",
};

static const char *gmacusb_root_sels[4] = {
	[0] = "osc_sys",
	[1] = "pll0_out",
	[2] = "pll2_out",
	[3] = "dummy",
};

static const char *perh0_root_sels[2] = {
	[0] = "osc_sys",
	[1] = "pll0_out",
};

static const char *perh1_root_sels[2] = {
	[0] = "osc_sys",
	[1] = "pll2_out",
};

static const char *vin_root_sels[4] = {
	[0] = "osc_sys",
	[1] = "pll1_out",
	[2] = "pll2_out",
	[3] = "dummy",
};

static const char *vout_root_sels[4] = {
	[0] = "osc_aud",
	[1] = "pll0_out",
	[2] = "pll2_out",
	[3] = "dummy",
};

static const char *cdechifi4_root_sels[4] = {
	[0] = "osc_sys",
	[1] = "pll1_out",
	[2] = "pll2_out",
	[3] = "dummy",
};

static const char *cdec_root_sels[4] = {
	[0] = "osc_sys",
	[1] = "pll0_out",
	[2] = "pll1_out",
	[3] = "dummy",
};

static const char *voutbus_root_sels[4] = {
	[0] = "osc_aud",
	[1] = "pll0_out",
	[2] = "pll2_out",
	[3] = "dummy",
};

static const char *pll2_refclk_sels[2] = {
	[0] = "osc_sys",
	[1] = "osc_aud",
};

static const char *ddrc0_sels[4] = {
	[0] = "ddrosc_div2",
	[1] = "ddrpll_div2",
	[2] = "ddrpll_div4",
	[3] = "ddrpll_div8",
};

static const char *ddrc1_sels[4] = {
	[0] = "ddrosc_div2",
	[1] = "ddrpll_div2",
	[2] = "ddrpll_div4",
	[3] = "ddrpll_div8",
};

static const char *nne_bus_sels[2] = {
	[0] = "cpu_axi",
	[1] = "nnebus_src1",
};

static const char *usbphy_25m_sels[2] = {
	[0] = "osc_sys",
	[1] = "usbphy_plldiv25m",
};

static const char *gmac_tx_sels[4] = {
	[0] = "gmac_gtxclk",
	[1] = "gmac_mii_txclk",
	[2] = "gmac_rmii_txclk",
	[3] = "dummy",
};

static const char *gmac_rx_pre_sels[2] = {
	[0] = "gmac_gr_mii_rxclk",
	[1] = "gmac_rmii_rxclk",
};

static const char *audio_clk_sels[2] = {
	[0] = "audio_src",
	[1] = "audio_12288",
};

static const char *i2sadc_mclk_sels[2] = {
	[0] = "clk_audio_src",
	[1] = "clk_aud",
};

static const char *i2sadc_bclk_sels[2] = {
	[0] = "adc_mclk",
	[1] = "i2sadc_bclk_iopad",
};

static const char *i2sadc_lrclk_sels[4] = {
	[0] = "i2sadc_bclk_n",
	[1] = "i2sadc_lrclk_iopad",
	[2] = "i2sadc_bclk",
	[3] = "dummy",
};

static const char *i2sdac0_bclk_sels[2] = {
	[0] = "audio_dac_mclk",
	[1] = "i2sdac0_bclk_iopad",
};

static const char *i2sdac0_lrclk_sels[4] = {
	[0] = "i2sdac0_bclk_n",
	[1] = "i2sdac0_lrclk_iopad",
	[2] = "i2sdac0_bclk",
	[3] = "dummy",
};


static const char *i2sdac1_bclk_sels[2] = {
	[0] = "i2s1_mclk",
	[1] = "i2sadc1_bclk_iopad",
};

static const char *i2sdac1_lrclk_sels[4] = {
	[0] = "i2sdac1_bclk_n",
	[1] = "i2sdac1_lrclk_iopad",
	[2] = "i2sdac1_bclk",
	[3] = "dummy",
};

struct clk_starfive_jh7100_priv {
	spinlock_t rmw_lock;
	struct device *dev;
	void __iomem *base;
	struct clk_hw_onecell_data clk_hws;
};

static const struct clk_div_table ahp_div_table[] = {
    { .val = 5, .div = 4, },
    { }
};

/* assume osc_sys as direct parent for clocks of yet unknown lineage */
#define UNKNOWN "osc_sys"

static struct clk_hw * __init starfive_clk_underspecifid(struct clk_starfive_jh7100_priv *priv,
							 const char *name,
							 const char *parent)
{
	/*
	 * TODO With documentation available, all users of this functions can be
	 * migrated to one of the above or to a clk_fixed_factor with
	 * appropriate factor
	 */
	return clk_hw_register_fixed_factor(priv->dev, name, parent, 0, 1,
						 1);
}

static struct clk_hw * __init starfive_clk_fixed_rate(struct clk_starfive_jh7100_priv *priv,
							 const char *name,
							 const char *parent,
							 unsigned long fixed_rate)
{
	return clk_hw_register_fixed_rate(priv->dev, name, parent, 0, fixed_rate);
}

static struct clk_hw * __init starfive_clk_fixed_factor(struct clk_starfive_jh7100_priv *priv,
						    const char *name,
						    const char *parent,
						    unsigned int mult,
						    unsigned int div)
{
	/*
	 * TODO With documentation available, all users of this functions can be
	 * migrated to one of the above or to a clk_fixed_factor with
	 * appropriate factor
	 */
	return clk_hw_register_fixed_factor(priv->dev, name, parent, 0,
						 mult, div);
}

static struct clk_hw * __init starfive_clk_divider_o(struct clk_starfive_jh7100_priv *priv,
						   const char *name,
						   const char *parent,
						   unsigned int offset,
						   unsigned int width)
{
	return starfive_clk_underspecifid(priv, name, parent);
}

static struct clk_hw * __init starfive_clk_divider(struct clk_starfive_jh7100_priv *priv,
						   const char *name,
						   const char *parent,
						   unsigned int offset,
						   unsigned int width)
{
	return clk_hw_register_divider(priv->dev, name, parent, 0,
						priv->base + offset, 0, width,
						CLK_DIVIDER_ONE_BASED,
						&priv->rmw_lock);
}

static struct clk_hw * __init starfive_clk_gate(struct clk_starfive_jh7100_priv *priv,
						const char *name,
						const char *parent,
						unsigned int offset)
{
	return clk_hw_register_gate(priv->dev, name, parent,
				    CLK_SET_RATE_PARENT, priv->base + offset,
				    STARFIVE_CLK_ENABLE_SHIFT, 0,
				    &priv->rmw_lock);
}

static struct clk_hw * __init starfive_clk_composite(struct clk_starfive_jh7100_priv *priv,
									const char *name,
									const char * const *parents,
									unsigned int num_parents,
									unsigned int offset,
									unsigned int mux_width,
									unsigned int gate_width,
									unsigned int div_width)
{
	struct clk_hw *hw;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;
	struct clk_hw *mux_hw = NULL, *gate_hw = NULL, *div_hw = NULL;
	const struct clk_ops *mux_ops = NULL, *gate_ops = NULL, *div_ops = NULL;
	int ret;
	int mask_arry[4] = {0x1, 0x3, 0x7, 0xF};
	int mask;

	if (mux_width) {
		if (mux_width > 4) {
			return ERR_PTR(-EPERM);
		}else {
			mask = mask_arry[mux_width-1];
		}

		mux = devm_kzalloc(priv->dev, sizeof(*mux), GFP_KERNEL);
		if (!mux) {
			return ERR_PTR(-ENOMEM);
		}

		mux->reg = priv->base + offset;
		mux->mask = mask;
		mux->shift = STARFIVE_CLK_MUX_SHIFT;
		mux_hw = &mux->hw;
		mux_ops = &clk_mux_ops;
		mux->lock = &priv->rmw_lock;

	}

	if (gate_width) {
		gate = devm_kzalloc(priv->dev, sizeof(*gate), GFP_KERNEL);
		if (!gate) {
			ret = -ENOMEM;
			return ret;
		}

		gate->reg = priv->base + offset;
		gate->bit_idx = STARFIVE_CLK_ENABLE_SHIFT;
		gate->lock = &priv->rmw_lock;

		gate_hw = &gate->hw;
		gate_ops = &clk_gate_ops;
	}

	if (div_width) {
		div = devm_kzalloc(priv->dev, sizeof(*div), GFP_KERNEL);
		if (!div) {
			ret = -ENOMEM;
			return ret;
		}

		div->reg = priv->base + offset;
		div->shift = 0;
		div->width = div_width;
		div->flags = CLK_DIVIDER_ONE_BASED;
		div->table = NULL;
		div->lock = &priv->rmw_lock;

		div_hw = &div->hw;
		div_ops = &clk_divider_ops;
	}

	hw = clk_hw_register_composite(priv->dev, name, parents,
				       num_parents, mux_hw, mux_ops, div_hw,
				       div_ops, gate_hw, gate_ops, 0);

	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		return ERR_PTR(ret);
	}

	return hw;
}

static struct clk_hw * __init starfive_clk_gated_divider_old(struct clk_starfive_jh7100_priv *priv,
							 const char *name,
							 const char *parent,
							 unsigned int offset,
							 unsigned int width)
{
	struct clk_hw *hw;
	struct clk_gate *gate = NULL;
	struct clk_divider *div = NULL;
	struct clk_hw *mux_hw = NULL, *gate_hw = NULL, *div_hw = NULL;
	const struct clk_ops *mux_ops = NULL, *gate_ops = NULL, *div_ops = NULL;
	const char * const *parent_names;
	int num_parents;
	int ret;

	parent_names = &parent;
	num_parents = 1;

	gate = devm_kzalloc(priv->dev, sizeof(*gate), GFP_KERNEL);
	if (!gate) {
		ret = -ENOMEM;
		return ret;
	}

	gate->reg = priv->base + offset;
	gate->bit_idx = STARFIVE_CLK_ENABLE_SHIFT;
	gate->lock = &priv->rmw_lock;

	gate_hw = &gate->hw;
	gate_ops = &clk_gate_ops;

	div = devm_kzalloc(priv->dev, sizeof(*div), GFP_KERNEL);
	if (!div) {
		ret = -ENOMEM;
		return ret;
	}

	div->reg = priv->base + offset;
	div->shift = 0;
	div->width = width;
	div->flags = CLK_DIVIDER_ONE_BASED;
	div->table = NULL;
	div->lock = &priv->rmw_lock;

	div_hw = &div->hw;
	div_ops = &clk_divider_ops;

	hw = clk_hw_register_composite(priv->dev, name, parent_names,
				       num_parents, mux_hw, mux_ops, div_hw,
				       div_ops, gate_hw, gate_ops, 0);

	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		return ret;
	}

	return hw;
}


static struct clk_hw * __init starfive_clk_gated_divider(struct clk_starfive_jh7100_priv *priv,
							 const char *name,
							 const char *parent,
							 unsigned int offset,
							 unsigned int width)
{
	const char * const *parents;

	parents  = &parent;

   	return starfive_clk_composite(priv, name, parents, 1, offset, 0, 1, width);
}


static struct clk_hw * __init starfive_clk_gate_dis(struct clk_starfive_jh7100_priv *priv,
						    const char *name,
						    const char *parent,
						    unsigned int offset)
{
	return clk_hw_register_gate(priv->dev, name, parent,
				    CLK_SET_RATE_PARENT, priv->base + offset,
				    STARFIVE_CLK_INVERT_SHIFT,
				    CLK_GATE_SET_TO_DISABLE, &priv->rmw_lock);
}

static struct clk_hw * __init starfive_clk_mux(struct clk_starfive_jh7100_priv *priv,
					       const char *name,
					       unsigned int offset,
					       unsigned int width,
					       const char * const *parents,
					       unsigned int num_parents)
{
	return clk_hw_register_mux(priv->dev, name, parents, num_parents,
					0, priv->base + offset,
					STARFIVE_CLK_MUX_SHIFT, width, 0,
					&priv->rmw_lock);
}

static struct clk_hw * __init starfive_clk_gated_mux(struct clk_starfive_jh7100_priv *priv,
					       const char *name,
					       unsigned int offset,
					       unsigned int width,
					       const char * const *parents,
					       unsigned int num_parents)
{

	return starfive_clk_composite(priv, name, parents, num_parents, offset, width, 1, 0);
}



static int __init starfive_clkgen_sys_init(struct clk_starfive_jh7100_priv *priv)
{
	struct clk_hw **hws = priv->clk_hws.hws;
	int i;
	int ret;

	printk(KERN_INFO ">>>>>>>starfive_clkgen_init\n");
    printk(">>>>>>>starfive_clkgen_init\n"); 

	hws[JH7100_CLK_OSC_SYS]			= starfive_clk_fixed_rate(priv, "osc_sys", NULL, 25000000);
	hws[JH7100_CLK_OSC_AUD]			= starfive_clk_fixed_rate(priv, "osc_aud", NULL, 27000000);

	hws[JH7100_CLK_PLL0_OUT]		= starfive_clk_fixed_factor(priv, "pll0_out", "osc_sys", 40, 1);
	hws[JH7100_CLK_PLL1_OUT]		= starfive_clk_fixed_factor(priv, "pll1_out", "osc_sys", 64, 1);
	hws[JH7100_CLK_PLL2_OUT]		= starfive_clk_fixed_factor(priv, "pll2_out", "pll2_refclk", 55, 1);

	//hws[JH7100_CLK_CPUNDBUS_ROOT]		= starfive_clk_fixed_factor(priv, "cpundbus_root", "pll0_out", 1, 1);
	hws[JH7100_CLK_CPUNDBUS_ROOT]	= starfive_clk_mux(priv, "cpundbus_root", 0x0, 2, cpundbus_root_sels, ARRAY_SIZE(cpundbus_root_sels));
	hws[JH7100_CLK_DLA_ROOT]		= starfive_clk_mux(priv, "dla_root",	0x4, 2, dla_root_sels, ARRAY_SIZE(dla_root_sels));
	hws[JH7100_CLK_DSP_ROOT]		= starfive_clk_mux(priv, "dsp_root",	0x8, 2, dsp_root_sels, ARRAY_SIZE(dsp_root_sels));
	hws[JH7100_CLK_GMACUSB_ROOT]	= starfive_clk_mux(priv, "gmacusb_root",	0xc, 2, gmacusb_root_sels, ARRAY_SIZE(gmacusb_root_sels));
	hws[JH7100_CLK_PERH0_ROOT]		= starfive_clk_mux(priv, "perh0_root",	0x10, 1, perh0_root_sels, ARRAY_SIZE(perh0_root_sels));
	hws[JH7100_CLK_PERH1_ROOT]		= starfive_clk_mux(priv, "perh1_root",	0x14, 1, perh1_root_sels, ARRAY_SIZE(perh1_root_sels));
	hws[JH7100_CLK_VIN_ROOT]		= starfive_clk_mux(priv, "vin_root",	0x18, 2, vin_root_sels, ARRAY_SIZE(vin_root_sels));
	hws[JH7100_CLK_VOUT_ROOT]		= starfive_clk_mux(priv, "vout_root",	0x1c, 2, vout_root_sels, ARRAY_SIZE(vout_root_sels));
	hws[JH7100_CLK_AUDIO_ROOT]		= starfive_clk_gated_divider(priv, "audio_root",		"pll0_out",	0x20, 4);
	hws[JH7100_CLK_CDECHIFI4_ROOT]	= starfive_clk_mux(priv, "cdechifi4_root",	0x24, 2, cdechifi4_root_sels, ARRAY_SIZE(cdechifi4_root_sels));
	hws[JH7100_CLK_CDEC_ROOT]		= starfive_clk_mux(priv, "cdec_root",	0x28, 2, cdec_root_sels, ARRAY_SIZE(cdec_root_sels));
	hws[JH7100_CLK_VOUTBUS_ROOT]	= starfive_clk_mux(priv, "voutbus_root",	0x2c, 2, voutbus_root_sels, ARRAY_SIZE(voutbus_root_sels));
	hws[JH7100_CLK_CPUNBUS_ROOT_DIV]= starfive_clk_divider(priv, "cpunbus_root_div",		"cpundbus_root",	0x30, 2);
	hws[JH7100_CLK_DSP_ROOT_DIV]	= starfive_clk_divider(priv, "dsp_root_div",		"dsp_root",	0x34, 3);
	hws[JH7100_CLK_PERH0_SRC]		= starfive_clk_divider(priv, "perh0_src",		"perh0_root",	0x38, 3);
	hws[JH7100_CLK_PERH1_SRC]		= starfive_clk_divider(priv, "perh1_src",		"perh1_root",	0x3c, 3);
	hws[JH7100_CLK_PLL0_TESTOUT]		= starfive_clk_gated_divider(priv, "pll0_testout",		"pll0_out",	0x40, 5);
	hws[JH7100_CLK_PLL1_TESTOUT]		= starfive_clk_gated_divider(priv, "pll1_testout",		"pll1_out",	0x44, 5);
	hws[JH7100_CLK_PLL2_TESTOUT]		= starfive_clk_gated_divider(priv, "pll2_testout",		"pll2_out",	0x48, 5);
	hws[JH7100_CLK_PLL2_REF]		= starfive_clk_mux(priv, "pll2_refclk",	0x4c, 1, pll2_refclk_sels, ARRAY_SIZE(pll2_refclk_sels));

	/*
	hws[JH7100_CLK_CPU_CORE]		= starfive_clk_fixed_factor(priv, "cpu_core", "cpunbus_root_div", 1, 1);
	hws[JH7100_CLK_CPU_AXI]			= starfive_clk_fixed_factor(priv, "cpu_axi", "cpu_core", 1, 2);
	hws[JH7100_CLK_AHB_BUS]			= starfive_clk_fixed_factor(priv, "ahb_bus", "cpunbus_root_div", 1, 4);
	hws[JH7100_CLK_APB1_BUS]		= starfive_clk_fixed_factor(priv, "apb1_bus", "ahb_bus", 1, 2);
	hws[JH7100_CLK_APB2_BUS]		= starfive_clk_fixed_factor(priv, "apb2_bus", "ahb_bus", 1, 2);
	*/

	/**/
	hws[JH7100_CLK_CPU_CORE]        = starfive_clk_divider(priv, "cpu_core", "cpunbus_root_div", 0x50, 4);
    hws[JH7100_CLK_CPU_AXI]         = starfive_clk_divider(priv, "cpu_axi", "cpu_core", 0x54, 4);
    hws[JH7100_CLK_AHB_BUS]         = clk_hw_register_divider_table(priv->dev, "ahb_bus", "cpunbus_root_div",
		                         CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE, priv->base + 0x58, 0, 4, 0, ahp_div_table, &priv->rmw_lock);
    hws[JH7100_CLK_APB1_BUS]        = starfive_clk_divider(priv, "apb1_bus", "ahb_bus",  0x5C, 4);
    hws[JH7100_CLK_APB2_BUS]		= starfive_clk_divider(priv,	"apb2_bus",	"ahb_bus",  0x60, 4);

	/*if you want to use this clock that you should use the function 'clk_prepare_enable()' 
	 *to enable the clock, otherwise it will have some problem.
	 *but this funtion 'clk_prepare_enable()' should be used in consumer.
	 */
	hws[JH7100_CLK_DOM3AHB_BUS]     = starfive_clk_gate(priv, "dom3ahb_bus", "ahb_bus", 0x64);
    hws[JH7100_CLK_DOM7AHB_BUS]     = starfive_clk_gate(priv, "dom7ahb_bus", "ahb_bus", 0x68);
	clk_prepare_enable(hws[JH7100_CLK_DOM3AHB_BUS]->clk);
	clk_prepare_enable(hws[JH7100_CLK_DOM7AHB_BUS]->clk);
	hws[JH7100_CLK_U74_CORE0]       = starfive_clk_gate(priv, "u74_core0", "cpu_core", 0x6C);
    hws[JH7100_CLK_U74_CORE1]       = starfive_clk_gated_divider(priv, "u74_core1", "cpu_core", 0x70, 4);
    hws[JH7100_CLK_U74_AXI]         = starfive_clk_gate(priv, "u74_axi", "cpu_axi", 0x74);
    hws[JH7100_CLK_U74RTC_TOGGLE]   = starfive_clk_gate(priv, "u74rtc_toggle", "osc_sys", 0x78);
	clk_prepare_enable(hws[JH7100_CLK_U74_CORE0]->clk);
	clk_prepare_enable(hws[JH7100_CLK_U74_CORE1]->clk);
	clk_prepare_enable(hws[JH7100_CLK_U74_AXI]->clk);
	clk_prepare_enable(hws[JH7100_CLK_U74RTC_TOGGLE]->clk);

	hws[JH7100_CLK_SGDMA2P_AXI]     = starfive_clk_gate(priv, "sgdma2p_axi", "cpu_axi", 0x7C);
    hws[JH7100_CLK_DMA2PNOC_AXI]    = starfive_clk_gate(priv, "dma2pnoc_axi", "cpu_axi", 0x80);
    hws[JH7100_CLK_SGDMA2P_AHB]     = starfive_clk_gate(priv, "sgdma2p_ahb", "ahb0_bus", 0x84);
    hws[JH7100_CLK_DLA_BUS]         = starfive_clk_divider(priv, "dla_bus", "dla_root", 0x88, 3);
	hws[JH7100_CLK_DLA_AXI]         = starfive_clk_gate(priv, "dla_axi", "dla_bus", 0x8C);
    hws[JH7100_CLK_DLANOC_AXI]      = starfive_clk_gate(priv, "dlanoc_axi", "dla_bus", 0x90);
    hws[JH7100_CLK_DLA_APB]         = starfive_clk_gate(priv, "dla_apb", "apb1_bus", 0x94);
    hws[JH7100_CLK_VP6_CORE]        = starfive_clk_gated_divider(priv, "vp6_core", "dsp_root_div", 0x98, 3);
    hws[JH7100_CLK_VP6BUS_SRC]      = starfive_clk_divider(priv, "vp6bus_src", "dsp_root", 0x9C, 3);
    hws[JH7100_CLK_VP6_AXI]         = starfive_clk_gated_divider(priv, "vp6_axi", "vp6bus_src", 0xA0, 3);  
    hws[JH7100_CLK_VCDECBUS_SRC]    = starfive_clk_divider(priv, "vcdecbus_src", "cdechifi4_root", 0xA4, 3);
    hws[JH7100_CLK_VDEC_BUS]        = starfive_clk_divider(priv, "vdec_bus", "vcdecbus_src", 0xA8, 4);
    hws[JH7100_CLK_VDEC_AXI]        = starfive_clk_gate(priv, "vdec_axi", "vdec_bus", 0xAC);
    hws[JH7100_CLK_VDECBRG_MAIN]    = starfive_clk_gate(priv, "vdecbrg_mainclk", "vdec_bus", 0xB0);
    hws[JH7100_CLK_VDEC_BCLK]       = starfive_clk_gated_divider(priv, "vdec_bclk", "vcdecbus_src", 0xB4, 4);
    hws[JH7100_CLK_VDEC_CCLK]       = starfive_clk_gated_divider(priv, "vdec_cclk", "cdec_root", 0xB8, 4);
    hws[JH7100_CLK_VDEC_APB]        = starfive_clk_gate(priv, "vdec_apb", "apb1_bus", 0xBC);
    hws[JH7100_CLK_JPEG_AXI]        = starfive_clk_gated_divider(priv, "jpeg_axi", "cpunbus_root_div", 0xC0, 4);
    hws[JH7100_CLK_JPEG_CCLK]       = starfive_clk_gated_divider(priv, "jpeg_cclk", "cpunbus_root_div", 0xC4, 4);
    hws[JH7100_CLK_JPEG_APB]        = starfive_clk_gate(priv, "jpeg_apb", "apb1_bus", 0xC8);
	hws[JH7100_CLK_GC300_2X]        = starfive_clk_gated_divider(priv, "gc300_2x", "cdechifi4_root", 0xCC, 4);
	//'ahb0_bus' have not been definded
    hws[JH7100_CLK_GC300_AHB]       = starfive_clk_gate(priv, "gc300_ahb", "ahb0_bus", 0xD0);
    hws[JH7100_CLK_JPCGC300_AXIBUS] = starfive_clk_divider(priv, "jpcgc300_axibus", "vcdecbus_src", 0xD4, 4);
    hws[JH7100_CLK_GC300_AXI]       = starfive_clk_gate(priv, "gc300_axi",  "jpcgc300_axibus", 0xD8);
    hws[JH7100_CLK_JPCGC300_MAIN]   = starfive_clk_gate(priv, "jpcgc300_mainclk", "jpcgc300_axibus", 0xDC);
    hws[JH7100_CLK_VENC_BUS]        = starfive_clk_divider(priv, "venc_bus","vcdecbus_src", 0xE0, 4);
    hws[JH7100_CLK_VENC_AXI]        = starfive_clk_gate(priv, "venc_axi", "venc_bus", 0xE4);
    hws[JH7100_CLK_VENCBRG_MAIN]    = starfive_clk_gate(priv, "vencbrg_mainclk", "venc_bus", 0xE8);
    hws[JH7100_CLK_VENC_BCLK]       = starfive_clk_gated_divider(priv, "venc_bclk", "vcdecbus_src", 0xEC, 4);
    hws[JH7100_CLK_VENC_CCLK]       = starfive_clk_gated_divider(priv, "venc_cclk", "cdec_root", 0xF0, 4);
    hws[JH7100_CLK_VENC_APB]        = starfive_clk_gate(priv, "venc_apb", "apb1_bus", 0xF4);
	//'ddr_root' have not been definded
	hws[JH7100_CLK_DDRPLL_DIV2]     = starfive_clk_gated_divider(priv, "ddrpll_div2", "ddr_root", 0xF8, 2);
    hws[JH7100_CLK_DDRPLL_DIV4]     = starfive_clk_gated_divider(priv, "ddrpll_div4", "ddrpll_div2", 0xFC, 2);
    hws[JH7100_CLK_DDRPLL_DIV8]     = starfive_clk_gated_divider(priv, "ddrpll_div8", "ddrpll_div4", 0x100, 2);
    hws[JH7100_CLK_DDROSC_DIV2]     = starfive_clk_gated_divider(priv, "ddrosc_div2", "osc_sys", 0x104, 2);
	clk_prepare_enable(hws[JH7100_CLK_DDRPLL_DIV2]->clk);
	clk_prepare_enable(hws[JH7100_CLK_DDRPLL_DIV4]->clk);
	clk_prepare_enable(hws[JH7100_CLK_DDRPLL_DIV8]->clk);
	clk_prepare_enable(hws[JH7100_CLK_DDROSC_DIV2]->clk);

	hws[JH7100_CLK_DDRC0]           = starfive_clk_gated_mux(priv, "ddrc0", 0x108, 2, 
                                                        ddrc0_sels, ARRAY_SIZE(ddrc0_sels));
	hws[JH7100_CLK_DDRC1]           = starfive_clk_gated_mux(priv, "ddrc1", 0x10C, 2,
                                                        ddrc1_sels, ARRAY_SIZE(ddrc1_sels));
	clk_prepare_enable(hws[JH7100_CLK_DDRC0]->clk);
	clk_prepare_enable(hws[JH7100_CLK_DDRC1]->clk);
	hws[JH7100_CLK_DDRPHY_APB]      = starfive_clk_gate(priv, "ddrphy_apb", "apb1_bus", 0x110);
    hws[JH7100_CLK_NOC_ROB]         = starfive_clk_divider(priv, "noc_rob", "cpunbus_root_div", 0x114, 4);
    hws[JH7100_CLK_NOC_COG]         = starfive_clk_divider(priv, "noc_cog", "dla_root", 0x118, 4);
    //'ahb0_bus' have not been definded
	hws[JH7100_CLK_NNE_AHB]         = starfive_clk_gate(priv, "nne_ahb", "ahb0_bus", 0x11C);
    hws[JH7100_CLK_NNEBUS_SRC1]     = starfive_clk_divider(priv, "nnebus_src1", "dsp_root", 0x120, 3);
	hws[JH7100_CLK_NNE_BUS]			= starfive_clk_mux(priv, "nne_bus", 0x124, 1, nne_bus_sels, ARRAY_SIZE(nne_bus_sels));
	hws[JH7100_CLK_NNE_AXI]			= starfive_clk_gate(priv, "nne_axi", "nne_bus", 0x128);	
	hws[JH7100_CLK_NNENOC_AXI]		= starfive_clk_gate(priv, "nnenoc_axi", "nne_bus", 0x12c);	
	hws[JH7100_CLK_DLASLV_AXI]		= starfive_clk_gate(priv, "dlaslv_axi", "nne_bus", 0x130);
	hws[JH7100_CLK_DSPX2C_AXI]		= starfive_clk_gate(priv, "dspx2c_axi", "nne_bus", 0x134);
	clk_prepare_enable(hws[JH7100_CLK_DLASLV_AXI]->clk);
	clk_prepare_enable(hws[JH7100_CLK_DSPX2C_AXI]->clk);

	/*'AUDSP'='HIFI4'?*/
	hws[JH7100_CLK_HIFI4_SRC]		= starfive_clk_divider(priv, "hifi4_src", "cdechifi4_root", 0x138, 3);
	hws[JH7100_CLK_HIFI4_COREFREE]	= starfive_clk_divider(priv, "hifi4_corefree", "hifi4_src", 0x13c, 4);
	hws[JH7100_CLK_HIFI4_CORE]		= starfive_clk_gate(priv, "hifi4_core", "hifi4_corefree", 0x140);
	hws[JH7100_CLK_HIFI4_BUS]		= starfive_clk_divider(priv, "hifi4_bus", "hifi4_corefree", 0x144, 4);
	hws[JH7100_CLK_HIFI4_AXI]		= starfive_clk_gate(priv, "hifi4_axi", "hifi4_bus", 0x148);
	hws[JH7100_CLK_HIFI4NOC_AXI]	= starfive_clk_gate(priv, "hifi4noc_axi", "hifi4_bus", 0x14c);
	hws[JH7100_CLK_SGDMA1P_BUS]		= starfive_clk_divider(priv, "sgdma1p_bus", "cpunbus_root_div", 0x150, 4);
	hws[JH7100_CLK_SGDMA1P_AXI]		= starfive_clk_gate(priv, "sgdma1p_axi", "sgdma1p_bus", 0X154);
	hws[JH7100_CLK_DMA1P_AXI]		= starfive_clk_gate(priv, "dma1p_axi", "sgdma1p_bus", 0x158);
	clk_prepare_enable(hws[JH7100_CLK_DMA1P_AXI]->clk);
	hws[JH7100_CLK_X2C_AXI]			= starfive_clk_gated_divider(priv, "x2c_axi", "cpunbus_root_div", 0x15c, 4);
	clk_prepare_enable(hws[JH7100_CLK_X2C_AXI]->clk);
	hws[JH7100_CLK_USB_BUS]			= starfive_clk_divider(priv, "usb_bus", "cpunbus_root_div", 0x160, 4);
	hws[JH7100_CLK_USB_AXI]			= starfive_clk_gate(priv, "usb_axi", "usb_bus", 0x164);
	hws[JH7100_CLK_USBNOC_AXI]		= starfive_clk_gate(priv, "usbnoc_axi", "usb_bus", 0x168);
	hws[JH7100_CLK_USBPHY_ROOTDIV]	= starfive_clk_divider(priv, "usbphy_rootdiv", "gmacusb_root", 0x16c, 3);
	hws[JH7100_CLK_USBPHY_125M]		= starfive_clk_gated_divider(priv, "usbphy_125m", "usbphy_rootdiv", 0x170, 4);
	hws[JH7100_CLK_USBPHY_PLLDIV25M]= starfive_clk_gated_divider(priv, "usbphy_plldiv25m", "usbphy_rootdiv", 0x174, 6);
	hws[JH7100_CLK_USBPHY_25M]		= starfive_clk_mux(priv, "usbphy_25m", 0x178, 1, usbphy_25m_sels, ARRAY_SIZE(usbphy_25m_sels));

	//hws[JH7100_CLK_AUDIO_DIV]		= starfive_clk_divider(priv, "audio_div",		"audio_root",	0x17c, 18);
	//1000M ====> 24.576M, use fixed factor to replace the 40.69 divider.
	hws[JH7100_CLK_AUDIO_DIV]		= starfive_clk_fixed_factor(priv, "audio_div",		"audio_root",	384,	15625);
	hws[JH7100_CLK_AUDIO_SRC]		= starfive_clk_gate(priv, "audio_src",		"audio_div",	0x180);
	hws[JH7100_CLK_AUDIO_12288]		= starfive_clk_gate(priv, "audio_12288",		"audio_src_12288",	0x184);	
	hws[JH7100_CLK_VIN_SRC]			= starfive_clk_gated_divider(priv, "vin_src",		"vin_root",	0x188, 3);
	hws[JH7100_CLK_ISP0_BUS]		= starfive_clk_divider_o(priv, "isp0_bus",		"vin_src",	0x18c, 4);
	hws[JH7100_CLK_ISP0_AXI]		= starfive_clk_gate(priv, "isp0_axi",		"isp0_bus",	0x190);
	hws[JH7100_CLK_ISP0NOC_AXI]		= starfive_clk_gate(priv, "isp0noc_axi",		"isp0_bus",	0x194);
	hws[JH7100_CLK_ISPSLV_AXI]		= starfive_clk_gate(priv, "ispslv_axi",		"isp0_bus",	0x198);
	hws[JH7100_CLK_ISP1_BUS]		= starfive_clk_divider_o(priv, "isp1_bus",		"vin_src",	0x19c, 4);
	hws[JH7100_CLK_ISP1_AXI]		= starfive_clk_gate(priv, "isp1_axi",		"isp1_bus",	0x1a0);
	hws[JH7100_CLK_ISP1NOC_AXI]		= starfive_clk_gate(priv, "isp1noc_axi",		"isp1_bus",	0x1a4);
	hws[JH7100_CLK_VIN_BUS]			= starfive_clk_divider_o(priv, "vin_bus",		"vin_src",	0x1a8, 4);
	hws[JH7100_CLK_VIN_AXI]			= starfive_clk_gate(priv, "vin_axi",		"vin_bus",	0x1ac);
	hws[JH7100_CLK_VINNOC_AXI]		= starfive_clk_gate(priv, "vinnoc_axi",		"vin_bus",	0x1b0);


	/**/
	hws[JH7100_CLK_VOUT_SRC]		= starfive_clk_gated_divider(priv, "vout_src", "vout_root", 0x1b4, 3);
	hws[JH7100_CLK_DISPBUS_SRC]		= starfive_clk_divider(priv, "dispbus_src", "voutbus_root", 0x1b8, 3);
	hws[JH7100_CLK_DISP_BUS]		= starfive_clk_divider(priv, "disp_bus", "dispbus_src", 0x1bc, 3);
	hws[JH7100_CLK_DISP_AXI]		= starfive_clk_gate(priv, "disp_axi", "disp_bus", 0x1c0);
	hws[JH7100_CLK_DISPNOC_AXI]		= starfive_clk_gate(priv, "dispnoc_axi", "disp_bus", 0x1c4);
		

	hws[JH7100_CLK_SDIO0_AHB]		= starfive_clk_gate(priv, "sdio0_ahb",		"ahb_bus",	0x1c8);
	hws[JH7100_CLK_SDIO0_CCLKINT]	= starfive_clk_gated_divider(priv, "sdio0_cclkint",		"perh0_src",	0x1cc, 5);
	hws[JH7100_CLK_SDIO0_CCLKINT_INV]= starfive_clk_gate_dis(priv, "sdio0_cclkint_inv",		"sdio0_cclkint",	0x1d0);
	hws[JH7100_CLK_SDIO1_AHB]		= starfive_clk_gate(priv, "sdio1_ahb",		"ahb_bus",	0x1d4);
	hws[JH7100_CLK_SDIO1_CCLKINT]	= starfive_clk_gated_divider(priv, "sdio1_cclkint",		"perh1_src",	0x1d8, 5);
	hws[JH7100_CLK_SDIO1_CCLKINT_INV]= starfive_clk_gate_dis(priv, "sdio1_cclkint_inv",		"sdio1_cclkint",	0x1dc);

	hws[JH7100_CLK_GMAC_AHB]		= starfive_clk_gate(priv, "gmac_ahb",		"ahb_bus",	0x1e0);
	hws[JH7100_CLK_GMAC_ROOT_DIV]	= starfive_clk_divider(priv, "gmac_root_div",		"gmacusb_root",	0x1e4, 4);
	hws[JH7100_CLK_GMAC_PTP_REF]	= starfive_clk_gated_divider(priv, "gmac_ptp_refclk",		"gmac_root_div",	0x1e8, 5);
	hws[JH7100_CLK_GMAC_GTX]		= starfive_clk_gated_divider(priv, "gmac_gtxclk",		"gmac_root_div",	0x1ec, 8);
	hws[JH7100_CLK_GMAC_RMII_TX]	= starfive_clk_gated_divider(priv, "gmac_rmii_txclk",		"gmac_rmii_ref",	0x1f0, 4);
	hws[JH7100_CLK_GMAC_RMII_RX]	= starfive_clk_gated_divider(priv, "gmac_rmii_rxclk",		"gmac_rmii_ref",	0x1f4, 4);
	hws[JH7100_CLK_GMAC_TX]			= starfive_clk_mux(priv, "gmac_tx",	0x1f8, 2, gmac_tx_sels, ARRAY_SIZE(gmac_tx_sels));
	hws[JH7100_CLK_GMAC_TX_INV]		= starfive_clk_gate_dis(priv, "gmac_tx_inv",	"gmac_tx",	0x1fc);
	hws[JH7100_CLK_GMAC_RX_PRE]		= starfive_clk_mux(priv, "gmac_rx_pre",	0x200, 1, gmac_rx_pre_sels, ARRAY_SIZE(gmac_rx_pre_sels));
	//"gmac_rx" have not been defined
	//"gmac_rmii_ref" have not been defined
	//"gmac_rx_dly" have not been defined
	hws[JH7100_CLK_GMAC_RX_INV]		= starfive_clk_gate_dis(priv, "gmac_rx_inv",	"gmac_rx_dly",	0x204);
	hws[JH7100_CLK_GMAC_RMII]		= starfive_clk_gate(priv, "gmac_rmii",	"gmac_rmii_ref",	0x208);
	hws[JH7100_CLK_GMAC_TOPHYREF]	= starfive_clk_gated_divider(priv, "gmac_tophyref",	"gmac_root_div",	0x20c, 7);


	/**/
	hws[JH7100_CLK_SPI2AHB_AHB]		= starfive_clk_gate(priv, "spi2ahb_ahb", "ahb0_bus", 0x210);
	hws[JH7100_CLK_SPI2AHB_CORE]	= starfive_clk_gated_divider(priv, "spi2ahb_core", "perh0_src", 0x214, 5);
	hws[JH7100_CLK_EZMASTER_AHB]	= starfive_clk_gate(priv, "ezmaster_ahb", "ahb0_bus", 0x218);
	hws[JH7100_CLK_E24_AHB]			= starfive_clk_gate(priv, "e24_ahb", "ahb_bus", 0x21c);
	hws[JH7100_CLK_E24RTC_TOGGLE]	= starfive_clk_gate(priv, "e24rtc_toggle", "osc_sys", 0x220);
	hws[JH7100_CLK_QSPI_AHB]		= starfive_clk_gate(priv,"qspi_ahb", "ahb0_bus", 0x224);
	hws[JH7100_CLK_QSPI_APB]		= starfive_clk_gate(priv, "qspi_apb", "apb1_bus", 0x228);
	hws[JH7100_CLK_QSPI_REF]		= starfive_clk_gated_divider(priv, "qspi_reclk", "perh0_src", 0x22c, 5);
	hws[JH7100_CLK_SEC_AHB]			= starfive_clk_gate(priv, "sec_ahb", "ahb0_bus", 0x230);
	hws[JH7100_CLK_AES]				= starfive_clk_gate(priv, "aes_clk", "sec_ahb", 0x234);
	hws[JH7100_CLK_SHA]				= starfive_clk_gate(priv, "sha_clk", "sec_ahb", 0x238);
	hws[JH7100_CLK_PKA]				= starfive_clk_gate(priv, "pka_clk", "sec_ahb", 0x23c);
	hws[JH7100_CLK_TRNG_APB]		= starfive_clk_gate(priv, "trng_apb", "apb1_bus", 0x240);
	hws[JH7100_CLK_OTP_APB]			= starfive_clk_gate(priv, "otp_apb", "apb1_bus", 0x244);
		

	hws[JH7100_CLK_UART0_APB]		= starfive_clk_gate(priv, "uart0_apb",		"apb1_bus",	0x248);
	hws[JH7100_CLK_UART0_CORE]		= starfive_clk_gated_divider(priv, "uart0_core",		"perh1_src",	0x24c, 6);
	hws[JH7100_CLK_UART1_APB]		= starfive_clk_gate(priv, "uart1_apb",		"apb1_bus",	0x250);
	hws[JH7100_CLK_UART1_CORE]		= starfive_clk_gated_divider(priv, "uart1_core",		"perh1_src",	0x254, 6);

	
	/**/
	hws[JH7100_CLK_SPI0_APB]		= starfive_clk_gate(priv, "spi0_apb", "apb1_bus", 0x258);
	hws[JH7100_CLK_SPI0_CORE]		= starfive_clk_gated_divider(priv, "spi0_core", "perh1_src", 0x25c, 6);
	hws[JH7100_CLK_SPI1_APB]		= starfive_clk_gate(priv, "spi1_apb", "apb1_bus", 0x260);
	hws[JH7100_CLK_SPI1_CORE]		= starfive_clk_gated_divider(priv, "spi1_core", "perh1_src", 0x264, 6);

	
	hws[JH7100_CLK_I2C0_APB]		= starfive_clk_gate(priv, "i2c0_apb",		"apb1_bus",	0x268);
	hws[JH7100_CLK_I2C0_CORE]		= starfive_clk_gated_divider(priv, "i2c0_core",		"perh1_src",	0x26c, 6);
	hws[JH7100_CLK_I2C1_APB]		= starfive_clk_gate(priv, "i2c1_apb",		"apb1_bus",	0x270);
	hws[JH7100_CLK_I2C1_CORE]		= starfive_clk_gated_divider(priv, "i2c1_core",		"perh1_src",	0x274, 6);

	/**/
	hws[JH7100_CLK_GPIO_APB]		= starfive_clk_gate(priv, "gpio_apb", "apb1_bus", 0x278);

	hws[JH7100_CLK_UART2_APB]		= starfive_clk_gate(priv, "uart2_apb",		"apb2_bus",	0x27c);
	hws[JH7100_CLK_UART2_CORE]		= starfive_clk_gated_divider(priv, "uart2_core",	"perh0_src",	0x280, 6);
	hws[JH7100_CLK_UART3_APB]		= starfive_clk_gate(priv, "uart3_apb",		"apb2_bus",	0x284);
	hws[JH7100_CLK_UART3_CORE]		= starfive_clk_gated_divider(priv, "uart3_core",	"perh0_src",	0x288, 6);
	//clk_set_rate(hws[JH7100_CLK_UART3_CORE]->clk, 120000000);

	/**/
	hws[JH7100_CLK_SPI2_APB]		= starfive_clk_gate(priv, "spi2_apb", "apb2_bus", 0x28c);
	hws[JH7100_CLK_SPI2_CORE]		= starfive_clk_gated_divider(priv, "spi2_core", "perh0_src", 0x290, 6);
	hws[JH7100_CLK_SPI3_APB]		= starfive_clk_gate(priv, "spi3_apb", "apb2_bus", 0x294);
	hws[JH7100_CLK_SPI3_CORE]		= starfive_clk_gated_divider(priv, "spi3_core", "perh0_src", 0x298, 6);


	hws[JH7100_CLK_I2C2_APB]		= starfive_clk_gate(priv, "i2c2_apb",		"apb2_bus",	0x29c);
	hws[JH7100_CLK_I2C2_CORE]		= starfive_clk_gated_divider(priv, "i2c2_core",		"perh0_src",	0x2a0, 6);
	hws[JH7100_CLK_I2C3_APB]		= starfive_clk_gate(priv, "i2c3_apb",		"apb2_bus",	0x2a4);
	hws[JH7100_CLK_I2C3_CORE]		= starfive_clk_gated_divider(priv, "i2c3_core",		"perh0_src",	0x2a8, 6);
	hws[JH7100_CLK_WDTIMER_APB]		= starfive_clk_gate(priv, "wdtimer_apb",		"apb2_bus",	0x2ac);
	hws[JH7100_CLK_WDT_CORE]		= starfive_clk_gated_divider(priv, "wdt_coreclk",		"perh0_src",	0x2b0, 6);

	/**/
	hws[JH7100_CLK_TIMER0_CORE]		= starfive_clk_gated_divider(priv, "timer0_coreclk", "perh0_src", 0x2b4, 6);
	hws[JH7100_CLK_TIMER1_CORE]		= starfive_clk_gated_divider(priv, "timer1_coreclk", "perh0_src", 0x2b8, 6);
	hws[JH7100_CLK_TIMER2_CORE]		= starfive_clk_gated_divider(priv, "timer2_coreclk", "perh0_src", 0x2bc, 6);
	hws[JH7100_CLK_TIMER3_CORE]		= starfive_clk_gated_divider(priv, "timer3_coreclk", "perh0_src", 0x2c0, 6);
	hws[JH7100_CLK_TIMER4_CORE]		= starfive_clk_gated_divider(priv, "timer4_coreclk", "perh0_src", 0x2c4, 6);
	hws[JH7100_CLK_TIMER5_CORE]		= starfive_clk_gated_divider(priv, "timer5_coreclk", "perh0_src", 0x2c8, 6);
	hws[JH7100_CLK_TIMER6_CORE]		= starfive_clk_gated_divider(priv, "timer6_coreclk", "perh0_src", 0x2cc, 6);
	hws[JH7100_CLK_VP6INTC_APB]		= starfive_clk_gate(priv, "vp6intc_apb", "apb2_bus", 0x2d0);
	hws[JH7100_CLK_PWM_APB]			= starfive_clk_gate(priv, "pwm_apb", "apb2_bus", 0x2d4);
	hws[JH7100_CLK_MSI_APB]			= starfive_clk_gate(priv, "msi_apb", "apb2_bus", 0x2d8);
	hws[JH7100_CLK_TEMP_APB]		= starfive_clk_gate(priv, "temp_apb", "apb2_bus", 0x2dc);
	hws[JH7100_CLK_TEMP_SENSE]		= starfive_clk_gated_divider(priv, "temp_sense", "osc_sys", 0x2e0, 5);
	hws[JH7100_CLK_SYSERR_APB]		= starfive_clk_gate(priv, "syserr_apb", "apb2_bus", 0x2e4);

	/*fixed the clock*/
	hws[JH7100_CLK_DDR_ROOT]		= starfive_clk_underspecifid(priv, "ddr_root", "pll1_out");
	hws[JH7100_CLK_AHB0_BUS]		= starfive_clk_underspecifid(priv, "ahb0_bus", "ahb_bus");

	/*
	clk_disable_unprepare(hws[JH7100_CLK_OSC_AUD]->clk);
	clk_set_rate(hws[JH7100_CLK_OSC_AUD]->clk, 25000000);
	clk_prepare_enable(hws[JH7100_CLK_OSC_AUD]->clk);
	*/

	for (i = 0; i < JH7100_CLK_SYS_MAX; i++)
		if (IS_ERR(hws[i])) {
			dev_err(priv->dev, "clock %d failed to register\n", i);
			ret = PTR_ERR(hws[i]);
			goto err_clk_register;
		}

	
	return 0;

err_clk_register:
	for (i = 0; i < JH7100_CLK_SYS_MAX; i++)
		if (hws[i] && !IS_ERR(hws[i]))
			clk_hw_unregister(hws[i]);

	return ret;
}

static int __init starfive_clkgen_audio_init(struct clk_starfive_jh7100_priv *priv)
{
	struct clk_hw **hws = priv->clk_hws.hws;
	int i;
	int ret;

	hws[JH7100_CLK_ADC_MCLK]	= starfive_clk_composite(priv, "adc_mclk", audio_clk_sels, ARRAY_SIZE(audio_clk_sels), 0x0, 1, 1, 4);
	hws[JH7100_CLK_I2S1_MCLK]	= starfive_clk_composite(priv, "i2s1_mclk", audio_clk_sels, ARRAY_SIZE(audio_clk_sels), 0x4, 1, 1, 4);
	hws[JH7100_CLK_APB_I2SADC_EN]	= starfive_clk_gate(priv, "i2sadc_apb", "apb2_bus", 0x8);
	hws[JH7100_CLK_I2SADC_BCLK]	= starfive_clk_composite(priv, "i2sadc_bclk", i2sadc_bclk_sels, ARRAY_SIZE(i2sadc_bclk_sels), 0xc, 1, 0, 5);
	hws[JH7100_CLK_I2SADC_BCLK_INV]	= starfive_clk_gate_dis(priv, "i2sadc_bclk_n", "i2sadc_bclk",	0x10);
	hws[JH7100_CLK_I2SADC_LRCLK]	= starfive_clk_composite(priv, "i2sadc_lrclk", i2sadc_lrclk_sels, ARRAY_SIZE(i2sadc_lrclk_sels), 0x14, 2, 0, 6);
	hws[JH7100_CLK_APB_PDM_EN]	= starfive_clk_gate(priv, "pdm_apb", "apb2_bus", 0x18);
	hws[JH7100_CLK_PDM_MCLK]	= starfive_clk_composite(priv, "pdm_mclk", audio_clk_sels, ARRAY_SIZE(audio_clk_sels), 0x1c, 1, 1, 4);
	hws[JH7100_CLK_APB_I2SVAD_EN]	= starfive_clk_gate(priv, "i2svad_apb", "apb2_bus", 0x20);
	hws[JH7100_CLK_SPDIF]		= starfive_clk_composite(priv, "spdif_mclk", audio_clk_sels, ARRAY_SIZE(audio_clk_sels), 0x24, 1, 1, 4);
	hws[JH7100_CLK_APB_SPDIF_EN]	= starfive_clk_gate(priv, "spdif_apb", "apb2_bus", 0x28);
	hws[JH7100_CLK_APB_PWMDAC_EN]	= starfive_clk_gate(priv, "pwmdac_apb", "apb2_bus", 0x2c);
	hws[JH7100_CLK_DAC_MCLK]	= starfive_clk_composite(priv, "dac_mclk", audio_clk_sels, ARRAY_SIZE(audio_clk_sels), 0x30, 1, 1, 4);
	hws[JH7100_CLK_APB_I2S0_EN]	= starfive_clk_gate(priv, "i2sdac_apb", "apb2_bus", 0x34);
	hws[JH7100_CLK_I2S0_BCLK]	= starfive_clk_composite(priv, "i2sdac0_bclk", i2sdac0_bclk_sels, ARRAY_SIZE(i2sdac0_bclk_sels), 0x38, 1, 0, 5);
	hws[JH7100_CLK_I2S0_BCLK_INV]	= starfive_clk_gate_dis(priv, "clk_i2sdac0_bclk_n", "clk_i2sdac0_bclk", 0x3c);
	hws[JH7100_CLK_I2S0_LRCLK]	= starfive_clk_composite(priv, "i2sdac0_lrclk", i2sdac0_lrclk_sels, ARRAY_SIZE(i2sdac0_lrclk_sels), 0x40, 2, 0, 6);
	hws[JH7100_CLK_APB_I2S1_EN]	= starfive_clk_gate(priv, "i2s1_apb", "apb2_bus", 0x44);
	hws[JH7100_CLK_I2S1_BCLK]	= starfive_clk_composite(priv, "i2sdac1_bclk", i2sdac1_bclk_sels, ARRAY_SIZE(i2sdac1_bclk_sels), 0x48, 1, 0, 5);
	hws[JH7100_CLK_I2S1_BCLK_INV]	= starfive_clk_gate_dis(priv, "clk_i2sdac1_bclk_n", "clk_i2sdac1_bclk", 0x4c);
	hws[JH7100_CLK_I2S1_LRCLK]	= starfive_clk_composite(priv, "i2sdac1_lrclk", i2sdac1_lrclk_sels, ARRAY_SIZE(i2sdac1_lrclk_sels), 0x50, 2, 0, 6);

	//ext clk
	hws[JH7100_CLK_ADC_BCLK_IO]	= starfive_clk_fixed_rate(priv, "i2sadc_bclk_iopad", NULL, 12288000);
	hws[JH7100_CLK_ADC_LRCLK_IO]	= starfive_clk_fixed_rate(priv, "i2sadc_lrclk_iopad", NULL, 12288000);
	hws[JH7100_CLK_DAC0_BCLK_IO]	= starfive_clk_fixed_rate(priv, "i2sdac0_bclk_iopad", NULL, 12288000);
	hws[JH7100_CLK_DAC0_LRCLK_IO]	= starfive_clk_fixed_rate(priv, "i2sdac0_lrclk_iopad", NULL, 12288000);
	hws[JH7100_CLK_DAC1_BCLK_IO]	= starfive_clk_fixed_rate(priv, "i2sadc1_bclk_iopad", NULL, 12288000);
	hws[JH7100_CLK_DAC1_LRCLK_IO]	= starfive_clk_fixed_rate(priv, "i2sdac1_lrclk_iopad", NULL, 12288000);
	hws[JH7100_CLK_CODEC_EXT]	= starfive_clk_fixed_rate(priv, "codec_ext_clock", NULL, 24576000);
	hws[JH7100_CLK_AUD_12288]	= starfive_clk_fixed_rate(priv, "audio_src_12288", NULL, 12288000);

	for (i = 0; i < JH7100_CLK_AUD_MAX; i++)
		if (IS_ERR(hws[i])) {
			dev_err(priv->dev, "clock %d failed to register\n", i);
			ret = PTR_ERR(hws[i]);
			goto err_clk_register;
		}

	return 0;

err_clk_register:
	for (i = 0; i < JH7100_CLK_AUD_MAX; i++)
		if (hws[i] && !IS_ERR(hws[i]))
			clk_hw_unregister(hws[i]);

	return ret;
}

static int __init starfive_clkgen_isp_init(struct clk_starfive_jh7100_priv *priv)
{
	struct clk_hw **hws = priv->clk_hws.hws;
	int i;
	int ret;

	for (i = 0; i < JH7100_CLK_ISP_MAX; i++)
		if (IS_ERR(hws[i])) {
			dev_err(priv->dev, "clock %d failed to register\n", i);
			ret = PTR_ERR(hws[i]);
			goto err_clk_register;
		}

	return 0;

err_clk_register:
	for (i = 0; i < JH7100_CLK_ISP_MAX; i++)
		if (hws[i] && !IS_ERR(hws[i]))
			clk_hw_unregister(hws[i]);

	return ret;
}

static int starfive_clk_sys_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_starfive_jh7100_priv *priv;
	int error;

	priv = devm_kzalloc(dev, struct_size(priv, clk_hws.hws, JH7100_CLK_SYS_MAX), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->clk_hws.num = JH7100_CLK_SYS_MAX;
	spin_lock_init(&priv->rmw_lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	error = starfive_clkgen_sys_init(priv);
	if (error)
		return error;

	error = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					    &priv->clk_hws);
	if (error)
		return error;

	return 0;
}
static int __init starfive_clk_audio_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_starfive_jh7100_priv *priv;
	int error;

	priv = devm_kzalloc(dev, struct_size(priv, clk_hws.hws, JH7100_CLK_AUD_MAX), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->clk_hws.num = JH7100_CLK_AUD_MAX;
	spin_lock_init(&priv->rmw_lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	error = starfive_clkgen_audio_init(priv);
	if (error)
		return error;

	error = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					    &priv->clk_hws);
	if (error)
		return error;

	return 0;
}

static int __init starfive_clk_isp_init(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_starfive_jh7100_priv *priv;
	int error;

	priv = devm_kzalloc(dev, struct_size(priv, clk_hws.hws, JH7100_CLK_ISP_MAX), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->clk_hws.num = JH7100_CLK_ISP_MAX;
	spin_lock_init(&priv->rmw_lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	error = starfive_clkgen_isp_init(priv);
	if (error)
		return error;

	error = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					    &priv->clk_hws);
	if (error)
		return error;

	return 0;
}

static int __init starfive_clk_vout_init(struct platform_device *pdev)
{
	return 0;
}

static int __init clk_starfive_jh7100_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int (*init_func)(struct platform_device *pdev);

	init_func = of_device_get_match_data(dev);
	if (!init_func)
		return -ENODEV;

	printk("%s clk definition success!", pdev->name);

	return init_func(pdev);
}

static const struct of_device_id clk_starfive_jh7100_match[] = {
	{
		.compatible = "starfive,jh7100-clkgen-sys",
		.data = starfive_clk_sys_init
	},
	/*
	{
		.compatible = "starfive,jh7100-clkgen-audio",
		.data = starfive_clk_audio_init
	},
	{
		.compatible = "starfive,jh7100-clkgen-isp",
		.data = starfive_clk_isp_init
	},
	{
		.compatible = "starfive,jh7100-clkgen-vout",
		.data = starfive_clk_vout_init
	},
	*/
	{ /* sentinel */ }
};

static struct platform_driver clk_starfive_jh7100_driver = {
	.driver		= {
		.name	= "clk-starfive-jh7100",
		.of_match_table = clk_starfive_jh7100_match,
	},
};

static int __init clk_starfive_jh7100_init(void)
{
	return platform_driver_probe(&clk_starfive_jh7100_driver,
				     clk_starfive_jh7100_probe);
}

subsys_initcall(clk_starfive_jh7100_init);

MODULE_DESCRIPTION("StarFive JH7100 Clock Generator Driver");
MODULE_AUTHOR("Geert Uytterhoeven <geert@linux-m68k.org>");
MODULE_AUTHOR("Micheal Yan <michael.yan@starfivetech.com>");
MODULE_AUTHOR("Xingyu Wu <xingyu.wu@starfivetech.com>");
MODULE_LICENSE("GPL v2");
