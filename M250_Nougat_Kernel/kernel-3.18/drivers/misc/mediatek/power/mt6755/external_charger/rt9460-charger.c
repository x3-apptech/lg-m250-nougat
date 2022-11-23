/*drives/power/rt9460-charger.c
 *I2C Driver for Richtek RT9460
 *
 * Copyright (C) 2014 Richtek Technology Corp.
 * Author: Jeff Chang <jeff_chang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#ifdef CONFIG_RT_BATTERY
#include <linux/power/rt-battery.h>
#endif /* CONFIG_RT_BATTERY */
#ifdef CONFIG_RT_POWER
#include <linux/power/rt-power.h>
#endif /*CONFIG_RT_POWER */

#include "rt9460.h"

#include <mt-plat/charging.h>
#include <mach/mt_charging.h>
#include <linux/async.h>

#ifdef CONFIG_LGE_PM_BATTERY_PRESENT
#include <soc/mediatek/lge/lge_cable_id.h>
#include <mt-plat/upmu_common.h>
#endif

static struct rt9460_charger_info *ri = NULL;
/*
static struct i2c_client *new_client = NULL;
unsigned char rt9460_reg[rt9460_REG_NUM] = { 0 };
*/
/*i2c*/

extern kal_bool chargin_hw_init_done;

static inline int rt9460_read_device(struct i2c_client *i2c,
				     int reg, int bytes, void *dest)
{
	int ret;

	if (bytes > 1)
		ret = i2c_smbus_read_i2c_block_data(i2c, reg, bytes, dest);
	else {
		ret = i2c_smbus_read_byte_data(i2c, reg);
		if (ret < 0)
			return ret;
		*(unsigned char *)dest = (unsigned char)ret;
	}
	return ret;
}

static int rt9460_reg_block_read(struct i2c_client *i2c,
				 int reg, int bytes, void *dest)
{
	return rt9460_read_device(i2c, reg, bytes, dest);
}

static inline int rt9460_write_device(struct i2c_client *i2c,
				      int reg, int bytes, void *dest)
{
	int ret;

	if (bytes > 1)
		ret = i2c_smbus_write_i2c_block_data(i2c, reg, bytes, dest);
	else {
		ret = i2c_smbus_write_byte_data(i2c, reg, *(u8 *) dest);
		if (ret < 0)
			return ret;
	}
	return ret;
}

static int rt9460_reg_block_write(struct i2c_client *i2c,
				  int reg, int bytes, void *dest)
{
	return rt9460_write_device(i2c, reg, bytes, dest);
}

static int rt9460_reg_read(struct i2c_client *i2c, int reg)
{
	struct rt9460_charger_info *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int rt9460_reg_write(struct i2c_client *i2c, int reg, unsigned char data)
{
	struct rt9460_charger_info *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = i2c_smbus_write_byte_data(i2c, reg, data);
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int rt9460_assign_bits(struct i2c_client *i2c, int reg,
			      unsigned char mask, unsigned char data)
{
	struct rt9460_charger_info *chip = i2c_get_clientdata(i2c);
	unsigned char value;
	int ret;

	mutex_lock(&chip->io_lock);

	ret = rt9460_read_device(i2c, reg, 1, &value);

	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= (data & mask);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}

static int rt9460_set_bits(struct i2c_client *i2c, int reg, unsigned char mask)
{
	return rt9460_assign_bits(i2c, reg, mask, mask);
}

static int rt9460_clr_bits(struct i2c_client *i2c, int reg, unsigned char mask)
{
	return rt9460_assign_bits(i2c, reg, mask, 0);
}

/* end of i2c*/

/*Attribute*/
#define RT9460_ATTR(_name)                        \
{                                                    \
	.attr = {.name = #_name, .mode = 0664},      \
	.show = rt9460_show_attrs,                     \
	.store = rt9460_store_attrs,                   \
}

static ssize_t rt9460_store_attrs(struct device *, struct device_attribute *,
				  const char *, size_t);
static ssize_t rt9460_show_attrs(struct device *, struct device_attribute *,
				 char *);
static void rt9460_chip_enable(struct rt9460_charger_info *);
static void rt9460_chip_disable(struct rt9460_charger_info *);

static struct device_attribute rt9460_attrs[] = {
	RT9460_ATTR(reg),
	RT9460_ATTR(data),
	RT9460_ATTR(regs),
};

static ssize_t rt9460_store_attrs(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	ptrdiff_t cmd;
	int tmp, ret;
	unsigned long yo;
	struct rt9460_charger_info *ri = dev_get_drvdata(dev->parent);

	tmp = kstrtoul(buf, 16, &yo);
	cmd = attr - rt9460_attrs;

	switch (cmd) {
	case RT9460_DBG_REG:

		if (yo == RT9460_REG_HIDDEN || yo == 0x20)
			ri->reg_addr = (unsigned char)yo;
		else if (yo < 0
			 || (yo > RT9460_REG_RANGE1_END
			     && yo < RT9460_REG_RANGE2_START)
			 || yo > RT9460_REG_RANGE2_END) {
			RTINFO("out of range\n");
		} else
			ri->reg_addr = (unsigned char)yo;
		break;

	case RT9460_DBG_DATA:

		ret = rt9460_reg_write(ri->i2c, ri->reg_addr, yo);
		if (ret < 0) {
			RTINFO("rt9460 dbg write data fail\n");
			return count;
		}
		break;

	default:
		break;
	}
	return count;
}

static ssize_t rt9460_show_attrs(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	ptrdiff_t cmd;
	struct rt9460_charger_info *ri = dev_get_drvdata(dev->parent);
	int ret, i;
	int max_size = 512;

	cmd = attr - rt9460_attrs;
	buf[0] = '\0';

	switch (cmd) {
	case RT9460_DBG_REG:
		snprintf(buf, max_size, "0x%02x\n", ri->reg_addr);
		break;

	case RT9460_DBG_DATA:
		ret = rt9460_reg_read(ri->i2c, ri->reg_addr);
		if (ret < 0) {
			RTINFO("rt9460 i2c read fail\n");
			return strnlen(buf, max_size);
		}
		snprintf(buf, max_size, "0x%02x\n", ret);
		break;

	case RT9460_DBG_REGS:
		for (i = RT9460_REG_RANGE1_START; i <= RT9460_REG_RANGE1_END;
		     i++) {
			ret = rt9460_reg_read(ri->i2c, i);
			snprintf(buf + strnlen(buf, max_size), max_size,
				 "reg 0x%02x : 0x%02x\n", i, ret);
		}
		for (i = RT9460_REG_RANGE2_START; i <= RT9460_REG_RANGE2_END;
		     i++) {
			ret = rt9460_reg_read(ri->i2c, i);
			snprintf(buf + strnlen(buf, max_size), max_size,
				 "reg 0x%02x : 0x%02x\n", i, ret);
		}

		break;
	default:
		break;
	}

	return strnlen(buf, max_size);
}

static int rt9460_create_attr(struct rt9460_charger_info *ri)
{
	int attr_size, i;
	int ret = 0;

	ri->reg_addr = 0x00;
	attr_size = sizeof(rt9460_attrs) / sizeof(rt9460_attrs[0]);
	for (i = 0; i < attr_size; i++) {
		ret = device_create_file(ri->psy.dev, rt9460_attrs + i);
		if (ret < 0) {
			RTINFO("create file fail\n");
			return ret;
		}
	}
	return ret;
}

/*  end attribute */

/*  power supply */
static char *rtdef_chg_name = "rt-charger";
//static char *rtdef_chg_name = "rt9460";

static char *rt_charger_supply_list[] = {
	"none",
};

static enum power_supply_property rt_charger_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
};

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT_DUMP_REGISTER
void rt9460_dump_register(struct rt9460_charger_info *ri)
{

	int ret, i = 0;
	int max_size = 512;
	char buf[max_size];

	buf[0] = '\0';

	for (i = RT9460_REG_RANGE1_START; i <= RT9460_REG_RANGE1_END; i++) {
		ret = rt9460_reg_read(ri->i2c, i);
		snprintf(buf + strnlen(buf, max_size), max_size,
			"0x%02X:0x%02X/", i, ret);
	}
	for (i = RT9460_REG_RANGE2_START; i <= RT9460_REG_RANGE2_END; i++) {
		ret = rt9460_reg_read(ri->i2c, i);
		snprintf(buf + strnlen(buf, max_size), max_size,
			"0x%02X:0x%02X/", i, ret);
	}

	RTINFO("rt9460_dump_register : %s\n", buf);

	schedule_delayed_work(&ri->reg_dump_work, msecs_to_jiffies(60000));
}

static void rt9460_register_dump_work(struct work_struct *work)
{
	struct rt9460_charger_info *ri =
		(struct rt9460_charger_info *)container_of(work,
							struct rt9460_charger_info,
							reg_dump_work.work);

	rt9460_dump_register(ri);
}
#endif

static int rt9460_get_status(struct rt9460_charger_info *info)
{
	int regval = 0;
	int status = 0;
	int ret = 0;

	regval = rt9460_reg_read(info->i2c, RT9460_REG_CTRL1);
	status = regval & RT9460_CHGSTAT_MASK;
	status >>= RT9460_CHGSTAT_SHFT;

	if (status == 1)
		ret = POWER_SUPPLY_STATUS_CHARGING;
	else if (status == 2)
		ret = POWER_SUPPLY_STATUS_FULL;
	else {
		if (ri->online == 1)
			ret = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else
			ret = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	return ret;
}

static void rt9460_enable(struct rt9460_charger_info *info, int enable)
{
	if (enable) {
		rt9460_set_bits(info->i2c,
				RT9460_REG_CTRL7,
				RT9460_CHGEN_MASK);
	}
	else {
		rt9460_clr_bits(ri->i2c,
				RT9460_REG_CTRL7,
				RT9460_CHGEN_MASK);
	}
}

#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
static void rt9460_otg_boost_mode(bool enable)
{
	int retval = 0;
	int rc = 0;

	RTINFO("[OTG] rt9460_otg_boost_mode\n");
	if (enable) {
		RTINFO("[OTG] rt9460_otg_boost_mode : enable\n");
		/* Register setting : otg boost enable */
		rc = rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL2,
				RT9460_HZ_MASK);

		rc = rt9460_reg_write(ri->i2c, RT9460_REG_HIDDEN, 0x32);

		retval = (ri->bst_volt - 4425) / 25;
		rc = rt9460_assign_bits(ri->i2c, RT9460_REG_CTRL3,
				RT9460_VOREG_MASK,
				retval << RT9460_VOREG_SHFT);

		rc = rt9460_set_bits(ri->i2c, RT9460_REG_CTRL2,
				RT9460_OPAMODE_MASK);

		rc = rt9460_reg_write(ri->i2c, RT9460_REG_HIDDEN, 0x0e);

	} else {
		/* Register setting : otg boost disable */
		RTINFO("[OTG] rt9460_otg_boost_mode : disable\n");
		retval = (ri->chg_volt - 3500) / 20;
		rc = rt9460_assign_bits(ri->i2c, RT9460_REG_CTRL3,
				RT9460_VOREG_MASK,
				retval << RT9460_VOREG_SHFT);

		rc = rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL2,
				RT9460_OPAMODE_MASK);
		rc = rt9460_reg_write(ri->i2c, RT9460_REG_HIDDEN, 0x32);

	}

	/* Add here the information register */
	retval = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL3);
	RTINFO("[OTG] VOREG = %d\n", retval & RT9460_VOREG_MASK);
	retval = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL2);
	RTINFO("[OTG] OPAMODE = %d\n", retval & RT9460_OPAMODE_MASK);

}

