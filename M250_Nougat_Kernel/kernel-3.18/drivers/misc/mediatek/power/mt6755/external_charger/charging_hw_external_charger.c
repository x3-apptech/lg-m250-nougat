#include <linux/types.h>
#include <mt-plat/charging.h>
#include <mt-plat/upmu_common.h>

#include <linux/delay.h>
#include <linux/reboot.h>
#include <mt-plat/mt_boot.h>
#include <mach/mt_charging.h>
#include <mach/mt_pmic.h>
#include <linux/power_supply.h>
#include <mt-plat/mt_gpio.h>
#ifdef CONFIG_MACH_LGE
#include <soc/mediatek/lge/board_lge.h>
#endif

#include <mach/mt_sleep.h>
#include <mach/mt_spm.h>
#define GETARRAYNUM(array) (sizeof(array)/sizeof(array[0]))
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
#include "rt9460.h"
#endif

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
#include "sm5424.h"
#endif

/*Base 6735 by JJB*/
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
#include <mt-plat/battery_common.h>
#endif
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24262_SUPPORT
#include "bq24262.h"
#endif
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ25898S_SLAVE_SUPPORT
#include "bq25898s_reg.h"
#endif
/* ============================================================ // */
/* define */
/* ============================================================ // */
#define STATUS_OK	0
#define STATUS_UNSUPPORTED	-1

// ============================================================ //
// global variable
// ============================================================ //
kal_bool chargin_hw_init_done = KAL_FALSE;

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
static int first_eoc = 0;
#endif
// ============================================================ //
// internal variable
// ============================================================ //
static char* support_charger[] = {
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
	"rt-charger",
#endif
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
	"sm5424",
#endif
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24262_SUPPORT
	"bq24262",
#endif
};
static struct power_supply *external_charger = NULL;

static kal_bool charging_type_det_done = KAL_TRUE;
static CHARGER_TYPE g_charger_type = CHARGER_UNKNOWN;

static int g_charging_enabled = -EINVAL;
static int g_battery_charging_enabled = -EINVAL;
static int g_charging_current = -EINVAL;
static int g_charging_current_limit = -EINVAL;
static int g_charging_voltage = -EINVAL;
static int g_charging_setting_changed = -EINVAL;

// ============================================================ //
// extern variable
// ============================================================ //

// ============================================================ //
// extern function
// ============================================================ //
extern unsigned int upmu_get_reg_value(unsigned int reg);
extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);
extern void mt_power_off(void);

const unsigned int VCDT_HV_VTH[] = {
        BATTERY_VOLT_04_200000_V, BATTERY_VOLT_04_250000_V, BATTERY_VOLT_04_300000_V,
        BATTERY_VOLT_04_350000_V,
        BATTERY_VOLT_04_400000_V, BATTERY_VOLT_04_450000_V, BATTERY_VOLT_04_500000_V,
        BATTERY_VOLT_04_550000_V,
        BATTERY_VOLT_04_600000_V, BATTERY_VOLT_06_000000_V, BATTERY_VOLT_06_500000_V,
        BATTERY_VOLT_07_000000_V,
        BATTERY_VOLT_07_500000_V, BATTERY_VOLT_08_500000_V, BATTERY_VOLT_09_500000_V,
        BATTERY_VOLT_10_500000_V
};

#ifdef CONFIG_MTK_BIF_SUPPORT
/* BIF related functions*/
#define BC (0x400)
#define SDA (0x600)
#define ERA (0x100)
#define WRA (0x200)
#define RRA (0x300)
#define WD  (0x000)
/*bus commands*/
#define BUSRESET (0x00)
#define RBL2 (0x22)
#define RBL4 (0x24)

/*BIF slave address*/
#define MW3790 (0x00)
#define MW3790_VBAT (0x0114)
#define MW3790_TBAT (0x0193)

static int bif_exist;
static int bif_checked;

void bif_reset_irq(void)
{
	unsigned int reg_val = 0;
	unsigned int loop_i = 0;

	pmic_set_register_value(PMIC_BIF_IRQ_CLR, 1);
	reg_val = 0;
	do {
		reg_val = pmic_get_register_value(PMIC_BIF_IRQ);

		if (loop_i++ > 50) {
			battery_log(BAT_LOG_CRTI, "[BIF][reset irq]failed.PMIC_BIF_IRQ 0x%x %d\n",
				    reg_val, loop_i);
			break;
		}
	} while (reg_val != 0);
	pmic_set_register_value(PMIC_BIF_IRQ_CLR, 0);
}

void bif_waitfor_slave(void)
{
	unsigned int reg_val = 0;
	int loop_i = 0;

	do {
		reg_val = pmic_get_register_value(PMIC_BIF_IRQ);

		if (loop_i++ > 50) {
			battery_log(BAT_LOG_FULL,
				    "[BIF][waitfor_slave] failed. PMIC_BIF_IRQ=0x%x, loop=%d\n",
				    reg_val, loop_i);
			break;
		}
	} while (reg_val == 0);

	if (reg_val == 1)
		battery_log(BAT_LOG_FULL, "[BIF][waitfor_slave]OK at loop=%d.\n", loop_i);

}

int bif_powerup_slave(void)
{
	int bat_lost = 0;
	int total_valid = 0;
	int timeout = 0;
	int loop_i = 0;

	do {
		battery_log(BAT_LOG_CRTI, "[BIF][powerup_slave] set BIF power up register\n");
		pmic_set_register_value(PMIC_BIF_POWER_UP, 1);

		battery_log(BAT_LOG_FULL, "[BIF][powerup_slave] trigger BIF module\n");
		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 1);

		udelay(10);

		bif_waitfor_slave();

		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 0);

		pmic_set_register_value(PMIC_BIF_POWER_UP, 0);

		/*check_bat_lost(); what to do with this? */
		bat_lost = pmic_get_register_value(PMIC_BIF_BAT_LOST);
		total_valid = pmic_get_register_value(PMIC_BIF_TOTAL_VALID);
		timeout = pmic_get_register_value(PMIC_BIF_TIMEOUT);

		if (loop_i < 5) {
			loop_i++;
		} else {
			battery_log(BAT_LOG_CRTI, "[BIF][powerup_slave]Failed at loop=%d", loop_i);
			break;
		}
	} while (bat_lost == 1 || total_valid == 1 || timeout == 1);
	if (loop_i < 5) {
		battery_log(BAT_LOG_FULL, "[BIF][powerup_slave]OK at loop=%d", loop_i);
		bif_reset_irq();
		return 1;
	}

	return -1;
}

void bif_set_cmd(int bif_cmd[], int bif_cmd_len)
{
	int i = 0;
	int con_index = 0;
	unsigned int ret = 0;

	for (i = 0; i < bif_cmd_len; i++) {
		ret = pmic_config_interface(MT6351_BIF_CON0 + con_index, bif_cmd[i], 0x07FF, 0);
		con_index += 0x2;
	}
}


