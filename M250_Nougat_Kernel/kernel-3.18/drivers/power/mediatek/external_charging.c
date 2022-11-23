/*****************************************************************************
 *
 * Filename:
 * ---------
 *    external_charging.c
 *
 * Project:
 * --------
 *   ALPS_Software
 *
 * Description:
 * ------------
 *   This file implements the interface between BMT and ADC scheduler.
 *
 * Author:
 * -------
 *  Oscar Liu
 *
 *============================================================================
  * $Revision:   1.0  $
 * $Modtime:   08 Apr 2014 07:47:16  $
 * $Log:   //mtkvs01/vmdata/Maui_sw/archives/mcu/hal/peripheral/inc/bmt_chr_setting.h-arc  $
 *             HISTORY
 * Below this line, this part is controlled by PVCS VM. DO NOT MODIFY!!
 *------------------------------------------------------------------------------
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by PVCS VM. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/
#include <linux/types.h>
#include <linux/kernel.h>
#include <mt-plat/battery_meter.h>


#include <mt-plat/battery_common.h>

#include <mt-plat/charging.h>
#include <mach/mt_charging.h>
#include <mt-plat/mt_boot.h>

#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
#include <linux/mutex.h>
#include <linux/wakelock.h>
#include <linux/delay.h>
#include <mt-plat/upmu_common.h>
#include <mach/upmu_sw.h>
#if !defined(TA_AC_CHARGING_CURRENT)
#include <mach/mt_pe.h>
#endif
#endif

#ifdef CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT
#include <mt-plat/diso.h>
#include <mach/mt_diso.h>
#endif

#ifdef CONFIG_MACH_LGE
#include <soc/mediatek/lge/board_lge.h>
#endif

#ifdef CONFIG_LGE_PM_USB_ID
#include <soc/mediatek/lge/lge_cable_id.h>
#endif

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
#include <soc/mediatek/lge/lge_pseudo_batt.h>
#endif

/* ============================================================ // */
/* define */
/* ============================================================ // */
/* cut off to full */
#define POST_CHARGING_TIME (30*60)	/* 30mins */
#define FULL_CHECK_TIMES 6

// ============================================================ //
//global variable
// ============================================================ //
unsigned int g_bcct_flag=0;
unsigned int g_bcct_value=0;
CHR_CURRENT_ENUM g_temp_CC_value = CHARGE_CURRENT_0_00_MA;
CHR_CURRENT_ENUM g_temp_input_CC_value = CHARGE_CURRENT_0_00_MA;
CHR_CURRENT_ENUM g_temp_thermal_CC_value = CHARGE_CURRENT_0_00_MA;
unsigned int g_usb_state = USB_UNCONFIGURED;
static bool usb_unlimited=false;
#if defined(CONFIG_MTK_HAFG_20)
#if defined(CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
BATTERY_VOLTAGE_ENUM g_cv_voltage = BATTERY_VOLT_04_400000_V;
#elif defined(HIGH_BATTERY_VOLTAGE_SUPPORT)
BATTERY_VOLTAGE_ENUM g_cv_voltage = BATTERY_VOLT_04_400000_V;
#else
BATTERY_VOLTAGE_ENUM g_cv_voltage = BATTERY_VOLT_04_350000_V;
#endif
unsigned int get_cv_voltage(void)
{
	return g_cv_voltage;
}
#endif

 /* ///////////////////////////////////////////////////////////////////////////////////////// */
 /* // PUMP EXPRESS */
 /* ///////////////////////////////////////////////////////////////////////////////////////// */
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
struct wake_lock TA_charger_suspend_lock;
kal_bool ta_check_chr_type = KAL_TRUE;
kal_bool ta_cable_out_occur; /* Plug out happened while detecting PE+ */
kal_bool is_ta_connect = KAL_FALSE;
kal_bool ta_vchr_tuning = KAL_TRUE;
//#if defined(PUMPEX_PLUS_RECHG)
kal_bool pep_det_rechg = KAL_FALSE;
//#endif
int ta_v_chr_org = 0;
kal_bool retransmint_flag = KAL_FALSE;
#define Ref_V_PEP_9V 8000
#endif
// ============================================================ //
// function prototype
// ============================================================ //

// ============================================================ //
//extern variable
// ============================================================ //
extern int g_platform_boot_mode;
#ifdef CONFIG_LGE_PM_AT_CMD_SUPPORT
extern int get_AtCmdChargingModeOff(void);
#endif

#if defined(CONFIG_LGE_PM_BACKLIGHT_CHG_CONTROL)
extern int get_cur_main_lcd_level(void);
#endif

// ============================================================ //
//extern function
// ============================================================ //

// ============================================================ //
void BATTERY_SetUSBState(int usb_state_value)
{
#if defined(CONFIG_POWER_EXT)
	battery_log(BAT_LOG_CRTI, "[BATTERY_SetUSBState] in FPGA/EVB, no service\n");
#else
	if ((usb_state_value < USB_SUSPEND) || (usb_state_value > USB_CONFIGURED)) {
		battery_log(BAT_LOG_CRTI,
			"[BATTERY] BAT_SetUSBState Fail! Restore to default value\n");
		g_usb_state = USB_UNCONFIGURED;
	} else {
		battery_log(BAT_LOG_CRTI,
			"[BATTERY] BAT_SetUSBState Success! Set %d\n", usb_state_value);
		g_usb_state = usb_state_value;
	}
#endif
}