int detect_otg_mode(int enable)
{
	RTINFO("[OTG] boost_enable = %d\n", enable);

	if (enable) {
		RTINFO("[OTG] detect_otg_mode : enable\n");
		rt9460_otg_boost_mode(1);
	} else {
		RTINFO("[OTG] detect_otg_mode : disable\n");
		rt9460_otg_boost_mode(0);
	}

	return 0;
}
#endif

static int rt9460_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct rt9460_charger_info *ri = dev_get_drvdata(psy->dev->parent);
	int ret = 0;
	int regval = 0;
	int regval2 = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = ri->online;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = rt9460_get_status(ri);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		regval = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL7);
		if (regval < 0)
			ret = -EINVAL;
		else {
			if (regval & RT9460_CHGEN_MASK)
				val->intval = 1;
			else
				val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = ri->charge_cable;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		regval = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL11);
		if (regval < 0)
			ret = -EINVAL;
		else {
			regval &= RT9460_CHGAICR_MASK;
			regval >>= RT9460_CHGAICR_SHFT;
			switch (regval) {
			case 0:
				val->intval = 100;
				break;
			case 1:
				val->intval = 150;
				break;
			case 2:
			case 3:
			case 4:
			case 5:
				val->intval = 500;
				break;
			case 31:
				val->intval = -1;
				break;
			default:
				val->intval = regval * 100;
				break;
			}
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		regval = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL6);
		regval2 = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL2);
		if ((regval < 0) || (regval2 < 0))
			ret = -EINVAL;
		else {
			if (regval2 & RT9460_OPAMODE_MASK)	/*boost mode */
				val->intval = -1;
			else {	/* charger mode */
				regval &= RT9460_ICHRG_MASK;
				regval >>= RT9460_ICHRG_SHFT;
				val->intval = 1250 + regval * 125;
			}
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		regval = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL3);
		if (regval < 0)
			ret = -EINVAL;
		else {
			regval &= RT9460_VOREG_MASK;
			regval >>= RT9460_VOREG_SHFT;
			if (regval >= 57)
				val->intval = 4620;
			else
				val->intval = regval * 20 + 3500;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
		val->intval = 3500;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4620;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	RTINFO("%s: prop = %d,  val->intval = %d\n", __func__, psp, val->intval);

	return ret;
}

static int rt9460_charger_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct rt9460_charger_info *ri = dev_get_drvdata(psy->dev->parent);
#ifdef CONFIG_RT_BATTERY
	union power_supply_propval pval;
	struct power_supply *bat = power_supply_get_by_name(RT_BATT_NAME);