int bif_reset_slave(void)
{
	int ret = 0;
	int bat_lost = 0;
	int total_valid = 0;
	int timeout = 0;
	int bif_cmd[1] = { 0 };
	int loop_i = 0;

	/*set command sequence */
	bif_cmd[0] = BC | BUSRESET;
	bif_set_cmd(bif_cmd, 1);

	do {
		/*command setting : 1 write command */
		ret = pmic_set_register_value(PMIC_BIF_TRASFER_NUM, 1);
		ret = pmic_set_register_value(PMIC_BIF_COMMAND_TYPE, 0);

		/*Command set trigger */
		ret = pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 1);

		udelay(10);
		/*Command sent; wait for slave */
		bif_waitfor_slave();

		/*Command clear trigger */
		ret = pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 0);
		/*check transaction completeness */
		bat_lost = pmic_get_register_value(PMIC_BIF_BAT_LOST);
		total_valid = pmic_get_register_value(PMIC_BIF_TOTAL_VALID);
		timeout = pmic_get_register_value(PMIC_BIF_TIMEOUT);

		if (loop_i < 50)
			loop_i++;
		else {
			battery_log(BAT_LOG_CRTI, "[BIF][bif_reset_slave]Failed at loop=%d",
				    loop_i);
			break;
		}
	} while (bat_lost == 1 || total_valid == 1 || timeout == 1);

	if (loop_i < 50) {
		battery_log(BAT_LOG_FULL, "[BIF][bif_reset_slave]OK at loop=%d", loop_i);
		/*reset BIF_IRQ */
		bif_reset_irq();
		return 1;
	}
	return -1;
}

/*BIF WRITE 8 transaction*/
int bif_write8(int addr, int *data)
{
	int ret = 1;
	int era, wra;
	int bif_cmd[4] = { 0, 0, 0, 0 };
	int loop_i = 0;
	int bat_lost = 0;
	int total_valid = 0;
	int timeout = 0;

	era = (addr & 0xFF00) >> 8;
	wra = addr & 0x00FF;
	battery_log(BAT_LOG_FULL, "[BIF][bif_write8]ERA=%x, WRA=%x\n", era, wra);
	/*set command sequence */
	bif_cmd[0] = SDA | MW3790;
	bif_cmd[1] = ERA | era;	/*[15:8] */
	bif_cmd[2] = WRA | wra;	/*[ 7:0] */
	bif_cmd[3] = WD | (*data & 0xFF);	/*data */


	bif_set_cmd(bif_cmd, 4);
	do {
		/*command setting : 4 transactions for 1 byte write command(0) */
		pmic_set_register_value(PMIC_BIF_TRASFER_NUM, 4);
		pmic_set_register_value(PMIC_BIF_COMMAND_TYPE, 0);

		/*Command set trigger */
		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 1);

		udelay(200);
		/*Command sent; wait for slave */
		bif_waitfor_slave();

		/*Command clear trigger */
		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 0);
		/*check transaction completeness */
		bat_lost = pmic_get_register_value(PMIC_BIF_BAT_LOST);
		total_valid = pmic_get_register_value(PMIC_BIF_TOTAL_VALID);
		timeout = pmic_get_register_value(PMIC_BIF_TIMEOUT);

		if (loop_i <= 50)
			loop_i++;
		else {
			battery_log(BAT_LOG_CRTI,
				    "[BIF][bif_write8] Failed. bat_lost = %d, timeout = %d, totoal_valid = %d\n",
				    bat_lost, timeout, total_valid);
			ret = -1;
			break;
		}
	} while (bat_lost == 1 || total_valid == 1 || timeout == 1);

	if (ret == 1)
		battery_log(BAT_LOG_FULL, "[BIF][bif_write8] OK for %d loop(s)\n", loop_i);
	else
		battery_log(BAT_LOG_CRTI, "[BIF][bif_write8] Failed for %d loop(s)\n", loop_i);

	/*reset BIF_IRQ */
	bif_reset_irq();

	return ret;
}

/*BIF READ 8 transaction*/
int bif_read8(int addr, int *data)
{
	int ret = 1;
	int era, rra;
	int val = -1;
	int bif_cmd[3] = { 0, 0, 0 };
	int loop_i = 0;
	int bat_lost = 0;
	int total_valid = 0;
	int timeout = 0;

	battery_log(BAT_LOG_FULL, "[BIF][READ8]\n");

	era = (addr & 0xFF00) >> 8;
	rra = addr & 0x00FF;
	battery_log(BAT_LOG_FULL, "[BIF][bif_read8]ERA=%x, RRA=%x\n", era, rra);
	/*set command sequence */
	bif_cmd[0] = SDA | MW3790;
	bif_cmd[1] = ERA | era;	/*[15:8] */
	bif_cmd[2] = RRA | rra;	/*[ 7:0] */

	bif_set_cmd(bif_cmd, 3);
	do {
		/*command setting : 3 transactions for 1 byte read command(1) */
		pmic_set_register_value(PMIC_BIF_TRASFER_NUM, 3);
		pmic_set_register_value(PMIC_BIF_COMMAND_TYPE, 1);
		pmic_set_register_value(PMIC_BIF_READ_EXPECT_NUM, 1);

		/*Command set trigger */
		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 1);

		udelay(200);
		/*Command sent; wait for slave */
		bif_waitfor_slave();

		/*Command clear trigger */
		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 0);
		/*check transaction completeness */
		bat_lost = pmic_get_register_value(PMIC_BIF_BAT_LOST);
		total_valid = pmic_get_register_value(PMIC_BIF_TOTAL_VALID);
		timeout = pmic_get_register_value(PMIC_BIF_TIMEOUT);

		if (loop_i <= 50)
			loop_i++;
		else {
			battery_log(BAT_LOG_CRTI,
				    "[BIF][bif_read16] Failed. bat_lost = %d, timeout = %d, totoal_valid = %d\n",
				    bat_lost, timeout, total_valid);
			ret = -1;
			break;
		}
	} while (bat_lost == 1 || total_valid == 1 || timeout == 1);

	/*Read data */
	if (ret == 1) {
		val = pmic_get_register_value(PMIC_BIF_DATA_0);
		battery_log(BAT_LOG_FULL, "[BIF][bif_read8] OK d0=0x%x, for %d loop(s)\n",
			    val, loop_i);
	} else
		battery_log(BAT_LOG_CRTI, "[BIF][bif_read8] Failed for %d loop(s)\n", loop_i);

	/*reset BIF_IRQ */
	bif_reset_irq();

	*data = val;
	return ret;
}

