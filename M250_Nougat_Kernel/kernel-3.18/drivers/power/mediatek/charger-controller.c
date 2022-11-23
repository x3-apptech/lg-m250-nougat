/* Copyright (C) 2015 LG Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*
 history:
 2015.05 1st version(1.0) was used for G4, initial version
 2015.09 2nd version(1.1) was used for V10, WLC was added and QC2.0 limit was changed
 2015.12 3rd version(1.2) was used for Alice E
*/

#define CONFIG_LGE_PM_CHARGE_INFO
#define CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/of_device.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#if defined (CONFIG_MACH_MT6750_LGPS32) || defined (CONFIG_MACH_MT6750_LV5) || defined (CONFIG_MACH_MT6750_LV7) || defined (CONFIG_MACH_MT6750_SF3)
#include <mt-plat/mt_boot.h>
#include <mt-plat/mtk_rtc.h>

#include <mach/mt_charging.h>
#include <mt-plat/upmu_common.h>

#include <mt-plat/charging.h>
#include <mt-plat/battery_meter.h>
#include <mt-plat/battery_common.h>
#include <mach/mt_battery_meter.h>
#include <mach/mt_charging.h>
#include <mach/mt_pmic.h>
#include <linux/async.h>

//#include "mtk_pep_intf.h"
//#include "mtk_pep20_intf.h"
#else
#include <linux/qpnp/qpnp-adc.h>
#endif
#include <linux/power/charger-controller.h>
#include <linux/wakelock.h>
#ifdef CONFIG_LGE_PM_USB_ID
#include <soc/mediatek/lge/lge_cable_id.h>
#include <soc/mediatek/lge/board_lge.h>
#endif
#if defined(CONFIG_LGE_PM_BATTERY_ID_CHECKER)
#include <soc/mediatek/lge/lge_battery_id.h>
#endif
#if defined (CONFIG_MACH_MT6750_LGPS32) || defined (CONFIG_MACH_MT6750_LV5) || defined (CONFIG_MACH_MT6750_LV7) || defined (CONFIG_MACH_MT6750_SF3)

#else
#include <soc/qcom/smem.h>
#endif

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
#include <soc/mediatek/lge/lge_pseudo_batt.h>
#endif

#ifdef CONFIG_LGE_BOARD_REVISION
#include <soc/mediatek/lge/lge_board_revision.h>
#endif

#ifdef CONFIG_LGE_BOOT_MODE
#include <soc/mediatek/lge/lge_boot_mode.h>
#endif

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

/* cable type */
#define CHARGER_CONTR_CABLE_NORMAL 0
#define CHARGER_CONTR_CABLE_FACTORY_CABLE	1

#define WIRELESS_OTP_LIMIT			500
#define WIRELESS_IUSB_NORMAL		900
#define WIRELESS_IUSB_THERMAL_1		700
#define WIRELESS_IUSB_THERMAL_2		500
#define WIRELESS_IBAT_THERMAL_1		1000
#define WIRELESS_IBAT_THERMAL_2		700

#define USB_PSY_NAME "usb"
#define BATT_PSY_NAME "battery"
#define FUELGAUGE_PSY_NAME "fuelgauge"
#define WIRELESS_PSY_NAME "dc"
#define CC_PSY_NAME	"charger_controller"
#define AC_PSY_NAME "ac"	// MTK only used
#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
#define BATT_ID_PSY_NAME "battery_id"
#endif

#define DEFAULT_BATT_TEMP               20
#define DEFAULT_BATT_VOLT               4000

#define CURRENT_DIVIDE_UNIT				1000

#define DISABLE_FEATURES_CC				0
enum print_reason {
	PR_DEBUG        = BIT(0),
	PR_INFO         = BIT(1),
	PR_ERR          = BIT(2),
};

static int cc_debug_mask = PR_INFO|PR_ERR|PR_DEBUG;
module_param_named(
	debug_mask, cc_debug_mask, int, S_IRUSR | S_IWUSR
);

