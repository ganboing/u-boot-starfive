// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2022 Starfive, Inc.
 * Author:	yanhong <yanhong.wang@starfivetech.com>
 *
 */

#include <dm.h>
#include <log.h>
#include <asm/csr.h>
#include <init.h>

#define L2_LIM_MEM_END  0x8200000UL
#define CSR_U74_FEATURE_DISABLE	0x7c1

int spl_soc_init(void)
{
	int ret;
	struct udevice *dev;

	/* I2C init */
	ret = uclass_get_device(UCLASS_I2C, 0, &dev);
	if (ret) {
		debug("I2C init failed: %d\n", ret);
		return ret;
	}

	/*read memory size info from eeprom and
	 *init gd->ram_size variable
	 */
	dram_init();

	/* DDR init */
	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret) {
		debug("DRAM init failed: %d\n", ret);
		return ret;
	}

	/*flash init*/
	ret = uclass_get_device(UCLASS_SPI_FLASH, 0, &dev);
	if (ret) {
		debug("SPI init failed: %d\n", ret);
		return ret;
	}
	return 0;
}

void harts_early_init(ulong secondary)
{
	/*
	 * Feature Disable CSR
	 *
	 * Clear feature disable CSR to '0' to turn on all features for
	 * each core. This operation must be in M-mode.
	 */
	if (CONFIG_IS_ENABLED(RISCV_MMODE))
		csr_write(CSR_U74_FEATURE_DISABLE, 0);

#ifdef CONFIG_SPL_BUILD
	if (!secondary) {
		extern char __bss_end;
		const char *l2lim_end = L2_LIM_MEM_END;
		memset(&__bss_end, 0, l2lim_end - &__bss_end);
	}
#endif
}