/*bif read 16 transaction*/
int bif_read16(int addr)
{
	int ret = 1;
	int era, rra;
	int val = -1;
	int bif_cmd[4] = { 0, 0, 0, 0 };
	int loop_i = 0;
	int bat_lost = 0;
	int total_valid = 0;
	int timeout = 0;

	battery_log(BAT_LOG_FULL, "[BIF][READ]\n");

	era = (addr & 0xFF00) >> 8;
	rra = addr & 0x00FF;
	battery_log(BAT_LOG_FULL, "[BIF][bif_read16]ERA=%x, RRA=%x\n", era, rra);
	/*set command sequence */
	bif_cmd[0] = SDA | MW3790;
	bif_cmd[1] = BC | RBL2;	/* read back 2 bytes */
	bif_cmd[2] = ERA | era;	/*[15:8] */
	bif_cmd[3] = RRA | rra;	/*[ 7:0] */

	bif_set_cmd(bif_cmd, 4);
	do {
		/*command setting : 4 transactions for 2 byte read command(1) */
		pmic_set_register_value(PMIC_BIF_TRASFER_NUM, 4);
		pmic_set_register_value(PMIC_BIF_COMMAND_TYPE, 1);
		pmic_set_register_value(PMIC_BIF_READ_EXPECT_NUM, 2);

		/*Command set trigger */
		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 1);

		udelay(200);
		/*Command sent; wait for slave */
		bif_waitfor_slave();

		/*Command clear trigger */
		pmic_set_register_value(PMIC_BIF_TRASACT_TRIGGER, 0);
		/*check transaction completeness */
		bat_lost = pmic_get_register_value(PMIC_BIF_BAT_LOST);
		total_valid = pmic_get_register_value(PMIC_BIF_TOTAL_VALID);
		timeout = pmic_get_register_value(PMIC_BIF_TIMEOUT);

		if (loop_i <= 50)
			loop_i++;
		else {
			battery_log(BAT_LOG_CRTI,
				    "[BIF][bif_read16] Failed. bat_lost = %d, timeout = %d, totoal_valid = %d\n",
				    bat_lost, timeout, total_valid);
			ret = -1;
			break;
		}
	} while (bat_lost == 1 || total_valid == 1 || timeout == 1);

	/*Read data */
	if (ret == 1) {
		int d0, d1;

		d0 = pmic_get_register_value(PMIC_BIF_DATA_0);
		d1 = pmic_get_register_value(PMIC_BIF_DATA_1);
		val = 0xFF & d1;
		val = val | ((d0 & 0xFF) << 8);
		battery_log(BAT_LOG_FULL, "[BIF][bif_read16] OK d0=0x%x, d1=0x%x for %d loop(s)\n",
			    d0, d1, loop_i);
	} else
		battery_log(BAT_LOG_CRTI, "[BIF][bif_read16] Failed for %d loop(s)\n", loop_i);

	/*reset BIF_IRQ */
	bif_reset_irq();


	return val;
}

void bif_ADC_enable(void)
{
	int reg = 0x18;

	bif_write8(0x0110, &reg);
	mdelay(50);

	reg = 0x98;
	bif_write8(0x0110, &reg);
	mdelay(50);

}

/* BIF init function called only at the first time*/
int bif_init(void)
{
	int pwr, rst;
	/*disable BIF interrupt */
	pmic_set_register_value(PMIC_INT_CON0_CLR, 0x4000);
	/*enable BIF clock */
	pmic_set_register_value(PMIC_TOP_CKPDN_CON2_CLR, 0x0070);

	/*enable HT protection */
	pmic_set_register_value(PMIC_RG_BATON_HT_EN, 1);

	/*change to HW control mode */
	/*pmic_set_register_value(MT6351_PMIC_RG_VBIF28_ON_CTRL, 0); */
	/*pmic_set_register_value(MT6351_PMIC_RG_VBIF28_EN, 1); */
	mdelay(50);

	/*Enable RX filter function */
	pmic_set_register_value(MT6351_PMIC_BIF_RX_DEG_EN, 0x8000);
	pmic_set_register_value(MT6351_PMIC_BIF_RX_DEG_WND, 0x17);
	pmic_set_register_value(PMIC_RG_BATON_EN, 0x1);
	pmic_set_register_value(PMIC_BATON_TDET_EN, 0x1);
	pmic_set_register_value(PMIC_RG_BATON_HT_EN_DLY_TIME, 0x1);


	/*wake up BIF slave */
	pwr = bif_powerup_slave();
	mdelay(10);
	rst = bif_reset_slave();

	/*pmic_set_register_value(MT6351_PMIC_RG_VBIF28_ON_CTRL, 1); */
	mdelay(50);

	battery_log(BAT_LOG_CRTI, "[BQ25896][BIF_init] done.");

	if (pwr + rst == 2)
		return 1;

	return -1;
}
#endif

/* Internal APIs for PowerSupply */
static int is_property_support(enum power_supply_property prop)
{
	int support = 0;
	int i;

	if (!external_charger)
		return 0;

	if (!external_charger->get_property)
		return 0;

	for(i = 0; i < external_charger->num_properties; i++) {
		if (external_charger->properties[i] == prop) {
			support = 1;
			break;
		}
	}

	return support;
}

static int is_property_writeable(enum power_supply_property prop)
{
	if (!external_charger->set_property)
		return 0;

	if (!external_charger->property_is_writeable)
		return 0;

	return external_charger->property_is_writeable(external_charger, prop);
}

static int get_property(enum power_supply_property prop, int *data)
{
	union power_supply_propval val;
	int rc = 0;

	*(int*)data = STATUS_UNSUPPORTED;

	if(!is_property_support(prop))
		return 0;

	rc = external_charger->get_property(external_charger, prop, &val);
	if (rc) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] failed to get property %d\n", prop);
		*(int*)data = 0;
		return rc;
	}

	*(int*)data = val.intval;

	battery_log(BAT_LOG_FULL, "[CHARGER] set property %d to %d\n", prop, val.intval);
	return rc;
}

static int set_property(enum power_supply_property prop, int data)
{
	union power_supply_propval val;
	int rc = 0;

	if (!is_property_writeable(prop))
		return 0;

	val.intval = data;
	rc = external_charger->set_property(external_charger, prop, &val);
	if (rc) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] failed to set property %d\n", prop);
	}
	battery_log(BAT_LOG_FULL, "[CHARGER] set property %d to %d\n", prop, data);
	return rc;
}

/* Charger Control Interface Handler */
static unsigned int charging_hw_init(void *data)
{
	static int hw_initialized = 0;

	if (hw_initialized)
		return STATUS_OK;

	bc11_set_register_value(PMIC_RG_USBDL_SET,0x0);//force leave USBDL mode
	bc11_set_register_value(PMIC_RG_USBDL_RST,0x1);//force leave USBDL mode

	hw_initialized = 1;
	battery_log(BAT_LOG_FULL, "[CHARGER] initialized.\n");

	return STATUS_OK;
}

