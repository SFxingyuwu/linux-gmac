/*
 * Generic DWMAC platform driver
 *
 * Copyright (C) 2007-2011  STMicroelectronics Ltd
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#include "stmmac.h"
#include "stmmac_platform.h"

#define INIT_FUNC_GMAC_FLAG		1
/*
 * GMAC_GTXCLK 为 gmac 的时钟分频寄存器，低8位为分频值
 * bit         name                    access  default         descript
 * [31]        clk_gmac_gtxclk enable  RW      0x0             "1:enable; 0:disable"
 * [30]        reserved                -       0x0             reserved
 * [29:8]      reserved                -       0x0             reserved
 * [7:0] clk_gmac_gtxclk divide ratio  RW      0x4             divide value
 *
 * gmac 的 root 时钟为500M, gtxclk 需求的时钟如下：
 * 1000M: gtxclk为125M，分频值为500/125 = 0x4
 * 100M:  gtxclk为25M， 分频值为500/25  = 0x14
 * 10M:   gtxclk为2.5M，分频值为500/2.5 = 0xc8
 */
#ifdef CONFIG_SOC_STARFIVE_VIC7100
#define CLKGEN_BASE                    0x11800000
#define CLKGEN_GMAC_GTXCLK_OFFSET      0x1EC
#define CLKGEN_GMAC_GTXCLK_ADDR        (CLKGEN_BASE + CLKGEN_GMAC_GTXCLK_OFFSET)

#define CLKGEN_125M_DIV                0x4
#define CLKGEN_25M_DIV                 0x14
#define CLKGEN_2_5M_DIV                0xc8

static void dwmac_fixed_speed(void *priv, unsigned int speed)
{
	u32 value;
	void *addr = ioremap(CLKGEN_GMAC_GTXCLK_ADDR, sizeof(value));
	if (!addr) {
		pr_err("%s can't remap CLKGEN_GMAC_GTXCLK_ADDR\n", __func__);
		return;
	}

	value = readl(addr) & (~0x000000FF);

	switch (speed) {
		case SPEED_1000: value |= CLKGEN_125M_DIV; break;
		case SPEED_100:  value |= CLKGEN_25M_DIV;  break;
		case SPEED_10:   value |= CLKGEN_2_5M_DIV; break;
		default: iounmap(addr); return;
	}
	writel(value, addr); /*set gmac gtxclk*/
	iounmap(addr);
}
#endif

/////////////////////////////////////////////
/*test:initialization of gmac for kernel*/
#if INIT_FUNC_GMAC_FLAG

#define RSTGEN_BASE_ADDR                0x11840000
#define rstgen_RESET_assert1_shift   0x4
#define rstgen_RESET_status1_shift   0x14

#define SYSCON_SYSMAIN_CTRL_BASE_ADDR   0x11850000
#define syscon_sysmain_ctrl_reg28_shift   0x70


void ASSERT_RESET_rstgen_gmac_ahb_func(void __iomem *base_addrs)
{
	u32 read_value = ioread32(base_addrs + 0x4);
	read_value |= (1<<28);
	iowrite32(read_value, base_addrs + 0x4);
	do {
		read_value = ioread32(base_addrs + 0x14) >>28;
		read_value &= 0x1;
	}while(read_value != 0x0);

}

void CLEAR_RESET_rstgen_gmac_ahb_func(void __iomem *base_addrs)
{
	u32 read_value = ioread32(base_addrs + 0x4);
	read_value &= ~(1<<28);
	iowrite32(read_value, base_addrs + 0x4);
	do {
		read_value = ioread32(base_addrs + 0x14) >>28;
		read_value &= 0x1;
	}while(read_value != 0x1);
}


void SET_SYSCON_REG28_gmac_phy_intf_sel_func(void __iomem *base_addrs, \
															u32 shift, u32 v)
{
	u32 read_value = ioread32(base_addrs + shift);
	read_value &= ~(0x7);
	read_value |= (v&0x7);
	iowrite32(read_value, base_addrs + shift);
}