#define pr_cc(reason, fmt, ...) \
	do { \
		if (cc_debug_mask & (reason))		\
			pr_info("[CC] " fmt, ##__VA_ARGS__); \
		else \
			pr_debug("[CC] " fmt, ##__VA_ARGS__); \
	} while (0)

#define cc_psy_setprop(_psy, _psp, _val) \
	({\
		struct power_supply *__psy = chgr_contr->_psy;\
		union power_supply_propval __propval = { .intval = _val };\
		int __rc = -ENXIO;\
		if (likely(__psy && __psy->set_property)) {\
			__rc = __psy->set_property(__psy, \
				POWER_SUPPLY_PROP_##_psp, \
				&__propval);\
		} \
		__rc;\
	})

#define cc_psy_getprop(_psy, _psp, _val)	\
	({\
		struct power_supply *__psy = chgr_contr->_psy;\
		int __rc = -ENXIO;\
		if (likely(__psy && __psy->get_property)) {\
			__rc = __psy->get_property(__psy, \
				POWER_SUPPLY_PROP_##_psp, \
				_val);\
		} \
		__rc;\
	})

/* To set init charging current according to temperature and voltage of battery */
#define CONFIG_LGE_SET_INIT_CURRENT

#ifdef CONFIG_LGE_PM_VZW_REQ
typedef enum vzw_chg_state {
	VZW_NO_CHARGER,
	VZW_NORMAL_CHARGING,
	VZW_INCOMPATIBLE_CHARGING,
	VZW_UNDER_CURRENT_CHARGING,
	VZW_USB_DRIVER_UNINSTALLED,
	VZW_CHARGER_STATUS_MAX,
} chg_state;
#endif

static enum power_supply_property pm_power_props_charger_contr_pros[] = {
	POWER_SUPPLY_PROP_STATUS, /* not used */
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_PRESENT, /* ChargerController init or not */
	POWER_SUPPLY_PROP_ONLINE, /* usb current is controlled by ChargerController or not */
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_MAX, /* changed current by ChargerController */
#ifndef DISABLE_FEATURES_CC
	POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT, /* limited current by external */
	POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL, /* temperature status */
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	POWER_SUPPLY_PROP_STORE_DEMO_ENABLED,
#endif
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	POWER_SUPPLY_PROP_PSEUDO_BATT,
#endif
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	POWER_SUPPLY_PROP_USB_CURRENT_MAX,
#endif
#ifdef CONFIG_LGE_BOARD_REVISION
	POWER_SUPPLY_PROP_HW_REV,
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	POWER_SUPPLY_PROP_VZW_CHG,
#endif
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	POWER_SUPPLY_PROP_FB_LIMIT_IUSB,
	POWER_SUPPLY_PROP_FB_LIMIT_IBAT,
#endif
};

struct charger_contr {
	struct device		*dev;
	struct kobject		*kobj;
	struct power_supply charger_contr_psy;
	struct power_supply *usb_psy;
	struct power_supply *batt_psy;
	struct power_supply *fg_psy;
	struct power_supply *wireless_psy;
#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	struct power_supply *batt_id_psy;
#endif
	struct power_supply *cc_psy;

	const char			*fg_psy_name;

	struct work_struct		cc_notify_work;
	struct mutex			notify_work_lock;
	int requester;

	struct delayed_work		battery_health_work;
#ifdef CONFIG_LGE_PM_CHARGE_INFO
	struct delayed_work		charging_inform_work;
#ifndef DISABLE_FEATURES_CC
	struct qpnp_vadc_chip		*vadc_usbin_dev;
#endif
	struct wake_lock		information_lock;
#endif

#ifdef CONFIG_LGE_PM_USB_ID
	struct cc_chg_cable_info *cable_info;
	unsigned int usb_id;
	struct delayed_work vadc_work;
#endif
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	struct delayed_work		otp_charging_work;
	int				otp_state_changed;
#endif
	int				ready;

	/* pre-defined value by DT*/
	int current_ibat;
	int current_ibat_lcd_off;
	int current_limit;
	int current_wlc_limit;
	int current_iusb_factory;
	int current_ibat_factory;

#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
	int current_ibat_pep;
	int current_iusb_pep;
#endif

#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
	int ibat_no_pep;
#endif

	int thermal_status; /* status of lg charging scenario. but not used */
	int status;
	int current_status;
	int batt_temp;
	int origin_batt_temp;
	int batt_volt;

	int current_iusb_max;
	int current_iusb_limit[MAX_IUSB_NODE];
	int currnet_iusb_wlc[MAX_IUSB_NODE_WIRELESS];
	int current_ibat_max;
	int current_ibat_limit[MAX_IBAT_NODE];
	int current_ibat_max_wireless;
	int current_wireless_limit;
	int ibat_limit_lcs;
	int iusb_limit_lcs;
	int wireless_lcs;

	int current_ibat_changed_by_cc;
	int changed_ibat_by_cc;
	int current_iusb_changed_by_cc;
	int changed_iusb_by_cc;

	int current_real_usb_psy;
	int usb_cable_info; /* normal cable (0) factory cable(1) */
	int battery_status_cc;
	int battery_status_charger;

#if defined(CONFIG_LGE_PM_BATTERY_ID_CHECKER)
	int batt_id_smem;
	int batt_smem_present;
#endif
	int usb_online;
	int wlc_online;
	int batt_eoc;

	int current_usb_psy;
	int current_wireless_psy;
	int wireless_init_current;

	int batt_temp_state;
	int batt_volt_state;
	int charge_type;

	struct wake_lock chg_wake_lock;
#ifdef CONFIG_LGE_PM_LLK_MODE
	int store_demo_enabled;
#endif
	int aicl_done_current;
#ifdef CONFIG_LGE_PM_VZW_REQ
	int vzw_chg_mode;
#endif

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	struct notifier_block fb_notify;
	int fb_status;
	int fb_limit_iusb;
	int fb_limit_ibat;
#endif
};

static char *charger_contr_supplied_to[] = {
	"battery",
};

/* charger controller node */
static int cc_ibat_limit = -1;
static int cc_iusb_limit = -1;
static int cc_wireless_limit = -1;

static int cc_init_ok;
struct charger_contr *chgr_contr;

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
unsigned int usb_current_max_enabled = 0;
#endif

static int get_prop_fuelgauge_capacity(void);
#ifdef CONFIG_LGE_PM_CHARGE_INFO
static bool is_usb_present(void);
static bool is_wireless_present(void);
static bool is_pep_present(void);
static void change_iusb(int value);

/* QCT
static char *cable_type_str[] = {
	"NOT INIT", "MHL 1K", "U_28P7K", "28P7K", "56K",
	"100K", "130K", "180K", "200K", "220K",
	"270K", "330K", "620K", "910K", "OPEN"
};
*/
static char *cable_type_str[] = {
	"NOT INIT", "0", "0", "0", "0", "0",
	"56K", "130K", "OPEN", "OPEN2", "NOT INIT",
	"910K", "NOT INIT"
};

static char *power_supply_type_str[] = {
	"Unknown", "Battery", "UPS", "Mains", "USB",
	"USB_DCP", "USB_CDP", "USB_ACA", "Wireless"
};

static char *usb_charger_source[] = {
	"ac",
	"usb",
};

static char *get_usb_type(void)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int i;

	for (i = 0; i < ARRAY_SIZE(usb_charger_source); i++) {
		psy = power_supply_get_by_name(usb_charger_source[i]);
		if (!psy || !psy->get_property)
			continue;

		psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (val.intval)
			break;

		psy = NULL;
	}

	if (!psy)
		return "None";

#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT
	psy->get_property(psy, POWER_SUPPLY_PROP_FASTCHG, &val);
	if (val.intval)
		return "PEP";
#endif

	return power_supply_type_str[psy->type];
}

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
static int chgr_contr_fb_notifer_callback(struct notifier_block *self,
						unsigned long event, void *data)
{
	struct fb_event *evdata = (struct fb_event *)data;
	int *blank = NULL;

	if (chgr_contr == NULL)
		return 0;

	if (evdata && evdata->data)
		blank = (int *)evdata->data;
	else
		return 0;

	if (chgr_contr->fb_status == *blank)
		return 0;

	chgr_contr->fb_status = *blank;

	switch (chgr_contr->fb_status) {
	case FB_BLANK_UNBLANK:
		pr_cc(PR_DEBUG, "UNBLANK\n");
		break;
	case FB_BLANK_POWERDOWN:
		pr_cc(PR_DEBUG, "BLANK\n");
		break;
	default:
		break;
	}

	if (chgr_contr->usb_online || chgr_contr->wlc_online) {
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
		notify_charger_controller(&chgr_contr->charger_contr_psy,
				REQUEST_BY_IUSB_LIMIT);
#else
		notify_charger_controller(&chgr_contr->charger_contr_psy,
				REQUEST_BY_IBAT_LIMIT);
#endif
	}

	return 0;
}
#endif

static int cc_get_usb_adc(void)
{
	union power_supply_propval val = {0, };

	cc_psy_getprop(batt_psy, ChargerVoltage, &val);

	return val.intval;
}

#ifdef CONFIG_LGE_PM_USB_ID
#define VADC_SET_DELAY_WS		500
static void vadc_set_work(struct work_struct *work)
{
	struct charger_contr *chip = container_of(work,
			struct charger_contr, vadc_work.work);
	int rc = 0;
	bool usb_present = is_usb_present();

	lge_read_cable_voltage(&rc);

	chip->usb_id = rc;

	if (usb_present) {
		chip->cable_info->cable_type = lge_read_cable_type();
		rc = chip->cable_info->cable_type;
	}
}

#endif

static int cc_info_time = 60000;
module_param_named(
	cc_info_time, cc_info_time, int, S_IRUSR | S_IWUSR
);

static void charging_information(struct work_struct *work)
{
	struct charger_contr *cc = container_of(work,
			struct charger_contr, charging_inform_work.work);
	union power_supply_propval val = {0, };
	int rc = 0;
	bool usb_present = is_usb_present();
	bool wireless_present = is_wireless_present();
	bool pep_present = is_pep_present();
	char *usb_type_name = get_usb_type();
	char *cable_type_name = "N/A";
	char *batt_mode = "Normal";
	int usbin_vol = cc_get_usb_adc();
	int batt_soc = 0;
	int batt_vol = 0;
	int main_iusb_set = chgr_contr->current_iusb_changed_by_cc;
	int main_ibat_set = chgr_contr->current_ibat_changed_by_cc;
	int total_ibat_now = 0;
	int batt_temp = 0;

	/* do not print any logs when cc is not ready */
	if (!cc->ready)
		goto reschedule;

	wake_lock(&chgr_contr->information_lock);

	rc = cc_psy_getprop(fg_psy, CAPACITY, &val);
	if (!rc)
		batt_soc = val.intval;

	rc = cc_psy_getprop(fg_psy, VOLTAGE_NOW, &val);
	if (!rc)
		batt_vol = div_s64(val.intval, 1000);

	cc_psy_getprop(batt_psy, TEMP, &val);
	chgr_contr->origin_batt_temp = val.intval;
	batt_temp = div_s64(chgr_contr->origin_batt_temp, 10);

	cc_psy_getprop(batt_psy, CURRENT_NOW, &val);
	total_ibat_now = div_s64(val.intval, CURRENT_DIVIDE_UNIT);

#ifdef CONFIG_LGE_PM_USB_ID
	if (usb_present)
		cable_type_name = cable_type_str[lge_read_cable_type()];
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	if (chgr_contr->store_demo_enabled)
		batt_mode = "Demo";
#endif
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	if (get_pseudo_batt_info(PSEUDO_BATT_MODE))
		batt_mode = "Fake";
#endif

	pr_cc(PR_INFO, "[CC-INFO] USB(%d, %s, %s, %dmV) PEP(%d) WIRELESS(%d) BATT(%d%%, %dmV, %ddeg, %s) CHG(%dmA, %dmA, %dmA)\n",
			usb_present, usb_type_name, cable_type_name, usbin_vol,
			pep_present,
			wireless_present,
			batt_soc, batt_vol, batt_temp, batt_mode,
			main_iusb_set, main_ibat_set, total_ibat_now);

	wake_unlock(&chgr_contr->information_lock);

reschedule:
	schedule_delayed_work(&chgr_contr->charging_inform_work,
		round_jiffies_relative(msecs_to_jiffies(cc_info_time)));
}
#endif

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
static void do_otp_charging(int state_changed)
{
	int ibat_limit_lcs = 0;
	int iusb_limit_lcs = 0;
	int wireless_lcs;

	pr_cc(PR_DEBUG, "%s: batt_temp = %ddeg (%d), batt_volt = %dmV (%d)\n", __func__,
			chgr_contr->batt_temp, chgr_contr->batt_temp_state,
			chgr_contr->batt_volt, chgr_contr->batt_volt_state);

	/* When temp is changed, limit value should be updated */
	if (state_changed & CC_BATT_TEMP_CHANGED) {
		if (chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_HIGH) {
			if (chgr_contr->batt_volt_state == CC_BATT_VOLT_UNDER_4_0) {
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
				ibat_limit_lcs = -1;
				iusb_limit_lcs = 500;
#else
				ibat_limit_lcs = 500;
				iusb_limit_lcs = -1;
#endif
			} else {
				ibat_limit_lcs = 0;
				iusb_limit_lcs = -1;
			}
		} else if ((chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_OVERHEAT) ||
				(chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_COLD)) {
			ibat_limit_lcs = 0;
			iusb_limit_lcs = -1;
		} else /* normal temperature(-10C ~ 45C) */ {
			ibat_limit_lcs = -1;
			iusb_limit_lcs = -1;
		}
	}

	/* Whend voltage is changed , limit value should be updated */
	if (state_changed & CC_BATT_VOLT_CHANGED) {
		if (chgr_contr->batt_volt_state == CC_BATT_VOLT_OVER_4_0) {
			if (chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_NORMAL) {
				ibat_limit_lcs = -1;
				iusb_limit_lcs = -1;
			}
			else /* over 45C or under -10C*/ {
				ibat_limit_lcs = 0;
				iusb_limit_lcs = -1;
			}
		} else { /* under 4.0 V */
			if (chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_NORMAL) {
				ibat_limit_lcs = -1;
				iusb_limit_lcs = -1;
			}
			else if (chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_HIGH) {
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
				ibat_limit_lcs = -1;
				iusb_limit_lcs = 500;
#else
				ibat_limit_lcs = 500;
				iusb_limit_lcs = -1;
#endif
			} else if ((chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_OVERHEAT) ||
				(chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_COLD)) {
					ibat_limit_lcs = 0;
					iusb_limit_lcs = -1;
			}
		}
	}
	pr_cc(PR_INFO, "%s: Iusb = %dmA, Ibat = %dmA\n", __func__, iusb_limit_lcs, ibat_limit_lcs);

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
	if (ibat_limit_lcs == 500 && chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_HIGH)
#else
	if (ibat_limit_lcs == 450)
#endif
		wireless_lcs = chgr_contr->current_wlc_limit;
	else
		wireless_lcs = ibat_limit_lcs;

	chgr_contr->ibat_limit_lcs = ibat_limit_lcs;
	chgr_contr->iusb_limit_lcs = iusb_limit_lcs;
	chgr_contr->wireless_lcs = wireless_lcs;

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
	notify_charger_controller(&chgr_contr->charger_contr_psy,
			REQUEST_BY_IBAT_LIMIT | REQUEST_BY_IUSB_LIMIT);
#else
	notify_charger_controller(&chgr_contr->charger_contr_psy,
			REQUEST_BY_IBAT_LIMIT);
#endif
}

static void otp_charging_work(struct work_struct *work)
{
	struct charger_contr *cc = container_of(work,
			struct charger_contr, otp_charging_work.work);

	if (!cc->otp_state_changed)
		return;

	do_otp_charging(cc->otp_state_changed);

	cc->otp_state_changed = 0;

	return;
}
#endif

static int battery_health_polling = 0;
module_param_named(
	battery_health_polling, battery_health_polling, int, S_IRUSR | S_IWUSR
);

static void battery_health_work(struct work_struct *work)
{
	int batt_temp_state;
	int batt_volt_state;

	batt_temp_state = get_temp_state();
	if (batt_temp_state != chgr_contr->batt_temp_state) {
		pr_cc(PR_INFO, "%s: batt_temp_state change %d -> %d\n", __func__,
				chgr_contr->batt_temp_state, batt_temp_state);
		chgr_contr->batt_temp_state = batt_temp_state;
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
		chgr_contr->otp_state_changed |= CC_BATT_TEMP_CHANGED;
#endif
	}

	batt_volt_state = get_volt_state();
	if (batt_volt_state != chgr_contr->batt_volt_state) {
		pr_cc(PR_INFO, "%s: batt_volt_state change %d -> %d\n", __func__,
				chgr_contr->batt_volt_state, batt_volt_state);
		chgr_contr->batt_volt_state = batt_volt_state;
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
		chgr_contr->otp_state_changed |= CC_BATT_VOLT_CHANGED;
#endif
	}

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	/* trigger otp work */
	if (chgr_contr->usb_online || chgr_contr->wlc_online) {
		if (chgr_contr->otp_state_changed)
			schedule_delayed_work(&chgr_contr->otp_charging_work, 0);
	}
#endif

	if (battery_health_polling)
		schedule_delayed_work(&chgr_contr->battery_health_work,
				msecs_to_jiffies(battery_health_polling));
}

#ifdef CONFIG_LGE_PM_LLK_MODE
#define LLK_MAX_THR_SOC 50
#define LLK_MIN_THR_SOC 45

static void update_llk_status(struct charger_contr *cc)
{
	static int store_demo_enabled = 0;
	static int last_capacity = 0;
	int capacity;
	union power_supply_propval val = {0,};

	if (!cc->usb_online && !cc->wlc_online) {
		last_capacity = -EINVAL;
		return;
	}

	if (store_demo_enabled != cc->store_demo_enabled) {
		store_demo_enabled = cc->store_demo_enabled;
		if (!store_demo_enabled) {
			cc->current_ibat_limit[IBAT_NODE_EXT_CTRL] = -1;
			notify_charger_controller(&cc->charger_contr_psy,
					REQUEST_BY_IBAT_LIMIT);
		}
	}

	if (!store_demo_enabled) {
		last_capacity = -EINVAL;
		return;
	}

	cc_psy_getprop(batt_psy, CAPACITY, &val);

	capacity = get_prop_fuelgauge_capacity();
	if (last_capacity == capacity)
		return;

	last_capacity = capacity;
	if (capacity >= LLK_MAX_THR_SOC) {
		cc->current_ibat_limit[IBAT_NODE_EXT_CTRL] = 0;
		pr_cc(PR_INFO, "%s: stop charging. capacity = %d%%\n", __func__,
				capacity);
	}
	if (capacity <= LLK_MIN_THR_SOC) {
		cc->current_ibat_limit[IBAT_NODE_EXT_CTRL] = -1;
		pr_cc(PR_INFO, "%s: start charging. capacity = %d%%\n", __func__,
				capacity);
	}
	notify_charger_controller(&cc->charger_contr_psy, REQUEST_BY_IBAT_LIMIT);
}
#endif

static int is_usb_online(void)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int i;

	for (i = 0; i < ARRAY_SIZE(usb_charger_source); i++) {
		psy = power_supply_get_by_name(usb_charger_source[i]);
		if (!psy || !psy->get_property)
			continue;

		psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (val.intval)
			break;

		psy = NULL;
	}

	if (!psy)
		return 0;

	return 1;
}