static unsigned int charging_dump_register(void *data)
{

//	if(lge_get_board_revno() == HW_REV_0) {
//		pr_info("[CHARGER] HW_REV_0\n");
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
		/* rt9460_dump_register(); */
#endif
//	} else {
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
		/* rt9460_dump_register(); */
		return STATUS_OK;
#endif
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
		sm5424_dump_register();
#endif
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ25898S_SLAVE_SUPPORT)
		bq2589x_dump_register();
#endif
		/*
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24262_SUPPORT
		if(HW_REV_A <= lge_get_board_revno()) {
			bq24262_dump_register();
		}
#endif
*/
//	}
	return STATUS_OK;
}


#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24296_SUPPORT
static unsigned int set_cmd_dpm(void *data)
{
	int dpm = *(int*)(data);

	bq24296_set_dpm(dpm);

	return STATUS_OK;
}
#endif


static unsigned int charging_enable(void *data)
{
	int enable = 0;
	int rc = 0;

	enable = *(int*)(data);

	if ((enable == g_battery_charging_enabled) && (!g_charging_setting_changed))
		return STATUS_OK;

	if (!is_property_writeable(POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED)) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] cannot %s battery charging.\n",
				enable ? "enable" : "disable");
		return STATUS_UNSUPPORTED;
	}

	rc = set_property(POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, enable);
	if (rc) {
		battery_log(BAT_LOG_CRTI,
			"[CHARGER] failed to %s battery charging.(%d)\n",
			(enable ? "start" : "stop"), rc);
		return STATUS_UNSUPPORTED;
	}

	g_battery_charging_enabled = enable;

	/* clear charging setting */
	if (!g_battery_charging_enabled) {
		g_charging_current = 0;
		g_charging_current_limit = 0;
		g_charging_voltage = 0;
	}

	g_charging_setting_changed = 0;

	battery_log(BAT_LOG_CRTI, "[CHARGER] %s battery charging.\n",
				(g_battery_charging_enabled ? "start" : "stop"));

	return STATUS_OK;

}

static unsigned int charging_set_cv_voltage(void *data)
{
	int voltage = *(int*)(data);
	int rc = 0;

	if (voltage == g_charging_voltage)
		return STATUS_OK;

	rc = set_property(POWER_SUPPLY_PROP_VOLTAGE_MAX, voltage);
	if (rc) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] Set CV Voltage failed.(%d)\n", rc);
		return STATUS_UNSUPPORTED;
	}

	g_charging_voltage = voltage;
	g_charging_setting_changed = 1;

	battery_log(BAT_LOG_CRTI, "[CHARGER] Set CV Voltage to %duV\n", g_charging_voltage);

	return STATUS_OK;
}

static unsigned int charging_get_current(void *data)
{
	int cur;
	int rc = 0;

	if (!g_battery_charging_enabled) {
		*(int*)data = 0;
		return STATUS_OK;
	}

	rc = get_property(POWER_SUPPLY_PROP_CURRENT_NOW, &cur);
	if (rc)
		*(int*)data = min(g_charging_current, g_charging_current_limit);
	else if (cur < 0)
		*(int*)data = min(g_charging_current, g_charging_current_limit);
	else
		*(int*)data = cur;

	/* match unit with CHR_CURRENT_ENUM */
	*(int*)data *= 100;

	return STATUS_OK;
}

static unsigned int charging_set_current(void *data)
{
	enum power_supply_property prop = POWER_SUPPLY_PROP_CURRENT_NOW;
	int cur = *(int*)(data);
	int rc = 0;

	/* convert unit to mA */
	cur = cur / 100;

	/* charging current & current limit is not separated */
	if (!is_property_writeable(POWER_SUPPLY_PROP_CURRENT_NOW)) {
		prop = POWER_SUPPLY_PROP_CURRENT_MAX;

		/* use current limit value here */
		if (cur > g_charging_current_limit) {
			cur = g_charging_current_limit;
		}
	}

#if !defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
	if (cur == g_charging_current)
		return STATUS_OK;
#endif

	rc = set_property(prop, cur);
	if (rc) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] Set Current failed.(%d)\n", rc);
		return STATUS_UNSUPPORTED;
	}

	g_charging_current = cur;
	g_charging_setting_changed = 1;

	battery_log(BAT_LOG_CRTI, "[CHARGER] Set Current to %dmA\n", g_charging_current);

	return STATUS_OK;
}

static unsigned int charging_set_input_current(void *data)
{
	int cur = *(int*)(data);
	int rc = 0;

	/* convert unit to mA */
	cur = cur / 100;
	if (cur == g_charging_current_limit)
		return STATUS_OK;

	rc = set_property(POWER_SUPPLY_PROP_CURRENT_MAX, cur);
	if (rc) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] Set Current Limit failed.(%d)\n", rc);
		return STATUS_UNSUPPORTED;
	}

	g_charging_current_limit = cur;
	g_charging_setting_changed = 1;

	battery_log(BAT_LOG_CRTI, "[CHARGER] Set Current Limit to %dmA\n", g_charging_current_limit);

	return STATUS_OK;
}

#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
int set_otg_boost_mode(int enable)
{
	pr_debug("[OTG] set_otg_boost_mode\n");
	if (enable) {
		pr_info("[OTG] set_otg_boost_mode : enable\n");
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT)
		detect_otg_mode(1);
#endif
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
		sm5424_set_otg_boost_enable(1);
#endif
	} else {
		pr_info("[OTG] set_otg_boost_mode : disable\n");
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT)
		detect_otg_mode(0);
#endif
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
		sm5424_set_otg_boost_enable(0);
#endif
	}
	return STATUS_OK;
}

static unsigned int set_otg_boost_modes(void *data)
{
	int enable = *(int*)(data);

	set_otg_boost_mode(enable);

	return STATUS_OK;
}
#endif

static unsigned int charging_get_charging_status(void *data)
{
	int voltage = *(int*)data;
	int status;
	int rc = 0;

	battery_log(BAT_LOG_CRTI, "[CHARGER] %s: Check Start\n", __func__);

	if (g_charger_type == CHARGER_UNKNOWN) {
		*(int*)data = 0;
		return STATUS_OK;
	}

	rc = get_property(POWER_SUPPLY_PROP_STATUS, &status);
	if (rc) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] failed to get charging status.(%d)\n", rc);
		return STATUS_UNSUPPORTED;
	}

	/* if eoc check is not supported in charger ic, check battery voltage instead */
	if (status == STATUS_UNSUPPORTED) {
		/* battery voltage is invalid range */
		if (voltage > 5000) {
			*(int*)data = 0;
			return STATUS_OK;
		}
		if (voltage > RECHARGING_VOLTAGE)
			*(int*)data = 1;
		else
			*(int*)data = 0;

		return STATUS_OK;
	}

	if (status == POWER_SUPPLY_STATUS_FULL) {
		*(int*)data = 1;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
		first_eoc = 1;
#endif
	} else {
		*(int*)data = 0;
	}

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
	battery_log(BAT_LOG_CRTI, "[CHARGER] End of Charging : %d , SM5424 First EOC : %d\n", *(int*)data, first_eoc);