static int init_func_gmac(void)
{
	int ret = 0;
	printk(">>>>>start init_func_gmac test<<<\n");


	static void __iomem *rstgen_test_addr;
	rstgen_test_addr = ioremap(RSTGEN_BASE_ADDR,  0x20);
	if (NULL == rstgen_test_addr) {
        printk(KERN_INFO "Failed to remap hardware register!\n");
        ret = -ENOMEM;
 	}	

	ASSERT_RESET_rstgen_gmac_ahb_func(rstgen_test_addr);
	CLEAR_RESET_rstgen_gmac_ahb_func(rstgen_test_addr);

	printk(">>>>>gmac_ahb test sucess<<<\n");


	static void __iomem *syscon_sysmain_ctrl_test_addr;
	syscon_sysmain_ctrl_test_addr = ioremap(SYSCON_SYSMAIN_CTRL_BASE_ADDR,  0x80);
	if (NULL == syscon_sysmain_ctrl_test_addr) {
        printk(KERN_INFO "Failed to remap hardware register!\n");
        ret = -ENOMEM;
 	}
	
	SET_SYSCON_REG28_gmac_phy_intf_sel_func(syscon_sysmain_ctrl_test_addr, \
											syscon_sysmain_ctrl_reg28_shift,\
											0x1	);//rgmi	

	printk(">>>>>>>>init_func_gmac test sucess<<<<<<<<\n");

	return ret;
}

#endif

static int dwmac_generic_probe(struct platform_device *pdev)
{
	struct plat_stmmacenet_data *plat_dat;
	struct stmmac_resources stmmac_res;
	int ret;

	ret = stmmac_get_platform_resources(pdev, &stmmac_res);
	if (ret)
		return ret;

	if (pdev->dev.of_node) {
		plat_dat = stmmac_probe_config_dt(pdev, stmmac_res.mac);
		if (IS_ERR(plat_dat)) {
			dev_err(&pdev->dev, "dt configuration failed\n");
			return PTR_ERR(plat_dat);
		}
	} else {
		plat_dat = dev_get_platdata(&pdev->dev);
		if (!plat_dat) {
			dev_err(&pdev->dev, "no platform data provided\n");
			return  -EINVAL;
		}

		/* Set default value for multicast hash bins */
		plat_dat->multicast_filter_bins = HASH_TABLE_SIZE;

		/* Set default value for unicast filter entries */
		plat_dat->unicast_filter_entries = 1;
	}

	/* Custom initialisation (if needed) */
	if (plat_dat->init) {
		ret = plat_dat->init(pdev, plat_dat->bsp_priv);
		if (ret)
			goto err_remove_config_dt;
	}
#ifdef CONFIG_SOC_STARFIVE_VIC7100
	plat_dat->fix_mac_speed = dwmac_fixed_speed;
#endif

	/*test:initialization of gmac for kernel*/
	#if INIT_FUNC_GMAC_FLAG
	init_func_gmac();
	#endif

	ret = stmmac_dvr_probe(&pdev->dev, plat_dat, &stmmac_res);
	if (ret)
		goto err_exit;

	return 0;

err_exit:
	if (plat_dat->exit)
		plat_dat->exit(pdev, plat_dat->bsp_priv);
err_remove_config_dt:
	if (pdev->dev.of_node)
		stmmac_remove_config_dt(pdev, plat_dat);

	return ret;
}

static const struct of_device_id dwmac_generic_match[] = {
	{ .compatible = "st,spear600-gmac"},
	{ .compatible = "snps,dwmac-3.50a"},
	{ .compatible = "snps,dwmac-3.610"},
	{ .compatible = "snps,dwmac-3.70a"},
	{ .compatible = "snps,dwmac-3.710"},
	{ .compatible = "snps,dwmac-4.00"},
	{ .compatible = "snps,dwmac-4.10a"},
	{ .compatible = "snps,dwmac"},
	{ .compatible = "snps,dwxgmac-2.10"},
	{ .compatible = "snps,dwxgmac"},
	{ }
};
MODULE_DEVICE_TABLE(of, dwmac_generic_match);

static struct platform_driver dwmac_generic_driver = {
	.probe  = dwmac_generic_probe,
	.remove = stmmac_pltfr_remove,
	.driver = {
		.name           = STMMAC_RESOURCE_NAME,
		.pm		= &stmmac_pltfr_pm_ops,
		.of_match_table = of_match_ptr(dwmac_generic_match),
	},
};
module_platform_driver(dwmac_generic_driver);

MODULE_DESCRIPTION("Generic dwmac driver");
MODULE_LICENSE("GPL v2");