static bool is_usb_present(void)
{
	struct power_supply *psy = NULL;
	union power_supply_propval val = {0, };
	int i;

	for (i = 0; i < ARRAY_SIZE(usb_charger_source); i++) {
		psy = power_supply_get_by_name(usb_charger_source[i]);
		if (!psy || !psy->get_property)
			continue;

		psy->get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
		if (val.intval)
			break;

		psy = NULL;
	}

	if (!psy)
		return 0;

	return 1;
}

static int is_wireless_online(void)
{
	union power_supply_propval val = {0, };

	cc_psy_getprop(wireless_psy, ONLINE, &val);

	return val.intval;
}

static bool is_wireless_present(void)
{
	union power_supply_propval val = {0, };

	cc_psy_getprop(wireless_psy, PRESENT, &val);

	if (val.intval == 1)
		return true;
	else
		return false;
}

static bool is_pep_present(void)
{
	union power_supply_propval val = {0, };
#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT
	cc_psy_getprop(usb_psy, FASTCHG, &val);
#endif
	if (val.intval == 1)
		return true;
	else
		return false;
}

static int pm_set_property_charger_contr(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	struct charger_contr *cc = container_of(psy, struct charger_contr, charger_contr_psy);

	if (cc == ERR_PTR(-EPROBE_DEFER))
		return -EPROBE_DEFER;

	switch (psp) {
#ifdef CONFIG_LGE_PM_LLK_MODE
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
		pr_cc(PR_INFO, "Set property store_demo_enabled : %d\n", val->intval);
		if (cc->store_demo_enabled != val->intval) {
			cc->store_demo_enabled = val->intval;
			update_llk_status(cc);
		}
		break;
#endif
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		if (val->intval == 0)
			chgr_contr->current_ibat_limit[IBAT_NODE_EXT_CTRL] = 0;
		else
			chgr_contr->current_ibat_limit[IBAT_NODE_EXT_CTRL] = -1;

		notify_charger_controller(psy, REQUEST_BY_IBAT_LIMIT);
		break;
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
		if (val->intval)
			usb_current_max_enabled = 1;
		else
			usb_current_max_enabled = 0;
		pr_cc(PR_DEBUG, "Set USB current max enabled : %d\n",
			usb_current_max_enabled);
		cc_psy_setprop(batt_psy, USB_CURRENT_MAX, val->intval);
		break;
#endif
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	case POWER_SUPPLY_PROP_FB_LIMIT_IUSB:
		cc->fb_limit_iusb = val->intval;
		pr_cc(PR_DEBUG, "Set fb_limit_iusb : %d\n", cc->fb_limit_iusb);
		notify_charger_controller(psy, REQUEST_BY_IUSB_LIMIT);
		break;
	case POWER_SUPPLY_PROP_FB_LIMIT_IBAT:
		cc->fb_limit_ibat = val->intval;
		pr_cc(PR_DEBUG, "Set fb_limit_ibat : %d\n", cc->fb_limit_ibat);
		notify_charger_controller(psy, REQUEST_BY_IBAT_LIMIT);
		break;
#endif
	default:
		pr_cc(PR_DEBUG, "Invalid property(%d)\n", psp);
		return -EINVAL;
	}

	return 0;
}

static int charger_contr_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
#endif
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	case POWER_SUPPLY_PROP_FB_LIMIT_IUSB:
	case POWER_SUPPLY_PROP_FB_LIMIT_IBAT:
#endif
#ifdef CONFIG_LGE_PM_LLK_MODE
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
#endif
		ret = 1;
		break;
	default:
		ret = 0;
		break;
	}

	return ret;
}

static int pm_get_property_charger_contr(struct power_supply *psy,
	enum power_supply_property cc_property, union power_supply_propval *val)
{
	struct charger_contr *cc = container_of(psy, struct charger_contr, charger_contr_psy);

	if (cc == ERR_PTR(-EPROBE_DEFER))
		return -EPROBE_DEFER;

	switch (cc_property) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = cc->charge_type;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = cc->usb_online | cc->wlc_online;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = cc->current_iusb_max;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_MAX:
		val->intval = cc->current_real_usb_psy;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
/* todo */
		val->intval = cc->changed_ibat_by_cc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = cc->current_ibat_limit[IBAT_NODE_LGE_CHG];
		break;
#ifndef DISABLE_FEATURES_CC
	case POWER_SUPPLY_PROP_INPUT_CURRENT_TRIM:
		val->intval = cc->current_ibat_max;
		break;
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
		val->intval = cc->thermal_status;
		break;
#endif
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		cc_psy_getprop(batt_psy, BATTERY_CHARGING_ENABLED_CC, val);
		break;
#ifdef CONFIG_LGE_PM_LLK_MODE
	case POWER_SUPPLY_PROP_STORE_DEMO_ENABLED:
		val->intval = cc->store_demo_enabled;
		break;
#endif
#ifdef CONFIG_LGE_PM_VZW_REQ
	case POWER_SUPPLY_PROP_VZW_CHG:
		pr_cc(PR_DEBUG, "POWER_SUPPLY_PROP_VZW_CHG(%d)\n", cc_property);
		val->intval = cc->vzw_chg_mode;
		break;
#endif
#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	case POWER_SUPPLY_PROP_PSEUDO_BATT:
		val->intval = get_pseudo_batt_info(PSEUDO_BATT_MODE);
		break;
#endif
#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	case POWER_SUPPLY_PROP_USB_CURRENT_MAX:
		cc_psy_getprop(batt_psy, USB_CURRENT_MAX, val); /* MTK added */
		if (val->intval != usb_current_max_enabled)
			usb_current_max_enabled = val->intval;
		val->intval = usb_current_max_enabled;
		break;
#endif
#ifdef CONFIG_LGE_BOARD_REVISION
	case POWER_SUPPLY_PROP_HW_REV:
		val->intval = lge_get_board_revno();
		break;
#endif
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	case POWER_SUPPLY_PROP_FB_LIMIT_IUSB:
		val->intval = cc->fb_limit_iusb;
		break;
	case POWER_SUPPLY_PROP_FB_LIMIT_IBAT:
		val->intval = cc->fb_limit_ibat;
		break;
#endif
	default:
		pr_cc(PR_DEBUG, "Invalid property(%d)\n", cc_property);
		return -EINVAL;
	}
	return 0;
}

/* todo need to be changed depend on adc of battery temperature */
#define CHARGER_CONTROLLER_BATTERY_DEFAULT_TEMP		250
static int charger_contr_get_battery_temperature(void)
{
	union power_supply_propval val = {0,};
	int rc = 0;

	rc = cc_psy_getprop(batt_psy, TEMP, &val);
	if (!rc)
		return val.intval;

	pr_cc(PR_ERR, "Battery temperature get failed\n");
	return CHARGER_CONTROLLER_BATTERY_DEFAULT_TEMP;
}