#else
	battery_log(BAT_LOG_CRTI, "[CHARGER] End of Charging : %d\n", *(int*)data);
#endif

	return STATUS_OK;
}

static unsigned int charging_reset_watch_dog_timer(void *data)
{
	/* nothing to do */
	return STATUS_OK;
}

static unsigned int charging_set_hv_threshold(void *data)
{
	/* nothing to do */
	return STATUS_OK;
}

static unsigned int charging_get_hv_status(void *data)
{
	/* nothing to do */
	*(kal_bool*)data = KAL_FALSE;
	return STATUS_OK;
}

static unsigned int charging_get_battery_status(void *data)
{
	bc11_set_register_value(PMIC_BATON_TDET_EN,1);
	bc11_set_register_value(PMIC_RG_BATON_EN,1);

	*(kal_bool*)(data) = bc11_get_register_value(PMIC_RGS_BATON_UNDET);

	return STATUS_OK;
}

/*Detect VBUS from SM5424 external charger ic reg*/
/*Before MT6353 VCDT initial setting*/
//#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
//extern int detect_vbus_ok(void);
//#endif

static unsigned int charging_get_charger_det_status(void *data)
{
#if 0  //Temporary block
	int online;
	int rc;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
	int intval = 0;
#endif
#endif

/*Detect VBUS from SM5424 external charger ic reg*/
/*Before MT6353 VCDT initial setting*/
//#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
//	int ex_chg_vbus;

//	ex_chg_vbus = detect_vbus_ok();
//	battery_log(BAT_LOG_FULL, "SM5424 Detect VBUS_OK=%d\n", ex_chg_vbus);
//#endif

	if (bc11_get_register_value(PMIC_RGS_CHRDET) == 0) {

/*Prevent for no detect to remove charger by JJB*/
/*Before MT6353 VCDT initial setting*/
//#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
//		if (ex_chg_vbus) {
//			*(kal_bool*)(data) = KAL_TRUE;
//			battery_log(BAT_LOG_FULL, "[CHARGER] SM5424 Detect VBUS_OK=%d\n", ex_chg_vbus);
//			return STATUS_OK;
//		} else {
//			battery_log(BAT_LOG_FULL, "[CHARGER] bc11_get_register_value is 0\n");
//			*(kal_bool*)(data) = KAL_FALSE;
//			g_charger_type = CHARGER_UNKNOWN;
//#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
//			first_eoc = 0;
//#endif
//		}
//#else
		pr_err("[CHARGER] bc11_get_register_value is 0\n");
		*(kal_bool*)(data) = KAL_FALSE;
		g_charger_type = CHARGER_UNKNOWN;
//#endif
	} else {
		*(kal_bool*)(data) = KAL_TRUE;
#if 0
		rc = get_property(POWER_SUPPLY_PROP_ONLINE, &online);
#ifndef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
		if (!online) {
			intval = 1;
			rc = set_property(POWER_SUPPLY_PROP_ONLINE, intval);
			if (rc) {
				battery_log(BAT_LOG_CRTI, "[CHARGER] cannot write online.\n");
				return STATUS_OK;
			}
			rc = get_property(POWER_SUPPLY_PROP_ONLINE, &online);
		}

#endif
#endif
#if 0
		pr_err("[CHARGER] online = %d\n", online);
		if (rc) {
			/* error reading online status. use pmic value */
			battery_log(BAT_LOG_CRTI, "[CHARGER] cannot read online.\n");
			return STATUS_OK;
		}

		if (!online) {
			pr_err("[CHARGER] online value is 0\n");
			/* OVP detected in external charger */
			*(kal_bool*)(data) = KAL_FALSE;
			g_charger_type = CHARGER_UNKNOWN;
		}
#endif
	}

	battery_log(BAT_LOG_FULL, "[CHARGER] g_charger_type = %d\n", g_charger_type);
#if 0
	/* for log */
	switch ((int)g_charger_type)
	{
		case CHARGER_UNKNOWN:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "CHARGER_UNKNOWN");
			break;
		case STANDARD_HOST:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "STANDARD_HOST");
			break;
		case CHARGING_HOST:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "CHARGING_HOST");
			break;
		case NONSTANDARD_CHARGER:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "NONSTANDARD_CHARGER");
			break;
		case STANDARD_CHARGER:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "STANDARD_CHARGER");
			break;
		case APPLE_2_1A_CHARGER:
		case APPLE_1_0A_CHARGER:
		case APPLE_0_5A_CHARGER:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "APPLE_CHARGER");
			break;
		case WIRELESS_CHARGER:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "WIRELESS_CHARGER");
			break;
		default:
			pr_err("[CHARGER] g_charger_type = %d, %s\n", g_charger_type, "DEFAULT");

	}
#endif
	return STATUS_OK;
}

extern int hw_charging_get_charger_type(void);

static unsigned int charging_get_charger_type(void *data)
{
	unsigned int status = STATUS_OK;
#if defined(CONFIG_POWER_EXT)
	*(CHARGER_TYPE*)(data) = STANDARD_HOST;
#else

#ifdef CONFIG_LGE_PM
	if (g_charger_type != CHARGER_UNKNOWN &&
		g_charger_type != WIRELESS_CHARGER && g_charger_type != NONSTANDARD_CHARGER)
#else
	if (g_charger_type != CHARGER_UNKNOWN && g_charger_type != WIRELESS_CHARGER)
#endif
	{
		*(CHARGER_TYPE*)(data) = g_charger_type;
		battery_log(BAT_LOG_CRTI, "[CHARGER] return %d!\n", g_charger_type);

		return status;
	}

	charging_type_det_done = KAL_FALSE;

	g_charger_type = hw_charging_get_charger_type();

	charging_type_det_done = KAL_TRUE;

	*(CHARGER_TYPE*)(data) = g_charger_type;
#endif

	return STATUS_OK;
}

static unsigned int charging_get_is_pcm_timer_trigger(void *data)
{
	unsigned int status = STATUS_OK;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SUPPORT
	/* skip code */
#else
	if(slp_get_wake_reason() == WR_PCM_TIMER)
		*(kal_bool*)(data) = KAL_TRUE;
	else
		*(kal_bool*)(data) = KAL_FALSE;

	battery_log(BAT_LOG_CRTI, "[CHARGER] slp_get_wake_reason=%d\n",
		slp_get_wake_reason());
#endif
	return status;
}