unsigned int get_charging_setting_current(void)
{
	return g_temp_CC_value;
}


#if defined(CONFIG_MTK_DYNAMIC_BAT_CV_SUPPORT)
static unsigned int get_constant_voltage(void)
{
	unsigned int cv;
#ifdef CONFIG_MTK_BIF_SUPPORT
	unsigned int vbat_bif;
	unsigned int vbat_auxadc;
	unsigned int vbat, bif_ok;
	int i;
#endif
	/*unit:mV defined in cust_charging.h */
	cv = V_CC2TOPOFF_THRES;
#ifdef CONFIG_MTK_BIF_SUPPORT
	/*Use BIF API to get vbat_core to adjust cv */
	i = 0;
	do {
		battery_charging_control(CHARGING_CMD_GET_BIF_VBAT, &vbat_bif);
		vbat_auxadc = battery_meter_get_battery_voltage(KAL_TRUE);
		if (vbat_bif < vbat_auxadc && vbat_bif != 0) {
			vbat = vbat_bif;
			bif_ok = 1;
			battery_log(BAT_LOG_FULL, "[BIF]using vbat_bif=%d\n with dV=%dmV", vbat,
				    (vbat_bif - vbat_auxadc));
		} else {
			vbat = vbat_auxadc;
			if (i < 5)
				i++;
			else {
				battery_log(BAT_LOG_FULL,
					    "[BIF]using vbat_auxadc=%d, check vbat_bif=%d\n", vbat,
					    vbat_bif);
				bif_ok = 0;
				break;
			}
		}
	} while (vbat_bif > vbat_auxadc || vbat_bif == 0);


	/*CV adjustment according to the obtained vbat */
	if (bif_ok == 1) {
#ifdef HIGH_BATTERY_VOLTAGE_SUPPORT
		int vbat1 = 4250;
		int vbat2 = 4300;
		int cv1 = 4450;
#else
		int vbat1 = 4100;
		int vbat2 = 4150;
		int cv1 = 4350;
#endif
		if (vbat >= 3400 && vbat < vbat1)
			cv = 4608;
		else if (vbat >= vbat1 && vbat < vbat2)
			cv = cv1;
		else
			cv = V_CC2TOPOFF_THRES;

		battery_log(BAT_LOG_FULL, "[BIF]dynamic CV=%dmV\n", cv);
	}
#endif
	return cv;
}
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
static void switch_charger_set_vindpm(unsigned int chr_v)
{
	/*unsigned int delta_v = 0; */
	unsigned int vindpm = 0;

	/*chr_v = battery_meter_get_charger_voltage();*/
	/*delta_v = chr_v - ta_v_chr_org; */

	if (chr_v > 11000)
		vindpm = SWITCH_CHR_VINDPM_12V;
	else if (chr_v > 8000)
		vindpm = SWITCH_CHR_VINDPM_9V;
	else if (chr_v > 6000)
		vindpm = SWITCH_CHR_VINDPM_7V;
	else
		vindpm = SWITCH_CHR_VINDPM_5V;

	battery_charging_control(CHARGING_CMD_SET_VINDPM, &vindpm);
	battery_log(BAT_LOG_CRTI,
		    "[PE+] switch charger set VINDPM=%dmV with charger volatge=%dmV\n",
		    vindpm * 100 + 2600, chr_v);
}
#endif
#endif

/*Base 6735 by JJB*/
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
static DEFINE_MUTEX(ta_mutex);

static void set_ta_charging_current(void)
{
	int real_v_chrA = 0;

	battery_log(BAT_LOG_FULL, "%s: [PE+] Start\n", __func__);
	real_v_chrA = battery_meter_get_charger_voltage();

	if ((real_v_chrA - ta_v_chr_org) > 3000) {
		g_temp_input_CC_value = TA_AC_9V_INPUT_CURRENT;	/* TA = 9V */
		g_temp_CC_value = TA_AC_CHARGING_CURRENT;
	} else if ((real_v_chrA - ta_v_chr_org) > 1000) {
		g_temp_input_CC_value = TA_AC_7V_INPUT_CURRENT;	/* TA = 7V */
		g_temp_CC_value = TA_AC_CHARGING_CURRENT;
	}
	battery_log(BAT_LOG_CRTI, "%s: [PE+]set Ichg=%dmA with Iinlim=%dmA, chrA=%d, chrB=%d\n",
		    __func__, g_temp_CC_value / 100, g_temp_input_CC_value / 100, ta_v_chr_org, real_v_chrA);
}

static void mtk_ta_reset_vchr(void)
{
	CHR_CURRENT_ENUM chr_current = CHARGE_CURRENT_70_00_MA;
	unsigned int charging_enable = KAL_TRUE;

	battery_charging_control(CHARGING_CMD_SET_INPUT_CURRENT, &chr_current);
	battery_charging_control(CHARGING_CMD_ENABLE, &charging_enable);
	msleep(250);		/* reset Vchr to 5V */

	battery_log(BAT_LOG_CRTI, "%s: [PE+] reset Vchr to 5V\n", __func__);
}

