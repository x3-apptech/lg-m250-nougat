/* include/linux/power/rt9460.h
 * RT9460 Switch Charger Driver
 * Copyright (C) 2014
 * Author: Jeff Chang <jeff_chang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_RT9460_H
#define __LINUX_RT9460_H

#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>

#define RT9460_DEV_NAME			"rt9460"
#define RT9460_DRV_VER			"1.0.1_G"
#define RT_BATT_NAME			"rt-battery"


enum {
	RT9460_REG_CTRL1,
	RT9460_REG_RANGE1_START = RT9460_REG_CTRL1,
	RT9460_REG_CTRL2,
	RT9460_REG_CTRL3,
	RT8460_REG_ID,
	RT9460_REG_CTRL4,
	RT9460_REG_CTRL5,
	RT9460_REG_CTRL6,
	RT9460_REG_CTRL7,
	RT9460_REG_IRQ1,
	RT9460_REG_IRQ2,
	RT9460_REG_IRQ3,
	RT9460_REG_MASK1,
	RT9460_REG_MASK2,
	RT9460_REG_MASK3,
	RT9460_REG_DPDM = 0x0e,
	RT9460_REG_RANGE1_END = RT9460_REG_DPDM,

/* hidden bit for otg mode */
	RT9460_REG_1C = 0x1c,
	RT9460_REG_HIDDEN = 0x18,

	RT9460_REG_CTRL9 = 0x21,
	RT9460_REG_RANGE2_START = RT9460_REG_CTRL9,
	RT9460_REG_CTRL10,
	RT9460_REG_CTRL11,
	RT9460_REG_CTRL12,
	RT9460_REG_CTRL13,
	RT9460_REG_STATIRQ,
	RT9460_REG_STATIRQMASK,
	RT9460_REG_RANGE2_END = RT9460_REG_STATIRQMASK,
	RT9450_REG_31 = 0x31,
};

/*ctrl1*/
#define RT9460_ENSTAT_MASK		0x40
#define RT9460_ENSTAT_SHFT		6
#define RT9460_CHGSTAT_MASK		0x30
#define RT9460_CHGSTAT_SHFT		4
#define RT9460_MIVRSTAT_MASK		0x01
#define RT9460_POWRRDY_MASK		0x04

/*ctrl2*/
#define RT9460_IEOC_MASK		0xe0
#define RT9460_IEOC_SHFT		5
#define RT9460_OPAMODE_MASK		0x01
#define RT9460_OPAMODE_SHFT		0
#define RT9460_TEEN_MASK		0x08
#define RT9460_TEEN_SHFT		3
#define RT9460_IININT_MASK		0x04
#define RT9460_HZ_MASK			0x02
#define RT9460_HZ_SHFT			1

/*ctrl3*/
#define RT9460_OTGEN_MASK		0x01
#define RT9460_OTGEN_SHFT		1
#define RT9460_VOREG_MASK		0xfc
#define RT9460_VOREG_SHFT		2

/*ctrl5*/
#define RT9460_ENTMR_MASK		0x80
#define RT9460_ENTMR_SHFT		7
#define RT9460_IPREC_MASK		0x0c
#define RT9460_IPREC_SHFT		2

/*ctrl6*/
#define RT9460_ICHRG_MASK		0xf0
#define RT9460_ICHRG_SHFT		4
#define RT9460_VPREC_MASK		0x07
#define RT9460_VPREC_SHFT		0


/*ctrl7*/
#define RT9460_CCJEITA_MASK		0x80
#define RT9460_CCJEITA_SHFT		7
#define RT9460_BATDEN_MASK		0x40
#define RT9460_BATDEN_SHFT		6
#define RT9460_CHGEN_MASK		0x10
#define RT9460_CHGEN_SHFT		4
#define RT9460_VMREG_MASK		0x0f
#define RT9460_VMREG_SHFT		0
#define RT9460_TSHC_MASK		0x09	/* hot and cold*/
#define RT9460_TSWC_MASK		0x06	/* warm and cool*/
#define RT9460_CHIPEN_MASK		0x20

/*ctrl10*/
#define RT9460_WTFC_MASK		0x38	/* Fast charge safety timer */
#define RT9460_WTFC_SHFT		3

/*ctrl11*/
#define RT9460_CHGAICR_MASK		0xf8
#define RT9460_CHGAICR_SHFT		3
#define RT9460_DEADBAT_MASK		0x07
#define RT9460_DEADBAT_SHFT	        0

/*ctrl12*/
#define RT9460_IRQREZ_MASK		0x01

/*ctrl13*/
#define RT9460_WDTEN_MASK		0x80

/*DPDM*/
#define RT9460_CHGRUN_MASK		0x01
#define RT9460_CHGTYPE_MASK		0xe0
#define RT9460_CHGTYPE_SHFT		5
#define RT9460_CHG2DET_MASK		0x04
#define RT9460_CHG1DET_MASK		0x02
#define RT9460_IICSEL_MASK		0x18
#define RT9460_IICSEL_SHFT		3

/*MASK1*/
#define RT9460_CHTERMTMRIM_MASK		0x04

/*stat irq mask*/
#define RT9460_MIVRI_MASK	0x04
#define RT9460_PWRRDYI_MASK	0x08
#define RT9460_TSWCI_MASK	0x60
#define RT9460_TSHCI_MASK	0x90
#define RT9460_TSEVENT_MASK	0xf0