/* return is max current cound provide to battery psy */
/* Update usb_psy, qc20, cable type, wireless */
static int update_pm_psy_status(int requester)
{
	union power_supply_propval val = {0,};
	int usb_online;
	int wlc_online;

	usb_online = is_usb_online();
	if (chgr_contr->usb_online != usb_online) {
		pr_cc(PR_DEBUG, "%s: update usb_online %d -> %d\n", __func__,
				chgr_contr->usb_online, usb_online);
		chgr_contr->usb_online = usb_online;
	}

	wlc_online = is_wireless_online();
	if (chgr_contr->wlc_online != wlc_online) {
		pr_cc(PR_DEBUG, "%s: update wlc_online %d -> %d\n", __func__,
				chgr_contr->wlc_online, wlc_online);
		chgr_contr->wlc_online = wlc_online;
	}

	if (chgr_contr->usb_online) {
		cc_psy_getprop(usb_psy, CURRENT_MAX, &val);
		if (val.intval &&
			chgr_contr->current_usb_psy != div_s64(val.intval, CURRENT_DIVIDE_UNIT)) {
			pr_cc(PR_DEBUG, "%s: present iusb current = %d\n", __func__,
				(int)div_s64(val.intval, CURRENT_DIVIDE_UNIT));
			chgr_contr->current_usb_psy = div_s64(val.intval, CURRENT_DIVIDE_UNIT);
		}

		cc_psy_getprop(batt_psy, CONSTANT_CHARGE_CURRENT_MAX, &val);
		if (val.intval &&
			chgr_contr->current_ibat != div_s64(val.intval, CURRENT_DIVIDE_UNIT)) {
			pr_cc(PR_DEBUG, "%s: present ibat current = %d\n", __func__,
				(int)div_s64(val.intval, CURRENT_DIVIDE_UNIT));
			chgr_contr->current_ibat = div_s64(val.intval, CURRENT_DIVIDE_UNIT);
		}

#ifdef CONFIG_LGE_PM_VZW_REQ
#ifdef CONFIG_LGE_PM_FLOATED_CHARGER_DETECT
		/* update incompatible charger for vzw */
		cc_psy_getprop(usb_psy, INCOMPATIBLE_CHG, &val);
		if (val.intval) {
			chgr_contr->vzw_chg_mode = VZW_INCOMPATIBLE_CHARGING;
		} else {
			chgr_contr->vzw_chg_mode = VZW_NORMAL_CHARGING;
		}
#endif
#endif
	} else if (chgr_contr->wlc_online) {
		cc_psy_getprop(wireless_psy, CURRENT_MAX, &val);
		if (val.intval &&
			chgr_contr->current_usb_psy != div_s64(val.intval, CURRENT_DIVIDE_UNIT)) {
			pr_cc(PR_DEBUG, "%s: present iwlc current = %d\n", __func__,
				(int)div_s64(val.intval, CURRENT_DIVIDE_UNIT));
			chgr_contr->current_usb_psy = div_s64(val.intval, CURRENT_DIVIDE_UNIT);
		}

		cc_psy_getprop(batt_psy, CONSTANT_CHARGE_CURRENT_MAX, &val);
		if (val.intval &&
			chgr_contr->current_ibat != div_s64(val.intval, CURRENT_DIVIDE_UNIT)) {
			pr_cc(PR_DEBUG, "%s: present ibat current = %d\n", __func__,
				(int)div_s64(val.intval, CURRENT_DIVIDE_UNIT));
			chgr_contr->current_ibat = div_s64(val.intval, CURRENT_DIVIDE_UNIT);
		}
	} else {
		chgr_contr->current_usb_psy = 0;
#ifdef CONFIG_LGE_PM_VZW_REQ
		chgr_contr->vzw_chg_mode = VZW_NO_CHARGER;
#endif
	}

	pr_cc(PR_INFO, "%s: usb_online = %d, wireless_online = %d\n", __func__,
			chgr_contr->usb_online, chgr_contr->wlc_online);

	if (chgr_contr->current_usb_psy != chgr_contr->current_iusb_changed_by_cc) {
		chgr_contr->changed_iusb_by_cc = 0;
		chgr_contr->current_real_usb_psy =
			(chgr_contr->current_usb_psy < 0) ? 0 : chgr_contr->current_usb_psy;
		pr_cc(PR_INFO, "%s: usb current from usb driver (%dmA)\n", __func__,
				chgr_contr->current_real_usb_psy);
	}

#ifdef CONFIG_LGE_PM_VZW_REQ
	pr_cc(PR_INFO, "%s: vzw_chg_mode = %d\n", __func__, chgr_contr->vzw_chg_mode);
#endif

#ifdef CONFIG_LGE_PM_USB_ID
	{
		/* update cable information */
		unsigned int cable_info;

		cable_info = (unsigned int) lge_read_cable_type();
		if ((cable_info == LT_CABLE_56K) || (cable_info == LT_CABLE_130K)
				|| (cable_info == LT_CABLE_910K))
			chgr_contr->usb_cable_info = CHARGER_CONTR_CABLE_FACTORY_CABLE;
		else
			chgr_contr->usb_cable_info = CHARGER_CONTR_CABLE_NORMAL;
	}
#endif

#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	/* update charger behavior */
	if (chgr_contr->usb_online || chgr_contr->wlc_online)
		schedule_delayed_work(&chgr_contr->otp_charging_work,
				msecs_to_jiffies(100));
#endif

	/* base information has been updated. cc is ready to do something */
	if (!chgr_contr->ready) {
		pr_cc(PR_DEBUG, "Charger Controller is ready to work\n");
		chgr_contr->ready = 1;
	}

	return 0;
}

#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
#define PSEUDO_BATT_USB_ICL	900
#endif

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
#define USB_CURRENT_MAX 900
#endif

static void change_iusb(int value)
{
	pr_cc(PR_DEBUG, "%s: value = %d -> %d\n", __func__,
			chgr_contr->current_iusb_changed_by_cc, value);

	if (chgr_contr->usb_cable_info == CHARGER_CONTR_CABLE_FACTORY_CABLE) {
		mutex_lock(&chgr_contr->notify_work_lock);

		power_supply_set_current_limit(chgr_contr->usb_psy,
				chgr_contr->current_iusb_factory * CURRENT_DIVIDE_UNIT);
		chgr_contr->current_iusb_changed_by_cc = value;
		chgr_contr->changed_iusb_by_cc = 1;

		mutex_unlock(&chgr_contr->notify_work_lock);
		pr_cc(PR_INFO, "[change_iusb] factory cable current value (%d mA)\n", chgr_contr->current_iusb_factory);
		return;
	}

#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
	if ((pseudo_batt_info.mode) && (value < PSEUDO_BATT_USB_ICL)
			&& chgr_contr->usb_online) {
		mutex_lock(&chgr_contr->notify_work_lock);

		power_supply_set_current_limit(chgr_contr->usb_psy,
				PSEUDO_BATT_USB_ICL * CURRENT_DIVIDE_UNIT);
		chgr_contr->current_iusb_changed_by_cc = value;
		chgr_contr->changed_iusb_by_cc = 1;

		mutex_unlock(&chgr_contr->notify_work_lock);
		pr_cc(PR_INFO,
			"[change_iusb] Pseudo battery current  value (%d mA)\n", PSEUDO_BATT_USB_ICL);
		return;
	}
#endif

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	if ((usb_current_max_enabled)
		&& (value < USB_CURRENT_MAX) && (chgr_contr->usb_online)) {
		mutex_lock(&chgr_contr->notify_work_lock);

		power_supply_set_current_limit(chgr_contr->usb_psy,
				USB_CURRENT_MAX * CURRENT_DIVIDE_UNIT);
		chgr_contr->current_iusb_changed_by_cc = value;
		chgr_contr->changed_iusb_by_cc = 1;

		mutex_unlock(&chgr_contr->notify_work_lock);
		pr_cc(PR_INFO,
			"[change_iusb] USB max current value (%d mA)\n", USB_CURRENT_MAX);
		return;
	}
#endif

	if (chgr_contr->current_iusb_changed_by_cc == value)
		return;

	mutex_lock(&chgr_contr->notify_work_lock);

	if (chgr_contr->usb_online) {
		pr_cc(PR_INFO, "%s: usb current value (%d mA)\n", __func__, value);
		power_supply_set_current_limit(chgr_contr->usb_psy,
				value * CURRENT_DIVIDE_UNIT);
	} else if (chgr_contr->wlc_online) {
		pr_cc(PR_INFO, "%s: wireless current value (%d mA)\n", __func__, value);
		power_supply_set_current_limit(chgr_contr->wireless_psy,
				value * CURRENT_DIVIDE_UNIT);
	}
	chgr_contr->current_iusb_changed_by_cc = value;
	chgr_contr->changed_iusb_by_cc = 1;

	mutex_unlock(&chgr_contr->notify_work_lock);
}

static void change_ibat(int value)
{
	pr_cc(PR_DEBUG, "%s: value = %d -> %d\n", __func__,
			chgr_contr->current_ibat_changed_by_cc, value);

	if (chgr_contr->current_ibat_changed_by_cc == value)
		return;

	if (chgr_contr->usb_cable_info == CHARGER_CONTR_CABLE_FACTORY_CABLE) {
		if (bc11_get_register_value(PMIC_RGS_BATON_UNDET)) {
			pr_cc(PR_INFO, "[change_ibat] No Battery Boot! change_ibat = 0\n");
			cc_psy_setprop(batt_psy, CONSTANT_CHARGE_CURRENT_MAX, 0);
		} else {
			pr_cc(PR_INFO, "[change_ibat] Battery Boot! change_ibat = current_ibat_factory %d\n",
			chgr_contr->current_ibat_factory );
			cc_psy_setprop(batt_psy, CONSTANT_CHARGE_CURRENT_MAX,
					chgr_contr->current_ibat_factory * CURRENT_DIVIDE_UNIT);
		}
	} else if (value != 0) {
		mutex_lock(&chgr_contr->notify_work_lock);

		pr_cc(PR_INFO, "%s: battery charging enable\n", __func__);
		cc_psy_setprop(batt_psy, CONSTANT_CHARGE_CURRENT_MAX,
				value * CURRENT_DIVIDE_UNIT);
		cc_psy_setprop(batt_psy, BATTERY_CHARGING_ENABLED_CC, 1);

		mutex_unlock(&chgr_contr->notify_work_lock);
	} else { /* value == 0 */
		mutex_lock(&chgr_contr->notify_work_lock);

		pr_cc(PR_INFO, "%s: battery charging disable\n", __func__);
		cc_psy_setprop(batt_psy, CONSTANT_CHARGE_CURRENT_MAX, 0);
		cc_psy_setprop(batt_psy, BATTERY_CHARGING_ENABLED_CC, 0);

		mutex_unlock(&chgr_contr->notify_work_lock);
	}
	chgr_contr->current_ibat_changed_by_cc = value;
	chgr_contr->changed_ibat_by_cc = 1;
}