static void mtk_ta_increase(void)
{
	kal_bool ta_current_pattern = KAL_TRUE;	/* TRUE = increase */

#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT
	battery_log(BAT_LOG_FULL, "%s: [PE+2.0] Start\n", __func__);
#else
	battery_log(BAT_LOG_FULL, "%s: [PE+] Start\n", __func__);
#endif

	if (BMT_status.charger_exist) {
#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT
		battery_log(BAT_LOG_CRTI, "%s: [PE+2.0]ta_cable_out_occur=%d, set_ta_current_pattern\n", __func__, ta_cable_out_occur);
		battery_charging_control(CHARGING_CMD_SET_TA20_CURRENT_PATTERN, &ta_current_pattern);
#else
		battery_log(BAT_LOG_CRTI, "%s: [PE+]ta_cable_out_occur=%d, set_ta_current_pattern\n", __func__, ta_cable_out_occur);
		battery_charging_control(CHARGING_CMD_SET_TA_CURRENT_PATTERN, &ta_current_pattern);
#endif
	} else {
		ta_check_chr_type = KAL_TRUE;
		battery_log(BAT_LOG_CRTI, "[PE+]mtk_ta_increase() Cable out\n");
	}
}

static kal_bool mtk_ta_retry_increase(void)
{
	int real_v_chrA;
	int real_v_chrB;
	kal_bool retransmit = KAL_TRUE;
	unsigned int retransmit_count = 0;
	kal_bool over_retransmit = KAL_FALSE;

	CHR_CURRENT_ENUM chr_current_reset = CHARGE_CURRENT_70_00_MA;
	CHR_CURRENT_ENUM chr_current_restart = CHARGE_CURRENT_500_00_MA;


	battery_log(BAT_LOG_CRTI, "%s: [PE+] Start\n", __func__);

	if (is_ta_connect == KAL_TRUE) {
		real_v_chrB = battery_meter_get_charger_voltage();

		if (real_v_chrB > Ref_V_PEP_9V) {
			battery_log(BAT_LOG_CRTI,
					    "%s: [PE+]Finished communicating with adapter real_v_chrB=%d \n",
					    __func__, real_v_chrB);
			return KAL_TRUE;
		} else {
			battery_log(BAT_LOG_CRTI, "%s: [PE+] Starting mtk_ta_retry_increase\n", __func__);
		}
	}

	do {
		real_v_chrA = battery_meter_get_charger_voltage();
		mtk_ta_increase();	/* increase TA voltage to 7V */
		real_v_chrB = battery_meter_get_charger_voltage();

#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT
		if ((real_v_chrA > 4400) && /* VBUS abnormal case*/
			((real_v_chrB - real_v_chrA >= 3000) || /* 3.0V */
			((real_v_chrB > Ref_V_PEP_9V) && (real_v_chrA > Ref_V_PEP_9V)))) /* 8.0V */
			retransmit = KAL_FALSE;
#else
		if ((real_v_chrA > 4400) && /* VBUS abnormal case*/
			(real_v_chrB - real_v_chrA >= 1000)) /* 1.0V */
			retransmit = KAL_FALSE;
#endif
		else {
			retransmit_count++;
			battery_log(BAT_LOG_CRTI,
				    "%s: [PE+]Communicated with adapter:retransmit_count =%d, chrA=%d, chrB=%d\n",
				    __func__, retransmit_count, real_v_chrA, real_v_chrB);

			if (over_retransmit == KAL_TRUE) {
				battery_charging_control(CHARGING_CMD_SET_INPUT_CURRENT, &chr_current_reset);
				/*retry delay 3sec*/
				msleep(3000);
				battery_charging_control(CHARGING_CMD_SET_INPUT_CURRENT, &chr_current_restart);
			} else {
				/*retry delay 2sec*/
				msleep(2000);
			}
		}

		if ((retransmit_count == 3) || (BMT_status.charger_exist == KAL_FALSE)) {
			over_retransmit = KAL_TRUE;
		} else if ((retransmit_count == 6) || (BMT_status.charger_exist == KAL_FALSE)) {
			over_retransmit = KAL_FALSE;
			retransmit = KAL_FALSE;
		}

	} while (retransmit == KAL_TRUE);

	battery_log(BAT_LOG_CRTI,
		    "%s: [PE+]Finished communicating with adapter real_v_chrA=%d, real_v_chrB=%d, retry=%d\n",
		    __func__, real_v_chrA, real_v_chrB, retransmit_count);

	if (retransmit_count == 6) {
		battery_log(BAT_LOG_CRTI, "%s: [PE+] retransmit_count = 6\n", __func__);
		return KAL_FALSE;
	}
		battery_log(BAT_LOG_CRTI, "%s: [PE+]Endding mtk_ta_retry_increase\n", __func__);
		return KAL_TRUE;
}

