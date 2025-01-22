/*
 * SPDX-License-Identifier:     GPL-2.0+
 *
 * Copyright (c) 2020 Rockchip Electronics Co., Ltd
 */

#ifndef __CONFIGS_RK3528_EVB_H
#define __CONFIGS_RK3528_EVB_H

#include <configs/rk3528_common.h>

#ifndef CONFIG_SPL_BUILD

#undef ROCKCHIP_DEVICE_SETTINGS
#define ROCKCHIP_DEVICE_SETTINGS \
		"stdin=serial,usbkbd\0" \
		"stdout=serial,vidconsole\0" \
		"stderr=serial,vidconsole\0"

#define CONFIG_SYS_MMC_ENV_DEV		0

#undef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND IXON_BOOTCOMMAND

#define PARTS_IXON \
	"uuid_disk=${uuid_gpt_disk};" \
	"name=idbloader,start=32KB,size=1136KB;"\
	"name=uboot,start=1168KB,size=2528KB;"\
	"name=dfu,start=4MB,size=64MB,uuid=${uuid_gpt_dfu};"\
	"name=roota,start=68MB,size=2048MB,uuid=${uuid_gpt_roota};" \
	"name=rootb,start=2116MB,size=2048MB,uuid=${uuid_gpt_rootb};" \
    "name=system,start=4164MB,size=1024MB,uuid=${uuid_gpt_system};" \
	"name=data,size=-,uuid=${uuid_gpt_data},type=data;\0" \

#define IXON_BOOTCOMMAND			\
	"run scan_dev_for_boot_part;"

#ifndef CONFIG_SPL_BUILD
#undef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS \
	"dev_dtb=/boot/rk3528-radxa-e20c.dtb\0 root_a=/dev/mmcblk1p4\0 root_b=/dev/mmcblk1p5\0"\
	"mmc_a=mmc 0:4\0 mmc_b=mmc 0:5\0 system_p=/dev/mmcblk0p6\0"\
	ENV_MEM_LAYOUT_SETTINGS \
	"partitions=" PARTS_IXON \
	ROCKCHIP_DEVICE_SETTINGS \
	RKIMG_DET_BOOTDEV \
	BOOTENV
#endif

#ifdef CONFIG_USB_FUNCTION_DFU
#define CONFIG_SET_DFU_ALT_INFO
#endif

#define DFU_ALT_BOOT_EMMC \
	"gpt raw 0x0 0x20000;" \
	"loader raw 0x20000 0xE0000;"\
	"uboot part 0 1;" \
	"boot part 0 2;" \
	"rootfs part 0 3;" \
	"userdata part 0 4\0"

#ifdef CONFIG_ANDROID_AB
#define DFU_ALT_BOOT_MTD_A \
	"gpt raw 0x0 0x20000;" \
	"loader raw 0x20000 0xE0000;"\
	"vnvm part vnvm;" \
	"uboot part uboot;" \
	"boot raw 0x700000 0x600000\0"

#define DFU_ALT_BOOT_MTD_B \
	"gpt raw 0x0 0x20000;" \
	"loader raw 0x20000 0xE0000;"\
	"vnvm part vnvm;" \
	"uboot part uboot;" \
	"boot raw 0xd00000 0x600000\0"
#else
#define DFU_ALT_BOOT_MTD \
	"gpt raw 0x0 0x20000;" \
	"loader raw 0x20000 0xE0000;"\
	"vnvm part vnvm;" \
	"uboot part uboot;" \
	"boot part boot;" \
	"rootfs partubi rootfs;" \
	"userdata partubi userdata\0"

#endif /* CONFIG_ANDROID_AB */
#endif /* CONFIG_SPL_BUILD */
#endif /* __CONFIGS_RK3528_EVB_H */