static int set_charger_control_current(int requester)
{
	int ibat_limit, ibat_limit_idx;
	int iusb_limit, iusb_limit_idx;
	int i;

	/* cc is not ready. do not try to set current */
	if (!chgr_contr->ready)
		return -EAGAIN;

#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
	if (is_ta_connect) {
		pr_cc(PR_INFO, "[PE+] Current Set!\n");
		chgr_contr->current_iusb_max =
			chgr_contr->current_iusb_pep;
		chgr_contr->current_ibat_max =
			chgr_contr->current_ibat_pep;

		chgr_contr->current_ibat_limit[IBAT_NODE_THERMAL] = cc_ibat_limit;
		chgr_contr->current_iusb_limit[IUSB_NODE_THERMAL] = cc_iusb_limit;
		chgr_contr->current_ibat_limit[IBAT_NODE_LGE_CHG] = chgr_contr->ibat_limit_lcs;
		chgr_contr->current_iusb_limit[IUSB_NODE_LGE_CHG] = chgr_contr->iusb_limit_lcs;
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
		if (lge_get_boot_mode() == LGE_BOOT_MODE_CHARGERLOGO) {
			pr_cc(PR_INFO, "[PE+]  Skip LCD On limit on Chargerlogo!\n");
		} else if (BMT_status.bat_charging_state == CHR_CC) {
			if (chgr_contr->fb_status == FB_BLANK_UNBLANK) {
				pr_cc(PR_INFO, "[PE+]  LCD On limit!\n");
				chgr_contr->current_iusb_limit[IUSB_NODE_FB] = chgr_contr->fb_limit_iusb;
				chgr_contr->current_ibat_limit[IBAT_NODE_FB] = chgr_contr->fb_limit_ibat;
			} else {
				pr_cc(PR_INFO, "[PE+]  LCD Off No limit!\n");
				chgr_contr->current_iusb_limit[IUSB_NODE_FB] = chgr_contr->current_iusb_pep;
				chgr_contr->current_ibat_limit[IBAT_NODE_FB] = chgr_contr->current_ibat_pep;
			}
		}
#endif
	} else
#endif
	if (chgr_contr->usb_online) {
		pr_cc(PR_DEBUG, "USB Current Set!\n");
		chgr_contr->current_iusb_max = chgr_contr->current_real_usb_psy;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
		if (BMT_status.charger_type == STANDARD_CHARGER)
			chgr_contr->current_ibat_max = chgr_contr->ibat_no_pep;
		else if (BMT_status.charger_type == NONSTANDARD_CHARGER)
			chgr_contr->current_ibat_max = chgr_contr->current_limit;
		else if (BMT_status.charger_type == STANDARD_HOST)
			chgr_contr->current_ibat_max = chgr_contr->current_limit;
		else
			chgr_contr->current_ibat_max = chgr_contr->current_ibat;
#else
		chgr_contr->current_ibat_max = chgr_contr->current_ibat;
#endif
		chgr_contr->current_iusb_limit[IUSB_NODE_THERMAL] = cc_iusb_limit;
		chgr_contr->current_ibat_limit[IBAT_NODE_THERMAL] = min(cc_ibat_limit,
				chgr_contr->current_ibat_lcd_off);
		chgr_contr->current_ibat_limit[IBAT_NODE_LGE_CHG] = chgr_contr->ibat_limit_lcs;
		chgr_contr->current_iusb_limit[IUSB_NODE_LGE_CHG] = chgr_contr->iusb_limit_lcs;
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
		if (chgr_contr->fb_status == FB_BLANK_UNBLANK) {
			chgr_contr->current_iusb_limit[IUSB_NODE_FB] = chgr_contr->fb_limit_iusb;
			chgr_contr->current_ibat_limit[IBAT_NODE_FB] = chgr_contr->fb_limit_ibat;
		} else {
			chgr_contr->current_iusb_limit[IUSB_NODE_FB] = -1;
			chgr_contr->current_ibat_limit[IBAT_NODE_FB] = -1;
		}
#endif
	} else if (chgr_contr->wlc_online) {
		pr_cc(PR_DEBUG, "Wireless Current Set!\n");
		/* Normal */
		chgr_contr->current_iusb_max = chgr_contr->current_wireless_psy;
		chgr_contr->current_ibat_max = chgr_contr->current_ibat_max_wireless;
		/* Thermal */
		if (cc_wireless_limit == -1 || cc_wireless_limit == WIRELESS_IUSB_NORMAL) {
			chgr_contr->currnet_iusb_wlc[IUSB_NODE_THERMAL_WIRELSS] =
				chgr_contr->current_wireless_psy;
			chgr_contr->current_ibat_limit[IBAT_NODE_THERMAL] =
				chgr_contr->current_ibat_max_wireless;
		} else if (cc_wireless_limit == WIRELESS_IUSB_THERMAL_1) {
			chgr_contr->currnet_iusb_wlc[IUSB_NODE_THERMAL_WIRELSS] =
				WIRELESS_IUSB_THERMAL_1;
			chgr_contr->current_ibat_limit[IBAT_NODE_THERMAL] =
				WIRELESS_IBAT_THERMAL_1;
		} else if (cc_wireless_limit == WIRELESS_IUSB_THERMAL_2) {
			chgr_contr->currnet_iusb_wlc[IUSB_NODE_THERMAL_WIRELSS] =
				WIRELESS_IUSB_THERMAL_2;
			chgr_contr->current_ibat_limit[IBAT_NODE_THERMAL] =
				WIRELESS_IBAT_THERMAL_2;
		} else {
			chgr_contr->currnet_iusb_wlc[IUSB_NODE_THERMAL_WIRELSS] = cc_wireless_limit;
			chgr_contr->current_ibat_limit[IBAT_NODE_THERMAL] = cc_wireless_limit;
		}
		/* OTP */
		if (chgr_contr->wireless_lcs == -1) {
			chgr_contr->currnet_iusb_wlc[IUSB_NODE_OTP_WIRELESS] = chgr_contr->current_wireless_psy;
			chgr_contr->current_ibat_limit[IBAT_NODE_LGE_CHG_WLC] =
				chgr_contr->current_ibat_max_wireless;
		} else if (chgr_contr->wireless_lcs == WIRELESS_OTP_LIMIT) {
			chgr_contr->currnet_iusb_wlc[IUSB_NODE_OTP_WIRELESS] = chgr_contr->wireless_lcs;
			chgr_contr->current_ibat_limit[IBAT_NODE_LGE_CHG_WLC] =
				chgr_contr->wireless_lcs;
		} else {
			chgr_contr->currnet_iusb_wlc[IUSB_NODE_OTP_WIRELESS] = chgr_contr->current_wlc_limit;
			chgr_contr->current_ibat_limit[IBAT_NODE_LGE_CHG_WLC] =
				chgr_contr->wireless_lcs;
		}
	} else if (chgr_contr->usb_cable_info == CHARGER_CONTR_CABLE_FACTORY_CABLE) {
		pr_cc(PR_DEBUG, "Factory cable insert!\n");
		chgr_contr->current_iusb_max = chgr_contr->current_iusb_factory;
		chgr_contr->current_ibat_max = chgr_contr->current_ibat_factory;
	} else {
		pr_cc(PR_DEBUG, "Charger Off_line!\n");
		chgr_contr->current_iusb_max = 0;
		chgr_contr->current_ibat_max = 0;
		chgr_contr->current_iusb_limit[IUSB_NODE_THERMAL] = cc_iusb_limit;
		chgr_contr->current_ibat_limit[IBAT_NODE_THERMAL] = cc_ibat_limit;
		chgr_contr->current_ibat_limit[IBAT_NODE_LGE_CHG] = chgr_contr->ibat_limit_lcs;
		chgr_contr->current_ibat_limit[IBAT_NODE_EXT_CTRL] = -1;
//Added
		chgr_contr->current_iusb_limit[IUSB_NODE_LGE_CHG] = chgr_contr->iusb_limit_lcs;
	}

	pr_cc(PR_INFO, "Ibat max current set to %d\n", chgr_contr->current_ibat_max);

	chgr_contr->current_iusb_limit[IUSB_NODE_NO] = chgr_contr->current_iusb_max;
	iusb_limit = chgr_contr->current_iusb_limit[IUSB_NODE_NO];
	iusb_limit_idx = IUSB_NODE_NO;
	chgr_contr->current_ibat_limit[IBAT_NODE_NO] = chgr_contr->current_ibat_max;
	ibat_limit = chgr_contr->current_ibat_limit[IBAT_NODE_NO];
	ibat_limit_idx = IBAT_NODE_NO;

	if (requester & (REQUEST_BY_IUSB_LIMIT | REQUEST_BY_POWER_SUPPLY_CHANGED)) {
		for (i = 0; i < MAX_IUSB_NODE; i++) {
			if (chgr_contr->current_iusb_limit[i] < 0)
				continue;

			if (iusb_limit > chgr_contr->current_iusb_limit[i]) {
				iusb_limit = chgr_contr->current_iusb_limit[i];
				iusb_limit_idx = i;
			}
		}
		pr_cc(PR_INFO, "iusb_limit is %dmA by %d\n", iusb_limit, iusb_limit_idx);
		change_iusb(iusb_limit);
	}

	if (requester & (REQUEST_BY_IBAT_LIMIT | REQUEST_BY_POWER_SUPPLY_CHANGED)) {
		for (i = 0; i < MAX_IBAT_NODE; i++) {
			if (chgr_contr->current_ibat_limit[i] < 0)
				continue;

			if (ibat_limit > chgr_contr->current_ibat_limit[i]) {
				ibat_limit = chgr_contr->current_ibat_limit[i];
				ibat_limit_idx = i;
			}
		}
		pr_cc(PR_INFO, "ibat_limit is %dmA by %d\n", ibat_limit, ibat_limit_idx);
		change_ibat(ibat_limit);
	}

	if ((chgr_contr->changed_ibat_by_cc == 1) || (chgr_contr->changed_iusb_by_cc == 1)) {
		pr_cc(PR_DEBUG, "Current has been changed. Run bat_thread\n");
		chgr_contr->changed_ibat_by_cc = 0;
		chgr_contr->changed_iusb_by_cc = 0;
		power_supply_changed(chgr_contr->cc_psy);
		return 1;
	}

	return 0;
}

static int charger_controller_main(int requester)
{
	pr_cc(PR_INFO, "%s: requester = 0x%x\n", __func__, requester);

	if (!cc_init_ok)
		return 0;

	if (requester & REQUEST_BY_POWER_SUPPLY_CHANGED)
		update_pm_psy_status(requester);

	set_charger_control_current(requester);

	return 0;
}

static void charger_contr_notify_worker(struct work_struct *work)
{
	struct charger_contr *chip = container_of(work,
				struct charger_contr,
				cc_notify_work);

	charger_controller_main(chip->requester);

	/* clear requester */
	chip->requester = 0;
}

void changed_by_power_source(int requester)
{
	chgr_contr->requester |= requester;
	schedule_work(&chgr_contr->cc_notify_work);
}

void changed_by_batt_psy(void)
{
	int batt_status = POWER_SUPPLY_STATUS_DISCHARGING;
	union power_supply_propval val = {0,};
	bool wireless_present, usb_present, charger_present;

	wireless_present = is_wireless_present();
	usb_present = is_usb_present();
	/* update battery status information */
	cc_psy_getprop(batt_psy, STATUS, &val);
	batt_status = val.intval;

	if (chgr_contr->battery_status_cc != batt_status) {
		pr_cc(PR_DEBUG, "changed batt_status_cc = (%d) -> (%d)\n",
			chgr_contr->battery_status_cc, batt_status);
		chgr_contr->battery_status_cc = batt_status;
	}

	charger_present = wireless_present || usb_present;

	if (!charger_present || (charger_present && chgr_contr->batt_eoc == 1)) {
		if (wake_lock_active(&chgr_contr->chg_wake_lock)) {
			pr_cc(PR_DEBUG, "chg_wake_unlocked\n");
			wake_unlock(&chgr_contr->chg_wake_lock);
		}
	} else if (chgr_contr->batt_eoc != 2) {
		if (!wake_lock_active(&chgr_contr->chg_wake_lock)) {
			pr_cc(PR_DEBUG, "chg_wake_locked\n");
			wake_lock(&chgr_contr->chg_wake_lock);
		}
	}

#ifdef CONFIG_LGE_PM_LLK_MODE
	update_llk_status(chgr_contr);
#endif

	/* need update battery health when polling mode */
	if (battery_health_polling)
		schedule_delayed_work(&chgr_contr->battery_health_work,
				msecs_to_jiffies(100));
}

