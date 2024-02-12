// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Starfive, Inc.
 * Author:	yanhong <yanhong.wang@starfivetech.com>
 *
 */

#include <common.h>
#include <init.h>
#include <asm/arch/spl.h>
#include <asm/io.h>
#include <asm/arch/gpio.h>
#include <asm/arch/jh7110-regs.h>
#include <asm/arch/clk.h>
#include <image.h>
#include <log.h>
#include <spl.h>

#define MODE_SELECT_REG		0x1702002c

int spl_board_init_f(void)
{
	int ret;

	ret = spl_soc_init();
	if (ret) {
		debug("JH7110 SPL init failed: %d\n", ret);
		return ret;
	}

	return 0;
}

u32 spl_boot_device(void)
{
	int boot_mode = 0;

	boot_mode = readl((const volatile void *)MODE_SELECT_REG) & 0x3;
	switch (boot_mode) {
	case 0:
		return BOOT_DEVICE_SPI;
	case 1:
		return BOOT_DEVICE_MMC2;
	case 2:
		return BOOT_DEVICE_MMC1;
	case 3:
		return BOOT_DEVICE_UART;
	default:
		debug("Unsupported boot device 0x%x.\n",
		      boot_mode);
		return BOOT_DEVICE_NONE;
	}
}

struct image_header *spl_get_load_buffer(ssize_t offset, size_t size)
{
	return (struct image_header *)(STARFIVE_SPL_BOOT_LOAD_ADDR);
}

void early_write_console(const char *buf, size_t sz)
{
	for (; sz; ++buf, --sz) {
		while((readl((void*)(0x10000000 + 0x14)) & 0x20) == 0);
		writel(buf[0], (void*)0x10000000);
	}
}

static u32 reg_dump_sys_crg[0x100];
static u32 reg_dump_stg_crg[0x100];
static u32 reg_dump_aon_crg[0x100];
static u32 reg_dump_otp[0x4];
static u32 otp_dump_mem[0x200];

struct {
	volatile u32 * const base;
	u32 * save;
	size_t n;
} reg_dumps[4] = {
	{ (void*)0x10230000, reg_dump_stg_crg, ARRAY_SIZE(reg_dump_stg_crg) },
	{ (void*)0x13020000, reg_dump_sys_crg, ARRAY_SIZE(reg_dump_sys_crg) },
	{ (void*)0x17000000, reg_dump_aon_crg, ARRAY_SIZE(reg_dump_aon_crg) },
	{ (void*)0x17050000, reg_dump_otp, ARRAY_SIZE(reg_dump_otp) },
};

void dump_regs(void)
{
	static volatile u32 * const otp_sr = (void*)0x17050008;
	static volatile u32 * const otp_opr = (void*)0x1705000c;
	static volatile u32 * const otp_mem = (void*)0x17050800;

	for (size_t i = 0; i < ARRAY_SIZE(reg_dumps); i++) {
		for (size_t j = 0; j < reg_dumps[i].n; j++) {
			reg_dumps[i].save[j] = readl(&reg_dumps[i].base[j]);
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(otp_dump_mem); i++) {
		// Set mode to READ
		writel(1, otp_opr);
		// wait for busy to clear
		while(readl(otp_sr) & (1 << 31));
		// read...
		otp_dump_mem[i] = readl(&otp_mem[i]);
		// wait for busy to clear
		while(readl(otp_sr) & (1 << 31));
		// Set mode to standby
		writel(0, otp_opr);
		// wait for busy to clear
		while(readl(otp_sr) & (1 << 31));
	}
}

void print_regs(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(reg_dumps); i++) {
		for (size_t j = 0; j < reg_dumps[i].n; j += 4) {
			printf("%08lx: %08x %08x %08x %08x\n",
				(unsigned long)(&reg_dumps[i].base[j]),
				reg_dumps[i].save[j],   reg_dumps[i].save[j+1],
				reg_dumps[i].save[j+2], reg_dumps[i].save[j+3]);
		}
	}
	for (size_t i = 0; i < ARRAY_SIZE(otp_dump_mem); i+=4) {
		printf("otp%03zx: %08x %08x %08x %08x\n", i * 4,
			otp_dump_mem[i],   otp_dump_mem[i+1],
			otp_dump_mem[i+2], otp_dump_mem[i+3]);
	}
}