static void mtk_ta_detector(void)
{
	int real_v_chrB = 0;

	battery_log(BAT_LOG_CRTI, "%s: [PE+]starting PE+ adapter detection, is_ta_connect=%d \n", __func__, is_ta_connect);

	ta_v_chr_org = battery_meter_get_charger_voltage();
	mtk_ta_retry_increase();
	real_v_chrB = battery_meter_get_charger_voltage();

#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT
	if ((ta_v_chr_org > 4400) && (real_v_chrB - ta_v_chr_org >= 3000))
		is_ta_connect = KAL_TRUE;
#else
	if ((ta_v_chr_org > 4400) && (real_v_chrB - ta_v_chr_org >= 1000))
		is_ta_connect = KAL_TRUE;
#endif
	else {
		is_ta_connect = KAL_FALSE;
	}

	mt_battery_update_status();

	battery_log(BAT_LOG_CRTI, "%s: [PE+]End of PE+ adapter detection, is_ta_connect=%d \n",
		    __func__, is_ta_connect);
}

static int ta_plugout_reset(void)
{
	int ret = 0;

	battery_log(BAT_LOG_FULL, "%s: starts\n", __func__);

	is_ta_connect = KAL_FALSE;
	ta_check_chr_type = KAL_TRUE;

	mtk_ta_reset_vchr();    /* decrease TA voltage to 5V */

	/* Set cable out occur to false */
	ta_cable_out_occur = KAL_FALSE;
	battery_log(BAT_LOG_CRTI, "%s: OK\n", __func__);
	return ret;
}

static void mtk_ta_init(void)
{
	// is_ta_connect = KAL_FALSE;
	// ta_cable_out_occur = KAL_FALSE;
	ta_vchr_tuning = KAL_FALSE;

	// if (batt_cust_data.ta_9v_support || batt_cust_data.ta_12v_support)
	// 	ta_vchr_tuning = KAL_FALSE;

	battery_charging_control(CHARGING_CMD_INIT, NULL);
}

static void battery_pump_express_charger_check(void)
{
	if (!BMT_status.charger_exist || ta_cable_out_occur) {
		battery_log(BAT_LOG_CRTI, "%s: ta_plugout\n", __func__);
		ta_plugout_reset();
	}

	battery_log(BAT_LOG_CRTI, "%s: CHR_Type %d\n", __func__, BMT_status.charger_type);

	if (KAL_TRUE == ta_check_chr_type &&
	    STANDARD_CHARGER == BMT_status.charger_type &&
	    BMT_status.SOC >= TA_START_BATTERY_SOC &&
	    BMT_status.SOC <= TA_STOP_BATTERY_SOC) {
		battery_log(BAT_LOG_CRTI, "%s: [PE+]Starting PE Adaptor detection\n", __func__);

		mutex_lock(&ta_mutex);
		wake_lock(&TA_charger_suspend_lock);

		mtk_ta_reset_vchr();

		mtk_ta_init();
		mtk_ta_detector();

		/* need to re-check if the charger plug out during ta detector */
		if (KAL_TRUE == ta_cable_out_occur)
			ta_check_chr_type = KAL_TRUE;
		else
			ta_check_chr_type = KAL_FALSE;
//#if defined(PUMPEX_PLUS_RECHG)
		/*PE detection disable in the event of recharge after 1st PE detection is finished */
		pep_det_rechg = KAL_FALSE;
//#endif
		wake_unlock(&TA_charger_suspend_lock);
		mutex_unlock(&ta_mutex);
		battery_log(BAT_LOG_CRTI, "%s: [PE+]Endding PE Adaptor detection\n", __func__);
	} else {
		battery_log(BAT_LOG_CRTI,
			    "%s: [PE+]Stop PE Adaptor detection, SOC=%d, ta_check_chr_type = %d, charger_type = %d\n",
			    __func__, BMT_status.SOC, ta_check_chr_type, BMT_status.charger_type);
	}
}

static void battery_pump_express_algorithm_start(void)
{
	signed int charger_vol;
	unsigned int charging_enable = KAL_FALSE;

	battery_log(BAT_LOG_FULL, "%s: [PE+] Start\n", __func__);

	mutex_lock(&ta_mutex);
	wake_lock(&TA_charger_suspend_lock);

	if (!BMT_status.charger_exist || ta_cable_out_occur) {
		battery_log(BAT_LOG_CRTI, "%s: ta_plugout\n", __func__);
		ta_plugout_reset();
	}

	if (KAL_TRUE == is_ta_connect) {
		/* check cable impedance */
		charger_vol = battery_meter_get_charger_voltage();
		battery_log(BAT_LOG_CRTI, "%s: is_ta_connect=%d , check cable impedance\n", __func__, is_ta_connect);

		if (KAL_FALSE == ta_vchr_tuning) {
			battery_log(BAT_LOG_CRTI, "%s: ta_vchr_tuning=%d\n", __func__, ta_vchr_tuning);

			mtk_ta_retry_increase();
			charger_vol = battery_meter_get_charger_voltage();

			ta_vchr_tuning = KAL_TRUE;
		} else if (BMT_status.SOC > TA_STOP_BATTERY_SOC) {
			/* disable charging, avoid Iterm issue */
			battery_charging_control(CHARGING_CMD_ENABLE, &charging_enable);
			mtk_ta_reset_vchr();	/* decrease TA voltage to 5V */
			charger_vol = battery_meter_get_charger_voltage();
			if (abs(charger_vol - ta_v_chr_org) <= 1000)	/* 1.0V */
				is_ta_connect = KAL_FALSE;

			battery_log(BAT_LOG_CRTI,
				    "[PE+]Stop battery_pump_express_algorithm, SOC=%d is_ta_connect =%d, TA_STOP_BATTERY_SOC: %d\n",
				    BMT_status.SOC, is_ta_connect, batt_cust_data.ta_stop_battery_soc);
		}
		battery_log(BAT_LOG_CRTI,
			    "[PE+]check cable impedance, VA(%d) VB(%d) delta(%d).\n",
			    ta_v_chr_org, charger_vol, charger_vol - ta_v_chr_org);

		battery_log(BAT_LOG_CRTI, "[PE+]mtk_ta_algorithm() end\n");
	} else {
		battery_log(BAT_LOG_CRTI, "%s: [PE+] Not PE+ charger, Normal TA\n", __func__);
	}

	wake_unlock(&TA_charger_suspend_lock);
	mutex_unlock(&ta_mutex);

	battery_log(BAT_LOG_FULL, "%s: [PE+] End\n", __func__);

}
#endif