int notify_charger_controller(struct power_supply *psy, int requester)
{
	if (!cc_init_ok)
		return -EFAULT;

	pr_cc(PR_DEBUG, "notify_charger_controller (by %s. request id : 0x%x)\n", psy->name, requester);

	if (psy->type == POWER_SUPPLY_TYPE_BATTERY) {
		changed_by_batt_psy();
	} else if (psy->type == POWER_SUPPLY_TYPE_UNKNOWN) {
		/* handle only charger controller notify */
		if (!strcmp(psy->name, CC_PSY_NAME))
			changed_by_power_source(requester);
	} else {
		changed_by_power_source(requester);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(notify_charger_controller);

static int set_ibat_limit(const char *val, struct kernel_param *kp)
{
	int ret;

	if (!cc_init_ok)
		return 0;

	cc_ibat_limit = -1;

	ret = param_set_int(val, kp);
	if (ret) {
		pr_cc(PR_ERR, "error setting value %d\n", ret);
		return ret;
	}
	pr_cc(PR_ERR, "set iBAT limit current(%d)\n", cc_ibat_limit);

	if (chgr_contr->usb_online)
		notify_charger_controller(&chgr_contr->charger_contr_psy,
			REQUEST_BY_IBAT_LIMIT);

	return 0;

}
module_param_call(cc_ibat_limit, set_ibat_limit,
	param_get_int, &cc_ibat_limit, 0644);

static int set_iusb_limit(const char *val, struct kernel_param *kp)
{
	int ret;

	if (!cc_init_ok)
		return 0;

	ret = param_set_int(val, kp);
	if (ret) {
		pr_cc(PR_ERR, "error setting value %d\n", ret);
		return ret;
	}
	pr_cc(PR_INFO, "set iUSB limit current(%d)\n", cc_iusb_limit);

	chgr_contr->current_iusb_limit[IUSB_NODE_THERMAL] = cc_iusb_limit;
	if (chgr_contr->usb_online || chgr_contr->wlc_online)
		notify_charger_controller(&chgr_contr->charger_contr_psy,
			REQUEST_BY_IUSB_LIMIT);

	return 0;
}
module_param_call(cc_iusb_limit, set_iusb_limit,
	param_get_int, &cc_iusb_limit, 0644);

static int set_wireless_limit(const char *val, struct kernel_param *kp)
{
	int ret;

	if (!cc_init_ok)
		return 0;

	ret = param_set_int(val, kp);
	if (ret) {
		pr_cc(PR_ERR, "error setting value %d\n", ret);
		return ret;
	}

	pr_cc(PR_INFO, "set Wireless limit current(%d)\n", cc_wireless_limit);

	if (!chgr_contr->usb_online && chgr_contr->wlc_online)
		notify_charger_controller(&chgr_contr->charger_contr_psy,
			REQUEST_BY_IBAT_LIMIT);

	return 0;
}
module_param_call(cc_wireless_limit, set_wireless_limit,
	param_get_int, &cc_wireless_limit, 0644);

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
int battery_id_check(void)
{
	int rc;
	union power_supply_propval val = {0,};

	rc = cc_psy_getprop(batt_id_psy, BATTERY_ID, &val);
	if (rc) {
		pr_cc(PR_ERR, "%s : fail to get BATTERY_ID from battery_id. rc = %d.\n", __func__, rc);
		return 0;
	} else {
		chgr_contr->batt_id_smem = val.intval;
	}

	if (chgr_contr->batt_id_smem == BATT_NOT_PRESENT)
		chgr_contr->batt_smem_present = 0;
	else
		chgr_contr->batt_smem_present = 1;

	return 0;
}
#endif

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
static ssize_t at_chg_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int r;
	bool b_chg_ok = false;
	int chg_type;
	union power_supply_propval val = {0,};

	cc_psy_getprop(batt_psy, CHARGE_TYPE, &val);
	chg_type = val.intval;

	if (chg_type != POWER_SUPPLY_CHARGE_TYPE_NONE) {
		b_chg_ok = true;
		r = snprintf(buf, 3, "%d\n", b_chg_ok);
		pr_cc(PR_INFO, "[Diag] true ! buf = %s, charging=1\n", buf);
	} else {
		b_chg_ok = false;
		r = snprintf(buf, 3, "%d\n", b_chg_ok);
		pr_cc(PR_INFO, "[Diag] false ! buf = %s, charging=0\n", buf);
	}

	return r;
}

static ssize_t at_chg_status_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	int intval;

	if (strncmp(buf, "0", 1) == 0) {
		/* stop charging */
		pr_cc(PR_INFO, "[Diag] stop charging start\n");
		intval = 0;

	} else if (strncmp(buf, "1", 1) == 0) {
		/* start charging */
		pr_cc(PR_INFO, "[Diag] start charging start\n");
		intval = 1;
	}
	cc_psy_setprop(batt_psy, BATTERY_CHARGING_ENABLED_CC, intval);

	if (ret)
		return -EINVAL;

	return 1;
}

static ssize_t at_chg_complete_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = 0;
	int guage_level = get_prop_fuelgauge_capacity();

	if (guage_level == 100) {
		ret = snprintf(buf, 3, "%d\n", 0);
		pr_cc(PR_INFO, "[Diag] buf = %s, gauge==100\n", buf);
	} else {
		ret = snprintf(buf, 3, "%d\n", 1);
		pr_cc(PR_INFO, "[Diag] buf = %s, gauge<=100\n", buf);
	}

	return ret;
}

static ssize_t at_chg_complete_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	int intval;

	if (strncmp(buf, "0", 1) == 0) {
		/* charging not complete */
		pr_cc(PR_INFO, "[Diag] charging not complete start\n");
		intval = 1;
	} else if (strncmp(buf, "1", 1) == 0) {
		/* charging complete */
		pr_cc(PR_INFO, "[Diag] charging complete start\n");
		intval = 0;
	}

	cc_psy_setprop(batt_psy, BATTERY_CHARGING_ENABLED_CC, intval);

	if (ret)
		return -EINVAL;

	return 1;
}

static ssize_t at_pmic_reset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int r = 0;
	bool pm_reset = true;

	msleep(3000); /* for waiting return values of testmode */

	machine_restart(NULL);

	r = snprintf(buf, 3, "%d\n", pm_reset);

	return r;
}

#ifdef CONFIG_LGE_PM_AT_OTG_SUPPORT
static ssize_t at_otg_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int otg_mode;
	int r = 0;
	union power_supply_propval val = {0,};

	cc_psy_getprop(batt_psy, OTG_MODE, &val);
	otg_mode = val.intval;
	if (otg_mode) {
		otg_mode = 1;
		r = snprintf(buf, 3, "%d\n", otg_mode);
		pr_cc(PR_INFO, "[Diag] true ! buf = %s, OTG Enabled\n", buf);
	} else {
		otg_mode = 0;
		r = snprintf(buf, 3, "%d\n", otg_mode);
		pr_cc(PR_INFO, "[Diag] false ! buf = %s, OTG Disabled\n", buf);
	}
	return r;
}

static ssize_t at_otg_status_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret = 0;
	int intval;

	if (strncmp(buf, "0", 1) == 0) {
		pr_cc(PR_INFO, "[Diag] OTG Disable start\n");
		intval = 0;
	} else if (strncmp(buf, "1", 1) == 0) {
		pr_cc(PR_INFO, "[Diag] OTG Enable start\n");
		intval = 1;
	}

	cc_psy_setprop(batt_psy, OTG_MODE, intval);

	if (ret)
		return -EINVAL;
	return 1;
}
DEVICE_ATTR(at_otg, 0644, at_otg_status_show, at_otg_status_store);
#endif

DEVICE_ATTR(at_charge, 0644, at_chg_status_show, at_chg_status_store);
DEVICE_ATTR(at_chcomp, 0644, at_chg_complete_show, at_chg_complete_store);
DEVICE_ATTR(at_pmrst, 0440, at_pmic_reset_show, NULL);
#endif

/* #ifdef CONFIG_LGE_BATTERY_PROP */
#define DEFAULT_FAKE_BATT_CAPACITY		50
#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
#define LGE_FAKE_BATT_PRES		1
#endif

static int get_prop_fuelgauge_capacity(void)
{
	union power_supply_propval val = {0, };
#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
	union power_supply_propval usb_online = {0, };
	int rc;
#endif

	if (chgr_contr->fg_psy == NULL) {
		pr_err("\n%s: fg_psy == NULL\n", __func__);
		chgr_contr->fg_psy = power_supply_get_by_name(chgr_contr->fg_psy_name);
		if (chgr_contr->fg_psy == NULL) {
			pr_err("[ChargerController]########## Not Ready(fg_psy) %s\n", chgr_contr->fg_psy_name);
			return DEFAULT_FAKE_BATT_CAPACITY;
		}
	}

#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
	if (pseudo_batt_info.mode) {
#ifdef CONFIG_MACH_MSM8992_PPLUS
		return pseudo_batt_info.capacity;
#endif
		rc = cc_psy_getprop(fg_psy, CAPACITY, &val);
		if (rc)
			val.intval = DEFAULT_FAKE_BATT_CAPACITY;
		rc = cc_psy_getprop(usb_psy, ONLINE, &usb_online);
		if (rc)
			usb_online.intval = 0;

		if (usb_online.intval == 0 && val.intval == 0) {
			pr_cc(PR_INFO, "CAPACITY[%d], USB_ONLINE[%d]. System will power off\n",
					val.intval, usb_online.intval);
			return val.intval;
		} else {
			return pseudo_batt_info.capacity;
		}
	}
#endif

	if (!cc_psy_getprop(fg_psy, CAPACITY, &val))
		return val.intval;
	else
		return DEFAULT_FAKE_BATT_CAPACITY;
}

int lge_battery_get_property(enum power_supply_property prop,
				       union power_supply_propval *val)
{
	if (!cc_init_ok)
		return -EAGAIN;

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		if (chgr_contr->battery_status_charger != val->intval) {
			pr_cc(PR_INFO, "changed battery_status_charger = (%d) -> (%d)\n",
				chgr_contr->battery_status_charger,
				val->intval);
			if (val->intval == POWER_SUPPLY_STATUS_FULL)
				pr_cc(PR_INFO, "End Of Charging!\n");
			chgr_contr->battery_status_charger = val->intval;
		}

		if (val->intval == POWER_SUPPLY_STATUS_FULL)
			chgr_contr->batt_eoc = 1;
		else if (val->intval == POWER_SUPPLY_STATUS_DISCHARGING)
			chgr_contr->batt_eoc = 2;
		else
			chgr_contr->batt_eoc = 0;