static unsigned int charging_set_platform_reset(void *data)
{
	battery_log(BAT_LOG_CRTI, "[CHARGER] charging_set_platform_reset\n");

	kernel_restart("battery service reboot system");

	return STATUS_OK;
}

static unsigned int charging_get_platform_boot_mode(void *data)
{
	*(unsigned int*)(data) = get_boot_mode();

	battery_log(BAT_LOG_CRTI, "[CHARGER] get_boot_mode=%d\n", get_boot_mode());

	return STATUS_OK;
}

static unsigned int charging_set_power_off(void *data)
{
	unsigned int status = STATUS_OK;

	battery_log(BAT_LOG_CRTI, "[CHARGER] charging_set_power_off\n");
	kernel_power_off();

	return status;
}

static unsigned int charging_get_power_source(void *data)
{
	unsigned int status = STATUS_OK;

	*(kal_bool *)data = KAL_FALSE;

	return status;
}
static unsigned int charging_get_csdac_full_flag(void *data)
{
	unsigned int status = STATUS_OK;

	*(kal_bool *)data = KAL_FALSE;

	return status;
}
static unsigned int charging_set_ta_current_pattern(void *data)
{
	unsigned int status = STATUS_OK;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_FAN5451x_SUPPORT
	unsigned int increase = *(unsigned int *) (data);

	if (increase == KAL_TRUE)
		fan5451x_pe_plus_ta_pattern_increase();
	else
		fan5451x_pe_plus_ta_pattern_decrease();
#endif
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
	unsigned int increase = *(unsigned int *) (data);

	battery_log(BAT_LOG_CRTI, "%s: [PE+] Start\n", __func__);

	if (increase == KAL_TRUE)
		sm5424_pe_plus_ta_pattern_increase();
	else
		sm5424_pe_plus_ta_pattern_decrease();

	battery_log(BAT_LOG_CRTI, "%s: [PE+] End\n", __func__);
#endif
	return status;
}

static unsigned int charging_set_ta20_current_pattern(void *data)
{
	unsigned int status = STATUS_OK;
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
	unsigned int increase = *(unsigned int *) (data);

	battery_log(BAT_LOG_CRTI, "%s: [PE+2.0] Start\n", __func__);

	if (increase == KAL_TRUE)
		sm5424_pe_plus_20_ta_pattern_increase();
	else
		sm5424_pe_plus_20_ta_pattern_decrease();

	battery_log(BAT_LOG_CRTI, "%s: [PE+2.0] End\n", __func__);
#endif
	return status;
}

static unsigned int charging_set_error_state(void *data)
{
	return STATUS_UNSUPPORTED;
}

static unsigned int(*charging_func[CHARGING_CMD_NUMBER]) (void *data);

static unsigned int charging_diso_init(void *data)
{
        unsigned int status = STATUS_OK;

#if defined(MTK_DUAL_INPUT_CHARGER_SUPPORT)
        struct device_node *node;
        DISO_ChargerStruct *pDISO_data = (DISO_ChargerStruct *) data;

        int ret;
        /* Initialization DISO Struct */
        pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
        pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
        pDISO_data->diso_state.cur_vdc_state = DISO_OFFLINE;

        pDISO_data->diso_state.pre_otg_state = DISO_OFFLINE;
        pDISO_data->diso_state.pre_vusb_state = DISO_OFFLINE;
        pDISO_data->diso_state.pre_vdc_state = DISO_OFFLINE;

        pDISO_data->chr_get_diso_state = KAL_FALSE;

        pDISO_data->hv_voltage = VBUS_MAX_VOLTAGE;

        /* Initial AuxADC IRQ */
        DISO_IRQ.vdc_measure_channel.number = AP_AUXADC_DISO_VDC_CHANNEL;
        DISO_IRQ.vusb_measure_channel.number = AP_AUXADC_DISO_VUSB_CHANNEL;
        DISO_IRQ.vdc_measure_channel.period = AUXADC_CHANNEL_DELAY_PERIOD;
        DISO_IRQ.vusb_measure_channel.period = AUXADC_CHANNEL_DELAY_PERIOD;
        DISO_IRQ.vdc_measure_channel.debounce = AUXADC_CHANNEL_DEBOUNCE;
        DISO_IRQ.vusb_measure_channel.debounce = AUXADC_CHANNEL_DEBOUNCE;

        /* use default threshold voltage, if use high voltage,maybe refine */
        DISO_IRQ.vusb_measure_channel.falling_threshold = VBUS_MIN_VOLTAGE / 1000;
        DISO_IRQ.vdc_measure_channel.falling_threshold = VDC_MIN_VOLTAGE / 1000;
        DISO_IRQ.vusb_measure_channel.rising_threshold = VBUS_MIN_VOLTAGE / 1000;
        DISO_IRQ.vdc_measure_channel.rising_threshold = VDC_MIN_VOLTAGE / 1000;

        node = of_find_compatible_node(NULL, NULL, "mediatek,AUXADC");
        if (!node) {
                battery_log(BAT_LOG_CRTI, "[diso_adc]: of_find_compatible_node failed!!\n");
        } else {
                pDISO_data->irq_line_number = irq_of_parse_and_map(node, 0);
                battery_log(BAT_LOG_FULL, "[diso_adc]: IRQ Number: 0x%x\n",
                            pDISO_data->irq_line_number);
        }

        mt_irq_set_sens(pDISO_data->irq_line_number, MT_EDGE_SENSITIVE);
        mt_irq_set_polarity(pDISO_data->irq_line_number, MT_POLARITY_LOW);

        ret = request_threaded_irq(pDISO_data->irq_line_number, diso_auxadc_irq_handler,
                                   pDISO_data->irq_callback_func, IRQF_ONESHOT, "DISO_ADC_IRQ",
                                   NULL);

        if (ret) {
                battery_log(BAT_LOG_CRTI, "[diso_adc]: request_irq failed.\n");
        } else {
                set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
                set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
                battery_log(BAT_LOG_FULL, "[diso_adc]: diso_init success.\n");
        }

#if defined(MTK_DISCRETE_SWITCH) && defined(MTK_DSC_USE_EINT)
        battery_log(BAT_LOG_CRTI, "[diso_eint]vdc eint irq registitation\n");
        mt_eint_set_hw_debounce(CUST_EINT_VDC_NUM, CUST_EINT_VDC_DEBOUNCE_CN);
        mt_eint_registration(CUST_EINT_VDC_NUM, CUST_EINTF_TRIGGER_LOW, vdc_eint_handler, 0);
        mt_eint_mask(CUST_EINT_VDC_NUM);
#endif
#endif

        return status;
}

