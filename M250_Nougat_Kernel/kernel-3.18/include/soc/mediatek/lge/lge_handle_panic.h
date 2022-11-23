/*
 * arch/arm/mach-msm/lge/lge_handle_panic.c
 *
 * Copyright (C) 2010 LGE, Inc
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LGE_HANDEL_PANIC_H__
#define __LGE_HANDEL_PANIC_H__

#define LGE_BSP_RAM_CONSOLE_PHY_ADDR	0x0011E800
#define LGE_BSP_RAM_CONSOLE_SIZE	(3 * SZ_1K)

#define LGE_CONSOLE_MAGIC_KEY		0x4C475243

extern unsigned int lge_crash_reason_magic;

#define LGE_CRASH_REASON_MASK           0xFFFF0000

#define LGE_CRASH_SYS_MASK              0x0000F000
#define LGE_CRASH_KERNEL                0x1000
#define LGE_CRASH_MODEM                 0x2000
#define LGE_CRASH_TZ                    0x3000
#define LGE_CRASH_LAF                   0x4000
#define LGE_CRASH_THERMAL               0x5000

#define LGE_CRASH_DESC_MASK             0x0000000F

#define LGE_CRASH_KERNEL_PANIC          (lge_crash_reason_magic | \
						LGE_CRASH_KERNEL | 0x0000)
#define LGE_CRASH_KERNEL_OOPS           (lge_crash_reason_magic | \
						LGE_CRASH_KERNEL | 0x0001)
#define LGE_CRASH_KERNEL_WDT            (lge_crash_reason_magic | \
						LGE_CRASH_KERNEL | 0x0002)


#define LGE_CRASH_MODEM_PANIC           (lge_crash_reason_magic | \
						LGE_CRASH_MODEM | 0x0000)

/*
 * this is not used in kernel.
 * LGE_CRASH_THERMAL_HW_WDT is directly triggered by HW
 */
#define LGE_CRASH_THERMAL_HW_WDT        (lge_crash_reason_magic | \
						LGE_CRASH_THERMAL | 0x0000)

#define LGE_CRASH_UNKNOWN               (lge_crash_reason_magic | 0x0000)
#define LGE_REBOOT_REASON_NORMAL		0x00000000
#define LGE_REBOOT_REASON_DLOAD			0x6C616664
#define LGE_REBOOT_REASON_LAF_RESTART	0x6F656D52
#define LGE_REBOOT_REASON_DMVERITY_CORRUPTED	0x77665567
#define LGE_REBOOT_REASON_WALLPAPER_CORRUPTED	0x77665568
#define LGE_REBOOT_REASON_FOTA					0x77665566
#ifdef CONFIG_LGE_LCD_OFF_DIMMING
#define LGE_REBOOT_REASON_FOTA_LCD_OFF			0x77665560
#define LGE_REBOOT_REASON_FOTA_OUT_LCD_OFF		0x77665561
#define LGE_REBOOT_REASON_LCD_OFF				0x77665562
#endif
#define LGE_MODEM_WILL_SIZE		256

struct _lge_crash_footprint {
	u32 magic_key;
	u32 reboot_reason;
	int fault_cpu;
	u32 regs[21];
	u32 modem_info;
	char modem_will[LGE_MODEM_WILL_SIZE];
	u32 ramconsole_paddr;
	u32 ramconsole_size;
	u32 fb_addr;
	u32 atf_buf_addr;
	u32 atf_buf_size;
};

extern void lge_set_reboot_reason(u32 rr);
extern u32 lge_get_reboot_reason(void);
extern void lge_set_modem_info(u32 modem_info);
extern void lge_set_modem_will(const char *modem_will);
extern void lge_set_ram_console_addr(unsigned int addr, unsigned int size);
extern int crash_handle_get_status(void);
extern void lge_gen_key_panic(int key);
#endif
