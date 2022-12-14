/*
* sm5424.c -- 3.5A Input, Switch Mode Charger device driver
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>

#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/errno.h>
#include <linux/switch.h>
#include <linux/timer.h>
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#include <mt-plat/charging.h>
#include <mach/mt_charging.h>
#include <mt_gpio.h>
#include <mach/gpio_const.h>

#include "sm5424.h"

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ25898S_SLAVE_SUPPORT
#include <mt-plat/battery_common.h>
#include "bq25898s_reg.h"
extern int old_bl_level;
extern PMU_ChargerStruct BMT_status;
#endif

typedef unsigned char BYTE;

extern kal_bool chargin_hw_init_done;

#define SM5424_DEV_NAME "sm5424"

#define VBUSLIMIT_MIN_MA	100
#define VBUSLIMIT_1STEP_MA	375
#define VBUSLIMIT_MAX_MA	3525
#define VBUSLIMIT_STEP_MA	25

#define FASTCHG_MIN_MA	350
#define FASTCHG_MAX_MA	3500
#define FASTCHG_STEP_MA	50

#define BATREG_MIN_MV	3990
#define BATREG_MAX_MV	4620
#define BATREG_STEP_MV	10

#define TOPOFF_MIN_MV	100
#define TOPOFF_MAX_MV	475
#define TOPOFF_STEP_MV	25

/* ============================================================ // */
/* Define */
/* ============================================================ // */
#define STATUS_OK    0
#define STATUS_UNSUPPORTED    -1
#define STATUS_FAIL -2

static struct i2c_board_info __initdata i2c_sm5424={I2C_BOARD_INFO(SM5424_DEV_NAME, 0x6B)};

static struct i2c_client *new_client = NULL;

unsigned char sm5424_reg[SM5424_REG_NUM] = { 0 };

static unsigned int att_addr = SM5424_INTMASK1;

static const struct i2c_device_id sm5424_i2c_id[] = {{SM5424_DEV_NAME, 0},{}};

static const struct of_device_id sm5424_of_match[] = {{.compatible = "sm,sm5424",},{},};

static enum power_supply_property sm5424_charger_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX
};

struct sm5424_info {
	struct i2c_client *i2c;
	struct mutex mutex;
	int irq;
	struct power_supply sm_psy;
	struct class *fan_class;

	int dis_set;

	int fastchg;
	int batreg;
	int vbuslimit;
	int total_chr_current;

	int status;
	int enable;
	int suspend;
};

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ25898S_SLAVE_SUPPORT
int Ibat_Miti(void)
{
int ibat_max = 2600;

	if(BMT_status.temperature >= 40) return (ibat_max-200);
	else return (ibat_max);
}
#endif

static int sm5424_read_reg(struct i2c_client *i2c, BYTE reg, BYTE *dest)
{
	int ret;
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret < 0) {
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:reg(0x%x), ret(%d)\n", __func__, reg, ret);
		return ret;
	}
	ret &= 0xff;
	*dest = ret;
	return 0;
}

static int sm5424_write_reg(struct i2c_client *i2c, BYTE reg, BYTE value)
{
	int ret;
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
	if (ret < 0)
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:reg(0x%x), ret(%d)\n", __func__, reg, ret);
	return ret;
}

static int sm5424_update_reg(struct i2c_client *i2c, u8 reg, BYTE val, BYTE mask)
{
	int ret;
	ret = i2c_smbus_read_byte_data(i2c, reg);
	if (ret >= 0) {
		BYTE old_val = ret & 0xff;
		BYTE new_val = (val & mask) | (old_val & (~mask));
		ret = i2c_smbus_write_byte_data(i2c, reg, new_val);
	}
	return ret;
}

static int sm5424_get_chgset(struct sm5424_info *info)
{
	u8 read_reg;
	int ret;

	sm5424_read_reg(new_client, SM5424_STATUS2, &read_reg);

	ret = ((read_reg & SM5424_STATUS2_CHGON) >> SM5424_STATUS2_SHIFT);

	pr_info("[sm5424] %s: STATUS2 : 0x%02X CHGON = %d \n", __func__, read_reg, ret);

	if (ret)
		return 1;
	else
		return 0;
}

static int sm5424_get_vbusok(struct sm5424_info *info)
{
	u8 read_reg_00;
	BYTE ret = 0;

	sm5424_read_reg(new_client, SM5424_STATUS1, &read_reg_00);
	ret = ((read_reg_00 & SM5424_STATUS1_VBUSPOK) >> SM5424_STATUS1_SHIFT);

	pr_info("[sm5424] %s: STATUS1 : 0x%02X VBUSPOK = %d\n", __func__, read_reg_00, ret);

	if (ret)
		return 1;
	else
		return 0;
}

#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
int detect_vbus_ok(void)
{
	u8 read_reg_00;
	u8 read_reg_01;
	int ret_vbus = 0;
	int ret_chgon = 0;

	sm5424_read_reg(new_client, SM5424_STATUS1, &read_reg_00);
	sm5424_read_reg(new_client, SM5424_STATUS2, &read_reg_01);

	ret_vbus = (read_reg_00 & (SM5424_STATUS1_VBUSPOK << SM5424_STATUS1_SHIFT));
	ret_chgon = (read_reg_01 & (SM5424_STATUS2_CHGON << SM5424_STATUS2_SHIFT));

/*
	pr_debug("[sm5424] %s: STATUS1 : 0x%02X STATUS2 : 0x%02X \n", __func__, read_reg_00, read_reg_01);
	pr_debug("[sm5424] %s: VBUSPOK = %d CHGON = %d\n", __func__, ret_vbus, ret_chgon);
*/
	return ret_vbus;
}
#endif

