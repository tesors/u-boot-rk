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
#define CONFIG_BOOTCOMMAND \
	"fdt addr ${fdt_addr} && fdt get value bootargs /chosen bootargs;" \
	"test -n \"${BOOT_ORDER}\" || setenv BOOT_ORDER \"A B\";" \
	"test -n \"${BOOT_A_LEFT}\" || setexpr BOOT_A_LEFT 3;" \
	"test -n \"${BOOT_B_LEFT}\" || setexpr BOOT_B_LEFT 3;" \
	"test -n \"${BOOT_DEV}\" || setenv BOOT_DEV \"mmc 0:3\";" \
	"test -n \"${FRESET}\" || setenv FRESET \"0\";" \
	"test -n \"${ROOT_A}\" || setenv ROOT_A \"/dev/mmcblk0p3\";" \
	"test -n \"${ROOT_B}\" || setenv ROOT_B \"/dev/mmcblk0p4\";" \
	"test -n \"${MMC_A}\" || setenv MMC_A \"mmc 0:3\";" \
	"test -n \"${MMC_B}\" || setenv MMC_B \"mmc 0:4\";" \
	"setenv silent 1;" \
	"setenv bootpart;" \
	"echo \"Bootpart: ${bootpart} detected\";" \
	"echo \"Boot left A: ${BOOT_A_LEFT} attempts remaining\";" \
	"echo \"Boot left B: ${BOOT_B_LEFT} attempts remaining\";" \
	"echo \"Boot order:  ${BOOT_ORDER} \";" \
	"echo \"Bootargs:  ${bootargs} \";" \
	"for BOOT_SLOT in \"${BOOT_ORDER}\"; do;" \
	"if test \"x${bootpart}\" != \"x\"; then;" \
	"	echo \"No bootpart: ${bootpart} detected\";" \
	"elif test \"x${BOOT_SLOT}\" = \"xA\"; then;" \
	"	if test ${BOOT_A_LEFT} > 0; then;" \
	"		setexpr BOOT_A_LEFT ${BOOT_A_LEFT} - 1;" \
	"		echo \"Found valid RAUC slot xA, ${BOOT_A_LEFT} attempts remaining\";" \
	"		setenv bootpart \"${ROOT_A} rootwait\";" \
	"		setenv raucslot \"A\";" \
	"		setenv BOOT_DEV ${MMC_A};" \
	"	fi;" \
	"elif test \"x${BOOT_SLOT}\" = \"xB\"; then;" \
	"	if test ${BOOT_B_LEFT} > 0; then;" \
	"		setexpr BOOT_B_LEFT ${BOOT_B_LEFT} - 1;" \
	"		echo \"Found valid RAUC slot xB, ${BOOT_B_LEFT} attempts remaining\";" \
	"		setenv bootpart \"${ROOT_B} rootwait\";" \
	"		setenv raucslot \"B\";" \
	"		setenv BOOT_DEV ${MMC_B};" \
	"	fi;" \
	"fi;" \
	"done;" \
	"setenv bootcmd_pxe "";" \
	"if test -n \"${bootpart}\"; then;" \
	"	setenv bootargs \"console=${console} root=${bootpart} panic=10 rauc.slot=${raucslot} rootrw=/dev/mmcblk0p5 freset=$FRESET quiet\";" \
	"	setenv FRESET \"0\";;" \
	"	saveenv;" \
	"else;" \
	"	echo \"No valid RAUC slot found. Resetting tries to 3\";" \
	"env default -a;" \
	"setenv BOOT_A_LEFT 3;" \
	"setenv BOOT_B_LEFT 3;" \
	"saveenv;" \
	"fastboot usb 0;" \
	"fi;" \
	"load ${BOOT_DEV} ${kernel_addr_r}  /boot/Image;" \
	"load ${BOOT_DEV} ${fdt_addr_r}  /boot/rk3528-radxa-e20c.dtb;" \
	"load ${BOOT_DEV} ${ramdisk_addr_r}  /boot/uramdisk.image.gz;" \
	"booti ${kernel_addr_r} ${ramdisk_addr_r} ${fdt_addr_r};"

#define PARTS_IXON \
	"uuid_disk=${uuid_gpt_disk};" \
	"name=uboot,start=1168KB,size=2528KB;"\
	"name=dfu,start=4MB,size=64MB,uuid=${uuid_gpt_dfu};"\
	"name=roota,start=68MB,size=2048MB,uuid=${uuid_gpt_roota};" \
	"name=rootb,start=2116MB,size=2048MB,uuid=${uuid_gpt_rootb};" \
    "name=system,start=4164MB,size=1024MB,uuid=${uuid_gpt_system},type=data;" \
	"name=data,size=9216MB,uuid=${uuid_gpt_data},type=data;\0" \

#define IXON_BOOTCOMMAND			\
	"run scan_dev_for_boot_part;"

#ifndef CONFIG_SPL_BUILD
#undef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS \
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
