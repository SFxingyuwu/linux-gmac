// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 */

#include <dt-bindings/clock/starfive-jh7100.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>


//#include "clk-vic7100-test.h"

#define STARFIVE_CLK_ENABLE_SHIFT	31
#define STARFIVE_CLK_INVERT_SHIFT	30
#define STARFIVE_CLK_MUX_SHIFT		24
#define STARFIVE_CLK_DIVIDER_SHIFT	0

struct clk_starfive_priv {
    spinlock_t rmw_lock;
    struct device *dev;
    void __iomem *base;
    struct clk_hw_onecell_data clk_hws;
};

static struct clk_hw * __init starfive_clk_hw_fixed_rate(struct clk_starfive_priv *priv, 
                                                const char *name, 
                                                const char *parent,int rate)
{
    return clk_hw_register_fixed_rate(priv->dev, name, parent, 0, rate);
}

static struct clk_hw * __init starfive_clk_hw_fixed_factor(struct clk_starfive_priv *priv,
                                                    const char *name, 
                                                    const char *parent)
{
    return clk_hw_register_fixed_factor(priv->dev, name, parent, 
                                    0, 1, 1);
}

static struct clk_hw * __init starfive_clk_mult(struct clk_starfive_priv *priv,
                                            const char *name, 
                                            const char *parent,
                                            unsigned int mult)
{
    return clk_hw_register_fixed_factor(priv->dev, name, parent, 0, mult, 1);
}

static struct clk_hw * __init starfive_clk_divider(struct clk_starfive_priv *priv,
                                            const char *name, 
                                            const char *parent,
                                            unsigned int offset, 
                                            unsigned int width)
{
    return clk_hw_register_divider(priv->dev, name, parent, CLK_SET_RATE_PARENT,
				       priv->base + offset, STARFIVE_CLK_DIVIDER_SHIFT, 
                       width, CLK_DIVIDER_ONE_BASED, &priv->rmw_lock);
}

static struct clk_hw * __init starfive_clk_gate(struct clk_starfive_priv *priv,
                                        const char *name,
                                        const char *parent,
                                        unsigned int offset)
{
    return clk_hw_register_gate(priv->dev, name, parent, 
                                CLK_SET_RATE_PARENT, priv->base + offset,
                                STARFIVE_CLK_ENABLE_SHIFT, 0, &priv->rmw_lock);
}

static struct clk_hw * __init starfive_clk_underspecifid(struct clk_starfive_priv *priv,
							 const char *name,
							 const char *parent)
{
	/*
	 * TODO With documentation available, all users of this functions can be
	 * migrated to one of the above or to a clk_fixed_factor with
	 * appropriate factor
	 */
	return clk_hw_register_fixed_factor(priv->dev, name, parent, 0, 1, 1);
}

static struct clk_hw * __init starfive_clk_mux(struct clk_starfive_priv *priv,
                                        const char *name,
                                        unsigned int offset,
                                        unsigned int width, 
                                        const char * const *parents, 
                                        unsigned int num_parents)
{
    return clk_hw_register_mux(priv->dev, name, parents, num_parents,
                                0, priv->base + offset,
                                STARFIVE_CLK_MUX_SHIFT, width, 
                                0, &priv->rmw_lock);
}