bool get_usb_current_unlimited(void)
{
	if (BMT_status.charger_type == STANDARD_HOST || BMT_status.charger_type == CHARGING_HOST)
		return usb_unlimited;
	else
		return false;
}

void set_usb_current_unlimited(bool enable)
{
	usb_unlimited = enable;
}

void select_charging_current_bcct(void)
{
	/* If usb is connected, bcct should be not work for bc1.2 */
	if (BMT_status.charger_type == STANDARD_HOST)
		return;

	/* If factory cable is connected, bcct should be not work */
	if (lge_is_factory_cable_boot())
		return;

#if defined(CONFIG_MACH_MT6755M_K6P) || defined(CONFIG_MACH_MT6750_LV7)
	if (g_temp_thermal_CC_value < g_temp_CC_value) {
		g_temp_CC_value = g_temp_thermal_CC_value;
		battery_log(BAT_LOG_CRTI, "[BATTERY] bcct battery current limit = %dmA\n",
			g_temp_CC_value / 100);

	}
#elif defined(CONFIG_MACH_MT6755M_PH1)
	if (g_temp_thermal_CC_value != g_temp_input_CC_value) {
		g_temp_input_CC_value = g_temp_thermal_CC_value;
		battery_log(BAT_LOG_CRTI, "[BATTERY] bcct input current limit = %dmA\n",
			g_temp_input_CC_value / 100);
	}
#else
	if (g_temp_thermal_CC_value <= g_temp_CC_value) {
		g_temp_CC_value = g_temp_thermal_CC_value;
		battery_log(BAT_LOG_CRTI, "[BATTERY] bcct input current limit = %d\n",
			g_temp_input_CC_value / 100);
	}
#endif
}

static void pchr_turn_on_charging (void);
unsigned int set_bat_charging_current_limit(int current_limit)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] set_bat_charging_current_limit (%d)\n", current_limit);

	if (current_limit != -1) {
		g_bcct_flag=1;
		g_bcct_value = current_limit;
		g_temp_thermal_CC_value = current_limit * 100;
	} else {
		//change to default current setting
		g_bcct_flag=0;
		g_temp_thermal_CC_value = CHARGE_CURRENT_2000_00_MA;
	}

	pchr_turn_on_charging();

	return g_bcct_flag;
}

