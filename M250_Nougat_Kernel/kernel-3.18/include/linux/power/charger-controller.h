/*
 * Copyright (C) LG Electronics 2014
 *
 * Charger Controller interface
 *
 */

enum {
	REQUEST_BY_POWER_SUPPLY_CHANGED = 0x01,
	REQUEST_BY_IBAT_LIMIT = 0x02,
	REQUEST_BY_IUSB_LIMIT = 0x04,
	REQUEST_BY_WIRELESS_LIMIT = 0x08,
};

#define RERUN_APSD_PARAM 0xf5f5f5f5

enum {
	CC_BATT_TEMP_CHANGED = 0x01,
	CC_BATT_VOLT_CHANGED = 0x02,
};

enum {
	CC_BATT_TEMP_STATE_COLD = -1,
	CC_BATT_TEMP_STATE_NORMAL = 0,
	CC_BATT_TEMP_STATE_HIGH,
	CC_BATT_TEMP_STATE_OVERHEAT,
	CC_BATT_TEMP_STATE_NODEFINED,
};

enum {
	CC_BATT_VOLT_UNDER_4_0 = 0,
	CC_BATT_VOLT_OVER_4_0,
};

/* Node for iUSB limit */
enum {
	IUSB_NODE_NO = 0,
	IUSB_NODE_LGE_CHG,
	IUSB_NODE_THERMAL,
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	IUSB_NODE_FB,
#endif
	MAX_IUSB_NODE,
};

/* Node for iBAT limit */
enum {
	IBAT_NODE_NO = 0,
	IBAT_NODE_LGE_CHG,
	IBAT_NODE_THERMAL,
	IBAT_NODE_CALL,
	IBAT_NODE_EXT_CTRL,
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER_LCD_LIMIT
	IBAT_NODE_FB,
#endif
	IBAT_NODE_LGE_CHG_WLC, /* WLC must be placed last of list. */
	MAX_IBAT_NODE,
};

/* Node for Wireless limit*/
enum {
	IUSB_NODE_THERMAL_WIRELSS = 0,
	IUSB_NODE_OTP_WIRELESS,
	MAX_IUSB_NODE_WIRELESS,
};

int notify_charger_controller(struct power_supply *psy, int requester);
int lge_battery_get_property(enum power_supply_property prop,
				       union power_supply_propval *val);
int get_temp_state(void);
int get_volt_state(void);

#ifdef CONFIG_LGE_PM_USB_ID
struct cc_chg_cable_info {
	int cable_type;
	unsigned ta_ma;
	unsigned usb_ma;
};
#endif