static int sm5424_get_chgcmp(struct sm5424_info *info)
{
	u8 read_reg;
	int ret;;

	sm5424_read_reg(new_client, SM5424_STATUS2, &read_reg);

	ret = (read_reg & SM5424_STATUS2_TOPOFF);

	pr_info("[sm5424] %s: STATUS2 : 0x%02X TOPOFF = %d \n", __func__, read_reg, ret);

	if(ret)
		return 1;
	else
		return 0;
}

static int sm5424_get_status(struct sm5424_info *info)
{
	int status = 0;

	if (sm5424_get_chgcmp(info)) {
		pr_info("[sm5424] %s: POWER_SUPPLY_STATUS_FULL\n", __func__);
		status = POWER_SUPPLY_STATUS_FULL;
	} else {
		if (sm5424_get_chgset(info)) {
			pr_info("[sm5424] %s: POWER_SUPPLY_STATUS_CHARGING\n", __func__);
			status = POWER_SUPPLY_STATUS_CHARGING;
		} else {
			pr_info("[sm5424] %s: POWER_SUPPLY_STATUS_NOT_CHARGING\n", __func__);
			status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
	}
	return status;
}

static int sm5424_set_fastchg(struct sm5424_info *info, int ma)
{
	int reg_val;

	if (ma < FASTCHG_MIN_MA)
		ma = FASTCHG_MIN_MA;
	else if(ma > FASTCHG_MAX_MA)
		ma = FASTCHG_MAX_MA;

	reg_val = (ma - FASTCHG_MIN_MA)/FASTCHG_STEP_MA;
	info->fastchg = reg_val*FASTCHG_STEP_MA + FASTCHG_MIN_MA;

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ25898S_SLAVE_SUPPORT
	if(bq25898s_charger_en == 1) info->fastchg = info->fastchg*2;
#endif

	battery_log(BAT_LOG_CRTI, "[sm5424] set_fastchg: 0x%x\n", reg_val);

	return sm5424_write_reg(info->i2c, SM5424_CHGCNTL2, reg_val);
}

static int sm5424_set_batreg(struct sm5424_info *info, int mv)
{
	int reg_val;

	if (mv < BATREG_MIN_MV)
		mv = BATREG_MIN_MV;
	else if(mv > BATREG_MAX_MV)
		mv = BATREG_MAX_MV;

	reg_val = (mv - BATREG_MIN_MV) / BATREG_STEP_MV;
	info->batreg = reg_val * BATREG_STEP_MV + BATREG_MIN_MV;

	battery_log(BAT_LOG_CRTI, "[sm5424] set_batreg: 0x%x\n", reg_val);

	return sm5424_write_reg(info->i2c, SM5424_CHGCNTL3, reg_val);
}

static int sm5424_set_vbuslimit(struct sm5424_info *info, int ma)
{
	int reg_val;

	if (ma < VBUSLIMIT_MIN_MA)
		ma = VBUSLIMIT_MIN_MA;
	else if(ma > VBUSLIMIT_MAX_MA)
		ma = VBUSLIMIT_MAX_MA;

	if (ma < VBUSLIMIT_1STEP_MA)
		reg_val = VBUSLIMIT_100mA;
	else
		reg_val = ((ma - VBUSLIMIT_1STEP_MA) / VBUSLIMIT_STEP_MA)+ VBUSLIMIT_375mA;

	info->vbuslimit = reg_val * VBUSLIMIT_STEP_MA + VBUSLIMIT_1STEP_MA;
	battery_log(BAT_LOG_CRTI, "[sm5424] set_vbuslimit : 0x%x\n", reg_val);
	return sm5424_write_reg(info->i2c, SM5424_VBUSCNTL, reg_val);
}

static int sm5424_enable(struct sm5424_info *info, int enable)
{
	if (enable) {
		sm5424_update_reg(info->i2c, SM5424_CNTL, CHARGE_EN << SM5424_CNTL_CHGEN_SHIFT,
			SM5424_CNTL_CHGEN_MASK << SM5424_CNTL_CHGEN_SHIFT);
	}
	else {
		sm5424_update_reg(info->i2c, SM5424_CNTL, CHARGE_DIS << SM5424_CNTL_CHGEN_SHIFT,
			SM5424_CNTL_CHGEN_MASK << SM5424_CNTL_CHGEN_SHIFT);
	}

#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ25898S_SLAVE_SUPPORT
	if(enable==0) bq2589x_adapter_out_handler();
#endif

	battery_log(BAT_LOG_CRTI, "[sm5424] enable : %d\n", enable);

	return 0;
}

static int sm5424_suspend(struct sm5424_info *info, int suspend)
{
	if (suspend){
		sm5424_update_reg(info->i2c, SM5424_CNTL, SUSPEND_EN << SM5424_CNTL_SUSPEND_SHIFT,
			SM5424_CNTL_SUSPEND_MASK << SM5424_CNTL_SUSPEND_SHIFT);
	} else {
		sm5424_update_reg(info->i2c, SM5424_CNTL, SUSPEND_DIS << SM5424_CNTL_SUSPEND_SHIFT,
			SM5424_CNTL_SUSPEND_MASK << SM5424_CNTL_SUSPEND_SHIFT);
	}
	battery_log(BAT_LOG_CRTI, "[sm5424] suspend : %d\n", suspend);

	return 0;
}

static inline unsigned char _calc_topoff_current_offset_to_mA(unsigned short mA)
{
	unsigned char offset;

	if (mA < TOPOFF_MIN_MV) {
		offset = TOPOFF_100mA;               /* Topoff = 100mA */
	} else if (mA < TOPOFF_MAX_MV) {
		offset = ((mA - TOPOFF_MIN_MV) / TOPOFF_STEP_MV) & SM5424_CHGCNTL4_TOPOFF_MASK;   /* Topoff = 125mA ~ 450mA in 25mA steps */
	} else {
		offset = TOPOFF_475mA;              /* Topoff = 475mA */
	}

	return offset;
}

static int sm5424_set_TOPOFF(struct sm5424_info *info, u8 val)
{
 //   u8 offset = _calc_topoff_current_offset_to_mA(topoff_mA);

    sm5424_update_reg(info->i2c, SM5424_CHGCNTL4, ((val & SM5424_CHGCNTL4_TOPOFF_MASK) << SM5424_CHGCNTL4_TOPOFF_SHIFT), (SM5424_CHGCNTL4_TOPOFF_MASK << SM5424_CHGCNTL4_TOPOFF_SHIFT));

	battery_log(BAT_LOG_CRTI, "[sm5424] set_TOPOFF : 0x%x\n", val);

    return 0;
}

static int sm5424_set_AICLEN(struct sm5424_info *info, bool enable)
{
    sm5424_update_reg(info->i2c, SM5424_CHGCNTL1, ((enable & SM5424_CHGCNTL1_AICLEN_MASK) << SM5424_CHGCNTL1_AICLEN_SHIFT), (SM5424_CHGCNTL1_AICLEN_MASK << SM5424_CHGCNTL1_AICLEN_SHIFT));

	battery_log(BAT_LOG_CRTI, "[sm5424] set_AICLEN : %d\n", enable);

    return 0;
}

static int sm5424_set_AICLTH(struct sm5424_info *info, u8 val)
{
    sm5424_update_reg(info->i2c, SM5424_CHGCNTL1, ((val & SM5424_CHGCNTL1_AICLTH_MASK) << SM5424_CHGCNTL1_AICLTH_SHIFT), (SM5424_CHGCNTL1_AICLTH_MASK << SM5424_CHGCNTL1_AICLTH_SHIFT));

	battery_log(BAT_LOG_CRTI, "[sm5424] set_AICLTH : %d\n", val);

    return 0;
}
static int sm5424_set_AUTOSTOP(struct sm5424_info *info, bool enable)
{
    sm5424_update_reg(info->i2c, SM5424_CNTL, ((enable & SM5424_CNTL_AUTOSTOP_MASK) << SM5424_CNTL_AUTOSTOP_SHIFT), (SM5424_CNTL_AUTOSTOP_MASK << SM5424_CNTL_AUTOSTOP_SHIFT));

	battery_log(BAT_LOG_CRTI, "[sm5424] set_AUTOSTOP : %d\n", enable);

    return 0;
}

static int sm5424_set_OTGCURRENT(struct sm5424_info *info, u8 val)
{
    sm5424_update_reg(info->i2c, SM5424_CHGCNTL5, ((val & SM5424_CHGCNTL5_OTGCURRENT_MASK) << SM5424_CHGCNTL5_OTGCURRENT_SHIFT), (SM5424_CHGCNTL5_OTGCURRENT_MASK << SM5424_CHGCNTL5_OTGCURRENT_SHIFT));

	battery_log(BAT_LOG_CRTI, "[sm5424] set_OTGCURRENT : 0x%x\n", val);

    return 0;
}

static int sm5424_set_BST_IQ3LIMIT(struct sm5424_info *info, u8 val)
{
    sm5424_update_reg(info->i2c, SM5424_CHGCNTL5, ((val & SM5424_CHGCNTL5_BST_IQ3LIMIT_MASK) << SM5424_CHGCNTL5_BST_IQ3LIMIT_SHIFT), (SM5424_CHGCNTL5_BST_IQ3LIMIT_MASK << SM5424_CHGCNTL5_BST_IQ3LIMIT_SHIFT));

	battery_log(BAT_LOG_CRTI, "[sm5424] set_BST_IQ3LIMIT : 0x%x\n", val);

    return 0;
}

static int sm5424_set_VOTG(struct sm5424_info *info, u8 val)
{
    sm5424_update_reg(info->i2c, SM5424_CHGCNTL6, ((val & SM5424_CHGCNTL6_VOTG_MASK) << SM5424_CHGCNTL6_VOTG_SHIFT), (SM5424_CHGCNTL6_VOTG_MASK << SM5424_CHGCNTL6_VOTG_SHIFT));

	battery_log(BAT_LOG_CRTI, "[sm5424] set_VOTG : 0x%x\n", val);

    return 0;
}

static int sm5424_charger_get_property(struct power_supply *psy, enum power_supply_property prop, union power_supply_propval *val)
{
	struct sm5424_info *info = container_of(psy, struct sm5424_info, sm_psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = sm5424_get_status(info);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = info->vbuslimit;
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:vbuslimit(%d)\n", __func__, info->vbuslimit);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = info->fastchg;
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:fastchg(%d)\n", __func__, info->fastchg);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = info->batreg;
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:batreg(%d)\n", __func__, info->batreg);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sm5424_charger_set_property(struct power_supply *psy, enum power_supply_property prop, const union power_supply_propval *val)
{
	int ret = 0;
	struct sm5424_info *info = container_of(psy, struct sm5424_info, sm_psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		info->enable = val->intval;
		sm5424_enable(info, info->enable);
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:enable(%d)\n", __func__, info->enable);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		sm5424_set_vbuslimit(info, val->intval);
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:vbuslimit(%d)\n", __func__, info->vbuslimit);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ25898S_SLAVE_SUPPORT
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:interval(%d),fastchg(%d),lcd_bl(%d)\n", __func__,val->intval, info->fastchg,old_bl_level);
		if(val->intval > 1800 && !old_bl_level && BMT_status.UI_SOC2 < 100){
			int temp=Ibat_Miti()/2;
			sm5424_set_fastchg(info, temp);
			bq2589x_set_ibat(temp);
			bq2589x_adapter_in_handler();
		} else {
			bq2589x_adapter_out_handler();
			sm5424_set_fastchg(info, val->intval);
		}
		/* PCH_TEST 1.8A????????? ?????? IBAT??? SM??? BQ??? ?????????.   
		1.8A????????? ????????? SM??? ???????????? BQ??? OFF?????????. 
		EOC ??? Recharging????????? bq??? on??? ?????? ????????? soc??? ?????? 100%????????? ????????? ?????? ????????? ????????? ????????? ???
		LCD Backlight??? off??? ????????? ?????? ??? ??? 
		vindpm??? ????????? ????????????? */
#else
		sm5424_set_fastchg(info, val->intval);
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:fastchg(%d)\n", __func__, info->fastchg);
#endif

		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		sm5424_set_batreg(info, val->intval/1000);
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:batreg(%d)\n", __func__, info->batreg);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		battery_log(BAT_LOG_CRTI,"[sm5424] %s:suspend(%d)\n", __func__, info->suspend);
		/*If val->intval == 1 charge enable, suspend disable
						 == 0 charge disable, suspend enable */
		if (val->intval)
			info->suspend = 0;
		else
			info->suspend = 1;
		sm5424_suspend(info, info->suspend);
		break;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SAFETY_TIMER_SUPPORT
       case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
		if (val->intval)
			sm5424_set_FASTTIMER_timer(FASTTIMER_8_0_HOUR); //SAFETY TIMER ON(8 HOURS)
		else
			sm5424_set_FASTTIMER_timer(FASTTIMER_DISABLED); //SAFETY TIMER OFF
		battery_log(BAT_LOG_CRTI,"[sm5424 %s:safetytimer(%d)\n", __func__, val->intval);
		break;
#endif
	default:
		return -EINVAL;
	}
	return ret;
}

static int sm5424_charger_property_is_writeable(struct power_supply *psy, enum power_supply_property prop)
{
	int rc = 0;
	switch (prop) {
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CURRENT_NOW:
	case POWER_SUPPLY_PROP_CURRENT_MAX:
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SAFETY_TIMER_SUPPORT
	case POWER_SUPPLY_PROP_SAFETY_TIMER_ENABLE:
#endif
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}
	return rc;
}

static struct power_supply sm5424_psy = {
	.name = "sm5424",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties	= sm5424_charger_properties,
	.num_properties = ARRAY_SIZE(sm5424_charger_properties),
	.get_property = sm5424_charger_get_property,
	.set_property = sm5424_charger_set_property,
	.property_is_writeable = sm5424_charger_property_is_writeable,
};

int sm5424_pe_plus_ta_pattern_increase()
{
	struct sm5424_info *info = i2c_get_clientdata(new_client);
	int ma_100 = VBUSLIMIT_100mA;
	int ma_500 = VBUSLIMIT_500mA;

	sm5424_set_fastchg(info, 500);

	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_increase() start\n", __func__);

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(281);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(281);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(281);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(485);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(50);
	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_increase() end\n", __func__);

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(200);
	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_increase() end_check\n", __func__);

	return 0;
}

int sm5424_pe_plus_ta_pattern_decrease()
{
	struct sm5424_info *info = i2c_get_clientdata(new_client);
	int ma_100 = VBUSLIMIT_100mA;
	int ma_500 = VBUSLIMIT_500mA;

	sm5424_set_fastchg(info, 500);

	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_decrease() start\n", __func__);

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(281);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(281);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(281);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(85);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(485);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(50);
	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_decrease() end\n", __func__);

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_decrease() end_check\n", __func__);

	return 0;
}


#define PEOFFTIME 40
#define PEONTIME 90

struct timespec ptime[2];
static int cptime_bufer[13];

static int dtime(int i)
{
	struct timespec time;

	time = timespec_sub(ptime[i], ptime[i-1]);
	return time.tv_nsec/1000000;
}

int sm5424_pe_plus_20_ta_pattern_increase()
{
	struct sm5424_info *info = i2c_get_clientdata(new_client);
	int ma_100 = VBUSLIMIT_100mA;
	int ma_500 = VBUSLIMIT_500mA;

	u16 value = 0x07; /*9V patten = 00111 = 0x07*/
	u16 value_check = 0x01;
	int i = 0; /*bit 4~0*/
	int j = 0; /*bit duty cycle check*/
	int bit_flag = 0; /*1 : High bit / 0 : Low bit */

	sm5424_set_fastchg(info, 500);

	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_increase() start\n", __func__);

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	mdelay(70);

	for (i = 4; i >= 0; i--) {
		battery_log(BAT_LOG_FULL, "%s: bit: %d value_ori = 0x%X \n", __func__, i, value);
		bit_flag = value_check & (value >> i);
		battery_log(BAT_LOG_FULL, "%s: bit: %d bit_flag = %d\n", __func__, i, bit_flag);

		if (bit_flag) { // bit 1
			battery_log(BAT_LOG_FULL, "%s: bit: %d = High\n", __func__, i);

			sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
			get_monotonic_boottime(&ptime[j++]);
			mdelay(PEONTIME);

			get_monotonic_boottime(&ptime[j]);
			cptime_bufer[j] = dtime(j);
			battery_log(BAT_LOG_CRTI, "%s: bit: %d = High(500mA) cptime_bufer[%d] = %d (%d) \n",
						__func__, i, j, cptime_bufer[j], PEONTIME);

			if (cptime_bufer[j] < 88 || cptime_bufer[j] > 110) {
				battery_log(BAT_LOG_CRTI,
					"%s: High(500mA) bit fail1: idx:%d target:%d actual:%d\n",
					__func__, i, PEONTIME, cptime_bufer[j]);
				return STATUS_FAIL;
			}

			sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
			get_monotonic_boottime(&ptime[j++]);
			mdelay(PEOFFTIME);

			get_monotonic_boottime(&ptime[j]);
			cptime_bufer[j] = dtime(j);
			battery_log(BAT_LOG_CRTI, "%s: bit: %d = High(100mA) cptime_bufer[%d] = %d (%d) \n",
						__func__, i, j, cptime_bufer[j], PEOFFTIME);

			if (cptime_bufer[j] < 30 || cptime_bufer[j] > 65) {
				battery_log(BAT_LOG_CRTI,
					"%s: High(100mA) bit fail2: idx:%d target:%d actual:%d\n",
					__func__, i, PEOFFTIME, cptime_bufer[j]);
				return STATUS_FAIL;
			}

		} else { // bit 0
			battery_log(BAT_LOG_FULL, "%s: bit: %d = Low\n", __func__, i);

			sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
			get_monotonic_boottime(&ptime[j++]);
			mdelay(PEOFFTIME);

			get_monotonic_boottime(&ptime[j]);
			cptime_bufer[j] = dtime(j);
			battery_log(BAT_LOG_CRTI, "%s: bit: %d = Low(500mA) cptime_bufer[%d] = %d (%d) \n",
						__func__, i, j, cptime_bufer[j], PEOFFTIME);

			if (cptime_bufer[j] < 30 || cptime_bufer[j] > 65) {
				battery_log(BAT_LOG_CRTI,
					"%s: Low(500mA) bit fail1: idx:%d target:%d actual:%d\n",
					__func__, i, PEOFFTIME, cptime_bufer[j]);
				return STATUS_FAIL;
			}

			sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
			get_monotonic_boottime(&ptime[j++]);
			mdelay(PEONTIME);

			get_monotonic_boottime(&ptime[j]);
			cptime_bufer[j] = dtime(j);
			battery_log(BAT_LOG_CRTI, "%s: bit: %d = Low(100mA) cptime_bufer[%d] = %d (%d) \n",
						__func__, i, j, cptime_bufer[j], PEONTIME);

			if (cptime_bufer[j] < 88 || cptime_bufer[j] > 110) {
				battery_log(BAT_LOG_CRTI,
					"%s: Low(100mA) bit fail2: idx:%d target:%d actual:%d\n",
					__func__, i, PEONTIME, cptime_bufer[j]);
				return STATUS_FAIL;
			}
		}
	}

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	get_monotonic_boottime(&ptime[j++]);
	mdelay(160);

	get_monotonic_boottime(&ptime[j]);
	cptime_bufer[j] = dtime(j);
	battery_log(BAT_LOG_CRTI, "%s: WDT cptime_bufer[%d] = %d (150) \n", __func__, j, cptime_bufer[j]);

	if (cptime_bufer[j] < 150 || cptime_bufer[j] > 240) {
		battery_log(BAT_LOG_CRTI,
			"%s: WDT fail: target:150 actual:%d\n",
			__func__, PEONTIME, cptime_bufer[j]);
		return STATUS_FAIL;
	}

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	get_monotonic_boottime(&ptime[j++]);
	mdelay(30);

	get_monotonic_boottime(&ptime[j]);
	cptime_bufer[j] = dtime(j);
	battery_log(BAT_LOG_CRTI, "%s: cptime_bufer[%d] = %d (30) \n", __func__, j, cptime_bufer[j]);

	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_increase() end\n", __func__);

//	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
//	mdelay(200);
	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_increase() end_check\n", __func__);

	return 0;
}

int sm5424_pe_plus_20_ta_pattern_decrease()
{
	struct sm5424_info *info = i2c_get_clientdata(new_client);
	int ma_100 = VBUSLIMIT_100mA;
	int ma_500 = VBUSLIMIT_500mA;

	sm5424_set_fastchg(info, 500);

	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_decrease() start\n", __func__);

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(90);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(40);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(90);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(40);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(90);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(40);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(90);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(40);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(90);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(40);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	msleep(150);
	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_100);
	msleep(30);
	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_increase() end\n", __func__);

	sm5424_write_reg(new_client, SM5424_VBUSCNTL, ma_500);
	battery_log(BAT_LOG_CRTI, "%s: mtk_ta_decrease() end_check\n", __func__);

	return 0;
}

int sm5424_dump_register()
{
	int i = 6;
	int ret = 0;

#if 0
/* For Check*/
	u8 read_reg_00;
	u8 read_reg_01;
	u8 read_reg_02;
	u8 read_reg_03;
	u8 read_reg_04;
	u8 read_reg_05;
	u8 read_reg_06;
	u8 read_reg_07;
	u8 read_reg_08;
	u8 read_reg_09;
	u8 read_reg_10;
	u8 read_reg_11;
#endif

	for (i = 6; i < SM5424_REG_NUM - 1; i++) {
		ret = i2c_smbus_read_i2c_block_data(new_client, i, 1, &sm5424_reg[i]);
		if (ret < 0) {
			battery_log(BAT_LOG_CRTI, "[sm5424 reg@]read fail\n");
			pr_err("%s: i2c read error\n", __func__);
			return ret;
		}
	}

	battery_log(BAT_LOG_CRTI,
		    "[sm5424 reg@][0x06]=0x%02x ,[0x07]=0x%02x ,[0x08]=0x%02x ,[0x09]=0x%02x ,[0x0A]=0x%02x\n",
		    sm5424_reg[6], sm5424_reg[7], sm5424_reg[8], sm5424_reg[9], sm5424_reg[10]);

	battery_log(BAT_LOG_CRTI,
		    "[sm5424 reg@][0x0B]=0x%02x ,[0x0C]=0x%02x ,[0x0D]=0x%02x ,[0x0E]=0x%02x ,[0x0F]=0x%02x ,[0x10]=0x%02x\n",
		    sm5424_reg[11], sm5424_reg[12], sm5424_reg[13], sm5424_reg[14], sm5424_reg[15], sm5424_reg[16]);

#if 0
/* For Check*/
	sm5424_read_reg(new_client, SM5424_STATUS1, &read_reg_00);
	sm5424_read_reg(new_client, SM5424_STATUS2, &read_reg_01);
	sm5424_read_reg(new_client, SM5424_STATUS3, &read_reg_02);
	sm5424_read_reg(new_client, SM5424_CNTL, &read_reg_03);
	sm5424_read_reg(new_client, SM5424_VBUSCNTL, &read_reg_04);
	sm5424_read_reg(new_client, SM5424_CHGCNTL1, &read_reg_05);
	sm5424_read_reg(new_client, SM5424_CHGCNTL2, &read_reg_06);
	sm5424_read_reg(new_client, SM5424_CHGCNTL3, &read_reg_07);
	sm5424_read_reg(new_client, SM5424_CHGCNTL4, &read_reg_08);
	sm5424_read_reg(new_client, SM5424_CHGCNTL5, &read_reg_09);
	sm5424_read_reg(new_client, SM5424_CHGCNTL6, &read_reg_10);

	pr_debug("[sm5424] %s: STATUS1 : 0x%02X STATUS2 : 0x%02X STATUS3 : 0x%02X CNTL : 0x%02X VBUSCNTL : 0x%02X\n"\
		"[sm5424] %s: CHGCNTL1 : 0x%02X CHGCNTL2 : 0x%02X CHGCNTL3 : 0x%02X CHGCNTL4 : 0x%02X CHGCNTL5 : 0x%02X CHGCNTL6 : 0x%02X \n",
		__func__, read_reg_00, read_reg_01, read_reg_02, read_reg_03, read_reg_04, __func__, read_reg_05, read_reg_06, read_reg_07, read_reg_08, read_reg_09, read_reg_10);
#endif

	return 0;
}

int sm5424_set_TOPOFFTIMER_timer(int topoff_min)
{
	sm5424_update_reg(new_client, SM5424_CHGCNTL5, topoff_min << TOPOFFTIMER_SHIFT, TOPOFFTIMER_MASK << TOPOFFTIMER_SHIFT);

	battery_log(BAT_LOG_CRTI, "[sm5424] set_TOPOFFTIMER_timer : 0x%x\n", topoff_min);

	return 0;
}

int sm5424_set_FASTTIMER_timer(int fast_hours)
{
	sm5424_update_reg(new_client, SM5424_CHGCNTL5, fast_hours << FASTTIMER_SHIFT, FASTTIMER_MASK << FASTTIMER_SHIFT); // Safety(FASTTIMER) Timer On

	battery_log(BAT_LOG_CRTI, "[sm5424] set_FASTTIMER_timer : 0x%x\n", fast_hours);

	return 0;
}

int sm5424_set_otg_boost_enable(int enable)
{
	struct sm5424_info *info = i2c_get_clientdata(new_client);

	if (enable) {
		sm5424_enable(info, CHARGE_DIS);
		sm5424_update_reg(new_client, SM5424_CNTL, ENBOOST_EN << SM5424_CNTL_ENBOOST_SHIFT, SM5424_CNTL_ENBOOST_MASK << SM5424_CNTL_ENBOOST_SHIFT);
	} else {
		sm5424_update_reg(new_client, SM5424_CNTL, ENBOOST_DIS << SM5424_CNTL_ENBOOST_SHIFT, SM5424_CNTL_ENBOOST_MASK << SM5424_CNTL_ENBOOST_SHIFT);
	}

	battery_log(BAT_LOG_CRTI, "[sm5424] OTG_BOOST_Enable : 0x%x\n", enable);

	return 0;
}

static int sm5424_gpio_init(struct sm5424_info *info)
{
	// GPIO setting
	mt_set_gpio_mode(GPIO8|0x80000000, GPIO_MODE_GPIO);
	mt_set_gpio_pull_enable(GPIO8|0x80000000, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(GPIO8|0x80000000, GPIO_PULL_UP);
	mt_set_gpio_dir(GPIO8|0x80000000, GPIO_DIR_OUT);

	mt_set_gpio_out(GPIO8|0x80000000, GPIO_OUT_ONE);	/* low disable */

	return 0;
}

static void sm5424_initialization(struct sm5424_info *info)
{

/* IRQ read & clear for nINT pin state initialization */
	u8 read_reg_00;
	u8 read_reg_01;
	u8 read_reg_02;

	sm5424_read_reg(info->i2c, SM5424_INT1, &read_reg_00);
	sm5424_read_reg(info->i2c, SM5424_INT2, &read_reg_01);
	sm5424_read_reg(info->i2c, SM5424_INT3, &read_reg_02);
/* IRQ read & clear for nINT pin state initialization */

	sm5424_write_reg(info->i2c, SM5424_INTMASK1, 0xC8); // AICL | BATOVP | VBUSOVP | VBUSUVLO | VBUSPOK
	sm5424_write_reg(info->i2c, SM5424_INTMASK2, 0xF2); // DONE | TOPOFF | CHGON
	sm5424_write_reg(info->i2c, SM5424_INTMASK3, 0xD8); // FASTTMROFF | OTGFAIL | THEMSHDN | THEMREG

	/*Switching Frequency 1.5MHz*/
	sm5424_update_reg(info->i2c, SM5424_CHGCNTL4,
			(0x02<< SM5424_CHGCNTL4_FREQSEL_SHIFT),
			(SM5424_CHGCNTL4_FREQSEL_MASK << SM5424_CHGCNTL4_FREQSEL_SHIFT));

	sm5424_set_AICLTH(info, AICL_THRESHOLD_4_5_V);
	sm5424_set_AICLEN(info, AICL_EN);
	sm5424_set_batreg(info, 4400); // 4.4V Charger output 'float' voltage
	sm5424_set_BST_IQ3LIMIT(info, BSTIQ3LIMIT_4P0A);
	sm5424_set_AUTOSTOP(info, AUTOSTOP_EN);
	sm5424_set_TOPOFF(info, TOPOFF_100mA);
//	sm5424_set_fastchg(info, FASTCHG_1500mA); // 1.5A  typical charging current during Fast charging
//	sm5424_set_vbuslimit(info, VBUSLIMIT_3000mA); // Input current 3A
	sm5424_set_TOPOFFTIMER_timer(TOPOFFTIMER_10MIN);
	sm5424_set_FASTTIMER_timer(FASTTIMER_8_0_HOUR);// FAST Timer On (8hour)
	sm5424_set_OTGCURRENT(info, OTGCURRENT_900mA); //OTG current
	sm5424_set_VOTG(info, VOTG_5_0_V); // OTG voltage
	sm5424_dump_register();

	//info->fastchg = 1500;
	info->batreg = 4400;
	//info->vbuslimit = 3000;
}

static irqreturn_t sm5424_irq_thread(int irq, void *handle)
{
	int ret_0;
	int ret_1;
	u8 read_reg_1;
	u8 read_reg_2;
	u8 read_reg_3;
	u8 temp;

	struct sm5424_info *info = (struct sm5424_info *)handle;

	mutex_lock(&info->mutex);

/* IRQ read & clear for nINT pin state initialization */
	sm5424_read_reg(info->i2c, SM5424_INT1, &read_reg_1);
	sm5424_read_reg(info->i2c, SM5424_INT2, &read_reg_2);
	sm5424_read_reg(info->i2c, SM5424_INT3, &read_reg_3);

	pr_debug("[sm5424] %s: INT1 : 0x%02X INT2 : 0x%02X INT3 : 0x%02X\n",
		__func__, read_reg_1, read_reg_2, read_reg_3);
/* IRQ read & clear for nINT pin state initialization */

	sm5424_read_reg(info->i2c, SM5424_STATUS1, &read_reg_1);
	sm5424_read_reg(info->i2c, SM5424_STATUS2, &read_reg_2);
	sm5424_read_reg(info->i2c, SM5424_STATUS3, &read_reg_3);

	pr_debug("[sm5424] %s: STATUS1 : 0x%02X STATUS2 : 0x%02X STATUS3 : 0x%02X\n",
		__func__, read_reg_1, read_reg_2, read_reg_3);


	if (read_reg_1 & SM5424_INT1_AICL)
	{
		battery_log(BAT_LOG_CRTI,"[sm5424] %s: SM5424_INT1_AICL\n", __func__);
	}

	if (read_reg_2 & SM5424_INT2_TOPOFF)
	{
		battery_log(BAT_LOG_CRTI,"[sm5424] %s: SM5424_INT2_TOPOFF\n", __func__);
	}

	if (read_reg_2 & SM5424_INT2_DONE)
	{
		// sm5424_enable(info,CHARGE_DIS);
		// msleep(100);
		// sm5424_enable(info,CHARGE_EN);
		battery_log(BAT_LOG_CRTI,"[sm5424] %s: SM5424_INT2_DONE\n", __func__);
	}

	if (read_reg_3 & SM5424_INT3_FASTTMROFF)
	{
		battery_log(BAT_LOG_CRTI,"[sm5424] %s: SM5424_INT3_FASTTMROFF\n", __func__);
	}

	temp = read_reg_3 & SM5424_INT3_FAIL;
	battery_log(BAT_LOG_FULL,"[sm5424] %s: SM5424_STATUS3_FAIL : 0x%02X\n", __func__, read_reg_3, temp);

	if (temp == 4) {
		pr_info("[sm5424] %s: OTG FAIL !\n", __func__);
	} else if (temp == 2) {
		pr_info("[sm5424] %s: Thermal Shutdown (150) !\n", __func__);
	} else if (temp == 1) {
		pr_info("[sm5424] %s: Thermal Regulation (110) !\n", __func__);
	}


	if (sm5424_get_vbusok(info)) {
		pr_info("[sm5424] %s: Charger exist !\n", __func__);
	} else {
		pr_info("[sm5424] %s: Charger NOT exist !\n", __func__);
	}

//	sm5424_dump_register();

	mutex_unlock(&info->mutex);
	return IRQ_HANDLED;
}

static ssize_t show_sm5424_addr_register(struct device *dev,struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%x\n", att_addr);
}

static ssize_t store_sm5424_addr_register(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	int ret=0;
	//struct sm5424_info *info = dev_get_drvdata(dev);

	if (!buf)
		return 0;

	if (size == 0)
		return 0;

	if (kstrtouint(buf, 10, &ret) == 0) {
		att_addr = ret;
		return size;
	}

	return size;
}
static DEVICE_ATTR(sm5424_addr_register, 0664, show_sm5424_addr_register, store_sm5424_addr_register);

static ssize_t show_sm5424_status_register(struct device *dev,struct device_attribute *attr, char *buf)
{
	BYTE ret=0;
	struct sm5424_info *info = dev_get_drvdata(dev);

	sm5424_read_reg(info->i2c, att_addr, &ret);
	return sprintf(buf, "%d\n", ret);
}

static ssize_t store_sm5424_status_register(struct device *dev,struct device_attribute *attr, const char *buf, size_t size)
{
	BYTE ret=0;
	struct sm5424_info *info = dev_get_drvdata(dev);

	if (!buf)
		return 0;

	if (size == 0)
		return 0;
	if (kstrtouint(buf, 10, &ret) == 0) {
		sm5424_write_reg(info->i2c, att_addr, ret);
		return size;
	}
	return size;
}
static DEVICE_ATTR(sm5424_status_register, 0664, show_sm5424_status_register, store_sm5424_status_register);

static int sm5424_parse_dt(struct device_node *dev_node, struct sm5424_info *info)
{
	int ret;

	ret = of_property_read_u32(dev_node, "batreg", &info->batreg);
	if (ret) {
		battery_log(BAT_LOG_CRTI,"batreg not defined\n");
		return ret;
	}

	ret = of_property_read_u32(dev_node, "vbuslimit", &info->vbuslimit);
	if (ret) {
		battery_log(BAT_LOG_CRTI,"batreg not defined\n");
		return ret;
	}

	ret = of_property_read_u32(dev_node, "fastchg", &info->fastchg);
	if (ret) {
		battery_log(BAT_LOG_CRTI,"batreg not defined\n");
		return ret;
	}

	ret = of_property_read_u32(dev_node, "dis_set", &info->dis_set);
	if (ret) {
		battery_log(BAT_LOG_CRTI,"batreg not defined\n");
		return ret;
	}
	return 0;
}

static int sm5424_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret = 0;
	struct sm5424_info *info;
	struct device_node *dev_node = client->dev.of_node;

	battery_log(BAT_LOG_CRTI,"[sm5424_PROBE_START]\n");
	info = (struct sm5424_info*)kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		dev_err(&client->dev, "memory allocation failed.\n");
		return -ENOMEM;
	}

	if (!(new_client = kmalloc(sizeof(struct i2c_client), GFP_KERNEL))) {
		return -ENOMEM;
	}
	memset(new_client, 0, sizeof(struct i2c_client));
	new_client = client;

	i2c_set_clientdata(client, info);
	info->i2c = client;
	info->irq = gpio_to_irq(8);
	mutex_init(&info->mutex);
	if (info->irq == -EINVAL)
		goto enable_irq_failed;

	if (dev_node) {
		ret = sm5424_parse_dt(dev_node, info);
		if (ret) {
			dev_err(&client->dev, "Failed to parse dt\n");
		}
	}

	sm5424_gpio_init(info);

	info->sm_psy = sm5424_psy;

	ret = power_supply_register(&client->dev, &info->sm_psy);

	if(ret < 0){
		dev_err(&client->dev, "power supply register failed.\n");
		return ret;
	}

	ret = request_threaded_irq(info->irq, NULL, sm5424_irq_thread, IRQF_TRIGGER_LOW | IRQF_ONESHOT, "sm5424_irq", info);

	if (ret){
		dev_err(&client->dev, "failed to reqeust IRQ\n");
		goto request_irq_failed;
	}

	sm5424_initialization(info);
	chargin_hw_init_done = KAL_TRUE;

	device_create_file(&(client->dev), &dev_attr_sm5424_addr_register);
	device_create_file(&(client->dev), &dev_attr_sm5424_status_register);

	battery_log(BAT_LOG_CRTI,"[sm5424_PROBE_SUCCESS]\n");
	return 0;

enable_irq_failed:
	free_irq(info->irq,NULL);
request_irq_failed:
	mutex_destroy(&info->mutex);
	i2c_set_clientdata(client, NULL);
	kfree(info);

	return ret;
}

static int sm5424_remove(struct i2c_client *client)
{
	struct sm5424_info *info = i2c_get_clientdata(client);

	if (client->irq) {
		disable_irq_wake(client->irq);
		free_irq(client->irq, info);
	}
	if (info)
		kfree(info);

	return 0;
}

static struct i2c_driver sm5424_driver = {
	.probe = sm5424_probe,
	.remove = sm5424_remove,
	.driver = {
		.name = "sm5424",
		.of_match_table = sm5424_of_match,
	},
	.id_table = sm5424_i2c_id,
};

static int __init sm5424_init(void)
{
	battery_log(BAT_LOG_CRTI,"[sm5424_init] init start\n");

	i2c_register_board_info(2, &i2c_sm5424, 1);

	if (i2c_add_driver(&sm5424_driver) != 0) {
		battery_log(BAT_LOG_CRTI,"[sm5424_init] failed to register sm5424 i2c driver.\n");
	}
	else {
		battery_log(BAT_LOG_CRTI,"[sm5424_init] Success to register sm5424 i2c driver.\n");
	}

	return 0;
}

static void __exit sm5424_exit(void)
{
	i2c_del_driver(&sm5424_driver);
}

module_init(sm5424_init);
module_exit(sm5424_exit);