void select_charging_current(void)
{
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24296_SUPPORT
	int vindpm = 4680;
#endif

#ifdef CONFIG_LGE_PM_USB_ID
	/* Set MAX Charging current when factory cable connected
	 * Set Ibat Charging current 0, when factory cable and no battery boot
	 * */
	if (lge_is_factory_cable_boot()) {
		g_temp_input_CC_value = CHARGE_CURRENT_1500_00_MA;
		g_temp_CC_value = CHARGE_CURRENT_500_00_MA;
		if (BMT_status.bat_exist == KAL_FALSE) {
			g_temp_CC_value = CHARGE_CURRENT_0_00_MA;
		}

		/* update current setting  */
		BMT_status.input_current_max = g_temp_input_CC_value;
		BMT_status.constant_charge_current_max = g_temp_CC_value;

		return;
	}
#endif

	/* select input current */
	switch (BMT_status.charger_type) {
	case CHARGER_UNKNOWN:
		g_temp_input_CC_value = CHARGE_CURRENT_0_00_MA;
		break;
	case STANDARD_HOST:
		g_temp_input_CC_value = batt_cust_data.usb_charger_current;
		switch (g_usb_state) {
		case USB_UNCONFIGURED:
			g_temp_input_CC_value = batt_cust_data.usb_charger_current_unconfigured;
			break;
		case USB_CONFIGURED:
			g_temp_input_CC_value = batt_cust_data.usb_charger_current_configured;
			break;
		case USB_SUSPEND:
			if (batt_cust_data.config_usb_if)
				g_temp_input_CC_value = batt_cust_data.usb_charger_current_suspend;
			else
				g_temp_input_CC_value = batt_cust_data.usb_charger_current;
			break;
		default:
			g_temp_input_CC_value = batt_cust_data.usb_charger_current;
			break;
		}
#ifdef CONFIG_LGE_PM_CHARGERLOGO
		/* If chargerlogo boot, usb current set 500mA */
		if (g_platform_boot_mode == CHARGER_BOOT) {
			g_temp_input_CC_value = batt_cust_data.usb_charger_current;
		}
#endif
		break;
	case CHARGING_HOST:
		g_temp_input_CC_value = batt_cust_data.charging_host_charger_current;
		break;
	case NONSTANDARD_CHARGER:
		g_temp_input_CC_value = batt_cust_data.non_std_ac_charger_current;
		break;
	case STANDARD_CHARGER:
		g_temp_input_CC_value = batt_cust_data.ac_charger_current;
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
		battery_log(BAT_LOG_CRTI, "%s: [PE+] STANDARD_CHARGER, is_ta_connect=%d\n", __func__, is_ta_connect);
		if (is_ta_connect == KAL_TRUE) {
			set_ta_charging_current();
		}
#endif
		break;
	case APPLE_2_1A_CHARGER:
		g_temp_input_CC_value = batt_cust_data.apple_2_1a_charger_current;
		break;
	case APPLE_1_0A_CHARGER:
		g_temp_input_CC_value = batt_cust_data.apple_1_0a_charger_current;
		break;
	case APPLE_0_5A_CHARGER:
		g_temp_input_CC_value = batt_cust_data.apple_0_5a_charger_current;
		break;
	default:
		g_temp_input_CC_value = batt_cust_data.usb_charger_current;
		break;
	}

#ifdef CONFIG_LGE_PM_USB_CURRENT_MAX
	/* To avoid power-off in ATS test, increase charging current */
	if (BMT_status.usb_current_max_enabled) {
		if (g_temp_input_CC_value < CHARGE_CURRENT_900_00_MA) {
			g_temp_input_CC_value = CHARGE_CURRENT_900_00_MA;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24296_SUPPORT
			battery_charging_control(CHARGING_CMD_SET_DPM, &vindpm);
#endif
		}
	}
#endif

#ifdef CONFIG_LGE_PM_PSEUDO_BATTERY
	/* To avoid power-off in ATS test, increase charging current in pseudo battery mode */
	if (get_pseudo_batt_info(PSEUDO_BATT_MODE)) {
		if (g_temp_input_CC_value < CHARGE_CURRENT_900_00_MA) {
			g_temp_input_CC_value = CHARGE_CURRENT_900_00_MA;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_BQ24296_SUPPORT
			battery_charging_control(CHARGING_CMD_SET_DPM, &vindpm);
#endif
		}
	}
#endif

	/* set maximum input current same as ac charger current */
	if (g_temp_input_CC_value > batt_cust_data.ac_charger_current)
		g_temp_input_CC_value = batt_cust_data.ac_charger_current;

	/* select constant charging current */
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
	g_temp_CC_value = g_temp_input_CC_value;
	if  (is_ta_connect) {
		g_temp_CC_value = CHARGE_CURRENT_3000_00_MA;
	}
#else
	g_temp_CC_value = CHARGE_CURRENT_3200_00_MA;
#endif

	/* update current setting  */
	BMT_status.input_current_max = g_temp_input_CC_value;
	BMT_status.constant_charge_current_max = g_temp_CC_value;

#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
	/* get input current from charging controller */
	if (BMT_status.input_current_cc != -EINVAL)
		g_temp_input_CC_value = BMT_status.input_current_cc;

	/* get charging current from charging controller */
	if (BMT_status.constant_charge_current_cc != -EINVAL)
		g_temp_CC_value = BMT_status.constant_charge_current_cc;
#endif
}

static int recharging_check(void)
{
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SUPPORT
	int data = true;
#endif
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT
	unsigned int bat_vol = battery_meter_get_battery_voltage(KAL_TRUE);
#endif

	if (BMT_status.bat_charging_state != CHR_BATFULL)
		return false;

	if (BMT_status.SOC < 100)
		return true;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_RT9460_SUPPORT
	battery_charging_control(CHARGING_CMD_GET_CHARGING_STATUS, &data);
	if (data == false) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] %s: EOC False\n", __func__);
		return true;
	}
#elif defined (CONFIG_LGE_PM_EXTERNAL_CHARGER_SM5424_SUPPORT)
	battery_charging_control(CHARGING_CMD_GET_CHARGING_STATUS, &data);
	battery_log(BAT_LOG_CRTI, "[BATTERY] recharging_check : bat_vol(%d), RECHARGING_VOLTAGE(%d)\n", bat_vol, batt_cust_data.recharging_voltage);	
//	battery_log(BAT_LOG_CRTI, "[BATTERY] recharging_check : BMT_status.bat_vol(%d), RECHARGING_VOLTAGE(%d)\n",BMT_status.bat_vol, batt_cust_data.recharging_voltage);	
//	if ((data == false) && (BMT_status.bat_vol <= batt_cust_data.recharging_voltage)) { BMT_status.bat_vol은 average값을 취하므로 bat가 drop되어도 빨리 반영되지 않음. 생산에서 charging test를 할 수 없음. 
	if ((data == false) && (bat_vol <= batt_cust_data.recharging_voltage)) { 
		battery_log(BAT_LOG_CRTI, "[BATTERY] %s: EOC False\n", __func__);
		return true;
	}
#else
	if (BMT_status.bat_vol <= batt_cust_data.recharging_voltage)
		return true;
#endif
	return false;
}