		if ((get_prop_fuelgauge_capacity() >= 100) &&
				(is_usb_present() || is_wireless_present())) {
			val->intval = POWER_SUPPLY_STATUS_FULL;
		} else if ((get_prop_fuelgauge_capacity() != 100) &&
				(chgr_contr->batt_eoc == 1)) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			pr_cc(PR_DEBUG, "Battery waiting for recharging.\n");
		}
		/* todo */
		if ((chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_HIGH) &&
					(is_usb_present() || is_wireless_present())) {
			pr_cc(PR_DEBUG, "over 45 temp and over 4.0V, pseudo charging\n");
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
#if defined(CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY) && defined(CONFIG_LGE_PM_USB_ID)
		if (pseudo_batt_info.mode && !lge_is_factory_cable()) {
			val->intval = LGE_FAKE_BATT_PRES;
			pr_cc(PR_DEBUG, "Fake battery is exist\n");
		}
#endif
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		chgr_contr->charge_type = val->intval;
		pr_cc(PR_DEBUG, "charge_type = %d\n", chgr_contr->charge_type);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_prop_fuelgauge_capacity();
		pr_cc(PR_DEBUG, "Battery Capacity = %d\n", val->intval);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		chgr_contr->origin_batt_temp = val->intval;
		pr_cc(PR_INFO, "Battery TEMP = %d\n", val->intval);
#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
		if (pseudo_batt_info.mode) {
			val->intval = pseudo_batt_info.temp;
			pr_cc(PR_DEBUG, "Temp is %d in Fake battery\n", val->intval);
		}
#endif
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
		if (pseudo_batt_info.mode)
				val->intval = pseudo_batt_info.volt * 1000;
#endif
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		/* update health */
		if (!battery_health_polling)
			battery_health_work(&chgr_contr->battery_health_work.work);

		if (chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_OVERHEAT)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (chgr_contr->batt_temp_state == CC_BATT_TEMP_STATE_COLD)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(lge_battery_get_property);



static int chargercontroller_parse_dt(struct device_node *dev_node, struct charger_contr *cc)
{
	int ret;

#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT
	ret = of_property_read_u32(dev_node,
		"lge,chargercontroller-iusb-pep20",
		&(cc->current_iusb_pep));
	pr_cc(PR_INFO, "current_iusb_pep = %d from DT\n",
				cc->current_iusb_pep);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_iusb_pep. Set 1800.\n");
		cc->current_iusb_pep = 1800; /* changed default current */
	}

	ret = of_property_read_u32(dev_node,
		"lge,chargercontroller-ibat-pep20",
		&(cc->current_ibat_pep));
	pr_cc(PR_INFO, "current_ibat_pep = %d from DT\n",
				cc->current_ibat_pep);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_ibat_pep. Set 2800.\n");
		cc->current_ibat_pep = 2800; /* changed default current */
	}
#endif
	ret = of_property_read_u32(dev_node,
		"lge,chargercontroller-current-ibat-max",
		&(cc->current_ibat));
	pr_cc(PR_DEBUG, "current_ibat = %d from DT\n",
				cc->current_ibat);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_ibat_max. Set 1600.\n");
		cc->current_ibat = 2300; /* changed default current */
	}

	ret = of_property_read_u32(dev_node,
		"lge,chargercontroller-current-ibat-lcd_off",
		&(cc->current_ibat_lcd_off));
	pr_cc(PR_DEBUG, "current_ibat_lcd_off = %d from DT\n",
				cc->current_ibat_lcd_off);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_ibat_lcd_off. Set 1000.\n");
		cc->current_ibat_lcd_off = 1000;
	}


#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
	ret = of_property_read_u32(dev_node,
		"lge,chargercontroller-ibat-no-pep",
		&(cc->ibat_no_pep));
	pr_cc(PR_DEBUG, "ibat-no-pep = %d from DT\n",
				cc->ibat_no_pep);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_ibat_lcd_off. Set 1000.\n");
		cc->ibat_no_pep = 1000;
	}
#endif

	ret = of_property_read_u32(dev_node, "lge,chargercontroller-current-limit",
				   &(cc->current_limit));
	pr_cc(PR_DEBUG, "current_limit = %d from DT\n",
				cc->current_limit);
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_limit. Set 500(rt9460).\n");
		cc->current_limit = 500;
	}
#else
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_limit. Set 450.\n");
		cc->current_limit = 450;
	}
#endif
	ret = of_property_read_u32(dev_node, "lge,chargercontroller-current-wlc-limit",
				   &(cc->current_wlc_limit));
	pr_cc(PR_DEBUG, "current_wlc_limit = %d from DT\n",
				cc->current_wlc_limit);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_wlc_limit. Set 500.\n");
		cc->current_wlc_limit = 500;
	}

	ret = of_property_read_u32(dev_node, "lge,chargercontroller-current-ibat-max-wireless",
				   &(cc->current_ibat_max_wireless));
	pr_cc(PR_DEBUG, "current-ibat-max-wireless = %d from DT\n",
				cc->current_ibat_max_wireless);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current-ibat-max-wireless. Set 1200.\n");
		cc->current_ibat_max_wireless = 1200;
	}

	ret = of_property_read_u32(dev_node,
			"lge,chargercontroller-current-iusb-factory",
				   &(cc->current_iusb_factory));
	pr_cc(PR_DEBUG, "current_iusb_factory = %d from DT\n",
				cc->current_iusb_factory);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_iusb_factory. Set 1500.\n");
		cc->current_iusb_factory = 1500;
	}

	ret = of_property_read_u32(dev_node,
			"lge,chargercontroller-current-ibat-factory",
				   &(cc->current_ibat_factory));
	pr_cc(PR_DEBUG, "current_ibat_factory = %d from DT\n",
				cc->current_ibat_factory);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read current_ibat_factory. Set 500.\n");
		cc->current_ibat_factory = 500;
	}

	/* read the fuelgauge power supply name */
	ret = of_property_read_string(dev_node, "lge,fuelgauge-psy-name",
						&cc->fg_psy_name);
	if (ret)
		cc->fg_psy_name = FUELGAUGE_PSY_NAME;

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	ret = of_property_read_u32(dev_node,
			"lge,chargercontroller-fb-limit-iusb",
				&(cc->fb_limit_iusb));
	pr_cc(PR_DEBUG, "fb_limit_iusb = %d from DT\n", cc->fb_limit_iusb);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read fb_limit_iusb. Don't set.\n");
		cc->fb_limit_iusb = -1;
	}

	ret = of_property_read_u32(dev_node,
			"lge,chargercontroller-fb-limit-ibat",
				&(cc->fb_limit_ibat));
	pr_cc(PR_DEBUG, "fb_limit_ibat = %d from DT\n", cc->fb_limit_ibat);
	if (ret) {
		pr_cc(PR_ERR, "Unable to read fb_limit_ibat. Don't set.\n");
		cc->fb_limit_ibat = -1;
	}
#endif

	return ret;
}

int get_temp_state()
{
	int temp;
	int cur_temp_state = chgr_contr->batt_temp_state;

	/* update battery temperature */
	temp = charger_contr_get_battery_temperature();
	chgr_contr->batt_temp = div_s64(temp, 10);

	if (chgr_contr->batt_temp < -10) {
		cur_temp_state = CC_BATT_TEMP_STATE_COLD;
	} else if (chgr_contr->batt_temp >= -5 &&
		chgr_contr->batt_temp <= 42) {
		cur_temp_state = CC_BATT_TEMP_STATE_NORMAL;
	} else if (chgr_contr->batt_temp > 45 && chgr_contr->batt_temp <= 52) {
		cur_temp_state = CC_BATT_TEMP_STATE_HIGH;
	} else if (chgr_contr->batt_temp >= 55) {
		cur_temp_state = CC_BATT_TEMP_STATE_OVERHEAT;
	/* hysterisis range */
	} else if (chgr_contr->batt_temp > 52) {
		if (chgr_contr->batt_temp_state < CC_BATT_TEMP_STATE_OVERHEAT)
			cur_temp_state = CC_BATT_TEMP_STATE_HIGH;
	} else if (chgr_contr->batt_temp > 42) {
		if (chgr_contr->batt_temp_state < CC_BATT_TEMP_STATE_NORMAL)
			cur_temp_state = CC_BATT_TEMP_STATE_NORMAL;
		if (chgr_contr->batt_temp_state > CC_BATT_TEMP_STATE_HIGH)
			cur_temp_state = CC_BATT_TEMP_STATE_HIGH;
	} else if (chgr_contr->batt_temp < -5) {
		if (chgr_contr->batt_temp_state > CC_BATT_TEMP_STATE_NORMAL)
			cur_temp_state = CC_BATT_TEMP_STATE_NORMAL;
	}

	return cur_temp_state;
}

int get_volt_state()
{
	int cur_volt_state = chgr_contr->batt_volt_state;
	union power_supply_propval val = {0, };

	/* update battery voltage */
	cc_psy_getprop(fg_psy, VOLTAGE_NOW, &val);
	chgr_contr->batt_volt = div_s64(val.intval, 1000);

	if (chgr_contr->batt_volt == 4000) /* default batt_volt_state */
		cur_volt_state = CC_BATT_VOLT_UNDER_4_0;
	if (chgr_contr->batt_volt < 4000)
		cur_volt_state = CC_BATT_VOLT_UNDER_4_0;
	else if (chgr_contr->batt_volt > 4000)
		cur_volt_state = CC_BATT_VOLT_OVER_4_0;

	return cur_volt_state;
}

#define LT_CABLE_56K		6
#define LT_CABLE_130K		7
#define LT_CABLE_910K		11
static int charger_contr_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	struct charger_contr *cc;
	union power_supply_propval val = {0,};
	struct device_node *dev_node = pdev->dev.of_node;
#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
	unsigned int cable_type;
	unsigned int cable_smem_size;
	unsigned int *p_cable_type = (unsigned int *)
		(smem_get_entry(SMEM_ID_VENDOR1, &cable_smem_size, 0, 0));
#endif

	pr_cc(PR_INFO, "charger_contr_probe start\n");

	cc = kzalloc(sizeof(struct charger_contr), GFP_KERNEL);

	if (!cc) {
		pr_cc(PR_ERR, "failed to alloc memory\n");
		return -ENOMEM;
	}

	chgr_contr = cc;

	wake_lock_init(&chgr_contr->chg_wake_lock,
			WAKE_LOCK_SUSPEND, "charging_wake_lock");
#ifdef CONFIG_LGE_PM_CHARGE_INFO
	wake_lock_init(&chgr_contr->information_lock, WAKE_LOCK_SUSPEND, "chgr_contr_info");
#endif

	cc->dev = &pdev->dev;

	cc->usb_psy = power_supply_get_by_name(USB_PSY_NAME);
	if (cc->usb_psy == NULL) {
		pr_cc(PR_ERR, "Not Ready(usb_psy)\n");
		ret = -EPROBE_DEFER;
		goto error;
	}

	cc->batt_psy = power_supply_get_by_name(BATT_PSY_NAME);
	if (cc->batt_psy == NULL) {
		pr_cc(PR_ERR, "Not Ready(batt_psy)\n");
		ret = -EPROBE_DEFER;
		goto error;
	}

#ifdef CONFIG_LGE_PM_WIRELESS_CHARGING
	cc->wireless_psy = power_supply_get_by_name(WIRELESS_PSY_NAME);
	if (cc->wireless_psy == NULL) {
		pr_cc(PR_ERR, "Not Ready(wireless_psy)\n");
		ret = -EPROBE_DEFER;
		goto error;
	}
#endif

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	cc->batt_id_psy = power_supply_get_by_name(BATT_ID_PSY_NAME);
	if (cc->batt_id_psy == NULL) {
		pr_cc(PR_ERR, "Not Ready(batt_id_psy)\n");
		ret =	-EPROBE_DEFER;
		goto error;
	}