static unsigned int charging_get_diso_state(void *data)
{
        unsigned int status = STATUS_OK;

#if defined(MTK_DUAL_INPUT_CHARGER_SUPPORT)
        int diso_state = 0x0;
        DISO_ChargerStruct *pDISO_data = (DISO_ChargerStruct *) data;

        _get_diso_interrupt_state();
        diso_state = g_diso_state;
        battery_log(BAT_LOG_FULL, "[do_chrdet_int_task] current diso state is %s!\n",
                    DISO_state_s[diso_state]);
        if (((diso_state >> 1) & 0x3) != 0x0) {
                switch (diso_state) {
                case USB_ONLY:
                        set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
                        set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
#ifdef MTK_DISCRETE_SWITCH
#ifdef MTK_DSC_USE_EINT
                        mt_eint_unmask(CUST_EINT_VDC_NUM);
#else
                        set_vdc_auxadc_irq(DISO_IRQ_ENABLE, 1);
#endif
#endif
                        pDISO_data->diso_state.cur_vusb_state = DISO_ONLINE;
                        pDISO_data->diso_state.cur_vdc_state = DISO_OFFLINE;
                        pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
                        break;
                case DC_ONLY:
                        set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
                        set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
                        set_vusb_auxadc_irq(DISO_IRQ_ENABLE, DISO_IRQ_RISING);
                        pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
                        pDISO_data->diso_state.cur_vdc_state = DISO_ONLINE;
                        pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
                        break;
                case DC_WITH_USB:
                        set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
                        set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
                        set_vusb_auxadc_irq(DISO_IRQ_ENABLE, DISO_IRQ_FALLING);
                        pDISO_data->diso_state.cur_vusb_state = DISO_ONLINE;
                        pDISO_data->diso_state.cur_vdc_state = DISO_ONLINE;
                        pDISO_data->diso_state.cur_otg_state = DISO_OFFLINE;
                        break;
                case DC_WITH_OTG:
                        set_vdc_auxadc_irq(DISO_IRQ_DISABLE, 0);
                        set_vusb_auxadc_irq(DISO_IRQ_DISABLE, 0);
                        pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
                        pDISO_data->diso_state.cur_vdc_state = DISO_ONLINE;
                        pDISO_data->diso_state.cur_otg_state = DISO_ONLINE;
                        break;
                default:        /* OTG only also can trigger vcdt IRQ */
                        pDISO_data->diso_state.cur_vusb_state = DISO_OFFLINE;
                        pDISO_data->diso_state.cur_vdc_state = DISO_OFFLINE;
                        pDISO_data->diso_state.cur_otg_state = DISO_ONLINE;
                        battery_log(BAT_LOG_FULL, " switch load vcdt irq triggerd by OTG Boost!\n");
                        break;  /* OTG plugin no need battery sync action */
                }
        }

        if (DISO_ONLINE == pDISO_data->diso_state.cur_vdc_state)
                pDISO_data->hv_voltage = VDC_MAX_VOLTAGE;
        else
                pDISO_data->hv_voltage = VBUS_MAX_VOLTAGE;
#endif

        return status;
}

static unsigned int charging_set_vindpm(void *data)
{
	unsigned int status = STATUS_OK;
	//      unsigned int v = *(unsigned int *) data;
	//
	//      //      bq25890_set_VINDPM(v);

	return status;
}

static unsigned int charging_set_vbus_ovp_en(void *data)
{
        unsigned int status = STATUS_OK;
//        unsigned int e = *(unsigned int *) data;

//        pmic_set_register_value(MT6351_PMIC_RG_VCDT_HV_EN, e);

        return status;
}

static unsigned int charging_get_bif_vbat(void *data)
{
        unsigned int status = STATUS_OK;
#ifdef CONFIG_MTK_BIF_SUPPORT
        int vbat = 0;

        /* turn on VBIF28 regulator*/
        /*bif_init();*/

        /*change to HW control mode*/
        /*pmic_set_register_value(MT6351_PMIC_RG_VBIF28_ON_CTRL, 0);
 *         pmic_set_register_value(MT6351_PMIC_RG_VBIF28_EN, 1);*/
        if (bif_checked != 1 || bif_exist == 1) {
                bif_ADC_enable();

                vbat = bif_read16(MW3790_VBAT);
                *(unsigned int *) (data) = vbat;
        }
        /*turn off LDO and change SW control back to HW control */
        /*pmic_set_register_value(MT6351_PMIC_RG_VBIF28_EN, 0);
 *         pmic_set_register_value(MT6351_PMIC_RG_VBIF28_ON_CTRL, 1);*/
#else
        *(unsigned int *) (data) = 0;
#endif
        return status;
}
/*
static unsigned int charging_set_chrind_ck_pdn(void *data)
{
        unsigned int status = STATUS_OK;
        unsigned int pwr_dn;

        pwr_dn = *(unsigned int *) data;

        pmic_set_register_value(PMIC_RG_DRV_CHRIND_CK_PDN, pwr_dn);

        return status;
}
*/
static unsigned int charging_sw_init(void *data)
{
        unsigned int status = STATUS_OK;
        /*put here anything needed to be init upon battery_common driver probe*/
#ifdef CONFIG_MTK_BIF_SUPPORT
        int vbat;

        vbat = 0;
        if (bif_checked != 1) {
                bif_init();
                charging_get_bif_vbat(&vbat);
                if (vbat != 0) {
                        battery_log(BAT_LOG_CRTI, "[BIF]BIF battery detected.\n");
                        bif_exist = 1;
                } else
                        battery_log(BAT_LOG_CRTI, "[BIF]BIF battery _NOT_ detected.\n");
                bif_checked = 1;
        }
#endif
        return status;
}

static unsigned int charging_enable_safetytimer(void *data)
{

        unsigned int status = STATUS_OK;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SAFETY_TIMER_SUPPORT
        unsigned int en;
	int rc = 0;

        en = *(unsigned int *) data;

	rc = set_property(POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE, en);
	if (rc) {
		battery_log(BAT_LOG_CRTI, "[CHARGER] Set Safety timer failed.(%d)\n", rc);
		status = STATUS_UNSUPPORTED;
	} else
		battery_log(BAT_LOG_CRTI, "[CHARGER] Set Safety timer : %s\n",
				en ? "enabled" : "disabled");
#endif
	return status;
}