#define RT9460_USBDET_TIMEOUT	512000

struct rt9460_charger_info {
	struct i2c_client *i2c;
	struct device *dev;
	struct mutex io_lock;
	struct power_supply psy;
	struct wake_lock usb_wake_lock;
	struct delayed_work irq_delayed_work;
	struct delayed_work usb_detect_work;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT_DUMP_REGISTER
	struct delayed_work reg_dump_work;
#endif
	int irq;
	int icc;
	int aicr;
	int chg_volt;
	int bst_volt;
	int charge_cable;
	unsigned char reg_addr;
	unsigned char online:1;
	unsigned char te_en:1;
	unsigned char batabs:1;
	unsigned char usb_det:1;
	unsigned char is_cold_hot:1;
	unsigned char chg_stat;
	unsigned char suspend;
};

/*TS STATUS*/
enum {
	TS_NORMAL,
	TS_WARM_COOL,
	TS_HOT_COLD,
};

/*IRQ EVENT */
enum {
	CHGEVENT_BSTLOWVI = 5,
	CHGEVENT_BSTOLI,
	CHGEVENT_BSTVINOVI,

	CHGEVENT_SYSWAKEUPI,
	CHGEVENT_CHTREGI,
	CHGEVENT_CHTMRI,
	CHGEVENT_CHRCHGI,
	CHGEVENT_CHTERMI,
	CHGEVENT_CHBATOVI,
	CHGEVENT_CHBADI,
	CHGEVENT_CHRVPI = 15,
	CHGEVENT_BATABI,
	CHGEVENT_SYSUVPI,
	CHGEVENT_CHTERMTMRI,
	CHGEVENT_WATCHDOGI = 20,
	CHGEVENT_WAKEUPI,
	CHGEVENT_VIMOVPI,
	CHGEVENT_TSDI,
	CHGEVENT_MAX,
};

/* iprec*/
enum {
	RT9460_IPREC_10P,
	RT9460_IPREC_20P = 3,
};

/* ieoc */
enum {
	RT9460_IEOC_100MA,
	RT9460_IEOC_150MA,
	RT9460_IEOC_200MA,
	RT9460_IEOC_250MA,
	RT9460_IEOC_300MA,
	RT9460_IEOC_350MA,
	RT9460_IEOC_400MA,
	RT9460_IEOC_450MA,
};

/* dead battery voltage level */
enum {
	RT9460_DEADBAT_2P9V,
	RT9460_DEADBAT_3P0V,
	RT9460_DEADBAT_3P1V,
	RT9460_DEADBAT_3P2V,
	RT9460_DEADBAT_3P3V,
	RT9460_DEADBAT_3P4V,
	RT9460_DEADBAT_3P5V,
	RT9460_DEADBAT_3P6V,
};

/* vprec */
enum {
	RT9460_VPREC_2P0V,
	RT9460_VPREC_2P2V,
	RT9460_VPREC_2P4V,
	RT9460_VPREC_2P6V,
	RT9460_VPREC_2P8V,
	RT9460_VPREC_3P0V,
	RT9460_VPREC_MAX = RT9460_VPREC_3P0V,
};

/* stat irq event */
enum {
	STATEVENT_MIVRIM = 2,
	STATEVENT_PWRRDYIM ,
	STATEVENT_TSCOLDIM,
	STATEVENT_TSCOOLIM,
	STATEVENT_TSWARMIM,
	STATEVENT_TSHOTIM,
};

/* attribute nodes */
enum {
	RT9460_DBG_REG,
	RT9460_DBG_DATA,
	RT9460_DBG_REGS,
};

enum {
	RT9460_TE_DIS,
	RT9460_TE_EN,
};

/* AICR reference */
enum {
	RT9460_IICSEL_CHGTYPE,
	RT9460_IICSEL_AICR,
	RT9460_IICSEL_HIGHER,
	RT9460_IICSEL_LOWER,
};

enum {
	RT9460_CHGDET_DIS,
	RT9460_CHGDET_EN,
};

enum {
	RT9460_USBDET_DIS,
	RT9460_USBDET_EN,
};

struct rt9460_platform_data {
	int irq_gpio;
	int icc;
	int chg_volt;
	int aicr;
	int bst_volt;
	unsigned char te_en:1;
	unsigned char usb_det:1;
	unsigned char chg_2det:1;
	unsigned char chg_1det:1;
	unsigned char iprec:2;
	unsigned char iinlm_sel:2;
	unsigned char ieoc:3;
	unsigned char deadbat:3;
	unsigned char vprec:3;
	unsigned char voreg:5;
};

typedef void (*rt_irq_handler)(void *info, int eventno);

#ifndef CONFIG_RT_SHOW_INFO
#define RTINFO(format, args...) \
	pr_info(KERN_INFO "%s:%s() line-%d: " format, \
	RT9460_DEV_NAME, __FUNCTION__, __LINE__, ##args)
#else
#define RTINFO(format, args...)
#endif /* CONFIG_RT_SHOW_INFO */

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT_DUMP_REGISTER
extern void rt9460_dump_register(struct rt9460_charger_info *);
#endif

#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
/* Boost mode for OTG */
extern int detect_otg_mode(int);
#endif
#endif /* __LINUX_RT9460_H */