#endif /*CONFIG_RT_BATTERY */
	int ret = 0;
	int regval = 0;
	int batvolt_mv = 0;

	RTINFO("%s: prop = %d,  val->intval = %d\n", __func__, psp, val->intval);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ri->online = val->intval;
		if (ri->online) {
			if (ri->te_en)
				rt9460_set_bits(ri->i2c, RT9460_REG_CTRL2,
						RT9460_TEEN_MASK);

			ri->chg_stat = POWER_SUPPLY_STATUS_CHARGING;
#ifdef CONFIG_RT_BATTERY
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				bat->set_property(bat,
						  POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(bat);
			} else
				dev_err(ri->dev, "couldn't get RT battery\n");
#endif /* CONFIG_RT_BATTERY */
		} else {
#ifdef CONFIG_RT_BATTERY
			if (bat) {
				pval.intval = POWER_SUPPLY_STATUS_DISCHARGING;
				bat->set_property(bat,
						  POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(bat);
			} else
				dev_err(ri->dev, "couldn't get RT battery\n");
#endif /* #ifdef CONFIG_RT_BATTERY */
			ri->chg_stat = POWER_SUPPLY_STATUS_DISCHARGING;
			if (ri->te_en)
				rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL2,
						RT9460_TEEN_MASK);
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		if (val->intval) {
			ret = rt9460_set_bits(ri->i2c,
					      RT9460_REG_CTRL7,
					      RT9460_CHGEN_MASK);
			ri->chg_stat = POWER_SUPPLY_STATUS_CHARGING;
		} else {
			ret = rt9460_clr_bits(ri->i2c,
					      RT9460_REG_CTRL7,
					      RT9460_CHGEN_MASK);
			ri->chg_stat = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SUPPORT
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		rt9460_enable(ri, val->intval);
		break;
        case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		if (val->intval)
			rt9460_chip_enable(ri);
		else
			rt9460_chip_disable(ri);
		break;
#endif
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ri->charge_cable = val->intval;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (val->intval <= 100)
			regval = 0;
		else if (val->intval <= 150)
			regval = 1;
		else if (val->intval < 500)
			regval = 2;
		else if (val->intval <= 3000)
			regval = val->intval / 100;
		else
			regval = 31;	/* disable aicr */
		if (!ri->batabs)
			ret = rt9460_assign_bits(ri->i2c,
						 RT9460_REG_CTRL11,
						 RT9460_CHGAICR_MASK,
						 regval << RT9460_CHGAICR_SHFT);
		if (ret < 0)
			return -EINVAL;

		/*RT9460 charging fail work around code - temporary*/
		rt9460_reg_write(ri->i2c,RT9460_REG_DPDM, 0x10);
		RTINFO("set IINLMTSEL : 0x10\n");
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (val->intval < 0) {	/* boost mode */
			ret =
			    rt9460_reg_write(ri->i2c, RT9460_REG_HIDDEN, 0x32);
			if (ret < 0)
				return -EINVAL;

			/* set boost output voltage */
			regval = (ri->bst_volt - 4425) / 25;
			ret = rt9460_assign_bits(ri->i2c, RT9460_REG_CTRL3,
						 RT9460_VOREG_MASK,
						 regval << RT9460_VOREG_SHFT);
			if (ret < 0)
				return -EINVAL;

			ret =
			    rt9460_set_bits(ri->i2c, RT9460_REG_CTRL2,
					    RT9460_OPAMODE_MASK);
			if (ret < 0)
				return -EINVAL;
			ret =
			    rt9460_reg_write(ri->i2c, RT9460_REG_HIDDEN, 0x0e);
			if (ret < 0)
				return -EINVAL;
		} else if (val->intval == 0) {	/* charger mode */
			/* set battery regulation voltage */
			regval = (ri->chg_volt - 3500) / 20;
			ret = rt9460_assign_bits(ri->i2c, RT9460_REG_CTRL3,
						 RT9460_VOREG_MASK,
						 regval << RT9460_VOREG_SHFT);
			if (ret < 0)
				return -EINVAL;

			ret =
			    rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL2,
					    RT9460_OPAMODE_MASK);
			if (ret < 0)
				return -EINVAL;
			ret =
			    rt9460_reg_write(ri->i2c, RT9460_REG_HIDDEN, 0x32);
			if (ret < 0)
				return -EINVAL;
		} else if (val->intval < 1250)
			regval = 0;
		else if (val->intval > 3125)
			regval = 15;
		else
			regval = (val->intval - 1250) / 125;
		if (!ri->batabs)
			ret = rt9460_assign_bits(ri->i2c,
						 RT9460_REG_CTRL6,
						 RT9460_ICHRG_MASK,
						 regval << RT9460_ICHRG_SHFT);
		if (ret < 0)
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		batvolt_mv = val->intval/1000;
		if (batvolt_mv < 3500)
			regval = 0;
		else if (batvolt_mv > 4620)
			regval = 63;
		else
			regval = (batvolt_mv - 3500) / 20;

		ret =
		    rt9460_assign_bits(ri->i2c, RT9460_REG_CTRL3,
				       RT9460_VOREG_MASK,
				       regval << RT9460_VOREG_SHFT);
		if (ret < 0)
			return -EINVAL;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
	case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		break;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SAFETY_TIMER_SUPPORT
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
		if (val->intval)
			regval = 2;
		else
			regval = 7;

		ret = rt9460_assign_bits(ri->i2c, RT9460_REG_CTRL10, RT9460_WTFC_MASK,
				regval << RT9460_WTFC_SHFT);
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* end power supply function */

/* batterry absence */
#ifndef CONFIG_LGE_PM
static void rt9460_batabs_irq_handler(void *info, int eventno)
{
	struct rt9460_charger_info *ri = info;
	struct power_supply *psy = &ri->psy;
#ifdef CONFIG_RT_BATTERY
	struct power_supply *bat = power_supply_get_by_name(RT_BATT_NAME);
#endif /* CONFIG_RT_BATTERY */
	union power_supply_propval val;
	int ret;

	RTINFO("eventno=%02d\n", eventno);
	dev_info(ri->dev, "IRQ : battery absence!\n");

	ret = rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL7, RT9460_BATDEN_MASK);
	if (ret < 0)
		dev_err(psy->dev, "set battery detection disable fail\n");

	/*set aicr to max */
	val.intval = 3000;
	ret = psy->set_property(psy, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	if (ret < 0)
		dev_err(psy->dev, "set aicr to 2000 fail\n");

	/*set icc to max */
	val.intval = 3125;
	ret = psy->set_property(psy, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	if (ret < 0)
		dev_err(psy->dev, "set icc to 1250 fail\n");

	ri->chg_stat = POWER_SUPPLY_STATUS_DISCHARGING;

	/* trun off TE */
	val.intval = 0;
	ret = psy->set_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret < 0)
		dev_err(ri->dev, "set te off fail\n");

#ifdef CONFIG_RT_BATTERY
	if (bat) {
		val.intval = 0;
		ret = bat->set_property(bat, POWER_SUPPLY_PROP_PRESENT, &val);
		if (ret < 0)
			dev_err(ri->dev, "set battery not present fail\n");
	} else
		dev_err(ri->dev, "couldn't get batt psy\n");
#endif /* #ifdef CONFIG_RT_BATTERY */

	ri->batabs = 1;
}
#endif
/* use detection work */
static void rt9460_usbdet_work(struct work_struct *work)
{
	struct rt9460_charger_info *ri = container_of(work,
						      struct
						      rt9460_charger_info,
						      usb_detect_work.work);
	struct power_supply *chg = power_supply_get_by_name("rt-charger");
#ifdef CONFIG_RT_POWER
	struct power_supply *ac = power_supply_get_by_name("rt-ac");
	struct power_supply *usb = power_supply_get_by_name("rt-usb");
#endif /*CONFIG_RT_POWER */
	union power_supply_propval val;
	int dpdm, type, ret;

	RTINFO(" usb charger detection....\n");

	dpdm = rt9460_reg_read(ri->i2c, RT9460_REG_DPDM);
	if (dpdm < 0) {
		dev_err(ri->dev, "rt9460 chgrun read fail\n");
		return;
	}

	if (dpdm & RT9460_CHGRUN_MASK)
		schedule_delayed_work(&ri->usb_detect_work,
				      msecs_to_jiffies(300));
	else {
		type = dpdm & RT9460_CHGTYPE_MASK;
		pr_err("[CHARGER_PDATA] type = %d\n", type);
		type >>= RT9460_CHGTYPE_SHFT;
		switch (type) {
		case 0:
			dev_info(ri->dev, "Standard USB\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_USB;
			break;
		case 1:
			dev_info(ri->dev, "Sony Charger-1\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_MAINS;
			break;
		case 2:
			dev_info(ri->dev, "Sony Charger-2\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_MAINS;
			break;
		case 3:
			dev_info(ri->dev, "APPLE 0.5A\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_MAINS;
			break;
		case 4:
			dev_info(ri->dev, "APPLE 1A\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_MAINS;
			break;
		case 5:
			dev_info(ri->dev, "Nikon 1A\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_MAINS;
			break;
		case 6:
			dev_info(ri->dev, "CDP\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_USB_CDP;
			break;
		case 7:
			dev_info(ri->dev, "DCP\n");
			ri->charge_cable = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		}

#ifdef CONFIG_RT_POWER
		if (ri->charge_cable == POWER_SUPPLY_TYPE_USB ||
		    ri->charge_cable == POWER_SUPPLY_TYPE_USB_CDP) {
			val.intval = 1;
			ret =
			    usb->set_property(usb, POWER_SUPPLY_PROP_ONLINE,
					      &val);
			if (ret < 0)
				dev_err(ri->dev, "usb set online fail\n");
		} else if (ri->charge_cable == POWER_SUPPLY_TYPE_MAINS ||
			   ri->charge_cable == POWER_SUPPLY_TYPE_USB_DCP) {
			val.intval = 1;
			ret =
			    ac->set_property(ac, POWER_SUPPLY_PROP_ONLINE,
					     &val);
			if (ret < 0)
				dev_err(ri->dev, "ac set online fail\n");
		}
#endif /*CONFIG_RT_POWER */

		val.intval = 1;
		ret = chg->set_property(chg, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret < 0)
			dev_err(ri->dev, "chg set online fail\n");
		wake_unlock(&ri->usb_wake_lock);
	}

}

static int rt9460_charger_property_is_writeable(struct power_supply *psy, enum power_supply_property prop)
{
	int rc = 0;
	switch (prop) {
		case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		case POWER_SUPPLY_PROP_CURRENT_NOW:
		case POWER_SUPPLY_PROP_CURRENT_MAX:
		case POWER_SUPPLY_PROP_CURRENT_AVG:
		case POWER_SUPPLY_PROP_PRESENT:
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SUPPORT
		case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		case POWER_SUPPLY_PROP_CHARGING_ENABLED:
#endif
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SAFETY_TIMER_SUPPORT
		case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
#endif
			rc = 1;
			break;
		default:
			rc = 0;
			break;
	}

	return rc;
}

static void rt9460_plug_irq_handler(void *info)
{
	struct rt9460_charger_info *ri = info;
	struct power_supply *chg = power_supply_get_by_name("rt-charger");
#ifdef CONFIG_RT_POWER
	struct power_supply *ac = power_supply_get_by_name("rt-ac");
	struct power_supply *usb = power_supply_get_by_name("rt-usb");
#endif /*CONFIG_RT_POEWR */
	union power_supply_propval val;
	int ret;

	RTINFO("statevent POWER\n");
	ret = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL1);
	if (ret < 0) {
		dev_err(ri->dev, "rt9460 i2c read fail\n");
		return;
	}

	if (ret & RT9460_POWRRDY_MASK) {
		dev_info(ri->dev, "cable in\n");
#ifndef CONFIG_LGE_PM
		/* set icc */
		val.intval = ri->icc;
		ret = chg->set_property(chg, POWER_SUPPLY_PROP_CURRENT_NOW,
					&val);
		if (ret < 0) {
			dev_err(ri->dev, "rt9460 set icc fail\n");
			return;
		}
		/*set aicr */
		val.intval = ri->aicr;
		ret = chg->set_property(chg, POWER_SUPPLY_PROP_CURRENT_MAX,
					&val);
		if (ret < 0) {
			dev_err(ri->dev, "rt9460 set aicr fail\n");
			return;
		}
		/*set chg voltage */
		val.intval = ri->chg_volt;
		ret = chg->set_property(chg, POWER_SUPPLY_PROP_VOLTAGE_MAX,
					&val);
		if (ret < 0) {
			dev_err(ri->dev, "rt9460 set chg voltage fail\n");
			return;
		}
#endif
		/*usb det */
		if (ri->usb_det) {
			wake_lock_timeout(&ri->usb_wake_lock,
					  msecs_to_jiffies
					  (RT9460_USBDET_TIMEOUT));
			schedule_delayed_work(&ri->usb_detect_work,
					      msecs_to_jiffies(100));
		} else {
//			msleep(30);
			ri->charge_cable = POWER_SUPPLY_TYPE_MAINS;
			val.intval = 1;
			ret =
			    chg->set_property(chg, POWER_SUPPLY_PROP_ONLINE,
					      &val);
			if (ret < 0)
				dev_err(ri->dev, "chg set online fail\n");
		}


		ret = rt9460_set_bits(ri->i2c, RT9460_REG_CTRL2, 0x04);

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT_DUMP_REGISTER
		schedule_delayed_work(&ri->reg_dump_work,
					msecs_to_jiffies(5000));
#endif
	} else {
		dev_info(ri->dev, "cable out\n");

#ifndef CONFIG_LGE_PM
#ifdef CONFIG_RT_POWER
		if (ri->charge_cable == POWER_SUPPLY_TYPE_USB ||
		    ri->charge_cable == POWER_SUPPLY_TYPE_USB_CDP) {
			val.intval = 0;
			ret =
			    usb->set_property(usb, POWER_SUPPLY_PROP_ONLINE,
					      &val);
			if (ret < 0) {
				dev_err(ri->dev, "usb set offline fail\n");
				return;
			}
		} else if (ri->charge_cable == POWER_SUPPLY_TYPE_MAINS ||
			   ri->charge_cable == POWER_SUPPLY_TYPE_USB_DCP) {
			val.intval = 0;
			ret =
			    ac->set_property(ac, POWER_SUPPLY_PROP_ONLINE,
					     &val);
			if (ret < 0) {
				dev_err(ri->dev, "ac set offline fail\n");
				return;
			}
		}
#endif /*CONFIG_RT_POWER */

		ri->charge_cable = 0;
		val.intval = 1250;
		ret =
		    chg->set_property(chg, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
		if (ret < 0) {
			dev_err(ri->dev, "set icc fail\n");
			return;
		}
		val.intval = 500;
		ret =
		    chg->set_property(chg, POWER_SUPPLY_PROP_CURRENT_MAX, &val);
		if (ret < 0) {
			dev_err(ri->dev, "set aicr fail\n");
			return;
		}
		val.intval = 4000;
		ret =
		    chg->set_property(chg, POWER_SUPPLY_PROP_VOLTAGE_MAX, &val);
		if (ret < 0) {
			dev_err(ri->dev, "set chg voltage fail\n");
			return;
		}
#endif
		val.intval = 0;
		ret = chg->set_property(chg, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret < 0) {
			dev_err(ri->dev, "set chg offline fail\n");
			return;
		}

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT_DUMP_REGISTER
		cancel_delayed_work(&ri->reg_dump_work);
#endif
#ifndef CONFIG_LGE_PM
		wake_unlock(&ri->usb_wake_lock);
#endif

	}
}

/* status change irq handler */
static void rt9460_stat_irq_handler(void *info, int new_stat)
{
	struct rt9460_charger_info *ri = info;
	int ret = 0;
#ifdef CONFIG_RT_BATTERY
	struct power_supply *psy = power_supply_get_by_name("rt-battery");
#endif /* CONFIG_RT_BATTERY */
	RTINFO("stat event 0x%02x\n", new_stat);

	if (new_stat & RT9460_MIVRI_MASK) {
		RTINFO("statevent MIVR\n");
		ret = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL1);
		if (ret & RT9460_MIVRSTAT_MASK)
			dev_info(ri->dev, "MIVR regulation is active\n");
		else
			dev_info(ri->dev, "MIVR regulatron is inactive\n");
	}

	if (new_stat & RT9460_TSEVENT_MASK) {
		RTINFO("statevent TS\n");
		ret = rt9460_reg_read(ri->i2c, RT9460_REG_CTRL7);
		if (ret < 0)
			return;

		if (ret & RT9460_TSWC_MASK) {
			dev_info(ri->dev, "warm or cool temperature\n");
			rt9460_set_bits(ri->i2c, RT9460_REG_CTRL7,
					RT9460_CCJEITA_MASK);
			ri->is_cold_hot = 0;
#ifdef CONFIG_RT_BATTERY
			if (psy) {
				val.intval = POWER_SUPPLY_HEALTH_GOOD;
				psy->set_property(psy,
						  POWER_SUPPLY_PROP_HEALTH,
						  &val);
				power_supply_changed(psy);
			} else
				dev_err(ri->dev, "couldn't get batt psy\n");
#endif /*CONFIG_RT_BATTERY */
		} else if (ret & RT9460_TSHC_MASK) {
			dev_info(ri->dev, "hot or cold temperature\n");

			rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL7,
					RT9460_CCJEITA_MASK);
			ri->is_cold_hot = 1;
#ifdef CONFIG_RT_BATTERY
			if (psy) {
				if (ret & 0x08) {
					val.intval =
					    POWER_SUPPLY_HEALTH_OVERHEAT;
					psy->set_property(psy,
						POWER_SUPPLY_PROP_HEALTH,
						&val);
					power_supply_changed(psy);
				} else if (ret & 0x01) {
					val.intval = POWER_SUPPLY_HEALTH_COLD;
					psy->set_property(psy,
						POWER_SUPPLY_PROP_HEALTH,
						&val);
					power_supply_changed(psy);
				}
			} else
				dev_err(ri->dev, "couldn't get batt psy\n");
#endif /*CONFIG_RT_BATTERY */
		} else {
			dev_info(ri->dev, "normal temperature\n");

			rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL7,
					RT9460_CCJEITA_MASK);
			ri->is_cold_hot = 0;
#ifdef CONFIG_RT_BATTERY
			if (psy) {
				val.intval = POWER_SUPPLY_HEALTH_GOOD;
				psy->set_property(psy,
						  POWER_SUPPLY_PROP_HEALTH,
						  &val);
				power_supply_changed(psy);
			} else
				dev_err(ri->dev, "couldn't get batt psy\n");
#endif /*CONFIG_RT_BATTERY */
		}
	}
}

static void rt9460_general_irq_handler(void *info, int eventno)
{
	struct rt9460_charger_info *ri = info;
	int ret;
#ifdef CONFIG_RT_BATTERY
	struct power_supply *psy = power_supply_get_by_name(RT_BATT_NAME);
	union power_supply_propval pval;
#endif /* CONFIG_RT_BATTERY */
	RTINFO("eventno=%02d\n", eventno);
	switch (eventno) {
	case CHGEVENT_CHRCHGI:
		if (!ri->online)
			dev_info(ri->dev, " recharger false alarm\n");
		else {
			dev_info(ri->dev, "IRQ : recharger event occured\n");
			ri->chg_stat = POWER_SUPPLY_STATUS_CHARGING;
#ifdef CONFIG_RT_BATTERY
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_CHARGING;
				psy->set_property(psy,
						  POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else
				dev_err(ri->dev, "couldn't get batt psy\n");
#endif /* #ifdfef CONFIG_RT_BATTERY */
		}
		break;
	case CHGEVENT_CHTERMI:
		if (ri->online){
			dev_info(ri->dev, "IRQ : charger terminated\n");
		}
		break;
	case CHGEVENT_CHTERMTMRI:
		if (ri->online) {
			if (rt9460_get_status(ri) == POWER_SUPPLY_STATUS_FULL)
				return;
			dev_info(ri->dev, "IRQ : EOC occurs\n");
			ri->chg_stat = POWER_SUPPLY_STATUS_FULL;
#ifdef CONFIG_RT_BATTERY
			if (psy) {
				pval.intval = POWER_SUPPLY_STATUS_FULL;
				psy->set_property(psy,
						  POWER_SUPPLY_PROP_STATUS,
						  &pval);
				power_supply_changed(psy);
			} else
				dev_err(ri->dev, "couldn't get batt psy\n");
#endif /* #ifdfef CONFIG_RT_BATTERY */
		}
		break;
	case CHGEVENT_WATCHDOGI:
		dev_info(ri->dev, "IRQ : watchdog timer\n");
		ret = rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL13,
				      RT9460_WDTEN_MASK);
		if (ret < 0)
			return;
		ret = rt9460_set_bits(ri->i2c, RT9460_REG_CTRL13,
				      RT9460_WDTEN_MASK);
		if (ret < 0)
			return;
		break;
	case CHGEVENT_CHTMRI:
		dev_info(ri->dev, "IRQ : safety timeout\n");
		ri->chg_stat = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		break;
	}
}

static rt_irq_handler rt_chgirq_handler[CHGEVENT_MAX] = {
	[CHGEVENT_BSTLOWVI] = rt9460_general_irq_handler,
	[CHGEVENT_BSTOLI] = rt9460_general_irq_handler,
	[CHGEVENT_BSTVINOVI] = rt9460_general_irq_handler,
	[CHGEVENT_SYSWAKEUPI] = rt9460_general_irq_handler,
	[CHGEVENT_CHTREGI] = rt9460_general_irq_handler,
	[CHGEVENT_CHTMRI] = rt9460_general_irq_handler,
	[CHGEVENT_CHRCHGI] = rt9460_general_irq_handler,
	[CHGEVENT_CHTERMI] = rt9460_general_irq_handler,
	[CHGEVENT_CHBATOVI] = rt9460_general_irq_handler,
	[CHGEVENT_CHBADI] = rt9460_general_irq_handler,
	[CHGEVENT_CHRVPI] = rt9460_general_irq_handler,
#ifndef CONFIG_LGE_PM
	[CHGEVENT_BATABI] = rt9460_batabs_irq_handler,
#endif
	[CHGEVENT_SYSUVPI] = rt9460_general_irq_handler,
	[CHGEVENT_CHTERMTMRI] = rt9460_general_irq_handler,
	[CHGEVENT_WATCHDOGI] = rt9460_general_irq_handler,
	[CHGEVENT_WAKEUPI] = rt9460_general_irq_handler,
	[CHGEVENT_VIMOVPI] = rt9460_general_irq_handler,
	[CHGEVENT_TSDI] = rt9460_general_irq_handler,
};

static unsigned char rt9460_init_regval[] = {
#if defined(CONFIG_MACH_MT6750_SF3) || defined(CONFIG_MACH_MT6750_LV5)
	0xC0,
#else
	0x40,			/* 0 0x00 ctrl1 */
#endif
	0x40,			/* 1 0x01 ctrl2 */
	0x66,			/* 2 0x02 ctrl3 */
#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
	0xda,
#else
	0x9a,			/* 3 0x05 ctrl5 */
#endif
	0x02,			/* 4 0x06 ctrl6 */
	0x50,			/* 5 0x07 ctrl7 */
	0x00,			/* 6 0x0b mask1 */
	0x01,			/* 7 0x0c mask2 */
	0x00,			/* 8 0x0d mask3 */
	0x16,			/* 9 0x0e dpdm */
#if defined (CONFIG_LGE_PM)
	0x42,           /* 10 0x21 ctrl9 MIVR 4.5V enable */
#else
	0x52,			/* 10 0x21 ctrl9 */
#endif
#if defined (CONFIG_LGE_PM)
	0x10,			/* Safety timer 8hrs */
#else
	0x00,			/* 11 0x22 ctrl10 */
#endif
	0x2f,			/* 12 0x23 ctrl11 */
	0x00,			/* 13 0x24 ctrl12 */
	0x04,			/* 14 0x27 mask4 */
};

static inline int rt9460_irq_release(struct rt9460_charger_info *ri)
{
	int ret = 0;

	/* Enable wake-up timer to trigger internal osc to be turnned on */
	ret = rt9460_set_bits(ri->i2c, RT9460_REG_CTRL12, 0x04);
	if (ret < 0)
		return ret;
	msleep(10);
	/* set irq_rez bit to trigger irq pin to be released */
	ret = rt9460_set_bits(ri->i2c, RT9460_REG_CTRL12, RT9460_IRQREZ_MASK);
	if (ret < 0)
		return ret;
	/* Disable wake-up timer */
	return rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL12, 0x04);
}

static irqreturn_t rt9460_chg_irq_handler(int irqno, void *param)
{
	struct rt9460_charger_info *ri = param;
	unsigned char regval[3];
	unsigned int irq_event;
	unsigned int masked_irq_event;
	int new_stat;
	int ret = 0, i = 0;

	RTINFO(" irqno = %d\n", irqno);
	/*handle stat irq */
	new_stat = rt9460_reg_read(ri->i2c, RT9460_REG_STATIRQ);
	if (new_stat < 0) {
		dev_err(ri->dev, "read stat irq event fail\n");
		goto irq_fin;
	}
	new_stat &= ~rt9460_init_regval[14];

	ret = rt9460_reg_block_read(ri->i2c, RT9460_REG_IRQ1,
				    ARRAY_SIZE(regval), regval);
	if (ret < 0) {
		dev_err(ri->dev, "read charger irq event fail\n");
		goto irq_fin;
	}
	irq_event = regval[0] << 16 | regval[1] << 8 | regval[2];

	/* stat TS handler */
	if ((new_stat & RT9460_TSEVENT_MASK) || (new_stat & RT9460_MIVRI_MASK))
		rt9460_stat_irq_handler(ri, new_stat);

	if (ri->is_cold_hot)
		goto irq_fin;

	/* plug in, out irq */
	if (new_stat & RT9460_PWRRDYI_MASK)
		rt9460_plug_irq_handler(ri);

	if (!ri->charge_cable)
		goto irq_fin;

	/*handle chg irq */
	masked_irq_event = (rt9460_init_regval[6] << 16) |
	    (rt9460_init_regval[7] << 8) | rt9460_init_regval[8];

	if (irq_event) {
		RTINFO("chg event %06x\n", irq_event);
		irq_event &= (~masked_irq_event);
		for (i = 0; i < CHGEVENT_MAX; i++) {
			if ((irq_event & (1 << i)) && rt_chgirq_handler[i])
				rt_chgirq_handler[i] (ri, i);
		}
	}

irq_fin:
	ret = rt9460_irq_release(ri);
	if (ret < 0)
		dev_err(ri->dev, "write irq release bit fail\n");
	return IRQ_HANDLED;

}

static void rt9460_irq_delayed_work(struct work_struct *work)
{
	struct rt9460_charger_info *ri =
	    (struct rt9460_charger_info *)container_of(work,
						       struct
						       rt9460_charger_info,
						       irq_delayed_work.work);

	rt9460_chg_irq_handler(ri->irq, ri);
}

static int rt9460_reg_init(struct rt9460_charger_info *info)
{
	int ret = 0;
	int regval = 0;

	ret =
	    rt9460_reg_block_write(info->i2c, RT9460_REG_CTRL1, 3,
				   &rt9460_init_regval[0]);
	if (ret < 0)
		return -EINVAL;
	ret = rt9460_reg_block_write(info->i2c,
				     RT9460_REG_CTRL5, 2,
				     &rt9460_init_regval[3]);
	if (ret < 0)
		return -EINVAL;
	ret = rt9460_reg_block_write(info->i2c,
				     RT9460_REG_MASK1, 3,
				     &rt9460_init_regval[6]);
	if (ret < 0)
		return -EINVAL;
	ret = rt9460_reg_write(info->i2c, RT9460_REG_DPDM,
			       rt9460_init_regval[9]);
	if (ret < 0)
		return -EINVAL;

	/*Does not set CTRL9 MIVR register set in kernel, only set in LK*/

	ret = rt9460_reg_write(info->i2c,
			       RT9460_REG_CTRL10, rt9460_init_regval[11]);
	if (ret < 0)
		return -EINVAL;
	ret = rt9460_reg_write(info->i2c,
			       RT9460_REG_CTRL12, rt9460_init_regval[13]);
	if (ret < 0)
		return -EINVAL;
	ret = rt9460_reg_write(info->i2c,
			       RT9460_REG_STATIRQMASK, rt9460_init_regval[14]);

	if (ret < 0)
		return -EINVAL;
	/*set VOREG Voltage for init*/
	regval = (info->chg_volt - 3500) / 20;
	ret = rt9460_assign_bits(info->i2c, RT9460_REG_CTRL3,
			RT9460_VOREG_MASK, regval << RT9460_VOREG_SHFT);
	return ret;
}

static int rt9460_irq_init(struct rt9460_charger_info *ri)
{
	rt9460_irq_release(ri);
	INIT_DELAYED_WORK(&ri->irq_delayed_work, rt9460_irq_delayed_work);

	if (ri->irq >= 0) {
		if (devm_request_threaded_irq(ri->dev, ri->irq, NULL,
					      rt9460_chg_irq_handler,
						  IRQF_TRIGGER_FALLING |
						  IRQF_ONESHOT, "rt9460-irq",
					//      IRQF_TRIGGER_FALLING |
					  //    IRQF_DISABLED, "rt9460-irq",
					      ri)) {
			dev_err(ri->dev, "request threaded irq fail\n");
			return -EINVAL;
		}
		enable_irq_wake(ri->irq);
		schedule_delayed_work(&ri->irq_delayed_work,
				      msecs_to_jiffies(1000));
	}
	/* read all IRQ status and set irq_rez bit */

	rt9460_plug_irq_handler(ri);
	return 0;
}

static void rt9460_chip_enable(struct rt9460_charger_info *ri)
{
	int ret;
	/*hidden bits for charger performance */
	ret = rt9460_reg_write(ri->i2c, 0x1c, 0x9f);
	if (ret < 0) {
		dev_err(ri->dev, "rt9460 i2c write fail\n");
		return;
	}
	ret = rt9460_reg_write(ri->i2c, 0x31, 0x3f);
	if (ret < 0) {
		dev_err(ri->dev, "rt9460 i2c write fail\n");
		return;
	}

	ret = rt9460_set_bits(ri->i2c, 0x31, 0x80);
	RTINFO("hidden bit to prevent i2c transaction error\n");
	if (ret < 0) {
		dev_err(ri->dev, "rt9460 0x31 reg i2c write fail\n");
		return;
	}

	/* chip enable bit */
	rt9460_set_bits(ri->i2c, RT9460_REG_CTRL7, RT9460_CHIPEN_MASK);
}

static void rt9460_chip_disable(struct rt9460_charger_info *ri)
{
	rt9460_clr_bits(ri->i2c, RT9460_REG_CTRL7, RT9460_CHIPEN_MASK);
}

static int rt_parse_dt(struct rt9460_charger_info *ri,
				 struct device *dev)
{
#ifdef CONFIG_OF
	struct device_node *np = dev->of_node;

	u32 val;

	val = of_get_named_gpio_flags(np, "rt,irq_gpio", 0, NULL);
//	val = of_get_named_gpio(np, "rt,irq_gpio", 0);
	if (gpio_is_valid(val)) {
		if (gpio_request(val, "rt9460_gpio") >= 0) {
			gpio_direction_input(val);
			ri->irq = gpio_to_irq(val);
		} else
			ri->irq = -1;
	} else
		ri->irq = -1;

	if (of_property_read_bool(np, "rt,usb_det"))
		ri->usb_det = 1;
	else
		ri->usb_det = 0;

	if (of_property_read_bool(np, "rt,te_en"))
		ri->te_en = 1;
	else
		ri->te_en = 0;

	if (of_property_read_bool(np, "rt,chg_2det"))
		rt9460_init_regval[9] |= RT9460_CHG2DET_MASK;
	else
		rt9460_init_regval[9] &= ~RT9460_CHG2DET_MASK;

	if (of_property_read_bool(np, "rt,chg_1det"))
		rt9460_init_regval[9] |= RT9460_CHG1DET_MASK;
	else
		rt9460_init_regval[9] &= ~RT9460_CHG1DET_MASK;

	if (of_property_read_u32(np, "rt,iinlm_sel", &val) >= 0) {
		if (val > RT9460_IICSEL_LOWER)
			val = RT9460_IICSEL_LOWER;
		rt9460_init_regval[9] &= (~RT9460_IICSEL_MASK);
		rt9460_init_regval[9] |= val << RT9460_IICSEL_SHFT;
	}

	if (of_property_read_u32(np, "rt,iprec", &val) >= 0) {
		if (val < RT9460_IPREC_20P)
			val = RT9460_IPREC_10P;
		if (val > RT9460_IPREC_20P)
			val = RT9460_IPREC_20P;
		rt9460_init_regval[3] &= (~RT9460_IPREC_MASK);
		rt9460_init_regval[3] |= val << RT9460_IPREC_SHFT;
	}

	if (of_property_read_u32(np, "rt,ieoc", &val) >= 0) {
		if (val > RT9460_IEOC_450MA)
			val = RT9460_IEOC_450MA;
		rt9460_init_regval[1] &= (~RT9460_IEOC_MASK);
		rt9460_init_regval[1] |= val << RT9460_IEOC_SHFT;
	}

	if (of_property_read_u32(np, "rt,deadbat", &val) >= 0) {
		if (val > RT9460_DEADBAT_3P6V)
			val = RT9460_DEADBAT_3P6V;
		rt9460_init_regval[12] &= (~RT9460_DEADBAT_MASK);
		rt9460_init_regval[12] |= val << RT9460_DEADBAT_SHFT;
	}

	if (of_property_read_u32(np, "rt,vprec", &val) >= 0) {
		if (val > RT9460_VPREC_MAX)
			val = RT9460_VPREC_MAX;
		rt9460_init_regval[4] &= (~RT9460_VPREC_MASK);
		rt9460_init_regval[4] |= val << RT9460_VPREC_SHFT;
	}

	if (of_property_read_u32(np, "rt,icc", &val)) {
		dev_info(ri->dev,
			 "no icc property, use 1250 as the default value\n");
		ri->icc = 1250;
	} else
		ri->icc = val;

	if (of_property_read_u32(np, "rt,aicr", &val)) {
		dev_info(ri->dev,
			 "no aicr property, use 500 as the default value\n");
		ri->aicr = 500;
	} else
		ri->aicr = val;

	if (of_property_read_u32(np, "rt,chg_volt", &val)) {
		dev_info(ri->dev,
			 "no chg_volt property, use 4000 as the default value\n");
		ri->chg_volt = 4000;
	} else
		ri->chg_volt = val;

	if (of_property_read_u32(np, "rt,bst_volt", &val)) {
		dev_info(ri->dev,
			 "no bst_volt property, use 5000 as the default value\n");
		ri->bst_volt = 5000;
	} else
		ri->bst_volt = val;

#endif /*CONFIG_OF */
	return 0;
}

static int rt_parse_pdata(struct rt9460_charger_info *ri,
				    struct device *dev)
{
	struct rt9460_platform_data *pdata = dev->platform_data;
	u32 val;

	RTINFO("\n");
	val = pdata->irq_gpio;
	if (gpio_is_valid(val)) {
		if (gpio_request(val, "rt9453_gpio") >= 0) {
			gpio_direction_input(val);
			ri->irq = gpio_to_irq(val);
		} else
			ri->irq = -1;
	} else
		ri->irq = -1;

	ri->te_en = (pdata->te_en) ? 1 : 0;
	ri->usb_det = (pdata->usb_det) ? 1 : 0;

	if (pdata->chg_2det)
		rt9460_init_regval[9] |= RT9460_CHG2DET_MASK;
	else
		rt9460_init_regval[9] &= ~RT9460_CHG2DET_MASK;

	if (pdata->chg_1det)
		rt9460_init_regval[9] |= RT9460_CHG1DET_MASK;
	else
		rt9460_init_regval[9] &= ~RT9460_CHG1DET_MASK;

	rt9460_init_regval[1] &= ~RT9460_IEOC_MASK;
	rt9460_init_regval[1] |= pdata->ieoc << RT9460_IEOC_SHFT;

	rt9460_init_regval[3] &= ~RT9460_IPREC_MASK;
	rt9460_init_regval[3] |= pdata->iprec << RT9460_IPREC_SHFT;

	rt9460_init_regval[4] &= ~RT9460_VPREC_MASK;
	rt9460_init_regval[4] |= pdata->vprec << RT9460_VPREC_SHFT;

	rt9460_init_regval[9] &= ~RT9460_IICSEL_MASK;
	rt9460_init_regval[9] |= pdata->iinlm_sel << RT9460_IICSEL_SHFT;

	rt9460_init_regval[12] &= ~RT9460_DEADBAT_MASK;
	rt9460_init_regval[12] |= pdata->deadbat << RT9460_DEADBAT_SHFT;

	ri->icc = pdata->icc;
	ri->aicr = pdata->aicr;
	ri->chg_volt = pdata->chg_volt;
	ri->bst_volt = pdata->bst_volt;

	return 0;
}

#ifdef CONFIG_RT_POWER
static struct platform_device rt_power_dev = {.name = "rt-power",
	.id = -1,
};
#endif /* CONFIG_RT_POWER */

#ifdef CONFIG_RT_BATTERY
static struct platform_device rt_battery_dev = {
	.name = RT_BATT_NAME,
	.id = -1,
};
#endif /* CONFIG_RT_BATTERY */

#ifdef CONFIG_LGE_PM_BATTERY_PRESENT
static int rt9460_batt_pres_status(void)
{
	pmic_set_register_value(PMIC_BATON_TDET_EN, 1);
	pmic_set_register_value(PMIC_RG_BATON_EN, 1);
	if(pmic_get_register_value(PMIC_RGS_BATON_UNDET) == 1)
		return 0;

	return 1;
}
#endif

static int rt9460_charger_probe(struct i2c_client *i2c,
					  const struct i2c_device_id *id)
{
#if 0
	struct rt9460_charger_info *ri;
#endif
	struct rt9460_platform_data *pdata = i2c->dev.platform_data;
#ifdef CONFIG_RT_POWER
	struct rt_power_data *rt_power_pdata;
#endif /* #ifdef CONFIG_RT_POWER */
	int ret;
	bool use_dt = i2c->dev.of_node;
#ifdef CONFIG_LGE_PM_BATTERY_PRESENT
	int batt_present = 0;
#endif

	RTINFO("rt9460 charger probing....\n");
	pr_err("rt9460 charger probing....\n");
	ri = devm_kzalloc(&i2c->dev, sizeof(*ri), GFP_KERNEL);
	if (!ri) {
		RTINFO("rt9460 memory allocate fail\n");
		return -ENOMEM;
	}

	if (use_dt)
		rt_parse_dt(ri, &i2c->dev);
	else {
		if (!pdata) {
			dev_info(&i2c->dev, "rt9453 platform data is NULL\n");
			return -EINVAL;
		}

		rt_parse_pdata(ri, &i2c->dev);
	}

	ri->i2c = i2c;
//	ri->irq = gpio_to_irq(8);
	ri->dev = &i2c->dev;
	i2c_set_clientdata(i2c, ri);

	mutex_init(&ri->io_lock);
	/*register power_supply */
	ri->psy.name = rtdef_chg_name;
	ri->psy.type = POWER_SUPPLY_TYPE_UNKNOWN;
	ri->psy.supplied_to = rt_charger_supply_list;
	ri->psy.properties = rt_charger_props;
	ri->psy.num_properties = ARRAY_SIZE(rt_charger_props);
	ri->psy.get_property = rt9460_charger_get_property;
	ri->psy.set_property = rt9460_charger_set_property;
	ri->psy.property_is_writeable = rt9460_charger_property_is_writeable;

	ret = power_supply_register(&i2c->dev, &ri->psy);
	if (ret < 0) {
		RTINFO("rt9460 power supply register fail\n");
		goto err_psyreg;
	}
#ifdef CONFIG_RT_BATTERY
	rt_battery_dev.dev.parent = &i2c->dev;
	ret = platform_device_register(&rt_battery_dev);
	if (ret < 0)
		goto out_dev;
#endif /*CONFIG_RT_BATTERY */

#ifdef CONFIG_RT_POWER
	rt_power_pdata =
	    devm_kzalloc(&i2c->dev, sizeof(*rt_power_pdata), GFP_KERNEL);
	if (!rt_power_pdata) {
		ret = -ENOMEM;
		goto out_pow;
	}
	rt_power_pdata->chg_volt = ri->chg_volt;
	rt_power_pdata->acchg_icc = ri->icc;
	rt_power_pdata->usbtachg_icc = ri->icc;
	rt_power_pdata->usbchg_icc = ri->icc;

	rt_power_dev.dev.platform_data = rt_power_pdata;
	rt_power_dev.dev.parent = &i2c->dev;
	ret = platform_device_register(&rt_power_dev);
	if (ret < 0)
		goto out_pow;
#endif /* #ifdef CONFIG_RT_POWER */

	ri->charge_cable = 0;
	ri->chg_stat = POWER_SUPPLY_STATUS_DISCHARGING;
	ri->is_cold_hot = 0;

	/* i2c register initailize */
	rt9460_reg_init(ri);

#ifdef CONFIG_LGE_PM_BATTERY_PRESENT
	batt_present = rt9460_batt_pres_status();

	if((batt_present == 1) && (lge_get_board_cable() != LT_CABLE_910K))
		rt9460_chip_disable(ri);
	else
		RTINFO("batt_present = %d, boot_cable = %d => skip chip disable\n", batt_present, lge_get_board_cable());
#else
	rt9460_chip_disable(ri);
#endif

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT_DUMP_REGISTER
	INIT_DELAYED_WORK(&ri->reg_dump_work, rt9460_register_dump_work);
#endif
	INIT_DELAYED_WORK(&ri->usb_detect_work, rt9460_usbdet_work);
	wake_lock_init(&ri->usb_wake_lock, WAKE_LOCK_SUSPEND,
		       "rt9460-usb-detect");

	ret = rt9460_irq_init(ri);
	if (ret < 0) {
		dev_err(&i2c->dev, "irq initialize failed\n");
		goto err_irqinit;
	}

	rt9460_chip_enable(ri);
#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
	RTINFO("[OTG] rt9460_probe otg_boost_mode init\n");
	detect_otg_mode(0);
#endif
	/*Attribute */
	rt9460_create_attr(ri);

	dev_info(&i2c->dev, "driver successfully loaded\n");

	chargin_hw_init_done = KAL_TRUE;
	return 0;

err_irqinit:
#ifdef CONFIG_RT_POWER
out_pow:
#endif /*CONFIG_RT_POWER */
#ifdef CONFIG_RT_BATTERY
	platform_device_unregister(&rt_battery_dev);
out_dev:
#endif /*CONFIG_RT_BATTERY */
	power_supply_unregister(&ri->psy);

err_psyreg:
	devm_kfree(&i2c->dev, ri);
	return -EINVAL;
}

static int rt9460_charger_remove(struct i2c_client *i2c)
{
	struct rt9460_charger_info *ri = i2c_get_clientdata(i2c);
	int i, attr_size = sizeof(rt9460_attrs) / sizeof(rt9460_attrs[0]);

	RTINFO("rt9460 charger removing\n");
	for (i = 0; i < attr_size; i++)
		device_remove_file(&i2c->dev, rt9460_attrs + i);

	if (ri) {
		mutex_destroy(&ri->io_lock);
		cancel_delayed_work(&ri->irq_delayed_work);
		cancel_delayed_work(&ri->usb_detect_work);
		devm_free_irq(&i2c->dev, ri->irq, ri);
		power_supply_unregister(&ri->psy);
		wake_lock_destroy(&ri->usb_wake_lock);
		kfree(ri);
	}
#ifdef CONFIG_RT_BATTERY
	platform_device_unregister(&rt_battery_dev);
#endif /* CONFIG_RT_BATTERY */
#ifdef CONFIG_RT_POWER
	platform_device_unregister(&rt_power_dev);
#endif /* CONFIG_RT_POWER */
	return 0;
}

#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
static int rt9460_charger_shutdown(struct i2c_client *i2c)
{
	struct rt9460_charger_info *ri = i2c_get_clientdata(i2c);

	RTINFO("rt9460 charger shutdown - poweroff\n");

	if (ri) {
		RTINFO("[OTG] rt9460_shutdown otg_boost_mode disable\n");
		detect_otg_mode(0);
	}
	return 0;
}
#endif

static struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt9460",},
/*	{.compatible = "mediatek,swithing_charger",}, */
	{},
};

static const struct i2c_device_id rt_dev_id[] = {
	{RT9460_DEV_NAME, 0},
	{}
};

struct i2c_driver rt9460_charger_driver = {
	.driver = {
		   .name = RT9460_DEV_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt9460_charger_probe,
	.remove = rt9460_charger_remove,
#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
	.shutdown = rt9460_charger_shutdown,
#endif
	.id_table = rt_dev_id,
};

static void async_rt9460_charger_init(void *data, async_cookie_t cookie)
{
	i2c_add_driver(&rt9460_charger_driver);
	return;
}

static int __init rt9460_charger_init(void)
{
	RTINFO("rt9460 init....\n");

	return async_schedule(async_rt9460_charger_init, NULL);
}

module_init(rt9460_charger_init);

static void __exit rt9460_charger_exit(void)
{
	RTINFO("rt9460 exiting....\n");
	i2c_del_driver(&rt9460_charger_driver);
}

module_exit(rt9460_charger_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jeff Chang <jeff_chang@richtek.com");
MODULE_DESCRIPTION("Charger driver for RT9460");
MODULE_VERSION(RT9460_DRV_VER);