static unsigned int charging_set_hiz_swchr(void *data)
{
	int enable = 0;
	int rc = 0;

	enable = *(int *) data;

	if (enable == g_charging_enabled)
		return STATUS_OK;

	/*
	* Do not disable charging when battery disconnected
	*/
	if (!enable && bc11_get_register_value(PMIC_RGS_BATON_UNDET))
		return STATUS_OK;

	rc = set_property(POWER_SUPPLY_PROP_CHARGING_ENABLED, enable);
	if (rc) {
		battery_log(BAT_LOG_CRTI,
			"[CHARGER] failed to %s charging.(%d)\n",
			(enable ? "start" : "stop"), rc);
		return STATUS_UNSUPPORTED;
	}

	g_charging_enabled = enable;

	/* clear charging setting */
	if (!g_charging_enabled) {
		g_charging_current = 0;
		g_charging_current_limit = 0;
		g_charging_voltage = 0;
	}

	battery_log(BAT_LOG_CRTI, "[CHARGER] %s charging.\n",
				(g_charging_enabled ? "start" : "stop"));

	return STATUS_OK;
}

static unsigned int charging_get_bif_tbat(void *data)
{
        unsigned int status = STATUS_OK;
#ifdef CONFIG_MTK_BIF_SUPPORT
        int tbat = 0;
        int ret;
        int tried = 0;

        mdelay(50);

        if (bif_exist == 1) {
                do {
                        bif_ADC_enable();
                        ret = bif_read8(MW3790_TBAT, &tbat);
                        tried++;
                        mdelay(50);
                        if (tried > 3)
                                break;
                } while (ret != 1);

                if (tried <= 3)
                        *(int *) (data) = tbat;
                else
                        status =  STATUS_UNSUPPORTED;
        }
#endif
        return status;
}


/*
 * FUNCTION
 *		Internal_chr_control_handler
 *
 * DESCRIPTION
 *		 This function is called to set the charger hw
 *
 * CALLS
 *
 * PARAMETERS
 *		None
 *
 * RETURNS
 *
 *
 * GLOBALS AFFECTED
 *	   None
 */
signed int chr_control_interface(CHARGING_CTRL_CMD cmd, void *data)
{
	signed int status;
	int i;

	static int chr_control_interface_init = 0;

	if (!chr_control_interface_init) {
		charging_func[CHARGING_CMD_INIT] = charging_hw_init;
		charging_func[CHARGING_CMD_DUMP_REGISTER] = charging_dump_register;
		charging_func[CHARGING_CMD_ENABLE] = charging_enable;
		charging_func[CHARGING_CMD_SET_CV_VOLTAGE] = charging_set_cv_voltage;
		charging_func[CHARGING_CMD_GET_CURRENT] = charging_get_current;
		charging_func[CHARGING_CMD_SET_CURRENT] = charging_set_current;
		charging_func[CHARGING_CMD_SET_INPUT_CURRENT] = charging_set_input_current;
		charging_func[CHARGING_CMD_GET_CHARGING_STATUS] = charging_get_charging_status;
		charging_func[CHARGING_CMD_RESET_WATCH_DOG_TIMER] = charging_reset_watch_dog_timer;
		charging_func[CHARGING_CMD_SET_HV_THRESHOLD] = charging_set_hv_threshold;
		charging_func[CHARGING_CMD_GET_HV_STATUS] = charging_get_hv_status;
		charging_func[CHARGING_CMD_GET_BATTERY_STATUS] = charging_get_battery_status;
		charging_func[CHARGING_CMD_GET_CHARGER_DET_STATUS] = charging_get_charger_det_status;
		charging_func[CHARGING_CMD_GET_CHARGER_TYPE] = charging_get_charger_type;
		charging_func[CHARGING_CMD_GET_IS_PCM_TIMER_TRIGGER] = charging_get_is_pcm_timer_trigger;
		charging_func[CHARGING_CMD_SET_PLATFORM_RESET] = charging_set_platform_reset;
		charging_func[CHARGING_CMD_GET_PLATFORM_BOOT_MODE] = charging_get_platform_boot_mode;
		charging_func[CHARGING_CMD_SET_POWER_OFF] = charging_set_power_off;
		charging_func[CHARGING_CMD_GET_POWER_SOURCE] = charging_get_power_source;
		charging_func[CHARGING_CMD_GET_CSDAC_FALL_FLAG] = charging_get_csdac_full_flag;
		charging_func[CHARGING_CMD_SET_TA_CURRENT_PATTERN] = charging_set_ta_current_pattern;
		charging_func[CHARGING_CMD_SET_TA20_CURRENT_PATTERN] = charging_set_ta20_current_pattern;
		charging_func[CHARGING_CMD_SET_ERROR_STATE] = charging_set_error_state;
		charging_func[CHARGING_CMD_DISO_INIT] = charging_diso_init;
		charging_func[CHARGING_CMD_GET_DISO_STATE] = charging_get_diso_state;
		charging_func[CHARGING_CMD_SET_VINDPM] = charging_set_vindpm;
		charging_func[CHARGING_CMD_SET_VBUS_OVP_EN] = charging_set_vbus_ovp_en;
		charging_func[CHARGING_CMD_GET_BIF_VBAT] = charging_get_bif_vbat;
//		charging_func[CHARGING_CMD_SET_CHRIND_CK_PDN] = charging_set_chrind_ck_pdn;
		charging_func[CHARGING_CMD_SW_INIT] = charging_sw_init;
		charging_func[CHARGING_CMD_ENABLE_SAFETY_TIMER] = charging_enable_safetytimer;
		charging_func[CHARGING_CMD_SET_HIZ_SWCHR] = charging_set_hiz_swchr;
		charging_func[CHARGING_CMD_GET_BIF_TBAT] = charging_get_bif_tbat;
#ifdef CONFIG_LGE_PM_OTG_BOOST_MODE
		charging_func[CHARGING_CMD_SET_OTG_BOOST_MODE] = set_otg_boost_modes;
#endif
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24296_SUPPORT
		charging_func[CHARGING_CMD_SET_DPM] = set_cmd_dpm;
#endif

		chr_control_interface_init = 1;
	}


	if (!external_charger) {
		/* find charger */
		if(lge_get_board_revno() == HW_REV_0) {
			pr_info("HW_REV_0\n");
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
			external_charger = power_supply_get_by_name("rt-charger");
#elif defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
			external_charger = power_supply_get_by_name("sm5424");
#endif
		} else {
			pr_info("Not HW_REV_0\n");
			for (i = 0; i < ARRAY_SIZE(support_charger); i++) {
				external_charger = power_supply_get_by_name(support_charger[i]);
				if (external_charger) {
					battery_log(BAT_LOG_CRTI, "[CHARGER] found charger %s\n",
							support_charger[i]);
					break;
				}
			}
		}

		/* if not found, cannot control charger */
		if (!external_charger) {
			battery_log(BAT_LOG_CRTI, "[CHARGER] failed to get external_charger.\n");
			return STATUS_UNSUPPORTED;
		}
	}

	if (!charging_func)
		return STATUS_UNSUPPORTED;

	status = charging_func[cmd](data);

	return status;
}