static int eoc_check(void)
{
	int data = BMT_status.bat_vol;
	int eoc = false;

	if (!BMT_status.bat_exist)
		return false;
#ifdef CONFIG_LGE_PM_EXTERNAL_CHARGER_SUPPORT
	/*Noting to do */
#else
	if (BMT_status.bat_vol < batt_cust_data.recharging_voltage)
		return false;
#endif

	battery_charging_control(CHARGING_CMD_GET_CHARGING_STATUS, &data);
	eoc = data;

	battery_log(BAT_LOG_CRTI, "[BATTERY] EOC = %d\n", eoc);
	if (eoc == true)
		return true;

	return false;
}

static int charging_enabled_check(void)
{
	if (BMT_status.charging_enabled == KAL_FALSE)
		return 0;

	return 1;
}

static int battery_charging_enabled_check(void)
{
	if (BMT_status.bat_exist == KAL_FALSE)
		return 0;
	if (BMT_status.battery_charging_enabled == KAL_FALSE)
		return 0;
#ifdef CONFIG_LGE_PM_CHARGER_CONTROLLER
	if (BMT_status.battery_charging_enabled_cc == KAL_FALSE)
		return 0;
#endif

	return 1;
}

static void pchr_turn_on_charging (void)
{
	unsigned int charger_enable = true;	/* enable input current */
	unsigned int charging_enable = true;	/* enable battery charging current */

	if (BMT_status.bat_charging_state == CHR_ERROR) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charger Error, turn OFF charging\n");
		charger_enable = false;
	}
	battery_charging_control(CHARGING_CMD_SET_HIZ_SWCHR, &charger_enable);

	/* charger disabled, don't need to process below sequences */
	if (charger_enable == false)
		return;

	if (BMT_status.bat_charging_state == CHR_BATFULL) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery Full, turn OFF charging\n");
		charging_enable = false;
	} else if (BMT_status.bat_charging_state == CHR_HOLD) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charging Hold, turn OFF charging\n");
		charging_enable = false;
	} else if ((g_platform_boot_mode==META_BOOT) || (g_platform_boot_mode==ADVMETA_BOOT)) {
		battery_log(BAT_LOG_CRTI,
			"[BATTERY] In meta or advanced meta mode, disable charging.\n");
		charging_enable = false;
	} else {
		/*HW initialization*/
		battery_charging_control(CHARGING_CMD_INIT, NULL);

/*Base 6735 by JJB*/
#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
		if (BMT_status.charger_type == STANDARD_CHARGER) {
			battery_pump_express_algorithm_start();
		}
#endif
	}

	/* Set Charging Current */
	select_charging_current();

	/* If temp. limit by bcct, reset ibat current */
	if (g_bcct_flag == 1)
		select_charging_current_bcct();

	battery_charging_control(CHARGING_CMD_SET_INPUT_CURRENT, &g_temp_input_CC_value);
	battery_charging_control(CHARGING_CMD_SET_CURRENT, &g_temp_CC_value);
	battery_charging_control(CHARGING_CMD_SET_CV_VOLTAGE, &g_cv_voltage);

	/* enable/disable charging */
	battery_charging_control(CHARGING_CMD_ENABLE, &charging_enable);

	battery_log(BAT_LOG_CRTI, "[BATTERY] pchr_turn_on_charging(), enable=%d\n", charging_enable);
}

PMU_STATUS BAT_PreChargeModeAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Pre-CC mode charge, timer=%u on %u\n",
			BMT_status.PRE_charging_time, BMT_status.total_charging_time);

	BMT_status.PRE_charging_time += BAT_TASK_PERIOD;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.total_charging_time += BAT_TASK_PERIOD;

	if ( BMT_status.bat_vol > V_PRE2CC_THRES ) {
		BMT_status.bat_charging_state = CHR_CC;
	}

	if (!charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charging disabled\n");

		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_ERROR;
	} else if (!battery_charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery charging disabled\n");

		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_HOLD;
	}

	pchr_turn_on_charging();

	return PMU_STATUS_OK;
}

PMU_STATUS BAT_ConstantCurrentModeAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] CC mode charge, timer=%u on %u\n",
			BMT_status.CC_charging_time, BMT_status.total_charging_time);

	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time += BAT_TASK_PERIOD;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.total_charging_time += BAT_TASK_PERIOD;

	if (eoc_check() == true) {
		if (BMT_status.SOC >= 100) {
			BMT_status.bat_charging_state = CHR_BATFULL;
		}
		BMT_status.bat_full = true;
		g_charging_full_reset_bat_meter = true;
	}

	if (!charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charging disabled\n");

		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_ERROR;
	} else if (!battery_charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery charging disabled\n");

		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_HOLD;
	}


	pchr_turn_on_charging();

	return PMU_STATUS_OK;
}

PMU_STATUS BAT_TopOffModeAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Top Off mode charge, timer=%d on %d !!\n\r",
			BMT_status.TOPOFF_charging_time, BMT_status.total_charging_time);

	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time += BAT_TASK_PERIOD;
	BMT_status.total_charging_time += BAT_TASK_PERIOD;

	if (eoc_check() == true) {
		if (BMT_status.SOC >= 100) {
			BMT_status.bat_charging_state = CHR_BATFULL;
		}
		BMT_status.bat_full = true;
		g_charging_full_reset_bat_meter = true;
	}

	if (!charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charging disabled\n");

		BMT_status.bat_in_recharging_state = false;
		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_ERROR;
	} else if (!battery_charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery charging disabled\n");

		BMT_status.bat_in_recharging_state = false;
		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_HOLD;
	}

	pchr_turn_on_charging();

	return PMU_STATUS_OK;
}