#endif

	if (cc->wireless_psy != NULL) {
		cc_psy_getprop(wireless_psy, CURRENT_MAX, &val);
		if (val.intval) {
			pr_info("[ChargerController] Present wireless current =%d\n", (int)div_s64(val.intval, CURRENT_DIVIDE_UNIT));
			cc->current_wireless_psy = div_s64(val.intval, CURRENT_DIVIDE_UNIT);
			cc->wireless_init_current = cc->current_wireless_psy;
		}
	}

	ret = chargercontroller_parse_dt(dev_node, cc);
	if (ret)
		pr_cc(PR_ERR, "failed to parse dt\n");

	cc->ready = 0;
	cc->usb_online = 0;
	cc->wlc_online = 0;
	cc->current_iusb_changed_by_cc = 0;
	cc->wireless_lcs = -1;
	cc->ibat_limit_lcs = -1;
	cc->iusb_limit_lcs = -1;
	cc->batt_temp_state = CC_BATT_TEMP_STATE_NORMAL;
	cc->batt_volt_state = CC_BATT_VOLT_UNDER_4_0;
#ifdef CONFIG_LGE_PM_LLK_MODE
	cc->store_demo_enabled = 0;
#endif

	for (i = 0; i < MAX_IBAT_NODE; i++)
		cc->current_ibat_limit[i] = -1;

	for (i = 0; i < MAX_IUSB_NODE; i++)
		cc->current_iusb_limit[i] = -1;

	/* power_supply register for controlling charger(batt psy) */
	cc->charger_contr_psy.name = CC_PSY_NAME;
	cc->charger_contr_psy.supplied_to = charger_contr_supplied_to;
	cc->charger_contr_psy.num_supplicants = ARRAY_SIZE(charger_contr_supplied_to);
	cc->charger_contr_psy.properties = pm_power_props_charger_contr_pros;
	cc->charger_contr_psy.num_properties = ARRAY_SIZE(pm_power_props_charger_contr_pros);
	cc->charger_contr_psy.get_property = pm_get_property_charger_contr;
	cc->charger_contr_psy.set_property = pm_set_property_charger_contr;
	cc->charger_contr_psy.property_is_writeable = charger_contr_property_is_writeable;

	ret = power_supply_register(cc->dev, &cc->charger_contr_psy);
	if (ret < 0) {
		pr_cc(PR_ERR, "%s power_supply_register charger controller failed ret=%d\n",
			__func__, ret);
		goto error;
	}
	cc->cc_psy = power_supply_get_by_name(CC_PSY_NAME);
	if (cc->cc_psy == NULL) {
		pr_cc(PR_ERR, "Not Ready(cc_psy)\n");
		ret = -EPROBE_DEFER;
		goto error;
	}

	if (cc->fg_psy == NULL) {
		get_prop_fuelgauge_capacity();
	}

	mutex_init(&cc->notify_work_lock);
#ifdef CONFIG_LGE_PM_USB_ID
	if (lge_is_factory_cable_boot()) {
		chgr_contr->usb_cable_info = CHARGER_CONTR_CABLE_FACTORY_CABLE;
		cc->current_ibat_limit[1] = 500;
		cc->current_iusb_limit[1] = 1500;
		change_iusb(1500);
		change_ibat(500);
	}
#endif
	platform_set_drvdata(pdev, cc);

#ifdef CONFIG_LGE_PM_BATTERY_ID_CHECKER
	battery_id_check();
#endif
#ifdef CONFIG_LGE_PM_FACTORY_PSEUDO_BATTERY
	if (p_cable_type)
		cable_type = *p_cable_type;
	else
		cable_type = 0;

	if ((cable_type == LT_CABLE_56K || cable_type == LT_CABLE_130K ||
		cable_type == LT_CABLE_910K) && cc->batt_smem_present == 0) {
		pseudo_batt_info.mode = 1;
	}
	pr_info("cable_type = %d, batt_present = %d, pseudo_batt_info.mode = %d\n",
		cable_type, cc->batt_smem_present, pseudo_batt_info.mode);
#endif

	INIT_WORK(&cc->cc_notify_work, charger_contr_notify_worker);
#ifdef CONFIG_LGE_PM_USB_ID
	cc->cable_info = kzalloc(sizeof(struct cc_chg_cable_info), GFP_KERNEL);

	if (!cc->cable_info) {
		pr_cc(PR_ERR, "failed to alloc memory\n");
		return -ENOMEM;
	}
	cc->cable_info->cable_type = 0;
	cc->cable_info->ta_ma = 0;
	cc->cable_info->usb_ma = 0;
	INIT_DELAYED_WORK(&cc->vadc_work, vadc_set_work);
#endif
#ifdef CONFIG_LGE_PM_CHARGE_INFO
	INIT_DELAYED_WORK(&cc->charging_inform_work, charging_information);
#endif
	INIT_DELAYED_WORK(&cc->battery_health_work, battery_health_work);
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	INIT_DELAYED_WORK(&cc->otp_charging_work, otp_charging_work);
#endif

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	cc->fb_notify.notifier_call = chgr_contr_fb_notifer_callback;
	cc->fb_status = FB_BLANK_POWERDOWN;

	if (lge_get_boot_mode() == LGE_BOOT_MODE_NORMAL)
		fb_register_client(&cc->fb_notify);
#endif

#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	ret = device_create_file(cc->dev, &dev_attr_at_charge);
	if (ret < 0) {
		pr_cc(PR_ERR, "%s:File dev_attr_at_charge creation failed: %d\n",
				__func__, ret);
		ret = -ENODEV;
		goto err_at_charge;
	}

	ret = device_create_file(cc->dev, &dev_attr_at_chcomp);
	if (ret < 0) {
		pr_cc(PR_ERR, "%s:File dev_attr_at_chcomp creation failed: %d\n",
				__func__, ret);
		ret = -ENODEV;
		goto err_at_chcomp;
	}

	ret = device_create_file(cc->dev, &dev_attr_at_pmrst);
	if (ret < 0) {
		pr_cc(PR_ERR, "%s:File device creation failed: %d\n", __func__, ret);
		ret = -ENODEV;
		goto err_at_pmrst;
	}
#ifdef CONFIG_LGE_PM_AT_OTG_SUPPORT
	ret = device_create_file(cc->dev, &dev_attr_at_otg);
	if (ret < 0) {
		pr_cc(PR_ERR, "%s:File device creation failed: %d\n", __func__, ret);
		ret = -ENODEV;
		goto err_at_otg;
	}
#endif
#endif

	cc_init_ok = 1;
	pr_cc(PR_INFO, "charger_contr_probe done\n");

#ifdef CONFIG_LGE_PM_CHARGE_INFO
	schedule_delayed_work(&chgr_contr->charging_inform_work,
		round_jiffies_relative(msecs_to_jiffies(cc_info_time)));
#endif

#ifdef CONFIG_LGE_PM_USB_ID
	schedule_delayed_work(&chgr_contr->vadc_work, 0);
#endif
	return ret;
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
#ifdef CONFIG_LGE_PM_AT_OTG_SUPPORT
err_at_otg:
	device_remove_file(cc->dev, &dev_attr_at_pmrst);
#endif
err_at_pmrst:
	device_remove_file(cc->dev, &dev_attr_at_chcomp);
err_at_chcomp:
	device_remove_file(cc->dev, &dev_attr_at_charge);
err_at_charge:
	power_supply_unregister(&cc->charger_contr_psy);
#endif
error:
	wake_lock_destroy(&cc->chg_wake_lock);
#ifdef CONFIG_LGE_PM_CHARGE_INFO
	wake_lock_destroy(&chgr_contr->information_lock);
#endif

	kfree(cc);
	return ret;

}

static void charger_contr_kfree(struct charger_contr *cc)
{
	pr_cc(PR_DEBUG, "kfree\n");
	kfree(cc);
}

static int charger_contr_remove(struct platform_device *pdev)
{
	struct charger_contr *cc = platform_get_drvdata(pdev);
#ifdef CONFIG_LGE_PM_FACTORY_TESTMODE
	device_remove_file(cc->dev, &dev_attr_at_charge);
	device_remove_file(cc->dev, &dev_attr_at_chcomp);
	device_remove_file(cc->dev, &dev_attr_at_pmrst);
#ifdef CONFIG_LGE_PM_AT_OTG_SUPPORT
	device_remove_file(cc->dev, &dev_attr_at_otg);
#endif
#endif
#ifdef CONFIG_LGE_PM_CHARGING_TEMP_SCENARIO
	cancel_delayed_work(&chgr_contr->otp_charging_work);
#endif
#ifdef CONFIG_LGE_PM_CHARGE_INFO
	cancel_delayed_work(&chgr_contr->charging_inform_work);
	wake_lock_destroy(&chgr_contr->information_lock);
#endif
	wake_lock_destroy(&cc->chg_wake_lock);
	power_supply_unregister(&cc->charger_contr_psy);

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	if (lge_get_boot_mode() == LGE_BOOT_MODE_NORMAL)
		fb_unregister_client(&cc->fb_notify);
#endif

	charger_contr_kfree(cc);

	return 0;
}

static int contr_suspend(struct device *dev)
{
#ifdef CONFIG_LGE_PM_CHARGE_INFO
	struct charger_contr *chip  = dev_get_drvdata(dev);
#endif
	if (!cc_init_ok)
		return 0;

#ifdef CONFIG_LGE_PM_CHARGE_INFO
	cancel_delayed_work(&chip->charging_inform_work);
#endif

	return 0;
}

static int contr_resume(struct device *dev)
{
#ifdef CONFIG_LGE_PM_CHARGE_INFO
	struct charger_contr *chip  = dev_get_drvdata(dev);
#endif
	if (!cc_init_ok)
		return 0;

#ifdef CONFIG_LGE_PM_CHARGE_INFO
	schedule_delayed_work(&chip->charging_inform_work, 0);
#endif

	return 0;
}

static const struct dev_pm_ops charger_contr_pm_ops = {
	.suspend = contr_suspend,
	.resume = contr_resume,
};
static struct of_device_id charger_contr_match_table[] = {
		{.compatible = "lge,charger-controller" ,},
		{},
};

static struct platform_driver charger_contr_device_driver = {
	.probe = charger_contr_probe,
	.remove = charger_contr_remove,

	.driver = {
		.name = "lge,charger-controller",
		.owner = THIS_MODULE,
		.of_match_table = charger_contr_match_table,
		.pm = &charger_contr_pm_ops,
	},
};

static void async_charger_contr_init(void *data, async_cookie_t cookie)
{
	platform_driver_register(&charger_contr_device_driver);
	return;
}

static int __init charger_contr_init(void)
{
	async_schedule(async_charger_contr_init, NULL);

	return 0;
}

static void __exit charger_contr_exit(void)
{
	platform_driver_unregister(&charger_contr_device_driver);
}

late_initcall(charger_contr_init);
module_exit(charger_contr_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("charger IC driver current controller");
MODULE_DEVICE_TABLE(of, charger_contr_match_table);
MODULE_AUTHOR("kwangmin.jeong@lge.com");
MODULE_VERSION("1.2");