static struct clk_hw * __init starfive_clk_composite(struct clk_starfive_priv *priv,
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
		div->shift = STARFIVE_CLK_DIVIDER_SHIFT;
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

static struct clk_hw * __init starfive_clk_gated_divider(struct clk_starfive_priv *priv,
							 const char *name,
							 const char *parent,
							 unsigned int offset,
							 unsigned int width)
{
	const char * const *parents;

	parents  = &parent;

   	return starfive_clk_composite(priv, name, parents, 1, offset, 0, 1, width);
}

static const char *pll2_reclk_sels[2] = {
    [0] = "osc_sys",
    [1] = "osc_aud",
};

static const char *cpundbus_root_sels[4] = {
    [0] = "osc_sys",
    [1] = "pll0_out",
    [2] = "pll1_out",
    [3] = "pll2_out",
};

static const char *dla_root_sels[3] = {
    [0] = "osc_sys",
    [1] = "pll1_out",
    [2] = "pll2_out",
};

static const char *dsp_root_sels[4] = {
    [0] = "osc_sys",
    [1] = "pll0_out",
    [2] = "pll1_out",
    [3] = "pll2_out",
};

static const char *perh1_root_sels[2] = {
    [0] = "osc_sys",
    [1] = "pll2_out",
};

static const char *perh0_root_sels[2] = {
    [0] = "osc_sys",
    [1] = "pll0_out",
};

static const char *vin_root_sels[3] = { 
    [0] = "osc_sys",
    [1] = "pll1_out",
    [2] = "pll2_out",
};

static const char *vout_root_sels[3] =  {
    [0] = "osc_sys",
    [1] = "pll0_out",
    [2] = "pll2_out",
};

static const char *gmacusb_root_sels[3] = {
    [0] = "osc_sys",
    [1] = "pll0_out",
    [2] = "pll2_out",
};

static const char *cdechifi4_root_sels[3] = {
    [0] = "osc_sys",
    [1] = "pll1_out",
    [2] = "pll2_out",
};

static const char *cdec_root_sels[3] = {
    [0] = "osc_sys",
    [1] = "pll0_out",
    [2] = "pll1_out",
};

static const char *voutbus_root_sels[3] = {
    [0] = "osc_sys",
    [1] = "pll0_out",
    [2] = "pll2_out",
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


static const struct clk_div_table ahp_div_table[] = {
    { .val = 5, .div = 4, },
    { }
};




static int __init starfive_clkgen_init(struct clk_starfive_priv *priv)
{
    struct clk_hw **hws = priv->clk_hws.hws;
    struct clk *osc_sys, *osc_aud;

    osc_sys = devm_clk_get(priv->dev, "osc_sys");
    if (IS_ERR(osc_sys))
        return PTR_ERR(osc_sys);

    osc_aud = devm_clk_get(priv->dev, "osc_aud");
    if (IS_ERR(osc_aud))
        return PTR_ERR(osc_aud);

    printk(KERN_INFO ">>>>>>>starfive_clkgen_init\n");
    printk(">>>>>>>starfive_clkgen_init\n"); 

    printk("osc_sys-rate =  %ld", clk_get_rate(osc_sys));

    //clk_enable(osc_sys);
    //clk_enable(osc_aud);


    hws[JH7100_CLK_OSC_SYS]			= __clk_get_hw(osc_sys);
	hws[JH7100_CLK_OSC_AUD]			= __clk_get_hw(osc_aud);



    
    hws[JH7100_CLK_PLL0_OUT]		= starfive_clk_mult(priv, "pll0_out",	"osc_sys",	40);
    hws[JH7100_CLK_PLL1_OUT]		= starfive_clk_mult(priv, "pll1_out",	"osc_sys",	64);
    hws[JH7100_CLK_PLL2_OUT]		= starfive_clk_mult(priv, "pll2_out",	"pll2_reclk",	55);

    hws[JH7100_CLK_CPUNDBUS_ROOT]   = starfive_clk_mux(priv, "cpundbus_root", 0x0, 2, 
                                                        cpundbus_root_sels, ARRAY_SIZE(cpundbus_root_sels));
    hws[JH7100_CLK_DLA_ROOT]        = starfive_clk_mux(priv, "dla_root", 0x4, 2, 
                                                        dla_root_sels, ARRAY_SIZE(dla_root_sels));
    hws[JH7100_CLK_DSP_ROOT]        = starfive_clk_mux(priv, "dsp_root", 0x8, 2, 
                                                        dsp_root_sels, ARRAY_SIZE(dsp_root_sels));
    hws[JH7100_CLK_GMACUSB_ROOT]    = starfive_clk_mux(priv, "gmacusb_root", 0xC, 2, 
                                                        gmacusb_root_sels, ARRAY_SIZE(gmacusb_root_sels));
    hws[JH7100_CLK_PERH0_ROOT]		= starfive_clk_mux(priv, "perh0_root", 0x10, 1, 
                                                        perh0_root_sels, ARRAY_SIZE(perh0_root_sels));
    hws[JH7100_CLK_PERH1_ROOT]	    = starfive_clk_mux(priv, "perh1_root", 0x14, 1, 
                                                        perh1_root_sels, ARRAY_SIZE(perh1_root_sels));
    hws[JH7100_CLK_VIN_ROOT]	    = starfive_clk_mux(priv, "vin_root", 0x18, 2, 
                                                        vin_root_sels, ARRAY_SIZE(vin_root_sels));
    hws[JH7100_CLK_VOUT_ROOT]       = starfive_clk_mux(priv, "vout_root", 0x1C, 2, 
                                                        vout_root_sels, ARRAY_SIZE(vout_root_sels));
    hws[JH7100_CLK_AUDIO_ROOT]      = starfive_clk_gated_divider(priv, "audio_root", "pll0_out", 0x20, 4);
    hws[JH7100_CLK_CDECHIFI4_ROOT]  = starfive_clk_mux(priv, "cdechifi4_root", 0x24, 2, 
                                                        cdechifi4_root_sels, ARRAY_SIZE(cdechifi4_root_sels));
    hws[JH7100_CLK_CDEC_ROOT]       = starfive_clk_mux(priv, "cdec_root", 0x28, 2, 
                                                        cdec_root_sels, ARRAY_SIZE(cdec_root_sels));
    hws[JH7100_CLK_VOUTBUS_ROOT]    = starfive_clk_mux(priv, "voutbus_root", 0x2C, 2, 
                                                        voutbus_root_sels, ARRAY_SIZE(voutbus_root_sels));
    
    hws[JH7100_CLK_CPUNBUS_ROOT_DIV]= starfive_clk_divider(priv,	"cpunbus_root_div",	"cpundbus_root",  0x30, 2);
    hws[JH7100_CLK_DSP_ROOT_DIV]    = starfive_clk_divider(priv, "dsp_root_div", "dsp_root", 0x34, 3);
    hws[JH7100_CLK_PERH0_SRC]		= starfive_clk_divider(priv, "perh0_src",	"perh0_root", 0x38, 2);
    hws[JH7100_CLK_PERH1_SRC]       = starfive_clk_divider(priv, "perh1_src", "perh1_root", 0x3C, 3);
    hws[JH7100_CLK_PLL0_TESTOUT]    = starfive_clk_gated_divider(priv, "pll0_testout", "perh0_src", 0x40, 5);
    hws[JH7100_CLK_PLL1_TESTOUT]    = starfive_clk_gated_divider(priv, "pll1_testout", "dla_root", 0x44, 5);
    hws[JH7100_CLK_PLL2_TESTOUT]    = starfive_clk_gated_divider(priv, "pll2_testout", "perh1_src", 0x48, 5);
    hws[JH7100_CLK_PLL2_REF]        = starfive_clk_mux(priv, "pll2_reclk", 0x4c, 1, 
                                                        pll2_reclk_sels, ARRAY_SIZE(pll2_reclk_sels));
    hws[JH7100_CLK_CPU_CORE]        = starfive_clk_divider(priv, "cpu_core", "cpunbus_root_div", 0x50, 4);
    hws[JH7100_CLK_CPU_AXI]         = starfive_clk_divider(priv, "cpu_axi", "cpu_core", 0x54, 4);
    hws[JH7100_CLK_AHB_BUS]         = clk_hw_register_divider_table(priv->dev, "ahb_bus", "cpunbus_root_div",
		                         CLK_SET_RATE_PARENT | CLK_SET_RATE_GATE, priv->base + 0x58, 0, 4, 0, ahp_div_table, &priv->rmw_lock);
    hws[JH7100_CLK_APB1_BUS]        = starfive_clk_divider(priv, "apb1_bus", "ahb_bus",  0x5C, 4);
    hws[JH7100_CLK_APB2_BUS]		= starfive_clk_divider(priv,	"apb2_bus",	"ahb_bus",  0x60, 4);
    
    hws[JH7100_CLK_DOM3AHB_BUS]     = starfive_clk_gate(priv, "dom3ahb_bus", "ahb_bus", 0x64);
    hws[JH7100_CLK_DOM7AHB_BUS]     = starfive_clk_gate(priv, "dom7ahb_bus", "ahb_bus", 0x68);
    


    
    //hws[JH7100_CLK_U74_CORE0]       = starfive_clk_gate(priv, "u74_core0", "cpu_core", 0x6C);
    //hws[JH7100_CLK_U74_CORE1]       = starfive_clk_gated_divider(priv, "u74_core1", "cpu_core", 0x70, 4);
    //hws[JH7100_CLK_U74_AXI]         = starfive_clk_gate(priv, "u74_axi", "cpu_axi", 0x74);
    //hws[JH7100_CLK_U74RTC_TOGGLE]   = starfive_clk_gate(priv, "u74rtc_toggle", "osc_sys", 0x78);
    /*
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
    //hws[JH7100_CLK_GC300_AHB]       = starfive_clk_gate(priv, "gc300_ahb", "ahb0_bus", 0xD0);
    hws[JH7100_CLK_JPCGC300_AXIBUS] = starfive_clk_divider(priv, "jpcgc300_axibus", "vcdecbus_src", 0xD4, 4);
    hws[JH7100_CLK_GC300_AXI]       = starfive_clk_gate(priv, "gc300_axi",  "jpcgc300_axibus", 0xD8);
    hws[JH7100_CLK_JPCGC300_MAIN]   = starfive_clk_gate(priv, "jpcgc300_mainclk", "jpcgc300_axibus", 0xDC);
    hws[JH7100_CLK_VENC_BUS]        = starfive_clk_divider(priv, "venc_bus","vcdecbus_src", 0xE0, 4);
    hws[JH7100_CLK_VENC_AXI]        = starfive_clk_gate(priv, "venc_axi", "venc_bus", 0xE4);
    hws[JH7100_CLK_VENCBRG_MAIN]    = starfive_clk_gate(priv, "vencbrg_mainclk", "venc_bus", 0xE8);
    hws[JH7100_CLK_VENC_BCLK]       = starfive_clk_gated_divider(priv, "venc_bclk", "vcdecbus_src", 0xEC, 4);
    hws[JH7100_CLK_VENC_CCLK]       = starfive_clk_gated_divider(priv, "venc_cclk", "cdec_root", 0xF0, 4);
    hws[JH7100_CLK_VENC_APB]        = starfive_clk_gate(priv, "venc_apb", "apb1_bus", 0xF4);
    

    //
    hws[JH7100_CLK_DDRPLL_DIV2]     = starfive_clk_gated_divider(priv, "ddrpll_div2", "ddr_root", 0xF8, 2);
    hws[JH7100_CLK_DDRPLL_DIV4]     = starfive_clk_gated_divider(priv, "ddrpll_div4", "ddrpll_div2", 0xFC, 2);
    hws[JH7100_CLK_DDRPLL_DIV8]     = starfive_clk_gated_divider(priv, "ddrpll_div8", "ddrpll_div4", 0x100, 2);
    hws[JH7100_CLK_DDROSC_DIV2]     = starfive_clk_gated_divider(priv, "ddrosc_div2", "osc_sys", 0x104, 2);
    
    //*****value and resiger is different*****
    //gate and mux
    hws[JH7100_CLK_DDRC0]           = starfive_clk_mux(priv, "ddrc0", 0x108, 2, 
                                                        ddrc0_sels, ARRAY_SIZE(ddrc0_sels));
    //gate and mux
    hws[JH7100_CLK_DDRC1]           = starfive_clk_mux(priv, "ddrc1", 0x10C, 2,
                                                         ddrc1_sels, ARRAY_SIZE(ddrc1_sels));
    
    hws[JH7100_CLK_DDRPHY_APB]      = starfive_clk_gate(priv, "ddrphy_apb", "apb1_bus", 0x110);
    hws[JH7100_CLK_NOC_ROB]         = starfive_clk_divider(priv, "noc_rob", "cpunbus_root_div", 0x114, 4);
    hws[JH7100_CLK_NOC_COG]         = starfive_clk_divider(priv, "noc_cog", "dla_root", 0x118, 4);
    //hws[JH7100_CLK_NNE_AHB]         = starfive_clk_gate(priv, "nne_ahb", "ahb0_bus", 0x11C);
    hws[JH7100_CLK_NNEBUS_SRC1]     = starfive_clk_divider(priv, "nnebus_src1", "dsp_root", 0x120, 3);
    /**/

    
    //hws[JH7100_CLK_UART3_APB]		= starfive_clk_gate(priv, "uart3_apb",	"apb2_bus",	0x284);	
	//hws[JH7100_CLK_UART3_CORE]		= starfive_clk_divider(priv, "uart3_core",	"perh0_src", 0x288, 6);
    
    hws[JH7100_CLK_UART3_CORE]		= devm_clk_hw_register_fixed_factor(priv->dev, "uart3_core",	"osc_sys", 0, 4, 1);
    /**/
    
    


    
    //clk_prepare_enable(hws[JH7100_CLK_OSC_SYS]->clk);
    //clk_prepare_enable(hws[JH7100_CLK_OSC_AUD]->clk);
    //clk_prepare_enable(hws[JH7100_CLK_PLL0_OUT]->clk);
    //clk_prepare_enable(hws[JH7100_CLK_PERH0_ROOT]->clk);
    //clk_prepare_enable(hws[JH7100_CLK_PERH0_SRC]->clk);
    //use one times,and their father clock source will be enable.
    //clk_prepare_enable(hws[JH7100_CLK_UART3_CORE]->clk);
    //clk_set_rate(hws[JH7100_CLK_UART3_CORE]->clk, 100000000);
    extern bool clk_ignore_unused;
    clk_ignore_unused = true;


    return 0;
}


static int __init clk_starfive_jh7100_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk_starfive_priv *priv;
	int error;

	priv = devm_kzalloc(dev, struct_size(priv, clk_hws.hws, JH7100_CLK_END), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;
	priv->clk_hws.num = JH7100_CLK_END;

	spin_lock_init(&priv->rmw_lock);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	error = starfive_clkgen_init(priv);
	if (error)
		goto cleanup;

	error = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					    &priv->clk_hws);
	if (error)
		goto cleanup;

    printk("clk definition success!");
	return 0;

cleanup:
	// FIXME unregister gate clocks on failure
	return error;
}

static const struct of_device_id clk_starfive_jh7100_match[] = {
	{
		.compatible = "starfive,jh7100-clkgen",
	},
	{ /* sentinel */ }
};

static struct platform_driver clk_starfive_jh7100_driver = {
	.driver		= {
		.name	= "clk-starfive-jh7100",
		.of_match_table = clk_starfive_jh7100_match,
	},
    //
    //.probe = clk_starfive_jh7100_probe,
};

static int __init clk_starfive_jh7100_init(void)
{
	return platform_driver_probe(&clk_starfive_jh7100_driver,
				     clk_starfive_jh7100_probe);
    //return platform_driver_register(&clk_starfive_jh7100_driver);
}

subsys_initcall(clk_starfive_jh7100_init);
//core_initcall(clk_starfive_jh7100_init);


MODULE_DESCRIPTION("StarFive JH7100 Clock Generator Driver");
MODULE_AUTHOR("wu xingyu <xingyu.wu@starfivetech.com>");
MODULE_LICENSE("GPL v2");