PMU_STATUS BAT_BatteryFullAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Battery full\n");

	BMT_status.bat_full = true;
	BMT_status.total_charging_time = 0;
	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.POSTFULL_charging_time = 0;
	BMT_status.bat_in_recharging_state = false;

	if (recharging_check() == true) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery Re-charging\n");

		BMT_status.bat_in_recharging_state = true;
		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_CC;
	}

	if (!charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charging disabled\n");

		BMT_status.bat_in_recharging_state = false;
		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_ERROR;
	} else if (!battery_charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery charging disabled\n");

		BMT_status.bat_in_recharging_state = false;
		BMT_status.bat_full = false;
		BMT_status.bat_charging_state = CHR_HOLD;
	}

	pchr_turn_on_charging();

	return PMU_STATUS_OK;
}

PMU_STATUS BAT_BatteryHoldAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Charging Hold\n");

	BMT_status.bat_full = false;
	BMT_status.total_charging_time = 0;
	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.POSTFULL_charging_time = 0;
	BMT_status.bat_in_recharging_state = false;

	if (!charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charging disabled\n");

		BMT_status.bat_in_recharging_state = false;
		BMT_status.bat_charging_state = CHR_ERROR;
	} else if (battery_charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Battery charging enabled\n");

		BMT_status.bat_charging_state = CHR_CC;

#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT
		pep_det_rechg = KAL_TRUE;
#endif
	}

	/* Disable charging */
	pchr_turn_on_charging();

	return PMU_STATUS_OK;
}

PMU_STATUS BAT_BatteryStatusFailAction(void)
{
	battery_log(BAT_LOG_CRTI, "[BATTERY] Charging Error\n");

	BMT_status.total_charging_time = 0;
	BMT_status.PRE_charging_time = 0;
	BMT_status.CC_charging_time = 0;
	BMT_status.TOPOFF_charging_time = 0;
	BMT_status.POSTFULL_charging_time = 0;


	if (charging_enabled_check()) {
		battery_log(BAT_LOG_CRTI, "[BATTERY] Charging disabled\n");

		BMT_status.bat_charging_state = CHR_HOLD;

		if (battery_charging_enabled_check()) {
			battery_log(BAT_LOG_CRTI, "[BATTERY] Battery charging enabled\n");

			BMT_status.bat_charging_state = CHR_CC;

#ifdef CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT
			pep_det_rechg = KAL_TRUE;
#endif
		}
	}

	/* Disable charger */
	pchr_turn_on_charging();

	return PMU_STATUS_OK;
}

void mt_battery_charging_algorithm()
{
	battery_charging_control(CHARGING_CMD_RESET_WATCH_DOG_TIMER, NULL);

#ifdef CONFIG_LGE_PM_AT_CMD_SUPPORT
	if (get_AtCmdChargingModeOff()) {
		if (BMT_status.bat_charging_state != CHR_ERROR) {
			BMT_status.bat_charging_state = CHR_ERROR;
			pchr_turn_on_charging();
		}

		battery_charging_control(CHARGING_CMD_DUMP_REGISTER, NULL);
		return;
	}
#endif

#if defined(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)
	if (lge_is_factory_cable_boot()) {
		battery_log(BAT_LOG_CRTI, "%s: [PE+] Factory Cable Boot and Skip battery_pump_express_charger_check\n", __func__);
	} else {
/*Base 6735 by JJB*/
		battery_log(BAT_LOG_CRTI, "%s: [PE+] Start battery_pump_express_charger_check\n", __func__);
		if (pep_det_rechg == KAL_TRUE)
			ta_check_chr_type = KAL_TRUE;

		battery_pump_express_charger_check();
	}
#endif

	battery_log(BAT_LOG_CRTI, "[BATTERY] Charging State = 0x%x\n", BMT_status.bat_charging_state);
	switch (BMT_status.bat_charging_state) {
	case CHR_PRE :
		/* Default State */
		BAT_PreChargeModeAction();
		break;
	case CHR_CC :
		/* Constant Current Charging */
		BAT_ConstantCurrentModeAction();
		break;
	case CHR_TOP_OFF :
		/* Constant Voltage Charging : Not Used */
		BAT_TopOffModeAction();
		break;
	case CHR_BATFULL:
		/* End of Charging */
		BAT_BatteryFullAction();
		break;
	case CHR_HOLD:
		/* Charging Suspended */
		BAT_BatteryHoldAction();
		break;
	case CHR_ERROR:
		/* Charger Error */
		BAT_BatteryStatusFailAction();
		break;
	default:
		battery_log(BAT_LOG_CRTI, "[BATTERY] Should not be in here. Check the code.\n");
		BMT_status.bat_charging_state = CHR_PRE;
		break;
	}

	battery_charging_control(CHARGING_CMD_DUMP_REGISTER, NULL);
}