void board_init_f(ulong dummy)
{
	int ret;

	dump_regs();
	/* Set pll0 cpufreq to 1000M */
	starfive_jh7110_pll_set_rate(PLL0, 1000000000);

	/*change pll2 to 1188MHz*/
	starfive_jh7110_pll_set_rate(PLL2, 1188000000);

	/*DDR control depend clk init*/
	clrsetbits_le32(SYS_CRG_BASE, CLK_CPU_ROOT_SW_MASK,
		BIT(CLK_CPU_ROOT_SW_SHIFT) & CLK_CPU_ROOT_SW_MASK);

	clrsetbits_le32(SYS_CRG_BASE + CLK_BUS_ROOT_OFFSET,
		CLK_BUS_ROOT_SW_MASK,
		BIT(CLK_BUS_ROOT_SW_SHIFT) & CLK_BUS_ROOT_SW_MASK);

	/*Set clk_perh_root clk default mux sel to pll2*/
	clrsetbits_le32(SYS_CRG_BASE + CLK_PERH_ROOT_OFFSET,
		CLK_PERH_ROOT_MASK,
		BIT(CLK_PERH_ROOT_SHIFT) & CLK_PERH_ROOT_MASK);

	clrsetbits_le32(SYS_CRG_BASE + CLK_NOC_BUS_STG_AXI_OFFSET,
		CLK_NOC_BUS_STG_AXI_EN_MASK,
		BIT(CLK_NOC_BUS_STG_AXI_EN_SHIFT)
		& CLK_NOC_BUS_STG_AXI_EN_MASK);

	clrsetbits_le32(AON_CRG_BASE + CLK_AON_APB_FUNC_OFFSET,
		CLK_AON_APB_FUNC_SW_MASK,
		BIT(CLK_AON_APB_FUNC_SW_SHIFT) & CLK_AON_APB_FUNC_SW_MASK);

	clrsetbits_le32(SYS_CRG_BASE + CLK_QSPI_REF_OFFSET,
		CLK_QSPI_REF_SW_MASK,
		(1 << CLK_QSPI_REF_SW_SHIFT) & CLK_QSPI_REF_SW_MASK);

	/* Improved GMAC0 TX I/O PAD capability */
	clrsetbits_le32(AON_IOMUX_BASE + 0x78, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x7c, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x80, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x84, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(AON_IOMUX_BASE + 0x88, 0x3, BIT(0) & 0x3);

	/* Improved GMAC1 TX I/O PAD capability */
	clrsetbits_le32(SYS_IOMUX_BASE + 0x26c, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x270, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x274, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x278, 0x3, BIT(0) & 0x3);
	clrsetbits_le32(SYS_IOMUX_BASE + 0x27c, 0x3, BIT(0) & 0x3);

	/*set GPIO to 3.3v*/
	setbits_le32(SYS_SYSCON_BASE + 0xC, 0x0);

	/*uart0 tx*/
	SYS_IOMUX_DOEN(5, LOW);
	SYS_IOMUX_DOUT(5, 20);
	/*uart0 rx*/
	SYS_IOMUX_DOEN(6, HIGH);
	SYS_IOMUX_DIN(6, 14);

	/*jtag*/
	SYS_IOMUX_DOEN(36, HIGH);
	SYS_IOMUX_DIN(36, 4);
	SYS_IOMUX_DOEN(61, HIGH);
	SYS_IOMUX_DIN(61, 19);
	SYS_IOMUX_DOEN(63, HIGH);
	SYS_IOMUX_DIN(63, 20);
	SYS_IOMUX_DOEN(60, HIGH);
	SYS_IOMUX_DIN(60, 29);
	SYS_IOMUX_DOEN(44, 8);
	SYS_IOMUX_DOUT(44, 22);

	/* reset emmc */
	SYS_IOMUX_DOEN(62, LOW);
	SYS_IOMUX_DOUT(62, 19);
	SYS_IOMUX_SET_DS(64, 2);
	SYS_IOMUX_SET_SLEW(64, 1);
	SYS_IOMUX_SET_DS(65, 1);
	SYS_IOMUX_SET_DS(66, 1);
	SYS_IOMUX_SET_DS(67, 1);
	SYS_IOMUX_SET_DS(68, 1);
	SYS_IOMUX_SET_DS(69, 1);
	SYS_IOMUX_SET_DS(70, 1);
	SYS_IOMUX_SET_DS(71, 1);
	SYS_IOMUX_SET_DS(72, 1);
	SYS_IOMUX_SET_DS(73, 1);
	/* reset sdio */
	SYS_IOMUX_DOEN(10, LOW);
	SYS_IOMUX_DOUT(10, 55);
	SYS_IOMUX_SET_DS(10, 2);
	SYS_IOMUX_SET_SLEW(10, 1);
	SYS_IOMUX_COMPLEX(9, 44, 57, 19);
	SYS_IOMUX_SET_DS(9, 1);
	SYS_IOMUX_COMPLEX(11, 45, 58, 20);
	SYS_IOMUX_SET_DS(11, 1);
	SYS_IOMUX_COMPLEX(12, 46, 59, 21);
	SYS_IOMUX_SET_DS(12, 1);
	SYS_IOMUX_COMPLEX(7, 47, 60, 22);
	SYS_IOMUX_SET_DS(7, 1);
	SYS_IOMUX_COMPLEX(8, 48, 61, 23);
	SYS_IOMUX_SET_DS(8, 1);

	/*i2c5*/
	SYS_IOMUX_COMPLEX(19, 79, 0, 42);//scl
	SYS_IOMUX_COMPLEX(20, 80, 0, 43);//sda

	ret = spl_early_init();
	if (ret)
		panic("spl_early_init() failed: %d\n", ret);

	arch_cpu_init_dm();

	preloader_console_init();

	print_regs();
	ret = spl_board_init_f();
	if (ret) {
		debug("spl_board_init_f init failed: %d\n", ret);
		return;
	}
}

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	/* boot using first FIT config */
	return 0;
}
#endif


