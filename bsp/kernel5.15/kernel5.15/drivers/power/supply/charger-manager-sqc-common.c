// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This driver enables to monitor battery health and control charger
 * during suspend-to-mem.
 * Charger manager depends on other devices. Register this later than
 * the depending devices.
 *
**/

//This file has been modified by Unisoc (Shanghai) Technologies Co., Ltd in 2023.

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/firmware.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/power/sprd_battery_info.h>
#include <linux/reboot.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>
#include <linux/usb/sprd_pd.h>
#include <linux/workqueue.h>
#include <linux/usb/sprd_tcpm.h>
#include <linux/usb/sprd_pd.h>

/*zte add begin*/
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
#include <sqc_common.h>
int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
#endif
#include <vendor/common/zte_misc.h>
/*zte add done*/

/*
 * Default temperature threshold for charging.
 * Every temperature units are in tenth of centigrade.
 */
#define CM_DEFAULT_RECHARGE_TEMP_DIFF		50
#define CM_DEFAULT_CHARGE_TEMP_MAX		500
#define CM_UVLO_OFFSET				50000
#define CM_FORCE_SET_FUEL_CAP_FULL		1000
#define CM_LOW_TEMP_REGION			100
#define CM_UVLO_CALIBRATION_VOLTAGE_THRESHOLD	3400000
#define CM_UVLO_CALIBRATION_CNT_THRESHOLD	5
#define CM_LOW_TEMP_SHUTDOWN_VALTAGE		3400000
#define CM_LOW_CAP_SHUTDOWN_VOLTAGE_THRESHOLD	3400000
#define CM_UNKNOW_TYPE_CURRENT_THRESHOLD_H	2000000
#define CM_UNKNOW_TYPE_CURRENT_THRESHOLD_L	500000

#define CM_CAP_ONE_PERCENT			10
#define CM_HCAP_DECREASE_STEP			8
#define CM_HCAP_THRESHOLD			995
#define CM_CAP_FULL_PERCENT			1000
#define CM_MAGIC_NUM				0x5A5AA5A5
#define CM_CAPACITY_LEVEL_CRITICAL		0
#define CM_CAPACITY_LEVEL_LOW			15
#define CM_CAPACITY_LEVEL_NORMAL		85
#define CM_CAPACITY_LEVEL_FULL			100
#define CM_CAPACITY_LEVEL_CRITICAL_VOLTAGE	3400000

/* Fast charge public parameters */
#define CM_FAST_CHARGE_ENABLE_CURRENT		1200000
#define CM_FAST_CHARGE_ENABLE_THERMAL_CURRENT	1000000
#define CM_FAST_CHARGE_CURRENT_2A		2000000
#define CM_FAST_CHARGE_VOLTAGE_20V		20000000
#define CM_FAST_CHARGE_VOLTAGE_15V		15000000
#define CM_FAST_CHARGE_VOLTAGE_12V		12000000
#define CM_FAST_CHARGE_VOLTAGE_9V		9000000
#define CM_FAST_CHARGE_VOLTAGE_5V		5000000
#define CM_FAST_CHARGE_START_VOLTAGE_LTHRESHOLD	3520000
#define CM_FAST_CHARGE_START_VOLTAGE_HTHRESHOLD	4200000
#define CM_FAST_CHARGE_VOLTAGE_CHECK_COUNT	3

/* Fixed fast charge parameters */
#define CM_FIXED_FCHG_DISABLE_BATTERY_VOLTAGE	3400000
#define CM_FIXED_FCHG_DISABLE_CURRENT		1000000
#define CM_FIXED_FCHG_C2C_CURRENT		1000000
#define CM_FIXED_FCHG_C2C_IBUS_THRESHOLD	1500000
#define CM_FIXED_FCHG_TRANSITION_CURRENT_1P5A	1500000
#define CM_FIXED_FCHG_VOLTAGE_5V_THRESHOLD	6500000
#define CM_FIXED_FCHG_ENABLE_COUNT		3
#define CM_FIXED_FCHG_DISABLE_COUNT		2
#define CM_FIXED_FCHG_CHK_DIS_WORK_MS		5000
#define CM_FIXED_FCHG_TRY_DIS_WORK_MS		100

/* Dynamic fast charge parameters */
#define CM_CP_VSTEP				20000
#define CM_CP_ISTEP				50000
#define CM_CP_PRIMARY_CHARGER_DIS_TIMEOUT	20
#define CM_CP_TAPER_DELTA_VBAT_THRESHOLD	50000
#define CM_CP_TAPER_UCP_THRESHOLD		5
#define CM_CP_IBAT_UCP_THRESHOLD		8
#define CM_CP_ADJUST_VOLTAGE_THRESHOLD		(5 * 1000 / CM_CP_WORK_TIME_MS)
#define CM_CP_ACC_VBAT_HTHRESHOLD		3850000
#define CM_CP_VBAT_STEP1			300000
#define CM_CP_VBAT_STEP2			200000
#define CM_CP_VBAT_STEP3			100000
#define CM_CP_VBAT_STEP4			50000
#define CM_CP_VBAT_STEP5			10000
#define CM_CP_IBAT_STEP1			2000000
#define CM_CP_IBAT_STEP2			1000000
#define CM_CP_IBAT_STEP3			100000
#define CM_CP_VBUS_STEP1			2000000
#define CM_CP_VBUS_STEP2			1000000
#define CM_CP_VBUS_STEP3			50000
#define CM_CP_IBUS_STEP1			1000000
#define CM_CP_IBUS_STEP2			500000
#define CM_CP_IBUS_STEP3			100000
#define CM_CP_DEFAULT_TAPER_CURRENT		1000000

#define CM_CP_STEP_CHG_DOWN_COUNT		2

#define CM_PPS_5V_PROG_MAX			6200000
#define CM_PPS_VOLTAGE_11V			11000000
#define CM_PPS_VOLTAGE_16V			16000000
#define CM_PPS_VOLTAGE_21V			21000000

#define CM_CP_VBUS_ERRORLO_THRESHOLD(x)		((int)(x * 205 / 100))
#define CM_CP_VBUS_ERRORHI_THRESHOLD(x)		((int)(x * 240 / 100))

#define CM_IR_COMPENSATION_TIME			3

#define CM_CP_WORK_TIME_MS			500

#define CM_CHARGER_TYPE_WORK_TIME_MS		500
#define CM_CHARGER_TYPE_TIME_OUT_CNT		20

#define CM_CAP_ONE_TIME_16S			16
#define CM_CAP_ONE_TIME_8S			8
#define CM_CAP_ONE_TIME_4S			4
#define CM_CAP_CYCLE_TRACK_TIME_15S		15
#define CM_CAP_CYCLE_TRACK_TIME_8S		8
#define CM_CAP_CYCLE_TRACK_TIME_4S		4
#define CM_CAP_CYCLE_TRACK_TIME_2S		2
#define CM_CAP_CALC_BATT_WORKS_LOW_TEMP		50
#define CM_CAP_CALC_BATT_WORKS_LOW_CAP		50
#define CM_CAP_CALC_BATT_WORKS_BIG_CUR_UA	3000000
#define CM_CAP_CALC_BATT_WORKS_SOC_GAP		50
#define CM_INIT_BOARD_TEMP			250
#define CUTOFF_VOLTAGE_UV 3400000

#define TYPEC_CC_NC			6
#define TYPEC_CC_OPEN			0

#ifdef ZTE_FEATURE_PV_AR
extern int get_c_to_c(void);
extern int musb_lpm_usb_disconnect(void);
#endif


/* Google limit power transfer support */

#define CM_LIMIT_POWER_TRANSFER_MA		110

/*zte add begin*/
#define STANDARD_USB_HOST_CURRENT_MA 400

#if IS_ENABLED(CONFIG_USB_TYPEC_TEMP)
extern void sc27xx_typec_set_enable(bool enable);
#define EXT_TYPEC_TEMP_HOT_DEC 700
#define EXT_TYPEC_TEMP_WARM_DEC 650
#define EVENT_STRING_LENGTH 64
enum typec_theraml_state_enum {
	TYPEC_TEMP_UNKNOWN = 0,
	TYPEC_TEMP_NORML,
	TYPEC_TEMP_WARM,
	TYPEC_TEMP_HOT,
};
#endif

static const char * const cm_cp_state_names[] = {
	[CM_CP_STATE_UNKNOWN] = "Charge pump state: UNKNOWN",
	[CM_CP_STATE_RECOVERY] = "Charge pump state: RECOVERY",
	[CM_CP_STATE_ENTRY] = "Charge pump state: ENTRY",
	[CM_CP_STATE_CHECK_VBUS] = "Charge pump state: CHECK VBUS",
	[CM_CP_STATE_TUNE] = "Charge pump state: TUNE",
	[CM_CP_STATE_EXIT] = "Charge pump state: EXIT",
};

static char *charger_manager_supplied_to[] = {
	"audio-ldo",
};

/*
 * Regard CM_JIFFIES_SMALL jiffies is small enough to ignore for
 * delayed works so that we can run delayed works with CM_JIFFIES_SMALL
 * without any delays.
 */
#define	CM_JIFFIES_SMALL	(2)

/* If y is valid (> 0) and smaller than x, do x = y */
#define CM_MIN_VALID(x, y)	x = (((y > 0) && ((x) > (y))) ? (y) : (x))

/*
 * Regard CM_RTC_SMALL (sec) is small enough to ignore error in invoking
 * rtc alarm. It should be 2 or larger
 */
#define CM_RTC_SMALL		(2)

#define CM_EVENT_TYPE_NUM	6

static struct charger_type charger_usb_type[20] = {
	{POWER_SUPPLY_USB_TYPE_SDP, CM_CHARGER_TYPE_SDP},
	{POWER_SUPPLY_USB_TYPE_DCP, CM_CHARGER_TYPE_DCP},
	{POWER_SUPPLY_USB_TYPE_CDP, CM_CHARGER_TYPE_CDP},
	{POWER_SUPPLY_USB_TYPE_UNKNOWN, CM_CHARGER_TYPE_UNKNOWN},
};

static struct charger_type charger_fchg_type[20] = {
	{POWER_SUPPLY_CHARGE_TYPE_FAST, CM_CHARGER_TYPE_FAST},
	{POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE, CM_CHARGER_TYPE_ADAPTIVE},
	{POWER_SUPPLY_CHARGE_TYPE_UNKNOWN, CM_CHARGER_TYPE_UNKNOWN},
};

static struct charger_type charger_wireless_type[20] = {
	{POWER_SUPPLY_WIRELESS_CHARGER_TYPE_BPP, CM_WIRELESS_CHARGER_TYPE_BPP},
	{POWER_SUPPLY_WIRELESS_CHARGER_TYPE_EPP, CM_WIRELESS_CHARGER_TYPE_EPP},
	{POWER_SUPPLY_WIRELESS_CHARGER_TYPE_UNKNOWN, CM_CHARGER_TYPE_UNKNOWN},
};

struct charger_otg_match {
	const char *charger_power_supply_name;
	const char *otg_name;
};

static LIST_HEAD(cm_list);
static DEFINE_MUTEX(cm_list_mtx);

/* About in-suspend (suspend-again) monitoring */
static struct alarm *cm_timer;

static bool cm_suspended;
static bool cm_timer_set;
static unsigned long cm_suspend_duration_ms;
static int cm_event_num;
static enum cm_event_types cm_event_type[CM_EVENT_TYPE_NUM];
static char *cm_event_msg[CM_EVENT_TYPE_NUM];

/* About normal (not suspended) monitoring */
static unsigned long next_polling; /* Next appointed polling time */
static struct workqueue_struct *cm_wq; /* init at driver add */

static bool allow_charger_enable;
static bool is_charger_mode;
struct charger_manager *g_cm;
static void cm_notify_type_handle(struct charger_manager *cm, enum cm_event_types type, char *msg);
static void cm_update_charger_type_status(struct charger_manager *cm);
static int cm_manager_get_jeita_status(struct charger_manager *cm, int cur_temp);
static bool cm_charger_is_support_fchg(struct charger_manager *cm);
static int cm_get_battery_temperature(struct charger_manager *cm, int *temp);
static bool cm_pd_is_ac_online(struct charger_manager *cm);
static void cm_update_charge_info(struct charger_manager *cm, int cmd);

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
int zte_sqc_get_property(enum zte_power_supply_property psp,
	union power_supply_propval *val);
int zte_sqc_set_property(enum zte_power_supply_property psp,
	const union power_supply_propval *val);
#endif
/*
 * cm_cap_advance_full - capacity value are
 * reported in advance based on percentage
 * @ fuel_cap: cap from fuel gauge
 * Return the fuel cap
 */
static int cm_cap_advance_full(struct charger_manager *cm, int fuel_cap)
{
	if (cm->desc->cap_remap_full_percent) {
		fuel_cap = fuel_cap * 100 / cm->desc->cap_remap_full_percent;
		if (fuel_cap > CM_CAP_FULL_PERCENT)
			fuel_cap  = CM_CAP_FULL_PERCENT;
	}

	return fuel_cap;
}

/**
 * is_batt_present - See if the battery presents in place.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_batt_present(struct charger_manager *cm)
{
	union power_supply_propval val;
	struct power_supply *psy;
	bool present = false;
	int i, ret;

	switch (cm->desc->battery_present) {
	case CM_BATTERY_PRESENT:
		present = true;
		break;
	case CM_NO_BATTERY:
		break;
	case CM_FUEL_GAUGE:
		psy = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
		if (!psy)
			break;

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
		if (ret == 0 && val.intval)
			present = true;
		power_supply_put(psy);
		break;
	case CM_CHARGER_STAT:
		for (i = 0; cm->desc->psy_charger_stat[i]; i++) {
			psy = power_supply_get_by_name(
					cm->desc->psy_charger_stat[i]);
			if (!psy) {
				dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
					cm->desc->psy_charger_stat[i]);
				continue;
			}

			ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
			power_supply_put(psy);
			if (ret == 0 && val.intval) {
				present = true;
				break;
			}
		}
		break;
	}

	return present;
}

static bool is_ext_wl_pwr_online(struct charger_manager *cm)
{
	union power_supply_propval val;
	struct power_supply *psy;
	bool online = false;
	int i, ret;

	if (!cm->desc->psy_wl_charger_stat)
		return online;

	/* If at least one of them has one, it's yes. */
	for (i = 0; cm->desc->psy_wl_charger_stat[i]; i++) {
		psy = power_supply_get_by_name(cm->desc->psy_wl_charger_stat[i]);
		if (!psy) {
			dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
					cm->desc->psy_wl_charger_stat[i]);
			continue;
		}

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
		power_supply_put(psy);
		if (ret == 0 && val.intval) {
			online = true;
			break;
		}
	}

	return online;
}

/**
 * is_ext_usb_pwr_online - See if an external power source is attached to charge
 * @cm: the Charger Manager representing the battery.
 *
 * Returns true if at least one of the chargers of the battery has an external
 * power source attached to charge the battery regardless of whether it is
 * actually charging or not.
 */
static bool is_ext_usb_pwr_online(struct charger_manager *cm)
{
	bool online = false;

	if (cm->vchg_info->ops && cm->vchg_info->ops->is_charger_online)
		return cm->vchg_info->ops->is_charger_online(cm->vchg_info);

	return online;
}

static bool is_ext_pwr_online(struct charger_manager *cm)
{
	bool online = false;

	if (is_ext_usb_pwr_online(cm) || is_ext_wl_pwr_online(cm))
		online = true;

	return online;
}

 /**
  * get_ibat_avg_uA - Get the current level of the battery
  * @cm: the Charger Manager representing the battery.
  * @uA: the current level returned.
  *
  * Returns 0 if there is no error.
  * Returns a negative value on error.
  */
static int get_ibat_avg_uA(struct charger_manager *cm, int *uA)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CURRENT_AVG, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*uA = val.intval;
	return 0;
}

 /**
  * get_ibat_now_uA - Get the current level of the battery
  * @cm: the Charger Manager representing the battery.
  * @uA: the current level returned.
  *
  * Returns 0 if there is no error.
  * Returns a negative value on error.
  */
static int get_ibat_now_uA(struct charger_manager *cm, int *uA)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	val.intval = 0;
	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CURRENT_NOW, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*uA = val.intval;
	return 0;
}

/**
 *
 * get_vbat_avg_uV - Get the voltage level of the battery
 * @cm: the Charger Manager representing the battery.
 * @uV: the voltage level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_vbat_avg_uV(struct charger_manager *cm, int *uV)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_VOLTAGE_AVG, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*uV = val.intval;
	return 0;
}

/*
 * get_batt_ocv - Get the battery ocv
 * level of the battery.
 * @cm: the Charger Manager representing the battery.
 * @uV: the voltage level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_batt_ocv(struct charger_manager *cm, int *ocv)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_VOLTAGE_OCV, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*ocv = val.intval;
	return 0;
}

/*
 * get_batt_now - Get the battery voltage now
 * level of the battery.
 * @cm: the Charger Manager representing the battery.
 * @uV: the voltage level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_vbat_now_uV(struct charger_manager *cm, int *ocv)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_VOLTAGE_NOW, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*ocv = val.intval;
	return 0;
}

/**
 * get_batt_cap - Get the cap level of the battery
 * @cm: the Charger Manager representing the battery.
 * @uV: the cap level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_batt_cap(struct charger_manager *cm, int *cap)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	val.intval = CM_CAPACITY;
	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CAPACITY, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*cap = val.intval;
	dev_info(cm->dev, "%s: %d\n", __func__, *cap);
	return 0;
}

/**
 * get_batt_total_cap - Get the total capacity level of the battery
 * @cm: the Charger Manager representing the battery.
 * @uV: the total_cap level returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_batt_total_cap(struct charger_manager *cm, u32 *total_cap)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_ENERGY_FULL,
					&val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*total_cap = val.intval;

	return 0;
}

/*
 * get_boot_cap - Get the battery boot capacity
 * of the battery.
 * @cm: the Charger Manager representing the battery.
 * @cap: the battery capacity returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_boot_cap(struct charger_manager *cm, int *cap)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	val.intval = CM_BOOT_CAPACITY;
	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CAPACITY, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		return ret;

	*cap = val.intval;
	return 0;
}

static int cm_get_charge_cycle(struct charger_manager *cm, int *cycle)
{
	struct power_supply *fuel_gauge = NULL;
	union power_supply_propval val;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge) {
		ret = -ENODEV;
		return ret;
	}

	*cycle = 0;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CYCLE_COUNT, &val);
	if (ret)
		return ret;

	power_supply_put(fuel_gauge);
	*cycle = val.intval;

	return 0;
}

static int cm_get_bat_aging_id(struct charger_manager *cm, int *aging_bat_id)
{
	int charge_cycle, ret = 0;

	*aging_bat_id = 0;

	ret = cm_get_charge_cycle(cm, &charge_cycle);
	if (ret) {
		dev_err(cm->dev, "failed to get charge cycle, ret = %d\n", ret);
		return ret;
	}

	*aging_bat_id = sprd_battery_get_aging_bat_id(cm->charger_psy, charge_cycle);
	dev_info(cm->dev, "%s %d, aging_bat_id = %d\n", __func__, __LINE__, *aging_bat_id);

	return ret;
}

static int cm_set_charge_cycle(struct charger_manager *cm, int cycle)
{
	struct power_supply *fuel_gauge = NULL;
	union power_supply_propval val;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge) {
		dev_err(cm->dev, "can not find fuel gauge device\n");
		ret = -ENODEV;
		return ret;
	}

	val.intval = cycle;
	ret = power_supply_set_property(fuel_gauge, POWER_SUPPLY_PROP_CYCLE_COUNT, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		dev_err(cm->dev, "failed to set battery cycle_count\n");

	return 0;
}

static int cm_get_bc1p2_type(struct charger_manager *cm, u32 *type)
{
	int ret = -EINVAL;

	*type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	if (cm->vchg_info->ops && cm->vchg_info->ops->get_bc1p2_type) {
		*type = cm->vchg_info->ops->get_bc1p2_type(cm->vchg_info);
		ret = 0;
	}

	return ret;
}

static void cm_get_charger_type(struct charger_manager *cm,
				enum cm_charger_type_flag chg_type_flag,
				u32 *type)
{
	struct charger_type *chg_type;
	enum cm_charger_type match_type = CM_CHARGER_TYPE_UNKNOWN;

	switch (chg_type_flag) {
	case CM_FCHG_TYPE:
		chg_type = charger_fchg_type;
		break;
	case CM_WL_TYPE:
		chg_type = charger_wireless_type;
		break;
	case CM_USB_TYPE:
	default:
		chg_type = charger_usb_type;
		break;
	}

	if (!chg_type) {
		dev_err(cm->dev, "%s, chg_type is NULL\n", __func__);
		*type = CM_CHARGER_TYPE_UNKNOWN;
		return;
	}

	while ((chg_type)->adap_type != CM_CHARGER_TYPE_UNKNOWN) {
		if (*type == chg_type->psy_type) {
			match_type = chg_type->adap_type;
			break;
		}

		chg_type++;
	}

	*type = match_type;
}

/**
 * get_usb_charger_type - Get the charger type
 * @cm: the Charger Manager representing the battery.
 * @type: the charger type returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_usb_charger_type(struct charger_manager *cm, u32 *type)
{
	int ret;

	mutex_lock(&cm->desc->charger_type_mtx);
	if (cm->desc->is_fast_charge) {
		mutex_unlock(&cm->desc->charger_type_mtx);
		return 0;
	}

	ret = cm_get_bc1p2_type(cm, type);
	cm_get_charger_type(cm, CM_USB_TYPE, type);

	mutex_unlock(&cm->desc->charger_type_mtx);
	return ret;
}


/**
 * set_batt_cap - Set the cap level of the battery
 * @cm: the Charger Manager representing the battery.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int set_batt_cap(struct charger_manager *cm, int cap)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge) {
		dev_err(cm->dev, "can not find fuel gauge device\n");
		return -ENODEV;
	}

	dev_dbg(cm->dev, "%s:line%d cap = %d\n", __func__, __LINE__, cap);

	val.intval = cap;
	ret = power_supply_set_property(fuel_gauge, POWER_SUPPLY_PROP_CAPACITY, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		dev_err(cm->dev, "failed to save current battery capacity\n");

	return ret;
}
/**
 * get_charger_voltage - Get the charging voltage from fgu
 * @cm: the Charger Manager representing the battery.
 * @cur: the charging input voltage returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_charger_voltage(struct charger_manager *cm, int *vol)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret = -ENODEV;

	if (!is_ext_pwr_online(cm))
		return 0;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge) {
		dev_err(cm->dev, "Cannot find power supply  %s\n",
			cm->desc->psy_fuel_gauge);
		return	ret;
	}

	ret = power_supply_get_property(fuel_gauge,
					POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
	power_supply_put(fuel_gauge);
	if (ret == 0)
		*vol = val.intval;

	return ret;
}

/**
 * adjust_fuel_cap - Adjust the fuel cap level
 * @cm: the Charger Manager representing the battery.
 * @cap: the adjust fuel cap level.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int adjust_fuel_cap(struct charger_manager *cm, int cap)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	dev_dbg(cm->dev, "%s:line%d cap = %d\n", __func__, __LINE__, cap);

	val.intval = cap;
	ret = power_supply_set_property(fuel_gauge,
					POWER_SUPPLY_PROP_CALIBRATE, &val);
	power_supply_put(fuel_gauge);
	if (ret)
		dev_err(cm->dev, "failed to adjust fuel cap\n");

	return ret;
}

/**
 * get_constant_charge_current - Get the charging current from charging ic
 * @cm: the Charger Manager representing the battery.
 * @cur: the charging current returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_constant_charge_current(struct charger_manager *cm, int *cur)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int i, ret = -ENODEV;

	*cur = 0;

	/* If at least one of them has one, it's yes. */
	for (i = 0; cm->desc->psy_charger_stat[i]; i++) {
		psy = power_supply_get_by_name(cm->desc->psy_charger_stat[i]);
		if (!psy) {
			dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
				cm->desc->psy_charger_stat[i]);
			continue;
		}

		ret = power_supply_get_property(psy,
						POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
		power_supply_put(psy);
		if (ret == 0) {
			*cur += val.intval;
		}
	}

	return ret;
}

/**
 * get_input_current_limit - Get the input current limit from charging ic
 * @cm: the Charger Manager representing the battery.
 * @cur: the charging input limit current returned.
 *
 * Returns 0 if there is no error.
 * Returns a negative value on error.
 */
static int get_input_current_limit(struct charger_manager *cm, int *cur)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int i, ret = -ENODEV;

	*cur = 0;

	/* If at least one of them has one, it's yes. */
	for (i = 0; cm->desc->psy_charger_stat[i]; i++) {
		psy = power_supply_get_by_name(cm->desc->psy_charger_stat[i]);
		if (!psy) {
			dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
				cm->desc->psy_charger_stat[i]);
			continue;
		}

		ret = power_supply_get_property(psy,
						POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
						&val);
		power_supply_put(psy);
		if (ret == 0)
			*cur += val.intval;
	}

	return ret;
}

static void cm_set_charger_present(struct charger_manager *cm, bool present)
{
	int ret, i;
	union power_supply_propval val = {0,};
	struct power_supply *psy;

	for (i = 0; cm->desc->psy_charger_stat[i]; i++) {
		psy = power_supply_get_by_name(cm->desc->psy_charger_stat[i]);
		if (!psy) {
			dev_err(cm->dev, "Cannot find primary power supply \"%s\"\n",
				cm->desc->psy_charger_stat[i]);
			continue;
		}

		val.intval = present;
		ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
		power_supply_put(psy);
		if (ret) {
			dev_err(cm->dev, "Fail to set present[%d] of %s, ret = %d\n",
				present, cm->desc->psy_charger_stat[i], ret);
			continue;
		}
	}
}

/**
 * is_charging - Returns true if the battery is being charged.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_charging(struct charger_manager *cm)
{
	bool charging = false;
	struct power_supply *psy;
	union power_supply_propval val;
	int i, ret;

	/* If there is no battery, it cannot be charged */
	if (!is_batt_present(cm))
		return false;

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	sqc_get_property(POWER_SUPPLY_PROP_STATUS, &val);
	if (val.intval == POWER_SUPPLY_STATUS_FULL ||
				val.intval == POWER_SUPPLY_STATUS_DISCHARGING ||
				val.intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
		charging = false;
	else
		charging = true;

	return charging;
#endif

	if (!is_ext_pwr_online(cm))
		return charging;

	/* If at least one of the charger is charging, return yes */
	for (i = 0; cm->desc->psy_charger_stat[i]; i++) {
		/* 1. The charger sholuld not be DISABLED */
		if (cm->emergency_stop)
			continue;
		if (!cm->charger_enabled)
			continue;

		psy = power_supply_get_by_name(cm->desc->psy_charger_stat[i]);
		if (!psy) {
			dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
					cm->desc->psy_charger_stat[i]);
			continue;
		}

		/*
		 * 2. The charger should not be FULL, DISCHARGING,
		 * or NOT_CHARGING.
		 */
		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_STATUS,
				&val);
		power_supply_put(psy);
		if (ret) {
			dev_warn(cm->dev, "Cannot read STATUS value from %s\n",
				 cm->desc->psy_charger_stat[i]);
			continue;
		}
		if (val.intval == POWER_SUPPLY_STATUS_FULL ||
		    val.intval == POWER_SUPPLY_STATUS_DISCHARGING ||
		    val.intval == POWER_SUPPLY_STATUS_NOT_CHARGING)
			continue;

		/* Then, this is charging. */
		charging = true;
		break;
	}

	return charging;
}

static bool cm_primary_charger_enable(struct charger_manager *cm, bool enable)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int ret;

	if (!cm->desc->psy_charger_stat || !cm->desc->psy_charger_stat[0])
		return false;

	psy = power_supply_get_by_name(cm->desc->psy_charger_stat[0]);
	if (!psy) {
		dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
			cm->desc->psy_charger_stat[0]);
		return false;
	}

	val.intval = enable;
	ret = power_supply_set_property(psy, POWER_SUPPLY_PROP_CALIBRATE, &val);
	power_supply_put(psy);
	if (ret) {
		dev_err(cm->dev, "failed to %s primary charger, ret = %d\n",
			enable ? "enable" : "disable", ret);
		return false;
	}

	return true;
}

/**
 * is_full_charged - Returns true if the battery is fully charged.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_full_charged(struct charger_manager *cm)
{
	struct charger_desc *desc = cm->desc;
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	bool is_full = false;
	int ret = 0;
	int uV, uA;

	/* If there is no battery, it cannot be charged */
	if (!is_batt_present(cm))
		return false;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return false;

	/* ZTE Set force_set_full and is_full_charged as false when in long time charging policy */
#if IS_ENABLED(CONFIG_VENDOR_CHARGER_POLICY_SERVICE)
	if (charger_policy_get_status() == true) {
		dev_info(cm->dev, "%s:charger_policy_get_status true\n", __func__);
		cm->desc->force_set_full = false;
		return false;
	}
#endif

	if (desc->fullbatt_full_capacity > 0) {
		val.intval = 0;

		/* Not full if capacity of fuel gauge isn't full */
		ret = power_supply_get_property(fuel_gauge,
						POWER_SUPPLY_PROP_CAPACITY, &val);
		if (!ret && val.intval >= desc->fullbatt_full_capacity * 10) {
			dev_info(cm->dev, "fgu report full!\n");
			is_full = true;
			goto out;
		}
	}

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	sqc_get_property(POWER_SUPPLY_PROP_STATUS, &val);
	if (val.intval == POWER_SUPPLY_STATUS_FULL) {
		if (cm->health == POWER_SUPPLY_HEALTH_WARM) {
			dev_info(cm->dev, "charger ic report warm full! health: %d\n", cm->health);
			is_full = true;
			cm->desc->force_set_full = true;
		} else if (cm->health == POWER_SUPPLY_HEALTH_COOL || cm->health == POWER_SUPPLY_HEALTH_GOOD) {
			ret = get_vbat_avg_uV(cm, &uV);
			if (ret)
				goto out;
			if (uV >= desc->fullbatt_uV)
				adjust_fuel_cap(cm, CM_FORCE_SET_FUEL_CAP_FULL);

			dev_info(cm->dev, "charger ic reprot normal full! health: %d\n", cm->health);
			is_full = true;
			cm->desc->force_set_full = true;
		}
	}
	goto out;
#endif

	/* Full, if it's over the fullbatt voltage */
	if (desc->fullbatt_uV > 0 && desc->fullbatt_uA > 0) {
		ret = get_vbat_now_uV(cm, &uV);
		if (ret)
			goto out;

		ret = get_ibat_now_uA(cm, &uA);
		if (ret)
			goto out;

		/* Battery is already full, checks voltage drop. */
		if (cm->battery_status == POWER_SUPPLY_STATUS_FULL && desc->fullbatt_vchkdrop_uV) {
			int batt_ocv;

			ret = get_batt_ocv(cm, &batt_ocv);
			if (ret || batt_ocv < 0)
				goto out;

			if ((u32)batt_ocv > (cm->desc->fullbatt_uV - cm->desc->fullbatt_vchkdrop_uV))
				is_full = true;
			goto out;
		}

		if (desc->first_fullbatt_uA > 0 && uV >= desc->fullbatt_uV &&
		    uA > desc->fullbatt_uA && uA <= desc->first_fullbatt_uA && uA >= 0) {
			if (++desc->first_trigger_cnt > 1)
				cm->desc->force_set_full = true;
		} else {
			desc->first_trigger_cnt = 0;
		}

		if (uV >= desc->fullbatt_uV && uA <= desc->fullbatt_uA && uA >= 0) {
			if (++desc->trigger_cnt > 1) {
				if (cm->desc->cap >= CM_CAP_FULL_PERCENT) {
					if (desc->trigger_cnt == 2)
						adjust_fuel_cap(cm, CM_FORCE_SET_FUEL_CAP_FULL);
					is_full = true;
				} else {
					is_full = false;
					adjust_fuel_cap(cm, CM_FORCE_SET_FUEL_CAP_FULL);
					if (desc->trigger_cnt == 2)
						cm_primary_charger_enable(cm, false);
				}
				cm->desc->force_set_full = true;
			} else {
				is_full = false;
			}
			goto out;
		} else {
			is_full = false;
			desc->trigger_cnt = 0;
			goto out;
		}
	}

	/* Full, if the capacity is more than fullbatt_soc */
	if (desc->fullbatt_soc > 0) {
		val.intval = 0;

		ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CAPACITY, &val);
		if (!ret && val.intval >= desc->fullbatt_soc) {
			is_full = true;
			goto out;
		}
	}

out:
	power_supply_put(fuel_gauge);
	return is_full;
}

/**
 * is_polling_required - Return true if need to continue polling for this CM.
 * @cm: the Charger Manager representing the battery.
 */
static bool is_polling_required(struct charger_manager *cm)
{
	switch (cm->desc->polling_mode) {
	case CM_POLL_DISABLE:
		return false;
	case CM_POLL_ALWAYS:
		return true;
	case CM_POLL_EXTERNAL_POWER_ONLY:
		return is_ext_pwr_online(cm);
	case CM_POLL_CHARGING_ONLY:
		return is_charging(cm);
	default:
		dev_warn(cm->dev, "Incorrect polling_mode (%d)\n",
			 cm->desc->polling_mode);
	}

	return false;
}

/*
 *  Relying on the fast charging protocol of DP/DM for handshake,
 *  the handshake can only be perfomed after the BC1.2 result is
 *  identified as DCP, such as the SFCP protocol.
 */
static void cm_enable_fixed_fchg_handshake(struct charger_manager *cm, bool enable)
{
	dev_info(cm->dev, "%s, handshake=%d, support_fchg=%d, is_fast_charge=%d, charger_type=%d\n",
		__func__, enable, cm->fchg_info->support_fchg, cm->desc->is_fast_charge, cm->desc->charger_type);

	if (!cm->fchg_info || !cm->fchg_info->ops || !cm->fchg_info->ops->enable_fixed_fchg) {
		dev_err(cm->dev, "%s, fchg_info or ops or enable_fixed_fchg is null\n", __func__);
		return;
	}

	if (!cm->desc->support_fixed_fchg || !cm->fchg_info->support_fchg) {
			dev_err(cm->dev, "%s, support_fixed_fchg or support_fchg is null\n", __func__);
			return;
	}

	if (enable && !cm->desc->is_fast_charge &&
	    cm->desc->charger_type == CM_CHARGER_TYPE_DCP)
		cm->fchg_info->ops->enable_fixed_fchg(cm->fchg_info, true);
	else if (!enable)
		cm->fchg_info->ops->enable_fixed_fchg(cm->fchg_info, false);
}

static int cm_set_charging_status(struct charger_manager *cm, bool enable)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	int ret = 0;

	val.intval = enable ? POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_DISCHARGING;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENOMEM;

	ret = power_supply_set_property(fuel_gauge, POWER_SUPPLY_PROP_STATUS, &val);
	power_supply_put(fuel_gauge);

	return ret;
}

static int cm_get_battery_temperature(struct charger_manager *cm, int *temp)
{
	struct power_supply *fuel_gauge;
	int ret;
	int64_t temp_val;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_TEMP,
				(union power_supply_propval *)&temp_val);
	power_supply_put(fuel_gauge);

	if (ret == 0)
		*temp = (int)temp_val;
	return ret;
}

static int cm_get_board_temperature(struct charger_manager *cm, int *temp)
{
	int ret = 0;

	*temp = CM_INIT_BOARD_TEMP;
	if (!cm->desc->measure_battery_temp)
		return -ENODEV;

#if IS_ENABLED(CONFIG_THERMAL)
	if (cm->tzd_batt) {
		ret = thermal_zone_get_temp(cm->tzd_batt, temp);
		if (!ret) {
			/* Calibrate temperature unit */
			*temp /= 100;
			return ret;
		}
	}
#endif
	dev_err(cm->dev, "Can not to get board temperature, return init_temp=%d\n", *temp);

	return ret;
}

enum cm_manager_jeita_status {
	STATUS_BELOW_T0 = 0,
	STATUS_T0_TO_T1,
	STATUS_T1_TO_T2,
	STATUS_T2_TO_T3,
	STATUS_T3_TO_T4,
	STATUS_ABOVE_T4,
};

static void jeita_info_init(struct cm_jeita_info *jeita_info)
{
	jeita_info->temp_up_trigger = 0;
	jeita_info->temp_down_trigger = 0;
	jeita_info->jeita_changed = true;
	jeita_info->jeita_status = 0;
	jeita_info->jeita_temperature = -200;
}

static int cm_manager_get_jeita_status(struct charger_manager *cm, int cur_temp)
{
	struct charger_desc *desc = cm->desc;
	static int jeita_status = STATUS_T2_TO_T3;
	int i;

	for (i = desc->jeita_tab_size - 1; i >= 0; i--) {
		if ((cur_temp >= desc->jeita_tab[i].temp && i > 0) ||
		    (cur_temp > desc->jeita_tab[i].temp && i == 0)) {
			break;
		}
	}

	switch (i) {
	case 4:
		jeita_status = STATUS_ABOVE_T4;
		break;
	case 3:
		if (jeita_status != STATUS_ABOVE_T4 ||
		    cur_temp <= desc->jeita_tab[4].recovery_temp)
			jeita_status = STATUS_T3_TO_T4;
		break;

	case 2:
		if ((jeita_status != STATUS_T3_TO_T4 ||
		     cur_temp <= desc->jeita_tab[3].recovery_temp) &&
		    (jeita_status != STATUS_T1_TO_T2 ||
		     cur_temp >= desc->jeita_tab[2].recovery_temp))
			jeita_status = STATUS_T2_TO_T3;
		break;

	case 1:
		if (jeita_status != STATUS_T0_TO_T1 ||
		     cur_temp >= desc->jeita_tab[1].recovery_temp)
			jeita_status = STATUS_T1_TO_T2;
		break;

	case 0:
		if (jeita_status != STATUS_BELOW_T0 ||
		    cur_temp >= desc->jeita_tab[0].recovery_temp)
			jeita_status = STATUS_T0_TO_T1;
		break;

	default:
		jeita_status = STATUS_BELOW_T0;
		break;
	}
	return jeita_status;
}

/**
 * cm_charger_type_polling - Polling charger type.
 * @cm: the Charger Manager representing the battery.
 */
static int cm_charger_type_polling(struct charger_manager *cm)
{
	int ret;
	u32 type;

	if (!is_ext_usb_pwr_online(cm))
		return 0;

	if (cm->desc->charger_type != POWER_SUPPLY_USB_TYPE_UNKNOWN)
		return 0;

	ret = cm_get_bc1p2_type(cm, &type);
	if (!ret && type != POWER_SUPPLY_USB_TYPE_UNKNOWN)
		cm->vchg_info->charger_type_cnt++;

	if (cm->vchg_info->charger_type_cnt > 1) {
		dev_info(cm->dev, "%s: update charger type:%d\n", __func__, type);
		cm->desc->charger_type = type;
		cm_update_charger_type_status(cm);
		cm_update_charge_info(cm, (CM_CHARGE_INFO_CHARGE_LIMIT |
					   CM_CHARGE_INFO_INPUT_LIMIT |
					   CM_CHARGE_INFO_JEITA_LIMIT));
		power_supply_changed(cm->charger_psy);
	}

	return ret;
}

static void cm_charger_type_update_check_start(struct charger_manager *cm)
{
	if (cm->desc->charger_type == POWER_SUPPLY_USB_TYPE_UNKNOWN &&
	    cm->desc->charge_type_poll_count < CM_CHARGER_TYPE_TIME_OUT_CNT) {
		cm->desc->charge_type_poll_count++;
		schedule_delayed_work(&cm->charger_type_update_work,
				      msecs_to_jiffies(CM_CHARGER_TYPE_WORK_TIME_MS));
	}
}

static void cm_charger_type_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct charger_manager *cm = container_of(dwork,
						  struct charger_manager,
						  charger_type_update_work);

	cm_charger_type_polling(cm);
	cm_charger_type_update_check_start(cm);
}

#if IS_ENABLED(CONFIG_USB_TYPEC_TEMP)
static int cm_get_typec_temperature(struct charger_manager *cm, int *temp)
{
	struct zte_power_supply *zte_fuel_gauge;
	int ret;
	union power_supply_propval temp_val = {0,};

	zte_fuel_gauge = zte_power_supply_get_by_name(cm->desc->zte_psy_fuel_gauge);
	if (!zte_fuel_gauge)
		return -ENODEV;

	ret = zte_power_supply_get_property(zte_fuel_gauge,
				POWER_SUPPLY_PROP_TYPEC_TEMP,
				&temp_val);
	zte_power_supply_put(zte_fuel_gauge);

	if (ret == 0)
		*temp = temp_val.intval;
	return ret;
}

static void cm_typec_temp_works(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct charger_manager *cm = container_of(dwork,
				struct charger_manager, update_typec_thermal_work);
	char event_string[EVENT_STRING_LENGTH];
	char *envp[2] = {event_string, NULL};

	if (!cm) {
		pr_err("cm fatal error: cm=null\n");
		return;
	}
	if (!cm->uevent_typec_thermal_device) {
		pr_err("cm uevent_typec_thermal_device error:null\n");
		return;
	}
	cm_get_typec_temperature(cm, &cm->typec_thermal_temp);
	pr_info("typec_thermal temp=%d status=%d\n", cm->typec_thermal_temp, cm->typec_thermal_status);
	if (cm->typec_thermal_temp >= EXT_TYPEC_TEMP_HOT_DEC && cm->typec_thermal_status != TYPEC_TEMP_HOT) {
		cm->typec_thermal_status = TYPEC_TEMP_HOT;
		cm->is_disconnect_cc = true;
		sc27xx_typec_set_enable(false);
		pr_info("typec_status=HOT\n");
		snprintf(event_string, EVENT_STRING_LENGTH, "typec_status=HOT");
		kobject_uevent_env(&cm->uevent_typec_thermal_device->dev.kobj, KOBJ_CHANGE, envp);
	} else if (cm->typec_thermal_temp >= EXT_TYPEC_TEMP_WARM_DEC && cm->typec_thermal_temp < EXT_TYPEC_TEMP_HOT_DEC && cm->typec_thermal_status != TYPEC_TEMP_WARM) {
		cm->typec_thermal_status = TYPEC_TEMP_WARM;
		pr_info("typec_status=WARM\n");
		snprintf(event_string, EVENT_STRING_LENGTH, "typec_status=WARM");
		kobject_uevent_env(&cm->uevent_typec_thermal_device->dev.kobj, KOBJ_CHANGE, envp);
	} else if (cm->typec_thermal_temp < EXT_TYPEC_TEMP_WARM_DEC && cm->typec_thermal_status != TYPEC_TEMP_NORML) {
		cm->typec_thermal_status = TYPEC_TEMP_NORML;
		pr_info("typec_status=NORMAL\n");
		if (cm->is_disconnect_cc) {
			sc27xx_typec_set_enable(true);
			cm->is_disconnect_cc = false;
		}

		snprintf(event_string, EVENT_STRING_LENGTH, "typec_status=NORMAL");
		kobject_uevent_env(&cm->uevent_typec_thermal_device->dev.kobj, KOBJ_CHANGE, envp);
	}

	schedule_delayed_work(&cm->update_typec_thermal_work,
				  msecs_to_jiffies(10000));

}

int typec_temp_init(struct platform_device *pdev, struct charger_manager *cm)
{
	int ret = -1;

	if (!cm || !pdev) {
		pr_err("[%s] cm or pdev is null\n", __func__);
		return ret;
	}

	pr_err("[%s] enter!\n", __func__);

	cm_get_typec_temperature(cm, &cm->desc->typec_temperature);

	cm->typec_thermal_status = TYPEC_TEMP_UNKNOWN;
	cm->uevent_typec_thermal_device = platform_device_alloc("ext_typec_thermal", -1);
	if (!cm->uevent_typec_thermal_device) {
		pr_err("%s failed to allocate platform device", __func__);
	}

	if (cm->uevent_typec_thermal_device) {
		ret = platform_device_add(cm->uevent_typec_thermal_device);
		if (ret < 0) {
			pr_err("%s failed to add platform device ret=%d", __func__, ret);
		}
	}

	INIT_DELAYED_WORK(&cm->update_typec_thermal_work, cm_typec_temp_works);
	schedule_delayed_work(&cm->update_typec_thermal_work,
					msecs_to_jiffies(5000));

	return ret;
}
#endif
/*hanlin.yang modify the samba tools will shut down after 30min without connection-20221020 end*/

/**
 * battout_handler - Event handler for CM_EVENT_BATT_OUT
 * @cm: the Charger Manager representing the battery.
 */
static void battout_handler(struct charger_manager *cm)
{
	if (cm_suspended)
		device_set_wakeup_capable(cm->dev, true);

	if (!is_batt_present(cm)) {
		dev_emerg(cm->dev, "Battery Pulled Out!\n");
	} else {
		dev_emerg(cm->dev, "Battery Pulled in!\n");
	}
}

static bool cm_charger_is_support_fchg(struct charger_manager *cm)
{
	struct charger_desc *desc = cm->desc;
	u32 fchg_type;
	int ret;

	if (!cm->fchg_info->support_fchg || !cm->fchg_info->ops ||
	    !cm->fchg_info->ops->get_fchg_type)
		return false;

	ret = cm->fchg_info->ops->get_fchg_type(cm->fchg_info, &fchg_type);
	if (!ret) {
		if (fchg_type == POWER_SUPPLY_CHARGE_TYPE_FAST ||
		    fchg_type == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE) {
			mutex_lock(&cm->desc->charger_type_mtx);
			desc->is_fast_charge = true;
			if (!desc->psy_cp_stat &&
			    fchg_type == POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE)
				fchg_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
			cm_get_charger_type(cm, CM_FCHG_TYPE, &fchg_type);
			desc->fast_charger_type = fchg_type;
			desc->charger_type = fchg_type;
			mutex_unlock(&cm->desc->charger_type_mtx);
			return true;
		}
	}

	return false;
}

static void cm_charger_int_handler(struct charger_manager *cm)
{
	dev_info(cm->dev, "%s into.\n", __func__);
	cm->desc->cm_check_int = true;
}

/**
 * fast_charge_handler - Event handler for CM_EVENT_FAST_CHARGE
 * @cm: the Charger Manager representing the battery.
 */
static void fast_charge_handler(struct charger_manager *cm)
{
	bool ext_pwr_online;

	if (cm_suspended)
		device_set_wakeup_capable(cm->dev, true);


	cm_charger_is_support_fchg(cm);

	ext_pwr_online = is_ext_pwr_online(cm);

	dev_info(cm->dev, "%s, fast_charger_type = %d, cp_running = %d, "
		 "charger_enabled = %d, ext_pwr_online = %d\n",
		 __func__, cm->desc->fast_charger_type, cm->desc->cp_sm.running,
		 cm->charger_enabled, ext_pwr_online);

	if (!ext_pwr_online)
		return;

	cm_update_charger_type_status(cm);
}

#ifdef CONFIG_TYPEC_PD_CTS_TEST
extern void temp_set_ichg(u32 mA);
extern void temp_set_limit_current(u32 limit_cur);
extern void sqc_hal_direct_set_fcc(int chg_pos, unsigned int mA);
extern void sqc_hal_direct_set_icl(int chg_pos, unsigned int mA);
#endif

#define RP_1_5_LIMINT 1500000
#define RP_3_0_LIMINT 3000000
#define RP_DEF_LIMINT 0
static void cm_update_charge_info(struct charger_manager *cm, int cmd)
{
	struct charger_desc *desc = cm->desc;
	int ret;
	u32 type;
	union power_supply_propval input_limit_val;
	union power_supply_propval charge_limit_val;

	mutex_lock(&cm->desc->charge_info_mtx);

	switch (desc->charger_type) {
	case CM_CHARGER_TYPE_DCP:
		desc->charge_limit_cur = desc->cur.dcp_cur;
		desc->input_limit_cur = desc->cur.dcp_limit;
		break;
	case CM_CHARGER_TYPE_CDP:
		desc->charge_limit_cur = desc->cur.cdp_cur;
		desc->input_limit_cur = desc->cur.cdp_limit;
		break;
	case CM_CHARGER_TYPE_ADAPTIVE:
		desc->charge_limit_cur = desc->cur.dcp_cur;
		desc->input_limit_cur = desc->cur.dcp_limit;
		break;
	case CM_WIRELESS_CHARGER_TYPE_BPP:
		desc->charge_limit_cur = desc->cur.wl_bpp_cur;
		desc->input_limit_cur = desc->cur.wl_bpp_limit;
		break;
	case CM_WIRELESS_CHARGER_TYPE_EPP:
		desc->charge_limit_cur = desc->cur.wl_epp_cur;
		desc->input_limit_cur = desc->cur.wl_epp_limit;
		break;
	case CM_CHARGER_TYPE_SDP:
#ifdef CONFIG_TYPEC_PD_CTS_TEST
		desc->charge_limit_cur = desc->cur.sdp_cur;
		if (cm->vchg_info->usb_limit == 0 && (cm->desc->limit_status & CM_CHARGE_PD_LIMIT_CMD))
			desc->input_limit_cur = desc->cur.sdp_limit;
		else
			desc->input_limit_cur = min(desc->cur.sdp_limit, cm->vchg_info->usb_limit);
#else
		desc->charge_limit_cur = desc->cur.sdp_cur;
		desc->input_limit_cur = desc->cur.sdp_limit;
#endif
		break;
	case CM_CHARGER_TYPE_FAST:
		desc->charge_limit_cur = desc->cur.dcp_cur;
		desc->input_limit_cur = desc->cur.dcp_limit;
		break;
	default:
		if (!is_ext_pwr_online(cm)) {
			desc->charge_limit_cur = 0;
			desc->input_limit_cur = 0;
			break;
		}
#ifdef CONFIG_TYPEC_PD_CTS_TEST
		desc->charge_limit_cur = 100000;
		desc->input_limit_cur = 100000;
#else
		desc->charge_limit_cur = desc->cur.unknown_cur;
		desc->input_limit_cur = desc->cur.unknown_limit;
#endif

		break;
	}

	switch (desc->charger_type) {
#ifdef CONFIG_TYPEC_PD_CTS_TEST
		case CM_CHARGER_TYPE_UNKNOWN:
			pr_info("typec pd cts test unkown type\n");
			break;
#endif
		case CM_CHARGER_TYPE_DCP:
		case CM_CHARGER_TYPE_SDP:
		case CM_CHARGER_TYPE_CDP:
#ifndef CONFIG_TYPEC_PD_CTS_TEST
		case CM_CHARGER_TYPE_UNKNOWN:
#endif
			if (desc->rp_limit_current == RP_1_5_LIMINT || desc->rp_limit_current == RP_3_0_LIMINT)
				desc->input_limit_cur = desc->rp_limit_current;
			if ((cm->desc->limit_status & CM_CHARGE_PD_LIMIT_CMD) && cm->desc->pd_req_vol_uv < CM_FIXED_FCHG_VOLTAGE_5V_THRESHOLD) {
				desc->input_limit_cur = cm->desc->pd_req_cur_ua;
				if (cm->vchg_info->usb_limit == 2000 && desc->charger_type == CM_CHARGER_TYPE_SDP)
					desc->input_limit_cur = min(desc->input_limit_cur, cm->vchg_info->usb_limit);
			}
			break;
		case CM_CHARGER_TYPE_FAST:
			if (desc->enable_fast_charge)
				break;

			if (!cm_pd_is_ac_online(cm)) {
				desc->charge_limit_cur = CM_FIXED_FCHG_C2C_CURRENT;
				desc->input_limit_cur = CM_FIXED_FCHG_C2C_CURRENT;
			}

			if ((cm->desc->limit_status & CM_CHARGE_PD_LIMIT_CMD) && cm->desc->pd_req_vol_uv < CM_FIXED_FCHG_VOLTAGE_5V_THRESHOLD) {
				desc->input_limit_cur = cm->desc->pd_req_cur_ua;
				ret = cm_get_bc1p2_type(cm, &type);
				if (!ret && type == POWER_SUPPLY_USB_TYPE_SDP && cm->vchg_info->usb_limit == 2000)
					desc->input_limit_cur = min(desc->input_limit_cur, cm->vchg_info->usb_limit);
			}
			break;
	}

	mutex_unlock(&cm->desc->charge_info_mtx);

	dev_info(cm->dev, "%s, desc->charger_type=%d, desc->input_limit_cur = %d, desc->charge_limit_cur = %d", __func__,desc->charger_type,  desc->input_limit_cur, desc->charge_limit_cur);

	input_limit_val.intval = desc->input_limit_cur;
	charge_limit_val.intval = desc->charge_limit_cur;
#ifdef CONFIG_TYPEC_PD_CTS_TEST
	temp_set_limit_current(desc->input_limit_cur);
	temp_set_ichg(desc->charge_limit_cur);
#else
	sqc_set_property(POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &input_limit_val);
	sqc_set_property(POWER_SUPPLY_PROP_CURRENT_MAX, &charge_limit_val);
#endif
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	sqc_notify_daemon_changed(SQC_NOTIFY_TYPEC_TEST,
					SQC_NOTIFY_TYPEC_TEST_CHANGED, 1);
#endif
}

static void cm_pd_negotiated_init_cfg(struct charger_manager *cm)
{
	cancel_work_sync(&cm->limit_current_work);

	if (cm->desc->pd_negotiated_limit_cur)
		cm->cm_charge_vote->vote(cm->cm_charge_vote, false,
					 SPRD_VOTE_TYPE_IBUS,
					 SPRD_VOTE_TYPE_IBUS_ID_PD_NEGOIIATED_LIMIT,
					 SPRD_VOTE_CMD_MIN, 0, cm);

	cm->desc->pd_negotiated_limit_cur = false;
}

/**
 * misc_event_handler - Handler for other events
 * @cm: the Charger Manager representing the battery.
 * @type: the Charger Manager representing the battery.
 */
static void misc_event_handler(struct charger_manager *cm, enum cm_event_types type)
{
	int ret;

	if (cm_suspended)
		device_set_wakeup_capable(cm->dev, true);

	dev_info(cm->dev, "%s, is_ext_pwr_online = %d, is_ext_usb_pwr_online = %d, "
		"is_fast_charge = %d, usb_charge_en = %d\n",
		 __func__, is_ext_pwr_online(cm), is_ext_usb_pwr_online(cm),
		 cm->desc->is_fast_charge, cm->desc->usb_charge_en);

	if (is_ext_pwr_online(cm)) {
		dev_info(cm->dev, "%s,line:%d,usb changed online\n", __func__, __LINE__);
		if (is_ext_wl_pwr_online(cm)) {
			if (cm->desc->usb_charge_en) {
				cm_enable_fixed_fchg_handshake(cm, false);
				cm->desc->force_pps_diasbled = false;
				cm->desc->is_fast_charge = false;
				cm->desc->enable_fast_charge = false;
				cm->desc->fast_charge_enable_count = 0;
				cm->desc->fast_charge_disable_count = 0;
				cm->desc->fixed_fchg_running = false;
				cm->desc->wait_vbus_stable = false;
				cm->desc->cp_sm.running = false;
				cm->desc->fast_charger_type = 0;
				cm->desc->cp_sm.target_vbus = 0;
				cm->desc->usb_charge_en = false;
				cm->desc->charger_type = 0;
			}
		} else {
			if (!cm->desc->is_fast_charge) {
				ret = get_usb_charger_type(cm, &cm->desc->charger_type);
				if (ret)
					dev_warn(cm->dev, "Fail to get usb charger type, ret = %d",
						 ret);

				cm_enable_fixed_fchg_handshake(cm, true);
			}

			cm->desc->usb_charge_en = true;
		}
		cm_set_charger_present(cm, true);
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		sqc_notify_daemon_changed(SQC_NOTIFY_TYPEC_TEST,
						SQC_NOTIFY_TYPEC_TEST_CHANGED, 1);
#endif

		cm_update_charge_info(cm, CM_CHARGE_INFO_INPUT_LIMIT);
		cm_charger_type_update_check_start(cm);
	} else {
		dev_info(cm->dev, "%s,line:%d,usb changed offline\n", __func__, __LINE__);
		cm_enable_fixed_fchg_handshake(cm, false);
		cm->charger_enabled = false;

		cm_pd_negotiated_init_cfg(cm);
		cm->vchg_info->usb_limit = -EINVAL;
		cm->desc->thm_info.thm_adjust_cur = -EINVAL;
		cm->desc->rp_limit_current = -EINVAL;
		cm->desc->limit_status = 0;

		cm->desc->force_pps_diasbled = false;
		cm->desc->is_fast_charge = false;
		cm->desc->ir_comp.ir_compensation_en = false;
		cm->desc->enable_fast_charge = false;
		cm->desc->fast_charge_enable_count = 0;
		cm->desc->fast_charge_disable_count = 0;
		cm->desc->fixed_fchg_running = false;
		cm->desc->wait_vbus_stable = false;
		cm->desc->cp_sm.running = false;
		cm->desc->cm_check_int = false;
		cm->desc->fast_charger_type = 0;
		cm->desc->charger_type = 0;
		cm->desc->cp_sm.target_vbus = 0;
		cm->desc->force_set_full = false;
		cm->emergency_stop = 0;
		cm->charging_status = 0;
		jeita_info_init(&cm->desc->jeita_info);

		cm->desc->thm_info.thm_adjust_cur = -EINVAL;
		cm->desc->thm_info.thm_pwr = 0;
		cm->desc->thm_info.adapter_default_charge_vol = 5;
		cm->desc->usb_charge_en = 0;
		cm->desc->xts_limit_cur = false;
		cm->desc->adapter_max_vbus = 0;

		cm_set_charger_present(cm, false);
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		sqc_notify_daemon_changed(SQC_NOTIFY_TYPEC_TEST,
						SQC_NOTIFY_TYPEC_TEST_CHANGED, 0);
#endif

	}

	cm_update_charger_type_status(cm);

	power_supply_changed(cm->charger_psy);
}

static int cm_get_battery_technology(struct charger_manager *cm, union power_supply_propval *val)
{
	struct power_supply *fuel_gauge = NULL;
	int ret;

	val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;

	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_TECHNOLOGY, val);
	power_supply_put(fuel_gauge);

	return ret;
}

static void cm_get_uisoc(struct charger_manager *cm, int *uisoc)
{
	int batt_uV = 0;

	if (!is_batt_present(cm)) {
		/* There is no battery. Assume 100% */
		*uisoc = 100;
		return;
	}

	if (cm->desc->cap_debug != -1)
		*uisoc = cm->desc->cap_debug;
	else {
		*uisoc = DIV_ROUND_CLOSEST(cm->desc->cap, 10);
		if (*uisoc > 100)
			*uisoc = 100;
		else if (*uisoc < 0)
			*uisoc = 0;

		if (cm->desc->cap <= 0) {
			get_vbat_now_uV(cm, &batt_uV);
			if (batt_uV >= CUTOFF_VOLTAGE_UV) {
				*uisoc = 1;
				dev_info(cm->dev, "## force report soc 1, vbat %d\n", batt_uV);
			}
		}
	}
}

static int cm_get_capacity_level_critical(struct charger_manager *cm)
{
	int level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	int batt_uv = 0, batt_uA = 0, ocv_uv = 0;

	if (get_vbat_now_uV(cm, &batt_uv)) {
		dev_err(cm->dev, "%s, get_batt_uv error.\n", __func__);
		return level;
	}

	if (get_ibat_now_uA(cm, &batt_uA)) {
		dev_err(cm->dev, "%s, get_ibat_uA error.\n", __func__);
		return level;
	}

	if (is_charging(cm) && batt_uA > 0 && batt_uv > CM_LOW_CAP_SHUTDOWN_VOLTAGE_THRESHOLD) {
		level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		return level;
	}

	if (get_batt_ocv(cm, &ocv_uv)) {
		dev_err(cm->dev, "%s, get_batt_ocV error.\n", __func__);
		return level;
	}

	if (is_charging(cm) && ocv_uv > CM_CAPACITY_LEVEL_CRITICAL_VOLTAGE &&
	    batt_uv > CM_LOW_CAP_SHUTDOWN_VOLTAGE_THRESHOLD)
		level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;

	return level;
}

static int cm_get_capacity_level(struct charger_manager *cm)
{
	int level = POWER_SUPPLY_CAPACITY_LEVEL_UNKNOWN;
	int uisoc;

	if (!is_batt_present(cm)) {
		/* There is no battery. Assume 100% */
		level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		return level;
	}

	uisoc = DIV_ROUND_CLOSEST(cm->desc->cap, 10);

	if (uisoc >= CM_CAPACITY_LEVEL_FULL)
		level = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else if (uisoc > CM_CAPACITY_LEVEL_NORMAL)
		level = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
	else if (uisoc > CM_CAPACITY_LEVEL_LOW)
		level = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	else if (uisoc > CM_CAPACITY_LEVEL_CRITICAL)
		level = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else
		level = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;

	if (level == POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL)
		level = cm_get_capacity_level_critical(cm);

	return level;
}

static int cm_get_charge_full_design(struct charger_manager *cm, union power_supply_propval *val)
{
	struct power_supply *fuel_gauge = NULL;
	int ret;

	val->intval = 0;
	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN, val);
	power_supply_put(fuel_gauge);

	return ret;
}

static int cm_get_charge_full(struct charger_manager *cm, union power_supply_propval *val)
{
	struct power_supply *fuel_gauge = NULL;
	int ret;

	val->intval = 0;
	fuel_gauge = power_supply_get_by_name(cm->desc->psy_fuel_gauge);
	if (!fuel_gauge)
		return -ENODEV;

	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_ENERGY_FULL, val);
	power_supply_put(fuel_gauge);

	return ret;
}


static int cm_get_charge_now(struct charger_manager *cm, int *charge_now)
{
	int total_uah;
	int ret;

	ret = get_batt_total_cap(cm, &total_uah);
	if (ret) {
		dev_err(cm->dev, "failed to get total uah.\n");
		return ret;
	}

	*charge_now = total_uah * cm->desc->cap / CM_CAP_FULL_PERCENT;

	return ret;
}

static int cm_get_charge_counter(struct charger_manager *cm, int *charge_counter)
{
	int ret;

	*charge_counter = 0;
	ret = cm_get_charge_now(cm, charge_counter);

	if (*charge_counter <= 0) {
		*charge_counter = 1;
		ret = 0;
	}

	return ret;
}

static int cm_get_charge_control_limit(struct charger_manager *cm,
				       union power_supply_propval *val)
{
	struct power_supply *psy = NULL;
	int i, ret = 0;

	for (i = 0; cm->desc->psy_charger_stat[i]; i++) {
		psy = power_supply_get_by_name(cm->desc->psy_charger_stat[i]);
		if (!psy) {
			dev_err(cm->dev, "Cannot find power supply \"%s\"\n",
				cm->desc->psy_charger_stat[i]);
			continue;
		}

		ret = power_supply_get_property(psy, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, val);
		power_supply_put(psy);
		if (!ret) {
			if (cm->desc->enable_fast_charge && cm->desc->psy_charger_stat[1])
				val->intval *= 2;

			break;
		}

		ret = power_supply_get_property(psy,
						POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
						val);
		if (!ret)
			break;
	}

	return ret;
}

static int cm_get_charge_full_uah(struct charger_manager *cm, union power_supply_propval *val)
{
	return cm_get_charge_full(cm, val);
}

static int cm_get_time_to_full_now(struct charger_manager *cm, int *time)
{
	unsigned int total_cap = 0;
	int ibat_cur = 0;
	int ret;
	static int max_time_to_full = -1;

	ret = get_ibat_now_uA(cm, &ibat_cur);
	if (ret) {
		dev_err(cm->dev, "get batt_uA error.\n");
		*time = 1;
		return ret;
	}
	ibat_cur = ibat_cur / 1000;

	ret = get_batt_total_cap(cm, &total_cap);
	if (ret) {
		dev_err(cm->dev, "failed to get total cap.\n");
		return ret;
	}

	total_cap = total_cap / 1000;

	*time = ((1000 - cm->desc->cap) * total_cap / 1000) * 3600 / ibat_cur;
	if (max_time_to_full == -1) {
		max_time_to_full = (1000 * total_cap / 1000) * 3600 / STANDARD_USB_HOST_CURRENT_MA;
		pr_info("max_time_to_full=%d\n", max_time_to_full);
	}

	if (*time > max_time_to_full)
		*time = max_time_to_full;
	if (*time <= 0) {
		*time = ((1000 - cm->desc->cap) * total_cap / 1000) * 3600 / STANDARD_USB_HOST_CURRENT_MA;
	}

	return ret;
}

static int cm_get_fchg_adapter_max_voltage(struct charger_manager *cm, int *max_vol)
{
	int ret;

	*max_vol = 0;
	if (!cm->fchg_info->ops || !cm->fchg_info->ops->get_fchg_vol_max) {
		dev_err(cm->dev, "%s, fchg ops or get_fchg_vol_max is null\n", __func__);
		return -EINVAL;
	}

	ret = cm->fchg_info->ops->get_fchg_vol_max(cm->fchg_info, max_vol);
	if (ret)
		dev_err(cm->dev, "%s, failed to get fchg max voltage, ret=%d\n",
			__func__, ret);

	return ret;
}

static int cm_get_fchg_adapter_max_current(struct charger_manager *cm, int input_vol, int *max_cur)
{
	int ret;

	*max_cur = 0;
	if (!cm->fchg_info->ops || !cm->fchg_info->ops->get_fchg_cur_max) {
		dev_err(cm->dev, "%s, fchg ops or get_fchg_cur_max is null\n", __func__);
		return -EINVAL;
	}

	ret = cm->fchg_info->ops->get_fchg_cur_max(cm->fchg_info, input_vol, max_cur);
	if (ret)
		dev_err(cm->dev, "%s, failed to get fchg max current, ret=%d\n",
			__func__, ret);

	return ret;
}
static void cm_get_voltage_max(struct charger_manager *cm, int *voltage_max)
{
	int adapter_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V, chg_type_max_vbus = 0;
	int ret = 0;

	if (!is_ext_pwr_online(cm)) {
		*voltage_max = min(chg_type_max_vbus, adapter_max_vbus);
		return;
	}

	switch (cm->desc->charger_type) {
	case CM_CHARGER_TYPE_FAST:
		if (!cm->desc->fast_charge_voltage_max) {
			chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V;
			break;
		}

		if (cm->desc->fast_charge_voltage_max > CM_FAST_CHARGE_VOLTAGE_20V)
			chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_20V;
		else if (cm->desc->fast_charge_voltage_max > CM_FAST_CHARGE_VOLTAGE_15V)
			chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_15V;
		else if (cm->desc->fast_charge_voltage_max > CM_FAST_CHARGE_VOLTAGE_12V)
			chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_12V;
		else if (cm->desc->fast_charge_voltage_max > CM_FAST_CHARGE_VOLTAGE_9V)
			chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_9V;

		ret = cm_get_fchg_adapter_max_voltage(cm, &adapter_max_vbus);
		if (ret) {
			adapter_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V;
			dev_err(cm->dev,
				"%s, failed to obtain the adapter max_vol in fixed fchg\n",
				__func__);
		}
		break;
	case CM_CHARGER_TYPE_ADAPTIVE:
		if (!cm->desc->flash_charge_voltage_max) {
			chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V;
			break;
		}

		if (cm->desc->flash_charge_voltage_max > CM_FAST_CHARGE_VOLTAGE_20V)
			chg_type_max_vbus = CM_PPS_VOLTAGE_21V;
		else if (cm->desc->flash_charge_voltage_max > CM_FAST_CHARGE_VOLTAGE_15V)
			chg_type_max_vbus = CM_PPS_VOLTAGE_16V;
		else if (cm->desc->flash_charge_voltage_max > CM_FAST_CHARGE_VOLTAGE_9V)
			chg_type_max_vbus = CM_PPS_VOLTAGE_11V;

		ret = cm_get_fchg_adapter_max_voltage(cm, &adapter_max_vbus);
		if (ret) {
			adapter_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V;
			dev_err(cm->dev,
				"%s, failed to obtain the adapter max_vol in pps\n",
				__func__);
			break;
		}

		if (cm->desc->charger_type == CM_CHARGER_TYPE_ADAPTIVE &&
		    cm->desc->force_pps_diasbled)
			adapter_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V;
		break;
	case CM_WIRELESS_CHARGER_TYPE_EPP:
		if (!cm->desc->wireless_fast_charge_voltage_max) {
			chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V;
			break;
		}

		chg_type_max_vbus = cm->desc->wireless_fast_charge_voltage_max;
		break;
	case CM_CHARGER_TYPE_DCP:
	case CM_CHARGER_TYPE_CDP:
	case CM_CHARGER_TYPE_SDP:
	case CM_CHARGER_TYPE_UNKNOWN:
	case CM_WIRELESS_CHARGER_TYPE_BPP:
	default:
		chg_type_max_vbus = CM_FAST_CHARGE_VOLTAGE_5V;
		break;
	}

	*voltage_max = min(chg_type_max_vbus, adapter_max_vbus);
}

static void cm_get_current_max(struct charger_manager *cm, int *current_max)
{
	int adapter_max_ibus = CM_FAST_CHARGE_CURRENT_2A, chg_type_max_ibus = 0;
	int opt_max_vbus;
	int ret = 0;

	if (!is_ext_pwr_online(cm)) {
		*current_max = min(chg_type_max_ibus, adapter_max_ibus);
		return;
	}

	switch (cm->desc->charger_type) {
	case CM_CHARGER_TYPE_DCP:
		chg_type_max_ibus = cm->desc->cur.dcp_limit;
		break;
	case CM_CHARGER_TYPE_SDP:
		chg_type_max_ibus = cm->desc->cur.sdp_limit;
		break;
	case CM_CHARGER_TYPE_CDP:
		chg_type_max_ibus = cm->desc->cur.cdp_limit;
		break;
	case CM_CHARGER_TYPE_FAST:
		chg_type_max_ibus = cm->desc->cur.fchg_limit;
		cm_get_voltage_max(cm, &opt_max_vbus);
		ret = cm_get_fchg_adapter_max_current(cm, opt_max_vbus, &adapter_max_ibus);
		if (ret) {
			adapter_max_ibus = CM_FAST_CHARGE_CURRENT_2A;
			dev_err(cm->dev,
				"%s, failed to obtain the adapter max_cur in fixed fchg\n",
				__func__);
		}
		break;
	case CM_CHARGER_TYPE_ADAPTIVE:
		chg_type_max_ibus = cm->desc->cur.flash_limit;
		cm_get_voltage_max(cm, &opt_max_vbus);
		ret = cm_get_fchg_adapter_max_current(cm, opt_max_vbus, &adapter_max_ibus);
		if (ret) {
			adapter_max_ibus = CM_FAST_CHARGE_CURRENT_2A;
			dev_err(cm->dev,
				"%s, failed to obtain the adapter max_cur in pps\n", __func__);
			break;
		}

		if (cm->desc->charger_type == CM_CHARGER_TYPE_ADAPTIVE &&
		    cm->desc->force_pps_diasbled)
			adapter_max_ibus = CM_FAST_CHARGE_CURRENT_2A;
		break;
	case CM_WIRELESS_CHARGER_TYPE_BPP:
		chg_type_max_ibus = cm->desc->cur.wl_bpp_limit;
		break;
	case CM_WIRELESS_CHARGER_TYPE_EPP:
		chg_type_max_ibus = cm->desc->cur.wl_epp_limit;
		break;
	case CM_CHARGER_TYPE_UNKNOWN:
	default:
		chg_type_max_ibus = cm->desc->cur.unknown_limit;
		break;
	}

	*current_max = min(chg_type_max_ibus, adapter_max_ibus);
}

static int cm_get_power_supply_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	int ret = 0;
	struct cm_power_supply_data *data = container_of(psy->desc, struct  cm_power_supply_data, psd);

	if (!data || !data->cm)
		return -ENOMEM;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = data->ONLINE;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		cm_get_current_max(data->cm, &val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		cm_get_voltage_max(data->cm, &val->intval);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void cm_get_charge_type(struct charger_manager *cm, int *charge_type)
{
	switch (cm->desc->charger_type) {
	case CM_CHARGER_TYPE_SDP:
	case CM_CHARGER_TYPE_DCP:
	case CM_CHARGER_TYPE_CDP:
		*charge_type = POWER_SUPPLY_CHARGE_TYPE_STANDARD;
		break;

	case CM_CHARGER_TYPE_FAST:
		*charge_type = POWER_SUPPLY_CHARGE_TYPE_FAST;
		break;

	case CM_CHARGER_TYPE_ADAPTIVE:
		*charge_type = POWER_SUPPLY_CHARGE_TYPE_ADAPTIVE;
		break;
	default:
		*charge_type = POWER_SUPPLY_CHARGE_TYPE_UNKNOWN;
	}
}

static bool cm_add_battery_psy_property(struct charger_manager *cm,
					enum power_supply_property psp,
					enum power_supply_property *properties,
					size_t *num_properties)
{
	u32 i;

	for (i = 0; i < *num_properties; i++)
		if (properties[i] == psp)
			break;

	if (i == *num_properties) {
		properties[*num_properties] = psp;
		(*num_properties)++;
		return true;
	}
	return false;
}

/*zte add begin*/
int charger_manager_get_ship_mode(struct charger_manager *cm)
{
	struct zte_power_supply *psy = NULL;
	union power_supply_propval val = {0,};
	int rc = 0;

	if (cm && cm->desc && cm->desc->zte_psy_hardware) {
		psy = zte_power_supply_get_by_name(cm->desc->zte_psy_hardware);
		if (!psy) {
			dev_err(cm->dev, "no %s psy!\n", cm->desc->zte_psy_hardware);
			return -1;
		}
	} else {
		dev_err(cm->dev, "no zte_psy_hardware node!\n");
		return -1;
	}

	rc = zte_power_supply_get_property(psy,
			POWER_SUPPLY_PROP_SET_SHIP_MODE, &val);
	if (rc < 0) {
		dev_err(cm->dev, "get ship mode failed, rc = %d\n", rc);
		zte_power_supply_put(psy);
		return -1;
	}

	dev_info(cm->dev, "%s get_ship_mode, val %d\n", __func__, val.intval);
	zte_power_supply_put(psy);

	return val.intval;
}

int charger_manager_set_ship_mode(struct charger_manager *cm, int enable)
{
	union power_supply_propval val = {0,};
	struct zte_power_supply *psy = NULL;
	int rc = 0;

	val.intval = enable;

	if (cm && cm->desc && cm->desc->zte_psy_hardware) {
		psy = zte_power_supply_get_by_name(cm->desc->zte_psy_hardware);
		if (!psy) {
			dev_err(cm->dev, "get zte_psy_hardware psy failed!!\n");
			return -ENODEV;
		}

		rc = zte_power_supply_set_property(psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, &val);
		if (rc < 0) {
			dev_err(cm->dev, "Failed to set zte_psy_hardware shipmode failed\n");
			zte_power_supply_put(psy);
			return -ENODEV;
		}

		zte_power_supply_put(psy);

		if (cm->desc->zte_psy_hardware2) {
			psy = zte_power_supply_get_by_name(cm->desc->zte_psy_hardware2);
			if (!psy) {
				dev_err(cm->dev, "get zte_psy_hardware2 psy failed!!\n");
				return -ENODEV;
			}

			rc = zte_power_supply_set_property(psy,
					POWER_SUPPLY_PROP_SET_SHIP_MODE, &val);
			if (rc < 0) {
				dev_err(cm->dev, "Failed to set zte_psy_hardware2 shipmode failed\n");
				zte_power_supply_put(psy);
				return -ENODEV;
			}

			zte_power_supply_put(psy);
		}
	} else {
		dev_err(cm->dev, "no zte_psy_hardware node!\n");
		return -ENODEV;
	}

	dev_info(cm->dev, "%s set_ship_mode, val %d\n", __func__, val.intval);

	return 0;
}
/*zte add end*/

static int charger_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *val)
{
	struct charger_manager *cm = power_supply_get_drvdata(psy);
	int ret = 0;
	int ui_soc_temp = 0;

	if (!cm)
		return -ENOMEM;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		cm_get_uisoc(cm, &ui_soc_temp);
		if ((ui_soc_temp < 100) && (cm->batt_chg_status == POWER_SUPPLY_STATUS_FULL)) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		} else {
			val->intval = cm->batt_chg_status;
		}
#ifdef ZTE_FEATURE_PV_AR
		if (sqc_get_property(POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX, NULL) == 0) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
#endif
		if (!is_ext_pwr_online(cm)) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		}
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = cm->health;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = is_batt_present(cm);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_AVG:
		ret = get_vbat_avg_uV(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = get_vbat_now_uV(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = get_ibat_avg_uA(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = get_ibat_now_uA(cm, &val->intval);
		#ifdef ZTE_FEATURE_PV_AR
		if (!ret)
			val->intval *= (-1);
		#endif
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		ret = cm_get_battery_technology(cm, val);
		break;

	case POWER_SUPPLY_PROP_TEMP:
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		sqc_get_property(POWER_SUPPLY_PROP_TEMP, val);
		if (!val->intval)
			val->intval = cm->desc->temperature;
#else
		val->intval = cm->desc->temperature;
#endif
		break;

	case POWER_SUPPLY_PROP_TEMP_AMBIENT:
		return cm_get_board_temperature(cm, &val->intval);

	case POWER_SUPPLY_PROP_CAPACITY:
		cm_get_uisoc(cm, &val->intval);

		break;

	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = cm_get_capacity_level(cm);
		break;

	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = is_ext_pwr_online(cm);
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = cm_get_charge_full_uah(cm, val);
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = cm_get_charge_now(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = get_constant_charge_current(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = get_input_current_limit(cm,  &val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = cm_get_charge_counter(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		ret = cm_get_charge_control_limit(cm, val);
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = cm_get_charge_full_design(cm, val);
		break;

	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		ret = cm_get_time_to_full_now(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_USB_TYPE:
		ret = cm_get_bc1p2_type(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		cm_get_charge_type(cm, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = cm_get_charge_cycle(cm, &val->intval);
		val->intval = val->intval / 1000;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int charger_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct charger_manager *cm = power_supply_get_drvdata(psy);
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	union power_supply_propval thermal_val;
#endif
	int ret = 0;

	if (!cm) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -ENOMEM;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		if (cm->batt_chg_status != val->intval) {
			cm->batt_chg_status = val->intval;
			if (delayed_work_pending(&cm->cap_update_work)) {
				mod_delayed_work(system_power_efficient_wq, &cm->cap_update_work, 0);
			}
		}
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		break;

	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		dev_info(cm->dev, "%s:line%d set battery cycle_count = %d\n", __func__, __LINE__, val->intval);
		ret = cm_set_charge_cycle(cm, val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		if (!cm->desc->thermal_control_en) {
			dev_info(cm->dev, "%s thermal control disabled\n", __func__);
			thermal_val.intval = -1;
			sqc_set_property(POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &thermal_val);
			break;
		}

		sqc_set_property(POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, val);
		dev_info(cm->dev, "%s:line%d control limit cur = %d\n",
			__func__, __LINE__, val->intval);
#else
		dev_dbg(cm->dev, "%s:line%d control limit cur = %d\n",
			__func__, __LINE__, val->intval);
#endif
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static int charger_property_is_writeable(struct power_supply *psy, enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}
#define NUM_CHARGER_PSY_OPTIONAL	(4)

static enum power_supply_property ac_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static enum power_supply_property default_charger_props[] = {
	/* Guaranteed to provide */
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TEMP_AMBIENT,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	/*
	 * Optional properties are:
	 * POWER_SUPPLY_PROP_CHARGE_NOW,
	 */
};

/* ac_data initialization */
static struct cm_power_supply_data ac_main = {
	.psd = {
		.name = "ac",
		.type = POWER_SUPPLY_TYPE_MAINS,
		.properties = ac_props,
		.num_properties = ARRAY_SIZE(ac_props),
		.get_property = cm_get_power_supply_property,
	},
	.ONLINE = 0,
};

/* usb_data initialization */
static struct cm_power_supply_data usb_main = {
	.psd = {
		.name = "usb",
		.type = POWER_SUPPLY_TYPE_USB,
		.properties = usb_props,
		.num_properties = ARRAY_SIZE(usb_props),
		.get_property = cm_get_power_supply_property,
	},
	.ONLINE = 0,
};

static enum power_supply_usb_type default_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};

static const struct power_supply_desc psy_default = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = default_charger_props,
	.num_properties = ARRAY_SIZE(default_charger_props),
	.get_property = charger_get_property,
	.set_property = charger_set_property,
	.property_is_writeable	= charger_property_is_writeable,
	.usb_types		= default_usb_types,
	.num_usb_types		= ARRAY_SIZE(default_usb_types),
	.no_thermal = false,
};

#if IS_ENABLED(CONFIG_SPRD_TYPEC_TCPM)
static bool cm_pd_is_ac_online(struct charger_manager *cm)
{
	struct adapter_power_cap pd_source_cap;
	struct sprd_tcpm_port *port;
	struct power_supply *psy;
	int i, index = 0;

	if (!cm->fchg_info->pd_enable)
		return true;

	psy = power_supply_get_by_name(SPRD_FCHG_TCPM_PD_NAME);
	if (!psy) {
		dev_err(cm->dev, "failed to get tcpm psy!\n");
		return true;
	}

	port = power_supply_get_drvdata(psy);
	if (!port) {
		dev_err(cm->dev, "failed to get tcpm port!\n");
		return true;
	}

	sprd_tcpm_get_source_capabilities(port, &pd_source_cap);

	for (i = 0; i < pd_source_cap.nr_source_caps; i++) {
		if ((pd_source_cap.max_mv[i] <= 5000) &&
		    (pd_source_cap.ma[i] < CM_FIXED_FCHG_C2C_IBUS_THRESHOLD / 1000) &&
		    (pd_source_cap.type[i] == SPRD_PDO_TYPE_FIXED))
			index++;
	}

	if (pd_source_cap.nr_source_caps == index) {
		dev_info(cm->dev, "pd type ac online is false, index = %d\n", index);
		return false;
	}

	return true;
}
#else
static bool cm_pd_is_ac_online(struct charger_manager *cm)
{
	return true;
}
#endif

static void cm_update_charger_type_status(struct charger_manager *cm)
{

	 if (is_ext_usb_pwr_online(cm)) {
		switch (cm->desc->charger_type) {
		case CM_CHARGER_TYPE_DCP:
		case CM_CHARGER_TYPE_FAST:
		case CM_CHARGER_TYPE_ADAPTIVE:
			if (cm->fchg_info->pd_enable && !cm->fchg_info->pps_enable &&
				!cm_pd_is_ac_online(cm) && !cm->desc->pd_port_partner) {
				ac_main.ONLINE = 0;
				usb_main.ONLINE = 1;
				dev_info(cm->dev, "usb online--pd type 5V\n");
			} else {
				usb_main.ONLINE = 0;
				ac_main.ONLINE = 1;
				dev_info(cm->dev, "ac online\n");
			}
			break;
		default:
			dev_info(cm->dev, "default usb online\n");
			ac_main.ONLINE = 0;
			usb_main.ONLINE = 1;
			break;
		}
	} else {
		ac_main.ONLINE = 0;
		usb_main.ONLINE = 0;
	}

	dev_info(cm->dev, "%s:line%d usb = %d, ac = %d\n",
		__func__, __LINE__, usb_main.ONLINE, ac_main.ONLINE);
}

static void cm_limit_current_work(struct work_struct *work)
{
	struct charger_manager *cm = container_of(work, struct charger_manager,
						  limit_current_work);

	if (cm->desc->limit_status)
		cm_update_charge_info(cm, CM_CHARGE_INFO_INPUT_LIMIT);
	dev_info(cm->dev, "%s:line%d, usb_limit %d, rp_limit %d, pd_req_cur_ua %d, limit_status %d\n", __func__, __LINE__,
			cm->vchg_info->usb_limit, cm->desc->rp_limit_current, cm->desc->pd_req_cur_ua,
			cm->desc->limit_status);
}

void cm_check_pd_negotiated_limit_current(enum sprd_pd_pdo_type pdo_type,
					  int req_vol_uv,
					  int req_cur_ua,
					  bool enable_limit)
{
	struct charger_manager *cm;
	bool found_power_supply = false;

	if (g_cm) {
		cm = g_cm;
	} else {
		mutex_lock(&cm_list_mtx);
		list_for_each_entry(cm, &cm_list, entry) {
			if (cm->charger_psy->desc) {
				if (strcmp(cm->charger_psy->desc->name, "battery") == 0) {
					found_power_supply = true;
					break;
				}
			}
		}

		mutex_unlock(&cm_list_mtx);

		if (!found_power_supply) {
			pr_err("%s:line%d no cm found!!!\n", __func__, __LINE__);
			return;
		}

		if (!cm) {
			pr_err("%s:line%d NULL pointer!!!\n", __func__, __LINE__);
			return;
		}
	}

	if (pdo_type != SPRD_PDO_TYPE_FIXED) {
		pr_err("%s, Unsupport pdo_type[%d]!!!\n", __func__, pdo_type);
		return;
	}
	cm->desc->pd_req_vol_uv = req_vol_uv;
	cm->desc->pd_req_cur_ua = req_cur_ua;
	dev_info(cm->dev, "sprd: %s, Requesting APDO: %d mV, %d mA, enable_limit: %d\n",
		 __func__, cm->desc->pd_req_vol_uv / 1000, cm->desc->pd_req_cur_ua / 1000,
		 cm->desc->pd_enable_limit);
	dev_info(cm->dev, "sprd: %s, pd_negotiated, limit_cur: %d\n",
		 __func__, cm->desc->pd_negotiated_limit_cur);

	if (cm->desc->pd_req_vol_uv != 0 && cm->desc->pd_req_vol_uv < CM_FIXED_FCHG_VOLTAGE_5V_THRESHOLD) {
		cm->desc->limit_status |= CM_CHARGE_PD_LIMIT_CMD;
		schedule_work(&cm->limit_current_work);
	}
}

void cm_check_rp_limit_current(int rp_limit)
{
	struct charger_manager *cm;
	bool found_power_supply = false;
	int rp_limit_current = rp_limit * 1000;
	int ret = 0;

	if (g_cm) {
		cm = g_cm;
	} else {
		mutex_lock(&cm_list_mtx);
		list_for_each_entry(cm, &cm_list, entry) {
			if (cm->charger_psy->desc) {
				if (strcmp(cm->charger_psy->desc->name, "battery") == 0) {
					found_power_supply = true;
					break;
				}
			}
		}

		mutex_unlock(&cm_list_mtx);

		if (!found_power_supply) {
			pr_err("%s:line%d no cm found!!!\n", __func__, __LINE__);
			return;
		}

		if (!cm) {
			pr_err("%s:line%d NULL pointer!!!\n", __func__, __LINE__);
			return;
		}
	}

	if (!cm->desc->is_fast_charge) {
		ret = get_usb_charger_type(cm, &cm->desc->charger_type);
		if (ret)
			dev_warn(cm->dev, "Fail to get usb charger type, ret = %d",ret);
	}

	if (cm->desc->rp_limit_current != rp_limit_current) {
		cm->desc->rp_limit_current = rp_limit_current;
		cm->desc->limit_status |= CM_CHARGE_RP_LIMIT_CMD;
		pr_err("%s,line:%d: rp limit cmd\n", __func__, __LINE__);
		schedule_work(&cm->limit_current_work);
	}
}

static struct sprd_charger_ops cm_sprd_charger_ops = {
	.name = "sprd_charger_manager",
	.negotiated_limit_current = cm_check_pd_negotiated_limit_current,
	.set_rp_limit_current = cm_check_rp_limit_current,
};

/*zte add begin*/
static int zte_charger_get_property(struct zte_power_supply *psy,
		enum zte_power_supply_property psp,
		union power_supply_propval *val)
{
	struct charger_manager *cm = zte_power_supply_get_drvdata(psy);
	struct zte_power_supply *fuel_gauge = NULL;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		val->intval = charger_manager_get_ship_mode(cm);
		break;
	case POWER_SUPPLY_PROP_BATTERY_ID:
		dev_err(cm->dev, "zte_psy_fuel_gauge %s\n", cm->desc->zte_psy_fuel_gauge);
		if (cm->desc->zte_psy_fuel_gauge) {
			fuel_gauge = zte_power_supply_get_by_name(
				cm->desc->zte_psy_fuel_gauge);
			if (!fuel_gauge) {
				dev_err(cm->dev, "failed to get psy cm->desc->psy_fuel_gauge\n");
				ret = -ENODEV;
				break;
			}
			ret = zte_power_supply_get_property(fuel_gauge,
					POWER_SUPPLY_PROP_BATTERY_ID,
					val);
			zte_power_supply_put(fuel_gauge);
		}
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		zte_sqc_get_property(psp, val);
#endif
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		zte_sqc_get_property(psp, val);
#endif
		break;
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
#ifdef ZTE_CHARGER_DETAIL_CAPACITY
		dev_info(cm->dev, "%s: Get fast charge raw_soc enable %d\n", __func__, cm->raw_soc_enabled);
		val->intval = cm->raw_soc_enabled;
#endif
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT_ZBL:
		cm_get_charge_cycle(cm, &val->intval);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int
zte_charger_set_property(struct zte_power_supply *psy,
		     enum zte_power_supply_property psp,
		     const union power_supply_propval *val)
{
	struct charger_manager *cm = zte_power_supply_get_drvdata(psy);
	int ret = 0;

	if (psp != POWER_SUPPLY_PROP_SET_SHIP_MODE && !is_ext_pwr_online(cm) && (psp != POWER_SUPPLY_PROP_CAPACITY_RAW) && (psp != POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED) && (psp != POWER_SUPPLY_PROP_CYCLE_COUNT_ZBL))
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		charger_manager_set_ship_mode(cm, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
		vote_debug("policy: Set charging enable %d\n", val->intval);
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		zte_sqc_set_property(POWER_SUPPLY_PROP_CHARGING_ENABLED, val);
#endif
		break;
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
		vote_debug("policy: Set battery charging enable %d\n", val->intval);
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
		zte_sqc_set_property(POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, val);
#endif
		if (val->intval == 0) {
			if (cm)
				cm->is_battery_charging_enabled = true;
		} else {
			if (cm)
				cm->is_battery_charging_enabled = false;
		}
		break;
	case POWER_SUPPLY_PROP_CAPACITY_RAW:
		vote_debug("%s: Set fast charge raw_soc_enabled %d\n", __func__, val->intval);
#ifdef ZTE_CHARGER_DETAIL_CAPACITY
		cm->raw_soc_enabled = val->intval;
		if (cm->raw_soc_enabled == true) {
			cm->desc->raw_soc = cm->desc->cap * 10;
			cm->desc->raw_soc = ((cm->desc->raw_soc + 50) > 10000) ? 10000 : (cm->desc->raw_soc + 50);
			sqc_send_raw_capacity_event(cm->desc->raw_soc);
			vote_debug("%s cap:%d, ui_soc:%d, raw_soc:%d\n",
				__func__, cm->desc->cap, cm->desc->ui_soc, cm->desc->raw_soc);

			queue_delayed_work(cm->cm_cap_wq, &cm->cap_update_work, msecs_to_jiffies(0));
			queue_delayed_work(cm->raw_cap_wq, &cm->raw_cap_update_work, msecs_to_jiffies(CM_CAP_SOC_UPDATE_MIN_TIME*2));
		} else {
			cancel_delayed_work_sync(&cm->raw_cap_update_work);
		}
#endif
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT_ZBL:
		dev_info(cm->dev, "%s:line%d set battery cycle_count = %d\n", __func__, __LINE__, val->intval);
		ret = cm_set_charge_cycle(cm, val->intval);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int zte_charger_property_is_writeable(struct zte_power_supply *psy,
					 enum zte_power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
	case POWER_SUPPLY_PROP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_CYCLE_COUNT_ZBL:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}

static enum zte_power_supply_property zte_default_charger_props[] = {
	/* Guaranteed to provide */
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_BATTERY_ID,
	POWER_SUPPLY_PROP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_CYCLE_COUNT_ZBL,
};

static const struct zte_power_supply_desc zte_psy_default = {
	.name = "zte_battery",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = zte_default_charger_props,
	.num_properties = ARRAY_SIZE(zte_default_charger_props),
	.get_property = zte_charger_get_property,
	.set_property = zte_charger_set_property,
	.property_is_writeable	= zte_charger_property_is_writeable,
	.no_thermal = true,
};

/*
 * support multi charger adapt
 */

static struct charger_otg_match psy_otg_table[PSY_OTG_NUM_MAX] = {
	/*{"bq2560x_charger", "bq2560x_otg_vbus"},
	{"fan54015_charger", "fan54015_otg_vbus"},
	{"bq25890_charger", "bq25890_otg_vbus"},
	{"eta6937_charger", "eta6937_otg_vbus"},
	{"sgm41511_charger", "sgm41511_otg_vbus"},
	{"sgm41513_charger", "sgm41513_otg_vbus"},
	{"rt9471_charger", "rt9471_otg_vbus"},*/
	{"sgm41510_charger", "sgm41510_otg_vbus"},
	{"sc8989x_charger", "sc8989x_otg_vbus"},
	{"hl7009_charger", "hl7009_otg_vbus"},
	{NULL, NULL},
};

static const char *cm_charger_find_otg(struct charger_manager *cm)
{
	int i;
	const char *psy_name;
	const char *otg_name = NULL;
	const char *psy_charger_name = cm->desc->psy_charger_stat[0];

	for (i = 0; psy_otg_table[i].charger_power_supply_name; i++) {
		psy_name = psy_otg_table[i].charger_power_supply_name;
		if (!strcmp(psy_name, psy_charger_name)) {
			otg_name = psy_otg_table[i].otg_name;
			break;
		}
	}

	return otg_name;
}

static int cm_charger_enable_otg(struct regulator_dev *dev)
{
	struct charger_manager *cm = rdev_get_drvdata(dev);
	int ret = 0;

	if (!cm->otg_name) {
		cm->otg_name = cm_charger_find_otg(cm);
		if (!cm->otg_name)
			return -EINVAL;
	}

	if (!cm->charger_otg) {
		cm->charger_otg = regulator_get(cm->dev, cm->otg_name);
		if (IS_ERR(cm->charger_otg)) {
			ret = PTR_ERR(cm->charger_otg);
			dev_err(cm->dev, "failed to get charger "
					"otg vbus supply:%s, ret = %d\n",
					cm->otg_name, ret);
			return ret;
		}
	}

	ret = regulator_enable(cm->charger_otg);
	if (ret) {
		dev_err(cm->dev, "cannot enable %s "
				"vbus regulator, ret=%d\n",
				cm->otg_name, ret);
		return ret;
	}

	return ret;
}

static int cm_charger_disable_otg(struct regulator_dev *dev)
{
	struct charger_manager *cm = rdev_get_drvdata(dev);
	int ret = 0;

	if (!cm->otg_name) {
		cm->otg_name = cm_charger_find_otg(cm);
		if (!cm->otg_name)
			return -EINVAL;
	}

	if (!cm->charger_otg) {
		cm->charger_otg = regulator_get(cm->dev, cm->otg_name);
		if (IS_ERR(cm->charger_otg)) {
			ret = PTR_ERR(cm->charger_otg);
			dev_err(cm->dev, "failed to get charger "
					"otg vbus supply:%s, ret = %d\n",
					cm->otg_name, ret);
			return ret;
		}
	}

	ret = regulator_disable(cm->charger_otg);
	if (ret) {
		dev_err(cm->dev, "cannot disable %s "
				"vbus regulator, ret=%d\n",
				cm->otg_name, ret);
		return ret;
	}
	return ret;
}

static int cm_charger_vbus_is_enabled(struct regulator_dev *dev)
{
	struct charger_manager *cm = rdev_get_drvdata(dev);
	int ret = 0;

	if (!cm->otg_name) {
		cm->otg_name = cm_charger_find_otg(cm);
		if (!cm->otg_name)
			return -EINVAL;
	}

	if (!cm->charger_otg) {
		cm->charger_otg = regulator_get(cm->dev, cm->otg_name);
		if (IS_ERR(cm->charger_otg)) {
			ret = PTR_ERR(cm->charger_otg);
			dev_err(cm->dev, "failed to get charger "
					"otg vbus supply:%s, ret = %d\n",
					cm->otg_name, ret);
			return ret;
		}
	}

	ret = regulator_is_enabled(cm->charger_otg);
	return ret;
}

static const struct regulator_ops cm_charger_vbus_ops = {
	.enable = cm_charger_enable_otg,
	.disable = cm_charger_disable_otg,
	.is_enabled = cm_charger_vbus_is_enabled,
};

static const struct regulator_desc cm_charger_vbus_desc = {
	.name = "cm_otg_vbus",
	.of_match = "cm_otg_vbus",
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
	.ops = &cm_charger_vbus_ops,
	.fixed_uV = 5000000,
	.n_voltages = 1,
};

static int
cm_charger_register_vbus_regulator(struct charger_manager *cm)
{
	struct regulator_config cfg = { };
	struct regulator_dev *reg;
	int ret = 0;

	if (cm->desc->psy_nums <= 0 || !cm->desc->enable_multi_charger_adapt) {
		dev_info(cm->dev, "psy nums:%d, enable_multi_charger_adapt:%d\n",
			cm->desc->psy_nums, cm->desc->enable_multi_charger_adapt);
		return 0;
	}

	cfg.dev = cm->dev;
	cfg.driver_data = cm;
	reg = devm_regulator_register(cm->dev, &cm_charger_vbus_desc, &cfg);
	if (IS_ERR(reg)) {
		ret = PTR_ERR(reg);
		dev_err(cm->dev, "Can't register regulator:%d\n", ret);
	}

	return ret;
}

static int usb_property_is_writeable(struct power_supply *psy,
					enum power_supply_property psp)
{
	switch (psp) {
	default:
		break;
	}

	return 0;
}

static int usb_psy_get_property(struct power_supply *psy,
				enum power_supply_property psp,
				union power_supply_propval *pval)
{
	struct charger_manager *cm = power_supply_get_drvdata(psy);
	bool usb_online = false;

	usb_online = is_ext_usb_pwr_online(cm);

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
		pval->intval = !!usb_online;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int usb_psy_set_property(struct power_supply *psy,
		     enum power_supply_property psp,
		    const union power_supply_propval *pval)
{

	switch (psp) {
	default:
		return -EINVAL;
	}

	return 0;
}

static void usb_external_power_changed(struct power_supply *psy)
{
	vote_debug("usb power supply changed\n");
}

static enum power_supply_property usb_psy_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc usb_psy_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = usb_psy_props,
	.num_properties = ARRAY_SIZE(usb_psy_props),
	.get_property = usb_psy_get_property,
	.set_property = usb_psy_set_property,
	.property_is_writeable = usb_property_is_writeable,
	.external_power_changed = usb_external_power_changed,
};

static int zte_usb_psy_get_property(struct zte_power_supply *psy,
				enum zte_power_supply_property psp,
				union power_supply_propval *pval)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_LPM_USB_DISCON:
		pval->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
	#ifdef ZTE_FEATURE_PV_AR
		if (get_c_to_c() == 1)
			pval->intval = 1;
		else if (get_c_to_c() == 2)
			pval->intval = 2;
		else
			pval->intval = 0;
	#else
		pval->intval = 0;
	#endif
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int zte_usb_psy_set_property(struct zte_power_supply *psy,
			 enum zte_power_supply_property psp,
			const union power_supply_propval *pval)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_LPM_USB_DISCON:
	#ifdef ZTE_FEATURE_PV_AR
		if (pval->intval == 0) {
			pr_info("start exit zte lpm usb test.\n");
			musb_lpm_usb_disconnect();
		}
	#endif
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int zte_usb_property_is_writeable(struct zte_power_supply *psy,
					enum zte_power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_LPM_USB_DISCON:
		return 1;
	default:
		break;
	}

	return 0;
}

static void zte_usb_external_power_changed(struct zte_power_supply *psy)
{
	vote_debug("zte usb power supply changed\n");
}

static enum zte_power_supply_property zte_usb_psy_props[] = {
	POWER_SUPPLY_PROP_LPM_USB_DISCON,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
};

static const struct zte_power_supply_desc zte_usb_psy_desc = {
	.name = "zte_usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = zte_usb_psy_props,
	.num_properties = ARRAY_SIZE(zte_usb_psy_props),
	.get_property = zte_usb_psy_get_property,
	.set_property = zte_usb_psy_set_property,
	.property_is_writeable = zte_usb_property_is_writeable,
	.external_power_changed = zte_usb_external_power_changed,
};

static int charger_manager_init_psy(struct charger_manager *cm)
{
	struct charger_policy *policy = &(cm->policy);
	struct power_supply_config batt_cfg = {};

	batt_cfg.drv_data = cm;
	batt_cfg.of_node = cm->dev->of_node;

	policy->usb_power_phy = devm_power_supply_register(cm->dev,
						   &usb_psy_desc,
						   &batt_cfg);
	if (IS_ERR(policy->usb_power_phy)) {
		vote_error("Couldn't register usb_power_phy power supply\n");
		return PTR_ERR(policy->usb_power_phy);
	}

	policy->zte_usb_power_phy = zte_devm_power_supply_register(cm->dev,
						   &zte_usb_psy_desc,
						   &batt_cfg);
	if (IS_ERR(policy->zte_usb_power_phy)) {
		vote_error("Couldn't register zte_usb_power_phy power supply\n");
		return PTR_ERR(policy->zte_usb_power_phy);
	}


	return 0;
}
/*zte add end*/
/**
 * cm_setup_timer - For in-suspend monitoring setup wakeup alarm
 *		    for suspend_again.
 *
 * Returns true if the alarm is set for Charger Manager to use.
 * Returns false if
 *	cm_setup_timer fails to set an alarm,
 *	cm_setup_timer does not need to set an alarm for Charger Manager,
 *	or an alarm previously configured is to be used.
 */
static bool cm_setup_timer(void)
{
	struct charger_manager *cm;
	unsigned int wakeup_ms = UINT_MAX;
	int timer_req = 0;

	if (time_after(next_polling, jiffies))
		CM_MIN_VALID(wakeup_ms,
			jiffies_to_msecs(next_polling - jiffies));

	mutex_lock(&cm_list_mtx);
	list_for_each_entry(cm, &cm_list, entry) {
		unsigned int fbchk_ms = 0;

		/* fullbatt_vchk is required. setup timer for that */
		if (cm->fullbatt_vchk_jiffies_at) {
			fbchk_ms = jiffies_to_msecs(cm->fullbatt_vchk_jiffies_at
						    - jiffies);
			if (time_is_before_eq_jiffies(
				cm->fullbatt_vchk_jiffies_at) ||
				msecs_to_jiffies(fbchk_ms) < CM_JIFFIES_SMALL) {
				fbchk_ms = 0;
			}
		}
		CM_MIN_VALID(wakeup_ms, fbchk_ms);

		/* Skip if polling is not required for this CM */
		if (!is_polling_required(cm) && !cm->emergency_stop)
			continue;
		timer_req++;
		if (cm->desc->polling_interval_ms == 0)
			continue;
		if (cm->desc->ir_comp.ir_compensation_en)
			CM_MIN_VALID(wakeup_ms, CM_IR_COMPENSATION_TIME * 1000);
		else
			CM_MIN_VALID(wakeup_ms, cm->desc->polling_interval_ms);
	}
	mutex_unlock(&cm_list_mtx);

	if (timer_req && cm_timer) {
		ktime_t now, add;

		/*
		 * Set alarm with the polling interval (wakeup_ms)
		 * The alarm time should be NOW + CM_RTC_SMALL or later.
		 */
		if (wakeup_ms == UINT_MAX ||
			wakeup_ms < CM_RTC_SMALL * MSEC_PER_SEC)
			wakeup_ms = 2 * CM_RTC_SMALL * MSEC_PER_SEC;

		pr_info("Charger Manager wakeup timer: %u ms\n", wakeup_ms);

		now = ktime_get_boottime();
		add = ktime_set(wakeup_ms / MSEC_PER_SEC,
				(wakeup_ms % MSEC_PER_SEC) * NSEC_PER_MSEC);
		alarm_start(cm_timer, ktime_add(now, add));

		cm_suspend_duration_ms = wakeup_ms;

		return true;
	}
	return false;
}


static int cm_init_thermal_data(struct charger_manager *cm,
				struct power_supply *fuel_gauge,
				enum power_supply_property *properties,
				size_t *num_properties)
{
	struct charger_desc *desc = cm->desc;
	union power_supply_propval val;
	int ret;

	/* Verify whether fuel gauge provides battery temperature */
	ret = power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_TEMP, &val);

	if (!ret) {
		if (!cm_add_battery_psy_property(cm, POWER_SUPPLY_PROP_TEMP,
						 properties, num_properties))
			dev_warn(cm->dev, "POWER_SUPPLY_PROP_TEMP is present\n");
		cm->desc->measure_battery_temp = true;
	}
#if IS_ENABLED(CONFIG_THERMAL)
	if (desc->thermal_zone) {
		cm->tzd_batt =
			thermal_zone_get_zone_by_name(desc->thermal_zone);
		if (IS_ERR(cm->tzd_batt))
			return PTR_ERR(cm->tzd_batt);

		/* Use external thermometer */
		if (!cm_add_battery_psy_property(cm, POWER_SUPPLY_PROP_TEMP_AMBIENT, properties, num_properties))
			dev_warn(cm->dev, "POWER_SUPPLY_PROP_TEMP_AMBIENT is present\n");
		cm->desc->measure_battery_temp = true;
		ret = 0;
	}
#endif
	if (cm->desc->measure_battery_temp) {
		/* NOTICE : Default allowable minimum charge temperature is 0 */
		if (!desc->temp_max)
			desc->temp_max = CM_DEFAULT_CHARGE_TEMP_MAX;
		if (!desc->temp_diff)
			desc->temp_diff = CM_DEFAULT_RECHARGE_TEMP_DIFF;
	}

	return ret;
}

static int cm_init_jeita_table(struct sprd_battery_info *info,
			       struct charger_desc *desc, struct device *dev)
{
	int i;

	for (i = SPRD_BATTERY_JEITA_DCP; i < SPRD_BATTERY_JEITA_MAX; i++) {
		desc->jeita_size[i] = info->sprd_battery_jeita_size[i];
		if (!desc->jeita_size[i]) {
			dev_warn(dev, "%s jeita_size is zero\n",
				 sprd_battery_jeita_type_names[i]);
			continue;
		}

		desc->max_current_jeita_index[i] = info->max_current_jeita_index[i];

		desc->jeita_tab_array[i] = devm_kmemdup(dev, info->jeita_table[i],
							desc->jeita_size[i] *
							sizeof(struct sprd_battery_jeita_table),
							GFP_KERNEL);
		if (!desc->jeita_tab_array[i]) {
			dev_warn(dev, "Fail to kmemdup %s\n",
				 sprd_battery_jeita_type_names[i]);
			return -ENOMEM;
		}
	}

	desc->jeita_tab = desc->jeita_tab_array[SPRD_BATTERY_JEITA_UNKNOWN];
	desc->jeita_tab_size = desc->jeita_size[SPRD_BATTERY_JEITA_UNKNOWN];
	jeita_info_init(&desc->jeita_info);

	dev_warn(dev, "%s jeita_size is: %d\n", desc->jeita_tab_size);

	return 0;
}

static const struct of_device_id charger_manager_match[] = {
	{
		.compatible = "charger-manager",
	},
	{},
};
MODULE_DEVICE_TABLE(of, charger_manager_match);

static struct charger_desc *of_cm_parse_desc(struct device *dev)
{
	struct charger_desc *desc;
	struct device_node *np = dev->of_node;
	u32 poll_mode = CM_POLL_DISABLE;
	u32 battery_stat = CM_NO_BATTERY;
	int ret = 0, i = 0, num_chgs = 0, num_cp_psys = 0;
	int num_psys = 0;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return ERR_PTR(-ENOMEM);

	of_property_read_string(np, "cm-name", &desc->psy_name);

	of_property_read_u32(np, "cm-poll-mode", &poll_mode);
	desc->polling_mode = poll_mode;

	desc->uvlo_shutdown_mode = CM_SHUTDOWN_MODE_ANDROID;
	of_property_read_u32(np, "cm-uvlo-shutdown-mode", &desc->uvlo_shutdown_mode);

	of_property_read_u32(np, "cm-poll-interval",
				&desc->polling_interval_ms);

	of_property_read_u32(np, "cm-fullbatt-vchkdrop-ms",
					&desc->fullbatt_vchkdrop_ms);
	of_property_read_u32(np, "cm-fullbatt-vchkdrop-volt",
					&desc->fullbatt_vchkdrop_uV);
	of_property_read_u32(np, "cm-fullbatt-soc", &desc->fullbatt_soc);
	of_property_read_u32(np, "cm-fullbatt-capacity",
					&desc->fullbatt_full_capacity);
	of_property_read_u32(np, "cm-shutdown-voltage", &desc->shutdown_voltage);
	of_property_read_u32(np, "cm-tickle-time-out", &desc->trickle_time_out);
	of_property_read_u32(np, "cm-one-cap-time", &desc->cap_one_time);
	of_property_read_u32(np, "cm-one-cap-time", &desc->default_cap_one_time);
	of_property_read_u32(np, "cm-wdt-interval", &desc->wdt_interval);

	of_property_read_u32(np, "cm-battery-stat", &battery_stat);
	desc->battery_present = battery_stat;

	/* power supply */
	num_psys = of_property_count_strings(np, "cm-power-supplys");
	dev_info(dev, "%s num_psys = %d\n", __func__, num_psys);
	desc->enable_multi_charger_adapt =
		device_property_read_bool(dev, "cm-multi-charger-adapt-enable");
	if (num_psys > 0) {
		desc->psy_nums = num_psys;
		/* Allocate empty bin at the tail of array */
		desc->psy_stat = devm_kzalloc(dev, sizeof(char *)
						* (num_psys + 1), GFP_KERNEL);
		if (desc->psy_stat) {
			for (i = 0; i < num_psys; i++)
				of_property_read_string_index(np, "cm-power-supplys",
						i, &desc->psy_stat[i]);
		} else {
			return ERR_PTR(-ENOMEM);
		}
	}

	/* chargers */
	num_chgs = of_property_count_strings(np, "cm-chargers");
	if (num_chgs > 0) {
		desc->buck_nums = num_chgs;
		/* Allocate empty bin at the tail of array */
		desc->psy_charger_stat = devm_kcalloc(dev,
						      num_chgs + 1,
						      sizeof(char *),
						      GFP_KERNEL);
		if (!desc->psy_charger_stat)
			return ERR_PTR(-ENOMEM);

		for (i = 0; i < num_chgs; i++)
			of_property_read_string_index(np, "cm-chargers", i,
						      &desc->psy_charger_stat[i]);
	}

	desc->enable_alt_cp_adapt =
		device_property_read_bool(dev, "cm-alt-cp-adapt-enable");

	/* alternative charge pupms power supply */
	num_cp_psys = of_property_count_strings(np, "cm-alt-cp-power-supplys");
	dev_info(dev, "%s num_cp_psys = %d\n", __func__, num_cp_psys);
	if (num_cp_psys > 0) {
		desc->alt_cp_nums = num_cp_psys;
		/* Allocate empty bin at the tail of array */
		desc->psy_alt_cp_adpt_stat = devm_kzalloc(dev, sizeof(char *)
						* (num_cp_psys + 1), GFP_KERNEL);
		if (desc->psy_alt_cp_adpt_stat) {
			for (i = 0; i < num_cp_psys; i++)
				of_property_read_string_index(np, "cm-alt-cp-power-supplys",
						i, &desc->psy_alt_cp_adpt_stat[i]);
		} else {
			return ERR_PTR(-ENOMEM);
		}
	}

	/* charge pumps */
	num_chgs = of_property_count_strings(np, "cm-charge-pumps");
	if (num_chgs > 0) {
		/* Allocate empty bin at the tail of array */
		desc->cp_nums = num_chgs;
		desc->psy_cp_stat =
			devm_kzalloc(dev, sizeof(char *) * (u32)(num_chgs + 1), GFP_KERNEL);
		if (!desc->psy_cp_stat)
			return ERR_PTR(-ENOMEM);

		for (i = 0; i < num_chgs; i++)
			of_property_read_string_index(np, "cm-charge-pumps", i,
						      &desc->psy_cp_stat[i]);
	}

	/* wireless chargers */
	num_chgs = of_property_count_strings(np, "cm-wireless-chargers");
	if (num_chgs > 0) {
		/* Allocate empty bin at the tail of array */
		desc->psy_wl_charger_stat =
			devm_kzalloc(dev,  sizeof(char *) * (u32)(num_chgs + 1), GFP_KERNEL);
		if (desc->psy_wl_charger_stat) {
			for (i = 0; i < num_chgs; i++)
				of_property_read_string_index(np, "cm-wireless-chargers",
						i, &desc->psy_wl_charger_stat[i]);
		} else {
			return ERR_PTR(-ENOMEM);
		}
	}

	/* wireless charge pump converters */
	num_chgs = of_property_count_strings(np, "cm-wireless-charge-pump-converters");
	if (num_chgs > 0) {
		/* Allocate empty bin at the tail of array */
		desc->psy_cp_converter_stat =
			devm_kzalloc(dev, sizeof(char *) * (u32)(num_chgs + 1), GFP_KERNEL);
		if (desc->psy_cp_converter_stat) {
			for (i = 0; i < num_chgs; i++)
				of_property_read_string_index(np, "cm-wireless-charge-pump-converters",
						i, &desc->psy_cp_converter_stat[i]);
		} else {
			return ERR_PTR(-ENOMEM);
		}
	}

/*zte_power_supply begin*/
	ret = of_property_read_string(np, "zte-cm-charger", &desc->zte_psy_hardware);
	if (ret) {
		pr_info("No zte-cm-charger psy config, ret=%d\n", ret);
		desc->zte_psy_hardware = NULL;
	} else
		pr_info("zte-cm-charger psy config: %s!\n", desc->zte_psy_hardware);

	ret = of_property_read_string(np, "zte-cm-charger2", &desc->zte_psy_hardware2);
	if (ret) {
		pr_info("No zte-cm-charger2 psy config, ret=%d\n", ret);
		desc->zte_psy_hardware2 = NULL;
	} else
		pr_info("cm-hardware2-psy config: %s!\n", desc->zte_psy_hardware2);
/*zte_power_supply end*/
	of_property_read_string(np, "cm-fuel-gauge", &desc->psy_fuel_gauge);
	of_property_read_string(np, "zte-cm-fuel-gauge", &desc->zte_psy_fuel_gauge);

	of_property_read_string(np, "cm-thermal-zone", &desc->thermal_zone);

	of_property_read_u32(np, "cm-battery-cold", &desc->temp_min);
	if (of_get_property(np, "cm-battery-cold-in-minus", NULL))
		desc->temp_min *= -1;
	of_property_read_u32(np, "cm-battery-hot", &desc->temp_max);
	of_property_read_u32(np, "cm-battery-temp-diff", &desc->temp_diff);

	of_property_read_u32(np, "cm-charging-max",
				&desc->charging_max_duration_ms);
	of_property_read_u32(np, "cm-discharging-max",
				&desc->discharging_max_duration_ms);
	of_property_read_u32(np, "cm-charge-voltage-max",
			     &desc->normal_charge_voltage_max);
	of_property_read_u32(np, "cm-charge-voltage-drop",
			     &desc->normal_charge_voltage_drop);
	of_property_read_u32(np, "cm-fast-charge-voltage-max",
			     &desc->fast_charge_voltage_max);
	of_property_read_u32(np, "cm-fast-charge-voltage-drop",
			     &desc->fast_charge_voltage_drop);
	of_property_read_u32(np, "cm-flash-charge-voltage-max",
			     &desc->flash_charge_voltage_max);
	of_property_read_u32(np, "cm-flash-charge-voltage-drop",
			     &desc->flash_charge_voltage_drop);
	of_property_read_u32(np, "cm-wireless-charge-voltage-max",
			     &desc->wireless_normal_charge_voltage_max);
	of_property_read_u32(np, "cm-wireless-charge-voltage-drop",
			     &desc->wireless_normal_charge_voltage_drop);
	of_property_read_u32(np, "cm-wireless-fast-charge-voltage-max",
			     &desc->wireless_fast_charge_voltage_max);
	of_property_read_u32(np, "cm-wireless-fast-charge-voltage-drop",
			     &desc->wireless_fast_charge_voltage_drop);
	of_property_read_u32(np, "cm-cp-taper-current",
			     &desc->cp_sm.taper_current);
	of_property_read_u32(np, "cm-cap-full-advance-percent",
			     &desc->cap_remap_full_percent);

	if (desc->psy_cp_stat && !desc->cp_sm.taper_current)
		desc->cp_sm.taper_current = CM_CP_DEFAULT_TAPER_CURRENT;

	return desc;
}

static inline struct charger_desc *cm_get_drv_data(struct platform_device *pdev)
{
	if (pdev->dev.of_node)
		return of_cm_parse_desc(&pdev->dev);
	return dev_get_platdata(&pdev->dev);
}

static int cm_get_bat_info(struct charger_manager *cm, int bat_aging_id)
{
	struct sprd_battery_info info = {};
	int ret;

	ret = sprd_battery_get_battery_info(cm->charger_psy, &info, bat_aging_id);
	if (ret) {
		dev_err(cm->dev, "failed to get battery information\n");
		sprd_battery_put_battery_info(cm->charger_psy, &info);
		return ret;
	}

	cm->desc->internal_resist = info.factory_internal_resistance_uohm / 1000;
	cm->desc->ir_comp.us = info.constant_charge_voltage_max_uv;
	cm->desc->ir_comp.us_upper_limit = info.ir.us_upper_limit_uv;
	cm->desc->ir_comp.rc = info.ir.rc_uohm / 1000;
	cm->desc->ir_comp.cp_upper_limit_offset = info.ir.cv_upper_limit_offset_uv;
	cm->desc->constant_charge_voltage_max_uv = info.constant_charge_voltage_max_uv;
	cm->desc->fullbatt_voltage_offset_uv = info.fullbatt_voltage_offset_uv;
	cm->desc->fchg_ocv_threshold = info.fast_charge_ocv_threshold_uv;
	cm->desc->cp_sm.default_max_ibat = info.cur.flash_cur;
	cm->desc->cp_sm.default_max_ibus = info.cur.flash_limit;
	cm->desc->cur.sdp_limit = info.cur.sdp_limit;
	cm->desc->cur.sdp_cur = info.cur.sdp_cur;
	cm->desc->cur.dcp_limit = info.cur.dcp_limit;
	cm->desc->cur.dcp_cur = info.cur.dcp_cur;
	cm->desc->cur.cdp_limit = info.cur.cdp_limit;
	cm->desc->cur.cdp_cur = info.cur.cdp_cur;
	cm->desc->cur.unknown_limit = info.cur.unknown_limit;
	cm->desc->cur.unknown_cur = info.cur.unknown_cur;
	cm->desc->cur.fchg_limit = info.cur.fchg_limit;
	cm->desc->cur.fchg_cur = info.cur.fchg_cur;
	cm->desc->cur.flash_limit = info.cur.flash_limit;
	cm->desc->cur.flash_cur = info.cur.flash_cur;
	cm->desc->cur.wl_bpp_limit = info.cur.wl_bpp_limit;
	cm->desc->cur.wl_bpp_cur = info.cur.wl_bpp_cur;
	cm->desc->cur.wl_epp_limit = info.cur.wl_epp_limit;
	cm->desc->cur.wl_epp_cur = info.cur.wl_epp_cur;
	cm->desc->fullbatt_uV = info.fullbatt_voltage_uv;
	cm->desc->fullbatt_uA = info.fullbatt_current_uA;
	cm->desc->first_fullbatt_uA = info.first_fullbatt_current_uA;

	dev_info(cm->dev, "SPRD_BATTERY_INFO: internal_resist= %d, us= %d, constant_charge_voltage_max_uv= %d, fchg_ocv_threshold= %d, cp_max_ibat= %d,"
	"cp_max_ibus= %d, sdp_limit= %d, sdp_cur= %d, dcp_limit= %d, dcp_cur= %d, cdp_limit= %d, cdp_cur= %d unknown_limit= %d, unknown_cur= %d, fchg_limit= %d, fchg_cur= %d, flash_limit= %d, flash_cur= %d, wl_bpp_limit= %d,"
	"wl_bpp_cur= %d, wl_epp_limit= %d, wl_epp_cur= %d, fullbatt_uV= %d, fullbatt_uA= %d, cm->desc->first_fullbatt_uA= %d, us_upper_limit= %d, rc= %d, cp_upper_limit_offset= %d, cm->battery_id=%d, temp_min=%d\n",
		 cm->desc->internal_resist, cm->desc->ir_comp.us,
		 cm->desc->constant_charge_voltage_max_uv, cm->desc->fchg_ocv_threshold,
		 cm->desc->cp_sm.default_max_ibat, cm->desc->cp_sm.default_max_ibus, cm->desc->cur.sdp_limit,
		 cm->desc->cur.sdp_cur, cm->desc->cur.dcp_limit, cm->desc->cur.dcp_cur,
		 cm->desc->cur.cdp_limit, cm->desc->cur.cdp_cur, cm->desc->cur.unknown_limit,
		 cm->desc->cur.unknown_cur, cm->desc->cur.fchg_limit, cm->desc->cur.fchg_cur,
		 cm->desc->cur.flash_limit, cm->desc->cur.flash_cur, cm->desc->cur.wl_bpp_limit,
		 cm->desc->cur.wl_bpp_cur, cm->desc->cur.wl_epp_limit, cm->desc->cur.wl_epp_cur,
		 cm->desc->fullbatt_uV, cm->desc->fullbatt_uA, cm->desc->first_fullbatt_uA,
		 cm->desc->ir_comp.us_upper_limit, cm->desc->ir_comp.rc,
		 cm->desc->ir_comp.cp_upper_limit_offset, cm->battery_id, cm->desc->temp_min);

	ret = cm_init_jeita_table(&info, cm->desc, cm->dev);
	if (ret) {
		sprd_battery_put_battery_info(cm->charger_psy, &info);
		return ret;
	}

	if (cm->desc->fullbatt_uV == 0)
		dev_info(cm->dev, "Ignoring full-battery voltage threshold as it is not supplied\n");

	if (cm->desc->fullbatt_uA == 0)
		dev_info(cm->dev, "Ignoring full-battery current threshold as it is not supplied\n");

	if (cm->desc->fullbatt_voltage_offset_uv == 0)
		dev_info(cm->dev, "Ignoring full-battery voltage offset as it is not supplied\n");

	sprd_battery_put_battery_info(cm->charger_psy, &info);

	if (cm->desc->cur.fchg_limit > 0 && cm->desc->cur.fchg_cur > 0)
		cm->desc->support_fixed_fchg = true;

	if (cm->desc->cur.flash_limit > 0 && cm->desc->cur.flash_cur > 0)
		cm->desc->support_adaptive_fchg = true;

	return 0;
}

static void cm_batt_aging_algo(struct charger_manager *cm)
{
	int ret = 0, bat_aging_id;

	ret = cm_get_bat_aging_id(cm, &bat_aging_id);
	if (ret)
		return;

	cm_get_bat_info(cm, bat_aging_id);
}

static void cm_charge_mode_shutdown_handle(struct charger_manager *cm)
{
	if (is_charger_mode) {
		dev_info(cm->dev, "%s: charge mode shutdown\n", __func__);
		kernel_power_off();
	}
}

static void cm_shutdown_handle(struct charger_manager *cm)
{
	if (is_charger_mode) {
		kernel_power_off();
	} else {
		dev_dbg(cm->dev, "%s: shutdown mode = %d\n", __func__, cm->desc->uvlo_shutdown_mode);
		switch (cm->desc->uvlo_shutdown_mode) {
		case CM_SHUTDOWN_MODE_ORDERLY:
			orderly_poweroff(true);
			break;

		case CM_SHUTDOWN_MODE_KERNEL:
			kernel_power_off();
			break;

		case CM_SHUTDOWN_MODE_ANDROID:
			cancel_delayed_work_sync(&cm->cap_update_work);
			cm->desc->cap = 0;
			power_supply_changed(cm->charger_psy);
			cm_charge_mode_shutdown_handle(cm);
			break;

		default:
			dev_warn(cm->dev, "Incorrect uvlo_shutdown_mode (%d)\n",
				 cm->desc->uvlo_shutdown_mode);
		}
	}
}

static void cm_uvlo_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct charger_manager *cm = container_of(dwork,
				struct charger_manager, uvlo_work);
	int batt_uV, ret;

	if (unlikely(cm->shutdown_flag)) {
		dev_info(cm->dev, "don't check uvlo when shutdown\n");
		return;
	}

	ret = get_vbat_now_uV(cm, &batt_uV);
	if (ret || batt_uV < 0) {
		dev_err(cm->dev, "get_vbat_now_uV error.\n");
		return;
	}

	dev_dbg(cm->dev, "%s:line%d: batt_uV = %d\n", __func__, __LINE__, batt_uV);
	if ((u32)batt_uV <= cm->desc->shutdown_voltage + 120000)
		cm->desc->uvlo_trigger_cnt++;
	else
		cm->desc->uvlo_trigger_cnt = 0;

	if (cm->desc->uvlo_trigger_cnt >= CM_UVLO_CALIBRATION_CNT_THRESHOLD) {
		if (DIV_ROUND_CLOSEST(cm->desc->cap, 10) <= 1) {
			dev_err(cm->dev, "WARN: trigger uvlo, will shutdown with uisoc less than 1%%\n");
			cm_shutdown_handle(cm);
		} else if ((u32)batt_uV <= cm->desc->shutdown_voltage) {
			dev_err(cm->dev, "WARN: batt_uV less than shutdown voltage, will shutdown,"
				"and force capacity to 0%%\n");
			set_batt_cap(cm, 0);
			cm_shutdown_handle(cm);
		}
	}

	if (batt_uV < CM_UVLO_CALIBRATION_VOLTAGE_THRESHOLD)
		queue_delayed_work(system_unbound_wq, &cm->uvlo_work, msecs_to_jiffies(800));
}

static int cm_get_charging_works_cycle(struct charger_manager *cm,
				       int ibat_avg_ma, int *work_cycle)
{
	int ret = 0, one_cap_time, mas_one_percent, total_uah, total_mah;

	*work_cycle = CM_CAP_CYCLE_TRACK_TIME_15S;

	if (ibat_avg_ma <= 0)
		return ret;

	ret = get_batt_total_cap(cm, &total_uah);
	if (ret) {
		dev_err(cm->dev, "%s failed to get total uah.\n", __func__);
		return ret;
	}

	/*
	 * When fast charging and high current charging,
	 * the work cycle needs to be updated according to the current value.
	 * formula: 1 mAh = 1mA * 3600s = 3600mAs.
	 * mas_one_percent = 3600mAs / 100.
	 * mas_one_percent represents how many mAs there are in 1% battery capacity.
	 * one_cap_time = mas_one_percent / ibat_avg_ma.
	 * one_cap_time represents 1% battery capacity the fastest update time(unit/s).
	 */
	total_mah = total_uah / 1000;
	mas_one_percent = total_mah * 3600 / 100;
	one_cap_time = DIV_ROUND_CLOSEST(mas_one_percent, ibat_avg_ma);
	if (one_cap_time <= 10) {
		cm->desc->cap_one_time = CM_CAP_ONE_TIME_4S;
		*work_cycle = CM_CAP_CYCLE_TRACK_TIME_2S;
	} else if (one_cap_time <= 20) {
		cm->desc->cap_one_time = CM_CAP_ONE_TIME_8S;
		*work_cycle = CM_CAP_CYCLE_TRACK_TIME_4S;
	} else if (one_cap_time < 30) {
		cm->desc->cap_one_time = CM_CAP_ONE_TIME_16S;
		*work_cycle = CM_CAP_CYCLE_TRACK_TIME_8S;
	}

	return ret;
}

static int cm_get_discharging_works_cycle(struct charger_manager *cm,
					  int ibat_avg_ua, int *work_cycle)
{
	int ret = 0, batt_uV;
	static int uvlo_check_cnt;

	*work_cycle = CM_CAP_CYCLE_TRACK_TIME_15S;

	if (ibat_avg_ua >= 0)
		return ret;

	ret = get_vbat_now_uV(cm, &batt_uV);
	if (ret) {
		dev_err(cm->dev, "%s failed to get vbat_now_uV\n", __func__);
		return ret;
	}

	if (batt_uV < CM_UVLO_CALIBRATION_VOLTAGE_THRESHOLD) {
		if (++uvlo_check_cnt > 2) {
			cm->desc->cap_one_time = CM_CAP_ONE_TIME_16S;
			*work_cycle = CM_CAP_CYCLE_TRACK_TIME_8S;
		}
	} else {
		uvlo_check_cnt = 0;
	}

	return ret;
}

static int cm_calc_batt_works_cycle(struct charger_manager *cm, int uisoc)
{
	int ibat_avg_ua, ret = 0, bat_soc, work_cycle = CM_CAP_CYCLE_TRACK_TIME_15S, bat_temp;

	cm->desc->cap_one_time = cm->desc->default_cap_one_time;

	ret = get_ibat_avg_uA(cm, &ibat_avg_ua);
	if (ret) {
		dev_err(cm->dev, "%s failed to get ibat_avg_uA.\n", __func__);
		goto out;
	}

	ret = cm_get_charging_works_cycle(cm, ibat_avg_ua / 1000, &work_cycle);
	if (ret || work_cycle != CM_CAP_CYCLE_TRACK_TIME_15S)
		goto out;

	ret = cm_get_discharging_works_cycle(cm, ibat_avg_ua, &work_cycle);
	if (ret || work_cycle != CM_CAP_CYCLE_TRACK_TIME_15S)
		goto out;

	ret = cm_get_battery_temperature(cm, &bat_temp);
	if (ret) {
		dev_err(cm->dev, "%s failed to get battery temperature\n", __func__);
		goto out;
	}

	ret = get_batt_cap(cm, &bat_soc);
	if (ret) {
		dev_err(cm->dev, "%s failed to get bat_soc\n", __func__);
		goto out;
	}

	bat_soc = clamp(bat_soc, 0, CM_CAP_FULL_PERCENT);

	if (bat_temp < CM_CAP_CALC_BATT_WORKS_LOW_TEMP || uisoc < CM_CAP_CALC_BATT_WORKS_LOW_CAP ||
	    abs(ibat_avg_ua) > CM_CAP_CALC_BATT_WORKS_BIG_CUR_UA ||
	    abs(bat_soc - uisoc) > CM_CAP_CALC_BATT_WORKS_SOC_GAP) {
		cm->desc->cap_one_time = CM_CAP_ONE_TIME_16S;
		work_cycle = CM_CAP_CYCLE_TRACK_TIME_8S;
	}

out:
	return work_cycle;
}

static void zte_cm_update_battery_health_status(struct charger_manager *cm, int temp)
{
	int cur_jeita_status = 0, bat_temp_diff = 10;
	static int pre_temp = 250, pre_health = POWER_SUPPLY_HEALTH_GOOD;

	cur_jeita_status = cm_manager_get_jeita_status(cm, temp);
	if (cur_jeita_status == STATUS_BELOW_T0)
		cm->health = POWER_SUPPLY_HEALTH_COLD;
	else if (cur_jeita_status <= STATUS_T1_TO_T2)
		cm->health = POWER_SUPPLY_HEALTH_COOL;
	else if (cur_jeita_status == STATUS_ABOVE_T4)
		cm->health = POWER_SUPPLY_HEALTH_OVERHEAT;
	else if (cur_jeita_status == STATUS_T3_TO_T4)
		cm->health = POWER_SUPPLY_HEALTH_WARM;
	else if (cur_jeita_status & CM_CHARGE_VOLTAGE_ABNORMAL)
		cm->health = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
	else
		cm->health = POWER_SUPPLY_HEALTH_GOOD;

	if ((temp >= 550) || (temp <= 0))
		bat_temp_diff = 10;
	else
		bat_temp_diff = 20;

	if ((cm->health != pre_health) || (abs(temp - pre_temp) >= bat_temp_diff)) {
		dev_info(cm->dev, "update battery health and temp, health:%d, temp:%d,%d\n",
			cm->health, temp, pre_temp);
		pre_temp = temp;
		pre_health = cm->health;
		power_supply_changed(cm->charger_psy);
	}
}


#ifdef ZTE_CHARGER_DETAIL_CAPACITY
static void cm_raw_soc_works(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct charger_manager *cm = container_of(dwork,
				struct charger_manager, raw_cap_update_work);
	int bat_uA = 0, ret = 0;
	int schedule_delay = CM_CAP_SOC_UPDATE_MIN_TIME;
	u32 total_cap = 0;
	int raw_cap = 0;
	int delta_time = 100;

	ret = get_ibat_now_uA(cm, &bat_uA);
	if (ret) {
		schedule_delay = CM_CAP_SOC_UPDATE_MIN_TIME*10;
		dev_err(cm->dev, "get bat_uA error.\n");
		goto end_loop;
	}

	ret = get_batt_total_cap(cm, &total_cap);
	if (ret) {
		dev_err(cm->dev, "failed to get total cap.\n");
		total_cap = 4980*1000;
	}

	/* Calc the raw cap for refs */
	raw_cap = cm->desc->cap * 10;
	raw_cap = ((raw_cap + 50) > 10000) ? 10000 : (raw_cap + 50);

	/* I(A)*1000/3600 * T(Sec) = 4980mAH    total_cap is 4980000
	Battery capcity = 4980*3600 mAS
	If Ibat=10A, T(1%) = 4980*3600/(10*1000)/100 = 17.928s
	Time(Sec) = Battery capcity/Ibat/100 stands for one percent period.
	Time(ms) = 4980*3600*1000mAmS/bat_uA/1000/100 = 4980*36/(bat_uA/1000)
	When in charging and ibat > 1000mA */
	if (bat_uA > 1000000) {
		schedule_delay = (((int)total_cap * 36)/(bat_uA/1000))/(100);

		dev_info(cm->dev, "%s chg_status:%d, total_cap = %d, bat_uA = %d, calc schedule_delay = %d(ms),"
			"ui_soc = %d, raw_soc = %d, raw_cap = %d",
			__func__, cm->desc->charger_status, total_cap, bat_uA, schedule_delay,
			cm->desc->ui_soc, cm->desc->raw_soc, raw_cap);

		if (schedule_delay < CM_CAP_SOC_UPDATE_MIN_TIME) {
			schedule_delay = CM_CAP_SOC_UPDATE_MIN_TIME;
		} else if (schedule_delay > CM_CAP_SOC_UPDATE_MIN_TIME*10) {
			schedule_delay = CM_CAP_SOC_UPDATE_MIN_TIME*10;
		}

		if (cm->desc->raw_soc < 10000) {
			cm->desc->raw_soc++;
		}

		if (cm->desc->raw_soc < raw_cap) {
			schedule_delay = schedule_delay - delta_time;
		}

		if (cm->desc->raw_soc > raw_cap) {
			schedule_delay = schedule_delay + delta_time;
		}
	} else {
		if (abs(cm->desc->raw_soc - raw_cap) <= 100) {
			cm->desc->raw_soc++;
			schedule_delay = CM_CAP_SOC_UPDATE_MIN_TIME*5;
		} else {
			cm->desc->raw_soc = raw_cap;
			schedule_delay = CM_CAP_SOC_UPDATE_MIN_TIME*20;
			dev_info(cm->dev, "%s WARN STATE ui_soc = %d, raw_soc = %d, raw_cap = %d",
			__func__, cm->desc->ui_soc, cm->desc->raw_soc, raw_cap);
		}
	}

	if (cm->desc->raw_soc < cm->desc->ui_soc*100) {
		cm->desc->raw_soc = cm->desc->ui_soc*100;
	}

	if (cm->desc->raw_soc >= (cm->desc->ui_soc + 1)*100) {
		cm->desc->raw_soc = (cm->desc->ui_soc + 1)*100;
	}

	if (cm->desc->raw_soc >= 10000) {
		cm->desc->raw_soc = 10000;
	}

	if ((cm->desc->charger_status == POWER_SUPPLY_STATUS_FULL) ||
		((cm->desc->ui_soc == 100) && (cm->desc->raw_soc == 10000))) {
		schedule_delay = CM_CAP_SOC_UPDATE_MIN_TIME*100;
	}

	dev_info(cm->dev, "%s charger_status:%d, total_cap = %d, bat_uA = %d, real schedule_delay = %d(ms),"
		"raw_soc_enabled = %d, raw_soc = %d, raw_cap = %d, ui_soc = %d",
		__func__, cm->desc->charger_status, total_cap, bat_uA, schedule_delay,
		cm->raw_soc_enabled, cm->desc->raw_soc, raw_cap, cm->desc->ui_soc);

	sqc_send_raw_capacity_event(cm->desc->raw_soc);

end_loop:
	queue_delayed_work(cm->raw_cap_wq,
			   &cm->raw_cap_update_work,
			   msecs_to_jiffies(schedule_delay));

}
#endif

#ifdef ZTE_FEATURE_PV_AR
static int sqc_sleep_mode = 0;
int qc3dp_sleep_node_set(const char *val, const void *arg)
{
	int sleep_mode_enable = 0;
	int ret = 0;

	ret = sscanf(val, "%d", &sleep_mode_enable);
	if (ret != 1) {
		pr_err("%s: wrong args num %d. usage: echo <0 or 1> > sleep_mode_enable\n", __func__, ret);
		return -EINVAL;
	}

	pr_info("%s %d, sleep_mode_enable = %d\n", __func__, __LINE__, sleep_mode_enable);

	if (sleep_mode_enable) {
		if (!sqc_sleep_mode) {
			pr_info("%s %d, sleep on status", __func__, __LINE__);

			/*disable cm_batt_works */
			sqc_sleep_mode = 1;
		}
	} else {
		if (sqc_sleep_mode) {
			sqc_sleep_mode = 0;
			pr_info("%s %d, sleep off status", __func__, __LINE__);
		}
	}

	return 0;
}

int qc3dp_sleep_node_get(char *val, const void *arg)
{

	return snprintf(val, PAGE_SIZE, "%u", !!sqc_sleep_mode);
}

static struct zte_misc_ops qc3dp_sleep_mode_node = {
	.node_name = "qc3dp_sleep_mode",
	.set = qc3dp_sleep_node_set,
	.get = qc3dp_sleep_node_get,
	.free = NULL,
	.arg = NULL,
};
#endif

#if IS_ENABLED(CONFIG_USB_TYPEC_CONNECT_NODE)
extern int sc27xx_typec_set_rp_rd(int cc);
int cc_connect_set(const char *val, const void *arg)
{
	struct charger_manager *cm = (struct charger_manager *) arg;
	int connect;
	int num = 0;

	if (!cm) {
		pr_info("%s: cm is null\n", __func__);
		return -EINVAL;
	}

	num = sscanf(val, "%d", &connect);
	if (num != 1) {
		pr_err("%s: get val error\n", __func__);
		return -EINVAL;
	}

	pr_info("%s %d, connect = %d\n", __func__, __LINE__, connect);

	if (connect)
		sc27xx_typec_set_rp_rd(TYPEC_CC_OPEN);
	else
		sc27xx_typec_set_rp_rd(TYPEC_CC_NC);

	return 0;
}

struct zte_misc_ops cc_connect_node = {
	.node_name = "cc_connect",
	.set = cc_connect_set,
	.get = NULL,
	.free = NULL,
	.arg = NULL,
};
#endif

static void cm_batt_works(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct charger_manager *cm = container_of(dwork,
				struct charger_manager, cap_update_work);
	struct timespec64 cur_time;
	int batt_uV, batt_ocV, batt_uA, fuel_cap, ret;
	int period_time, flush_time, cur_temp, board_temp = 0;
	int chg_cur = 0, chg_limit_cur = 0, input_cur = 0;
	int chg_vol = 0, vbat_avg = 0, ibat_avg = 0, recharge_uv = 0;
	static int last_fuel_cap = CM_MAGIC_NUM;
	int work_cycle = CM_CAP_CYCLE_TRACK_TIME_15S;
	pr_info("%s enter\n", __func__);

	ret = get_vbat_now_uV(cm, &batt_uV);
	if (ret) {
		dev_err(cm->dev, "get_vbat_now_uV error.\n");
		goto schedule_cap_update_work;
	}

	ret = get_vbat_avg_uV(cm, &vbat_avg);
	if (ret)
		dev_err(cm->dev, "get_vbat_avg_uV error.\n");

	ret = get_batt_ocv(cm, &batt_ocV);
	if (ret) {
		dev_err(cm->dev, "get_batt_ocV error.\n");
		goto schedule_cap_update_work;
	}

	ret = get_ibat_now_uA(cm, &batt_uA);
	if (ret) {
		dev_err(cm->dev, "get batt_uA error.\n");
		goto schedule_cap_update_work;
	}

	ret = get_ibat_avg_uA(cm, &ibat_avg);
	if (ret)
		dev_err(cm->dev, "get ibat_avg_uA error.\n");

	ret = get_batt_cap(cm, &fuel_cap);
	if (ret) {
		dev_err(cm->dev, "get fuel_cap error.\n");
		goto schedule_cap_update_work;
	}
	cm->raw_soc = fuel_cap;

	fuel_cap = cm_cap_advance_full(cm, fuel_cap);

	ret = get_constant_charge_current(cm, &chg_cur);
	if (ret)
		dev_warn(cm->dev, "get constant charge error.\n");

	ret = get_input_current_limit(cm, &chg_limit_cur);
	if (ret)
		dev_warn(cm->dev, "get chg_limit_cur error.\n");

	ret = get_charger_voltage(cm, &chg_vol);
	if (ret)
		dev_warn(cm->dev, "get chg_vol error.\n");

	ret = cm_get_battery_temperature(cm, &cur_temp);
	if (ret) {
		dev_err(cm->dev, "failed to get battery temperature\n");
		goto schedule_cap_update_work;
	}

	cm->desc->temperature = cur_temp;
	zte_cm_update_battery_health_status(cm, cm->desc->temperature);

	ret = cm_get_board_temperature(cm, &board_temp);
	if (ret)
		dev_warn(cm->dev, "failed to get board temperature\n");

	if (cur_temp <= CM_LOW_TEMP_REGION &&
	    batt_uV <= CM_LOW_TEMP_SHUTDOWN_VALTAGE) {
		if (cm->desc->low_temp_trigger_cnt++ > 1)
			fuel_cap = 0;
	} else if (cm->desc->low_temp_trigger_cnt != 0) {
		cm->desc->low_temp_trigger_cnt = 0;
	}

	fuel_cap = clamp(fuel_cap, 0, CM_CAP_FULL_PERCENT);

	if (last_fuel_cap == CM_MAGIC_NUM)
		last_fuel_cap = fuel_cap;

	cur_time = ktime_to_timespec64(ktime_get_boottime());

	if (is_full_charged(cm))
		cm->battery_status = POWER_SUPPLY_STATUS_FULL;
	else if (is_charging(cm))
		cm->battery_status = POWER_SUPPLY_STATUS_CHARGING;
	else if (is_ext_pwr_online(cm))
		cm->battery_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	else
		cm->battery_status = POWER_SUPPLY_STATUS_DISCHARGING;

	/*
	 * Record the charging time when battery
	 * capacity is larger than 99%.
	 */
	if (cm->battery_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (cm->desc->cap >= 985) {
			cm->desc->trickle_time =
				cur_time.tv_sec - cm->desc->trickle_start_time;
		} else {
			cm->desc->trickle_start_time = cur_time.tv_sec;
			cm->desc->trickle_time = 0;
		}
	} else {
		cm->desc->trickle_start_time = cur_time.tv_sec;
		cm->desc->trickle_time = cm->desc->trickle_time_out +
				cm->desc->cap_one_time;
	}

	flush_time = cur_time.tv_sec - cm->desc->update_capacity_time;
	period_time = cur_time.tv_sec - cm->desc->last_query_time;
	cm->desc->last_query_time = cur_time.tv_sec;

	if (cm->desc->force_set_full && is_ext_pwr_online(cm))
		cm->desc->charger_status = POWER_SUPPLY_STATUS_FULL;
	else
		cm->desc->charger_status = cm->battery_status;

	dev_info(cm->dev, "vbat: %d, vbat_avg: %d, OCV: %d, ibat: %d, ibat_avg: %d, ibus: %d,"
		 " vbus: %d, msoc: %d, chg_sts: %d, frce_full: %d, chg_lmt_cur: %d,"
		 " inpt_lmt_cur: %d, chgr_type: %d, Tboard: %d, Tbatt: %d, thm_cur: %d,"
		 " thm_pwr: %d, is_fchg: %d, fchg_en: %d, tflush: %d, tperiod: %d\n",
		 batt_uV, vbat_avg, batt_ocV, batt_uA, ibat_avg, input_cur, chg_vol, fuel_cap,
		 cm->desc->charger_status, cm->desc->force_set_full, chg_cur, chg_limit_cur,
		 cm->desc->charger_type, board_temp, cur_temp,
		 cm->desc->thm_info.thm_adjust_cur, cm->desc->thm_info.thm_pwr,
		 cm->desc->is_fast_charge, cm->desc->enable_fast_charge, flush_time, period_time);
	cm_set_charging_status(cm, (cm->desc->charger_status == POWER_SUPPLY_STATUS_CHARGING) ? 1 : 0);

	switch (cm->desc->charger_status) {
	case POWER_SUPPLY_STATUS_CHARGING:
		last_fuel_cap = fuel_cap;
		if (fuel_cap < cm->desc->cap) {
			if (batt_uA >= 0) {
				fuel_cap = cm->desc->cap;
			} else {
				if (period_time < cm->desc->cap_one_time) {
					/*
					 * The percentage of electricity is not
					 * allowed to change by 1% in cm->desc->cap_one_time.
					 */
					if ((cm->desc->cap - fuel_cap) >= 5)
						fuel_cap = cm->desc->cap - 5;
					if (flush_time < cm->desc->cap_one_time &&
					    DIV_ROUND_CLOSEST(fuel_cap, 10) !=
					    DIV_ROUND_CLOSEST(cm->desc->cap, 10))
						fuel_cap = cm->desc->cap;
				} else {
					/*
					 * If wake up from long sleep mode,
					 * will make a percentage compensation based on time.
					 */
					if ((cm->desc->cap - fuel_cap) >=
					    (period_time / cm->desc->cap_one_time) * 10)
						fuel_cap = cm->desc->cap -
							(period_time / cm->desc->cap_one_time) * 10;
				}
			}
		} else if (fuel_cap > cm->desc->cap) {
			if (period_time < cm->desc->cap_one_time) {
				if ((fuel_cap - cm->desc->cap) >= 5)
					fuel_cap = cm->desc->cap + 5;
				if (flush_time < cm->desc->cap_one_time &&
				    DIV_ROUND_CLOSEST(fuel_cap, 10) !=
				    DIV_ROUND_CLOSEST(cm->desc->cap, 10))
					fuel_cap = cm->desc->cap;
			} else {
				/*
				 * If wake up from long sleep mode,
				 * will make a percentage compensation based on time.
				 */
				if ((fuel_cap - cm->desc->cap) >=
				    (period_time / cm->desc->cap_one_time) * 10)
					fuel_cap = cm->desc->cap +
						(period_time / cm->desc->cap_one_time) * 10;
			}
		}

		if (cm->desc->cap >= 985 && cm->desc->cap <= 994 &&
		    fuel_cap >= CM_CAP_FULL_PERCENT)
			fuel_cap = 994;
		/*
		 * Record 99% of the charging time.
		 * if it is greater than 1500s,
		 * it will be mandatory to display 100%,
		 * but the background is still charging.
		 */
		if (cm->desc->cap >= 985 &&
		    cm->desc->trickle_time >= cm->desc->trickle_time_out &&
		    cm->desc->trickle_time_out > 0 &&
		    batt_uA > 0)
			cm->desc->force_set_full = true;

		break;

	case POWER_SUPPLY_STATUS_NOT_CHARGING:
	case POWER_SUPPLY_STATUS_DISCHARGING:
		/*
		 * In not charging status,
		 * the cap is not allowed to increase.
		 */
		if (fuel_cap >= cm->desc->cap) {
			last_fuel_cap = fuel_cap;
			fuel_cap = cm->desc->cap;
		} else if (cm->desc->cap >= CM_HCAP_THRESHOLD) {
			if (last_fuel_cap - fuel_cap >= CM_HCAP_DECREASE_STEP) {
				if (cm->desc->cap - fuel_cap >= CM_CAP_ONE_PERCENT)
					fuel_cap = cm->desc->cap - CM_CAP_ONE_PERCENT;
				else
					fuel_cap = cm->desc->cap - CM_HCAP_DECREASE_STEP;

				last_fuel_cap -= CM_HCAP_DECREASE_STEP;
			} else {
				fuel_cap = cm->desc->cap;
			}
		} else {
			if (period_time < cm->desc->cap_one_time) {
				if ((cm->desc->cap - fuel_cap) >= 5)
					fuel_cap = cm->desc->cap - 5;
				if (flush_time < cm->desc->cap_one_time &&
				    DIV_ROUND_CLOSEST(fuel_cap, 10) !=
				    DIV_ROUND_CLOSEST(cm->desc->cap, 10))
					fuel_cap = cm->desc->cap;
			} else {
				/*
				 * If wake up from long sleep mode,
				 * will make a percentage compensation based on time.
				 */
				if ((cm->desc->cap - fuel_cap) >=
				    (period_time / cm->desc->cap_one_time) * 10)
					fuel_cap = cm->desc->cap -
						(period_time / cm->desc->cap_one_time) * 10;
			}
		}
		break;

	case POWER_SUPPLY_STATUS_FULL:
		last_fuel_cap = fuel_cap;
		cm->desc->update_capacity_time = cur_time.tv_sec;
		recharge_uv = cm->desc->fullbatt_uV - cm->desc->fullbatt_vchkdrop_uV - 50000;
		if ((batt_ocV < recharge_uv) && (batt_uA < 0)) {
			cm->desc->force_set_full = false;
			dev_info(cm->dev, "recharge_uv = %d\n", recharge_uv);
		}

		if (is_ext_pwr_online(cm)) {
			if (fuel_cap != CM_CAP_FULL_PERCENT)
				fuel_cap = CM_CAP_FULL_PERCENT;

			if (fuel_cap > cm->desc->cap) {
				if (cm->desc->cap < 900)
					fuel_cap = cm->desc->cap + 10;
				else if (cm->desc->cap < 960)
					fuel_cap = cm->desc->cap + 5;
				else if (cm->desc->cap < 990)
					fuel_cap = cm->desc->cap + 3;
				else
					fuel_cap = cm->desc->cap + 1;
			}
		}

		break;
	default:
		break;
	}

	work_cycle = cm_calc_batt_works_cycle(cm, fuel_cap);

	if (batt_uV < CM_UVLO_CALIBRATION_VOLTAGE_THRESHOLD) {
		dev_info(cm->dev, "batt_uV is less than UVLO calib volt\n");
		queue_delayed_work(system_unbound_wq, &cm->uvlo_work, msecs_to_jiffies(100));
	}

	dev_info(cm->dev, "new_uisoc = %d, old_uisoc = %d, work_cycle = %ds, cap_one_time = %ds\n",
		 fuel_cap, cm->desc->cap, work_cycle, cm->desc->cap_one_time);

	if (fuel_cap != cm->desc->cap) {
		if (DIV_ROUND_CLOSEST(fuel_cap, 10) != DIV_ROUND_CLOSEST(cm->desc->cap, 10)) {
			cm->desc->cap = fuel_cap;
			cm->desc->update_capacity_time = cur_time.tv_sec;
			power_supply_changed(cm->charger_psy);
		}

		cm->desc->cap = fuel_cap;
		set_batt_cap(cm, cm->desc->cap);
	}


schedule_cap_update_work:
	queue_delayed_work(system_power_efficient_wq,
			   &cm->cap_update_work,
			   work_cycle * HZ);
}

static int cm_check_alt_cp_psy_ready_status(struct charger_manager *cm)
{
	struct charger_desc *desc = cm->desc;
	struct power_supply *psy;
	int i;

	if (!desc->psy_cp_stat || !desc->psy_alt_cp_adpt_stat) {
		dev_err(cm->dev, "%s, cp not exit\n", __func__);
		return 0;
	}

	psy = power_supply_get_by_name(desc->psy_cp_stat[0]);
	if (psy) {
		dev_info(cm->dev, "%s, find preferred cp \"%s\"\n",
			 __func__, desc->psy_cp_stat[0]);
		goto done;
	}

	for (i = 0; desc->psy_alt_cp_adpt_stat[i]; i++) {
		psy = power_supply_get_by_name(desc->psy_alt_cp_adpt_stat[i]);
		if (!psy) {
			dev_warn(cm->dev, "%s, cannot find alt cp \"%s\"\n",
				 __func__, desc->psy_alt_cp_adpt_stat[i]);
		} else {
			dev_info(cm->dev, "%s, find alt cp \"%s\"\n",
				 __func__, desc->psy_alt_cp_adpt_stat[i]);
			desc->psy_cp_stat[0] = desc->psy_alt_cp_adpt_stat[i];
			goto done;
		}
	}

	if (i == desc->alt_cp_nums) {
		dev_err(cm->dev, "%s, cannot find all cp\n", __func__);
		return -EPROBE_DEFER;
	}

done:
	if (psy)
		power_supply_put(psy);
	return 0;
}

static int get_boot_mode(void)
{
	struct device_node *cmdline_node;
	const char *cmd_line;
	int ret;

	cmdline_node = of_find_node_by_path("/chosen");
	ret = of_property_read_string(cmdline_node, "bootargs", &cmd_line);
	if (ret)
		return ret;

	if (strstr(cmd_line, "sprdboot.mode=cali") ||
	    strstr(cmd_line, "sprdboot.mode=autotest"))
		allow_charger_enable = true;
	else if (strstr(cmd_line, "sprdboot.mode=charger"))
		is_charger_mode =  true;

	return 0;
}


static void policy_usb_change_handler_work(struct work_struct *work)
{
	int usb_icl = 0, battery_fcc = 0;
	struct charger_policy *info =
			container_of(work, struct charger_policy, usb_changed_work.work);
	struct charger_manager *cm = container_of(info,
			struct charger_manager, policy);

	if (!cm) {
		return;
	}

	if (info->limit) {
		/* set current limitation and start to charge */
		switch (info->usb_phy->chg_type) {
		case SDP_TYPE:
			usb_icl = info->battery_current.sdp_limit;
			battery_fcc = info->battery_current.sdp_cur;
			break;
		case DCP_TYPE:
			usb_icl = info->battery_current.dcp_limit;
			battery_fcc = info->battery_current.dcp_cur;
			break;
		case CDP_TYPE:
			usb_icl = info->battery_current.cdp_limit;
			battery_fcc = info->battery_current.cdp_cur;
			break;
		default:
			usb_icl = info->battery_current.unknown_limit;
			battery_fcc = info->battery_current.unknown_cur;
		}

		pr_info("%s chg_type: %d, usb_icl %d, fcc %d\n", __func__, info->usb_phy->chg_type, usb_icl, battery_fcc);
	}

	/* Clear the force full flag when charged full once.
	During normal charging, this flag is cleared through CM_EVENT_CHG_START_STOP */
	if (cm->desc->force_set_full)
		cm->desc->force_set_full = false;

	if (cm->charger_psy)
		power_supply_changed(cm->charger_psy);
	if (info->usb_power_phy)
		power_supply_changed(info->usb_power_phy);
}

static int policy_usb_change_callback(struct notifier_block *nb,
				       unsigned long limit, void *data)
{
	struct charger_policy *info =
		container_of(nb, struct charger_policy, usb_notify);

	info->limit = limit;

	vote_debug("%s: %d\n", __func__, (uint)limit);

	schedule_delayed_work(&info->usb_changed_work, msecs_to_jiffies(100));

	return NOTIFY_OK;
}

static int charger_manager_policy_init(struct charger_manager *cm)
{
	int ret = 0;

	charger_manager_init_psy(cm);

	INIT_DELAYED_WORK(&(cm->policy.usb_changed_work), policy_usb_change_handler_work);

	cm->policy.usb_phy = devm_usb_get_phy_by_phandle(cm->dev, "phys", 0);
	if (IS_ERR_OR_NULL(cm->policy.usb_phy)) {
		vote_error("######@failed to find USB phy %d\n", (int)PTR_ERR(cm->policy.usb_phy));
		return PTR_ERR(cm->policy.usb_phy);
	}

	cm->policy.usb_notify.notifier_call = policy_usb_change_callback;
	ret = usb_register_notifier(cm->policy.usb_phy, &cm->policy.usb_notify);
	if (ret) {
		vote_error("failed to register notifier:%d\n", ret);
		return ret;
	}

	return 0;
}

int cap_debug_set(const char *val, const void *arg)
{
	struct charger_manager *cm = (struct charger_manager *) arg;
	int cap_debug = -1;
	int delay_secs = 0;
	int num = 0;

	if (!cm) {
		pr_info("%s: cm is null\n", __func__);
		return -EINVAL;
	}

	num = sscanf(val, "%d %d", &delay_secs, &cap_debug);

	pr_info("%s: set cap_debug %d after %ds\n", __func__, cap_debug, delay_secs);

	if (num != 2) {
		pr_err("%s: wrong args num %d. usage: echo <delay> <cap> > cap_debug\n", __func__, num);
		return -EINVAL;
	}

	cm->desc->cap_debug = cap_debug;

	if (!cm_wq)
		return -EFAULT;

	return 0;
}

int cap_debug_get(char *val, const void *arg)
{
	struct charger_manager *cm = (struct charger_manager *) arg;

	if (!cm) {
			pr_info("%s: cm is null\n", __func__);
			return -EINVAL;
	}

	pr_info("%s: get cap_debug: %d\n", __func__, cm->desc->cap_debug);

	return snprintf(val, PAGE_SIZE, "%d", cm->desc->cap_debug);
}

struct zte_misc_ops cap_debug_node = {
	.node_name = "cap_debug",
	.set = cap_debug_set,
	.get = cap_debug_get,
	.free = NULL,
	.arg = NULL,
};

int thermal_control_en_set(const char *val, const void *arg)
{
	struct charger_manager *cm = (struct charger_manager *) arg;
	int thermal_control_en = -1;
	int num = 0;

	if (!cm) {
		pr_info("%s: cm is null\n", __func__);
		return -EINVAL;
	}

	num = sscanf(val, "%d", &thermal_control_en);

	pr_info("%s: set thermal_control_en %d\n", __func__, thermal_control_en);

	if (num != 1) {
		pr_err("%s: wrong args num %d. usage: echo <0 or 1> > thermal_control_en\n", __func__, num);
		return -EINVAL;
	}

	if (thermal_control_en >= 1)
		cm->desc->thermal_control_en = true;
	else
		cm->desc->thermal_control_en = false;

	return 0;
}

int thermal_control_en_get(char *val, const void *arg)
{
	struct charger_manager *cm = (struct charger_manager *) arg;

	if (!cm) {
			pr_info("%s: cm is null\n", __func__);
			return -EINVAL;
	}

	pr_info("%s: get thermal_control_en: %d\n", __func__, cm->desc->thermal_control_en);

	return snprintf(val, PAGE_SIZE, "%d", cm->desc->thermal_control_en);

	return 0;
}

struct zte_misc_ops thermal_control_en_node = {
	.node_name = "thermal_control_en",
	.set = thermal_control_en_set,
	.get = thermal_control_en_get,
	.free = NULL,
	.arg = NULL,
};

int raw_soc_get(char *val, const void *arg)
{
	struct charger_manager *cm = (struct charger_manager *) arg;

	if (!cm) {
		pr_info("%s: cm is null\n", __func__);
		return -EINVAL;
	}

	pr_info("%s: get raw_soc: %d\n", __func__, cm->raw_soc);

	return snprintf(val, PAGE_SIZE, "%d", cm->raw_soc);
}

struct zte_misc_ops raw_soc_node = {
	.node_name = "raw_soc",
	.set = NULL,
	.get = raw_soc_get,
	.free = NULL,
	.arg = NULL,
};

static int charger_manager_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct charger_desc *desc = NULL;
	struct charger_manager *cm;
	int ret, i = 0;
	union power_supply_propval val;
	struct power_supply *fuel_gauge;
	enum power_supply_property *properties;
	size_t num_properties;
	struct zte_power_supply *zte_fuel_gauge;
	struct power_supply_config psy_cfg = {};
	struct timespec64 cur_time;
	unsigned int min = 0, max = 0;
	int chg_num = 0;

	pr_info("%s:line:%d sqc common probe enter!\n", __func__, __LINE__);
	desc = cm_get_drv_data(pdev);
	if (IS_ERR(desc)) {
		dev_err(&pdev->dev, "No platform data (desc) found\n");
		return PTR_ERR(desc);
	}

	cm = devm_kzalloc(&pdev->dev, sizeof(*cm), GFP_KERNEL);
	if (!cm)
		return -ENOMEM;

	/* Basic Values. Unspecified are Null or 0 */
	cm->dev = &pdev->dev;
	cm->desc = desc;
	psy_cfg.drv_data = cm;
	cm->desc->thermal_control_en = true;
	cm->health = POWER_SUPPLY_HEALTH_GOOD;
	cm->battery_id = 0;
	cm->desc->cap_debug = -1;
	/* Check if charger's supplies are present at probe */
	if ((desc->psy_nums > 0) && desc->enable_multi_charger_adapt) {
		for (i = 0; desc->psy_charger_stat[i]; i++) {
			struct power_supply *psy;

			psy = power_supply_get_by_name(desc->psy_charger_stat[i]);
			if (!psy) {
				dev_warn(&pdev->dev, "Cannot find power supply \"%s\"\n",
					desc->psy_charger_stat[i]);
			} else {
				chg_num++;
				dev_info(&pdev->dev, "find power supply \"%s\"\n",
					desc->psy_charger_stat[i]);
				power_supply_put(psy);
			}

			dev_info(&pdev->dev, "i = %d, chg_num = %d\n", i, chg_num);

			if (chg_num >= desc->buck_nums)
				break;
		}

		if (i == desc->psy_nums) {
			dev_err(&pdev->dev, "Cannot find all power supply\n");
			return -EPROBE_DEFER;
		}

		if (desc->psy_charger_stat[i])
			desc->psy_charger_stat[0] = desc->psy_charger_stat[i];
	} else {
		for (i = 0; desc->psy_charger_stat[i]; i++) {
			struct power_supply *psy;

			psy = power_supply_get_by_name(desc->psy_charger_stat[i]);
			if (!psy) {
				dev_err(&pdev->dev, "Cannot find power supply \"%s\"\n",
					desc->psy_charger_stat[i]);
				return -EPROBE_DEFER;
			}
			power_supply_put(psy);
		}
	}

	/* Initialize alarm timer */
	if (alarmtimer_get_rtcdev()) {
		cm_timer = devm_kzalloc(cm->dev, sizeof(*cm_timer), GFP_KERNEL);
		if (!cm_timer)
			return -ENOMEM;
		alarm_init(cm_timer, ALARM_BOOTTIME, NULL);
	}

	cm->vchg_info = sprd_vchg_info_register(cm->dev);
	if (IS_ERR(cm->vchg_info)) {
		dev_info(&pdev->dev, "Fail to init vchg info\n");
		return -ENOMEM;
	}

	if (cm->vchg_info->ops && cm->vchg_info->ops->parse_dts &&
	    cm->vchg_info->ops->parse_dts(cm->vchg_info)) {
		dev_err(&pdev->dev, "failed to parse sprd vchg parameters\n");
		return -EPROBE_DEFER;
	}

	cm->fchg_info = sprd_fchg_info_register(cm->dev);
	if (IS_ERR(cm->fchg_info)) {
		dev_err(&pdev->dev, "Fail to register fchg info\n");
		return -ENOMEM;
	}

	/*
	 * Some of the following do not need to be errors.
	 * Users may intentionally ignore those features.
	 */

	if (!desc->fullbatt_vchkdrop_ms || !desc->fullbatt_vchkdrop_uV) {
		dev_info(&pdev->dev, "Disabling full-battery voltage drop checking mechanism as it is not supplied\n");
		desc->fullbatt_vchkdrop_ms = 0;
		desc->fullbatt_vchkdrop_uV = 0;
	}
	if (desc->fullbatt_soc == 0)
		dev_info(&pdev->dev, "Ignoring full-battery soc(state of charge) threshold as it is not supplied\n");

	if (desc->fullbatt_full_capacity == 0)
		dev_info(&pdev->dev, "Ignoring full-battery full capacity threshold as it is not supplied\n");

	if (!desc->psy_charger_stat || !desc->psy_charger_stat[0]) {
		dev_err(&pdev->dev, "No power supply defined\n");
		return -EINVAL;
	}

	if (!desc->psy_fuel_gauge) {
		dev_err(&pdev->dev, "No fuel gauge power supply defined\n");
		return -EINVAL;
	}

	ret = get_boot_mode();
	if (ret) {
		pr_err("boot_mode can't not parse bootargs property\n");
		return ret;
	}


	if (desc->enable_alt_cp_adapt && (desc->alt_cp_nums > 0)) {
		ret = cm_check_alt_cp_psy_ready_status(cm);
		if (ret < 0) {
			dev_err(&pdev->dev, "can't find cp\n");
			return ret;
		}
	}

	if (cm->desc->polling_mode != CM_POLL_DISABLE &&
	    (desc->polling_interval_ms == 0 ||
	     msecs_to_jiffies(desc->polling_interval_ms) <= CM_JIFFIES_SMALL)) {
		dev_err(&pdev->dev, "polling_interval_ms is too small\n");
		return -EINVAL;
	}

	if (!desc->charging_max_duration_ms ||
			!desc->discharging_max_duration_ms) {
		dev_info(&pdev->dev, "Cannot limit charging duration checking mechanism to prevent overcharge/overheat and control discharging duration\n");
		desc->charging_max_duration_ms = 0;
		desc->discharging_max_duration_ms = 0;
	}

	if (!desc->charge_voltage_max || !desc->charge_voltage_drop) {
		dev_info(&pdev->dev, "Cannot validate charge voltage\n");
		desc->charge_voltage_max = 0;
		desc->charge_voltage_drop = 0;
	}

	platform_set_drvdata(pdev, cm);

	memcpy(&cm->charger_psy_desc, &psy_default, sizeof(psy_default));

	if (!desc->psy_name)
		strncpy(cm->psy_name_buf, psy_default.name, PSY_NAME_MAX);
	else
		strncpy(cm->psy_name_buf, desc->psy_name, PSY_NAME_MAX);
	cm->charger_psy_desc.name = cm->psy_name_buf;

	/* Allocate for psy properties because they may vary */
	properties = devm_kcalloc(&pdev->dev,
			     ARRAY_SIZE(default_charger_props) +
				NUM_CHARGER_PSY_OPTIONAL,
			     sizeof(*properties), GFP_KERNEL);
	if (!properties)
		return -ENOMEM;

	memcpy(properties, default_charger_props,
		sizeof(enum power_supply_property) *
		ARRAY_SIZE(default_charger_props));
	num_properties = ARRAY_SIZE(default_charger_props);

	/* Find which optional psy-properties are available */
	fuel_gauge = power_supply_get_by_name(desc->psy_fuel_gauge);
	if (!fuel_gauge) {
		dev_err(&pdev->dev, "Cannot find power supply \"%s\"\n",
			desc->psy_fuel_gauge);
		return -EPROBE_DEFER;
	}

	if (!power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CHARGE_NOW, &val)) {
		if (!cm_add_battery_psy_property(cm, POWER_SUPPLY_PROP_CHARGE_NOW, properties, &num_properties))
			dev_warn(&pdev->dev, "POWER_SUPPLY_PROP_CHARGE_NOW is present\n");
	}

	val.intval = CM_IBAT_CURRENT_NOW_CMD;
	if (!power_supply_get_property(fuel_gauge, POWER_SUPPLY_PROP_CURRENT_NOW, &val)) {
		if (!cm_add_battery_psy_property(cm, POWER_SUPPLY_PROP_CURRENT_NOW, properties, &num_properties))
			dev_warn(&pdev->dev, "POWER_SUPPLY_PROP_CURRENT_NOW is present\n");
	}

	ret = get_boot_cap(cm, &cm->desc->cap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get initial battery capacity\n");
		return ret;
	}
	if (device_property_read_bool(&pdev->dev, "cm-keep-awake"))
		cm->desc->keep_awake = true;
	cm->desc->thm_info.thm_adjust_cur = -EINVAL;
	cm->desc->ir_comp.ibat_buf[CM_IBAT_BUFF_CNT - 1] = CM_MAGIC_NUM;
	cm->desc->ir_comp.us_lower_limit = cm->desc->ir_comp.us;

	if (device_property_read_bool(&pdev->dev, "cm-support-linear-charge"))
		cm->desc->thm_info.need_calib_charge_lmt = true;
	charger_manager_policy_init(cm);

	ret = cm_get_battery_temperature(cm, &cm->desc->temperature);
	if (ret) {
		dev_err(cm->dev, "failed to get battery temperature\n");
		return ret;
	}
	cur_time = ktime_to_timespec64(ktime_get_boottime());
	cm->desc->update_capacity_time = cur_time.tv_sec;
	cm->desc->last_query_time = cur_time.tv_sec;

	ret = cm_init_thermal_data(cm, fuel_gauge, properties, &num_properties);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize thermal data\n");
		cm->desc->measure_battery_temp = false;
	}
	power_supply_put(fuel_gauge);

	cm->charger_psy_desc.properties = properties;
	cm->charger_psy_desc.num_properties = num_properties;

	if (cm->desc->zte_psy_fuel_gauge) {
		zte_fuel_gauge = zte_power_supply_get_by_name(cm->desc->zte_psy_fuel_gauge);
		if (!zte_fuel_gauge) {
			dev_err(cm->dev, "failed to get psy cm->desc->zte_psy_fuel_gauge\n");
			return -EPROBE_DEFER;
		}
		ret = zte_power_supply_get_property(zte_fuel_gauge,
				POWER_SUPPLY_PROP_BATTERY_ID,
				&val);
		cm->battery_id = val.intval;
		dev_info(cm->dev, "cm->battery_id=%d\n", cm->battery_id);
		zte_power_supply_put(zte_fuel_gauge);
	}

	INIT_DELAYED_WORK(&cm->cap_update_work, cm_batt_works);
	INIT_WORK(&cm->limit_current_work, cm_limit_current_work);
	INIT_DELAYED_WORK(&cm->charger_type_update_work, cm_charger_type_update_work);

	mutex_init(&cm->desc->charge_info_mtx);

	psy_cfg.attr_grp = NULL;
	psy_cfg.of_node = np;

	cm->zte_charger_psy = zte_power_supply_register(&pdev->dev,
						&zte_psy_default,
						&psy_cfg);
	if (IS_ERR(cm->zte_charger_psy)) {
		dev_err(&pdev->dev, "Cannot register charger-manager with name \"%s\"\n",
			zte_psy_default.name);
		return PTR_ERR(cm->zte_charger_psy);
	}

	cm->charger_psy = devm_power_supply_register(&pdev->dev,
						&cm->charger_psy_desc,
						&psy_cfg);
	if (IS_ERR(cm->charger_psy)) {
		dev_err(&pdev->dev, "Cannot register charger-manager with name \"%s\"\n",
			cm->charger_psy_desc.name);
		return PTR_ERR(cm->charger_psy);
	}
	cm->charger_psy->supplied_to = charger_manager_supplied_to;
	cm->charger_psy->num_supplicants =
		ARRAY_SIZE(charger_manager_supplied_to);

	ac_main.cm = cm;
	ac_main.psy = devm_power_supply_register(&pdev->dev, &ac_main.psd, NULL);
	if (IS_ERR(ac_main.psy)) {
		dev_err(&pdev->dev, "Cannot register usb_main.psy with name \"%s\"\n",
			ac_main.psd.name);
		return PTR_ERR(ac_main.psy);
	}

	mutex_init(&cm->desc->keep_awake_mtx);

	/* Add to the list */
	mutex_lock(&cm_list_mtx);
	list_add(&cm->entry, &cm_list);
	mutex_unlock(&cm_list_mtx);

	/*
	 * Charger-manager is capable of waking up the system from sleep
	 * when event is happened through cm_notify_event()
	 */
	device_init_wakeup(&pdev->dev, true);
	device_set_wakeup_capable(&pdev->dev, false);
	cm->charge_ws = wakeup_source_create("charger_manager_wakelock");
	wakeup_source_add(cm->charge_ws);
	cm->cp_ws = wakeup_source_create("charger_pump_wakelock");
	wakeup_source_add(cm->cp_ws);
	mutex_init(&cm->desc->charger_type_mtx);

	ret = cm_get_bat_info(cm, 0);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get battery information\n");
		goto err;
	}

	if (cm->fchg_info->ops && cm->fchg_info->ops->extcon_init &&
	    cm->fchg_info->ops->extcon_init(cm->fchg_info, cm->charger_psy)) {
		dev_err(&pdev->dev, "Failed to initialize fchg extcon\n");
		ret = -EPROBE_DEFER;
		goto err;
	}

	if (cm->vchg_info->ops && cm->vchg_info->ops->init &&
	    cm->vchg_info->ops->init(cm->vchg_info, cm->charger_psy)) {
		dev_err(&pdev->dev, "Failed to register vchg detect notify\n");
		ret = -EPROBE_DEFER;
		goto err;
	}

	g_cm = cm;
	cm->vchg_info->usb_limit = -EINVAL;

	desc->rp_limit_current = -EINVAL;

	if (is_ext_usb_pwr_online(cm) && cm->fchg_info->ops && cm->fchg_info->ops->fchg_detect)
		cm->fchg_info->ops->fchg_detect(cm->fchg_info);

	if (desc->enable_multi_charger_adapt) {
		dev_info(&pdev->dev, "enable_multi_charger_adapt is true!\n");
		ret = cm_charger_register_vbus_regulator(cm);
		if (ret) {
			dev_err(&pdev->dev, "Failed to register cm otg vbus, ret = %d\n", ret);
			ret = -EPROBE_DEFER;
			goto err;
		}
	}
	sprd_tcpm_charger_ops_register(&cm_sprd_charger_ops);
	if (cm_event_num > 0) {
		for (i = 0; i < cm_event_num; i++)
			cm_notify_type_handle(cm, cm_event_type[i], cm_event_msg[i]);
		cm_event_num = 0;
	}
	/*
	 * Charger-manager have to check the charging state right after
	 * initialization of charger-manager and then update current charging
	 * state.
	 */

	if (!IS_ERR(cm->policy.usb_phy)) {
		vote_debug("chg_state: %d\n", cm->policy.usb_phy->chg_state);
		if (cm->policy.usb_phy->chg_state == USB_CHARGER_PRESENT) {
			usb_phy_get_charger_current(cm->policy.usb_phy, &min, &max);
			cm->policy.limit = min;
			vote_debug("charger_current limit: %d\n", min);
			schedule_delayed_work(&cm->policy.usb_changed_work, msecs_to_jiffies(100));
		}
	}

	queue_delayed_work(system_power_efficient_wq, &cm->cap_update_work, CM_CAP_CYCLE_TRACK_TIME_15S * HZ);
	INIT_DELAYED_WORK(&cm->uvlo_work, cm_uvlo_check_work);

#if IS_ENABLED(CONFIG_USB_TYPEC_TEMP)
	typec_temp_init(pdev, cm);
#endif

	cm->is_battery_charging_enabled = false;
	zte_misc_register_callback(&cap_debug_node, cm);
	zte_misc_register_callback(&thermal_control_en_node, cm);
	zte_misc_register_callback(&raw_soc_node, cm);
#if IS_ENABLED(CONFIG_USB_TYPEC_CONNECT_NODE)
	zte_misc_register_callback(&cc_connect_node, cm);
#endif

#ifdef ZTE_FEATURE_PV_AR
	zte_misc_register_callback(&qc3dp_sleep_mode_node, cm);
#endif

	pr_info("%s probe success\n", __func__);
	return 0;

err:
	zte_power_supply_unregister(cm->zte_charger_psy);
	power_supply_unregister(cm->charger_psy);
	wakeup_source_remove(cm->charge_ws);

	return ret;
}

static int charger_manager_remove(struct platform_device *pdev)
{
	struct charger_manager *cm = platform_get_drvdata(pdev);

	/* Remove from the list */
	mutex_lock(&cm_list_mtx);
	list_del(&cm->entry);
	mutex_unlock(&cm_list_mtx);

	if (cm->fchg_info->ops && cm->fchg_info->ops->remove)
		cm->fchg_info->ops->remove(cm->fchg_info);

	if (cm->vchg_info->ops && cm->vchg_info->ops->remove)
		cm->vchg_info->ops->remove(cm->vchg_info);

	cancel_delayed_work_sync(&cm->cap_update_work);
	cancel_delayed_work_sync(&cm->uvlo_work);

	power_supply_unregister(cm->charger_psy);


	return 0;
}

static void charger_manager_shutdown(struct platform_device *pdev)
{
	struct charger_manager *cm = platform_get_drvdata(pdev);

	cm->shutdown_flag = true;
	if (cm->desc->uvlo_trigger_cnt < CM_UVLO_CALIBRATION_CNT_THRESHOLD)
		set_batt_cap(cm, cm->desc->cap);

	if (cm->fchg_info->ops && cm->fchg_info->ops->shutdown)
		cm->fchg_info->ops->shutdown(cm->fchg_info);

	if (cm->vchg_info->ops && cm->vchg_info->ops->shutdown)
		cm->vchg_info->ops->shutdown(cm->vchg_info);

	cancel_delayed_work_sync(&cm->cap_update_work);
	cancel_delayed_work_sync(&cm->uvlo_work);
}

static const struct platform_device_id charger_manager_id[] = {
	{ "charger-manager", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, charger_manager_id);

static int cm_suspend_noirq(struct device *dev)
{
	if (device_may_wakeup(dev)) {
		device_set_wakeup_capable(dev, false);
		return -EAGAIN;
	}

	return 0;
}

static int cm_suspend_prepare(struct device *dev)
{
	struct charger_manager *cm = dev_get_drvdata(dev);

	if (!cm_suspended)
		cm_suspended = true;

	cm_timer_set = cm_setup_timer();

	if (cm_timer_set) {
		dev_dbg(cm->dev, "%s:line%d prepare\n", __func__, __LINE__);
		cancel_delayed_work_sync(&cm->cap_update_work);
		cancel_delayed_work_sync(&cm->uvlo_work);
	}

	return 0;
}

static void cm_suspend_complete(struct device *dev)
{
	struct charger_manager *cm = dev_get_drvdata(dev);

	if (cm_suspended)
		cm_suspended = false;

	if (cm_timer_set) {
		ktime_t remain;

		alarm_cancel(cm_timer);
		cm_timer_set = false;
		remain = alarm_expires_remaining(cm_timer);
		if (remain > 0)
			cm_suspend_duration_ms -= ktime_to_ms(remain);
	}

	cm_batt_works(&cm->cap_update_work.work);

	/* Re-enqueue delayed work (fullbatt_vchk_work) */
	if (cm->fullbatt_vchk_jiffies_at) {
		unsigned long delay = 0;
		unsigned long now = jiffies + CM_JIFFIES_SMALL;

		if (time_after_eq(now, cm->fullbatt_vchk_jiffies_at)) {
			delay = (unsigned long)((long)now
				- (long)(cm->fullbatt_vchk_jiffies_at));
			delay = jiffies_to_msecs(delay);
		} else {
			delay = 0;
		}

		/*
		 * Account for cm_suspend_duration_ms with assuming that
		 * timer stops in suspend.
		 */
		if (delay > cm_suspend_duration_ms)
			delay -= cm_suspend_duration_ms;
		else
			delay = 0;
	}
	device_set_wakeup_capable(cm->dev, false);
	dev_dbg(cm->dev, "%s:line%d complete\n", __func__, __LINE__);
}

static const struct dev_pm_ops charger_manager_pm = {
	.prepare	= cm_suspend_prepare,
	.suspend_noirq	= cm_suspend_noirq,
	.complete	= cm_suspend_complete,
};

static struct platform_driver charger_manager_driver = {
	.driver = {
		.name = "charger-manager",
		.pm = &charger_manager_pm,
		.of_match_table = charger_manager_match,
	},
	.probe = charger_manager_probe,
	.remove = charger_manager_remove,
	.shutdown = charger_manager_shutdown,
	.id_table = charger_manager_id,
};

static int __init charger_manager_init(void)
{
	cm_wq = create_freezable_workqueue("charger_manager");
	if (unlikely(!cm_wq)) {
		return -ENOMEM;
	}

	return platform_driver_register(&charger_manager_driver);
}
late_initcall(charger_manager_init);

static void __exit charger_manager_cleanup(void)
{
	destroy_workqueue(cm_wq);
	cm_wq = NULL;

	platform_driver_unregister(&charger_manager_driver);
}
module_exit(charger_manager_cleanup);

/**
 * cm_notify_type_handle - charger driver handle charger event
 * @cm: the Charger Manager representing the battery
 * @type: type of charger event
 * @msg: optional message passed to uevent_notify function
 */
static void cm_notify_type_handle(struct charger_manager *cm, enum cm_event_types type, char *msg)
{
	if (cm->shutdown_flag)
		return;

	switch (type) {
	case CM_EVENT_BATT_FULL:
		dev_info(cm->dev, "%s: type CM_EVENT_BATT_FULL\n", __func__);
		break;
	case CM_EVENT_BATT_IN:
	case CM_EVENT_BATT_OUT:
		battout_handler(cm);
		break;
	case CM_EVENT_WL_CHG_START_STOP:
	case CM_EVENT_EXT_PWR_IN_OUT ... CM_EVENT_CHG_START_STOP:
		misc_event_handler(cm, type);
		break;
	case CM_EVENT_FAST_CHARGE:
		fast_charge_handler(cm);
		break;
	case CM_EVENT_INT:
		cm_charger_int_handler(cm);
		break;
	case CM_EVENT_BATT_OVERVOLTAGE:
		break;
	case CM_EVENT_IGNORE_HARD_RESET:
		cm_enable_fixed_fchg_handshake(cm, false);
		break;
	case CM_EVENT_UPDATE_USB_LIMINT:
		dev_err(cm->dev, "%s: update usb limit\n", __func__);
		cm->desc->limit_status |= CM_CHARGE_USB_LIMIT_CMD;
		schedule_work(&cm->limit_current_work);
		break;
	case CM_EVENT_BATT_AGING:
		dev_info(cm->dev, "%s: update cm bat_info by battery aging.\n", __func__);
		cm_batt_aging_algo(cm);
		break;
	case CM_EVENT_UNKNOWN:
	case CM_EVENT_OTHERS:
	default:
		dev_err(cm->dev, "%s: type not specified\n", __func__);
		break;
	}

	power_supply_changed(cm->charger_psy);

}

/**
 * cm_notify_event - charger driver notify Charger Manager of charger event
 * @psy: pointer to instance of charger's power_supply
 * @type: type of charger event
 * @msg: optional message passed to uevent_notify function
 */
void cm_notify_event(struct power_supply *psy, enum cm_event_types type,
		     char *msg)
{
	struct charger_manager *cm;
	bool found_power_supply = false;

	if (psy == NULL)
		return;

	mutex_lock(&cm_list_mtx);
	list_for_each_entry(cm, &cm_list, entry) {
		if (cm->charger_psy->desc) {
			if (strcmp(psy->desc->name, cm->charger_psy->desc->name) == 0) {
				found_power_supply = true;
				break;
			}
		}

		if (cm->desc->psy_charger_stat) {
			if (match_string(cm->desc->psy_charger_stat, -1,
					 psy->desc->name) >= 0) {
				found_power_supply = true;
				break;
			}
		}

		if (cm->desc->psy_fuel_gauge) {
			/*
			 * fgu has only one string and no null pointer at the end,
			 * only needs to compare once before exiting th loop, so 1 here and -1 elsewhere.
			 */
			if (match_string(&cm->desc->psy_fuel_gauge, 1,
					 psy->desc->name) >= 0) {
				found_power_supply = true;
				break;
			}
		}

		if (cm->desc->psy_cp_stat) {
			if (match_string(cm->desc->psy_cp_stat, -1,
					 psy->desc->name) >= 0) {
				found_power_supply = true;
				break;
			}
		}

		if (cm->desc->psy_wl_charger_stat) {
			if (match_string(cm->desc->psy_wl_charger_stat, -1,
					 psy->desc->name) >= 0) {
				found_power_supply = true;
				break;
			}
		}
	}

	mutex_unlock(&cm_list_mtx);

	if (!found_power_supply) {
		if (cm_event_num < CM_EVENT_TYPE_NUM) {
			cm_event_msg[cm_event_num] = msg;
			cm_event_type[cm_event_num++] = type;
		} else {
			pr_err("%s: too many cm_event_num!!\n", __func__);
		}
		return;
	}

	cm_notify_type_handle(cm, type, msg);
}
EXPORT_SYMBOL_GPL(cm_notify_event);

/* mag sensor get changer current -by hulang start */
#define MAG_GET_CUR
#ifdef MAG_GET_CUR
void charger_cur_callback(int *charger_cur, int *status)
{
	int ret, charge_state = -1;
	struct charger_manager *cm;

	*charger_cur = 0;
	*status = 0;

	pr_err("%s:line%d enter\n", __func__, __LINE__);
	if (g_cm) {
		cm = g_cm;

		charge_state = cm->batt_chg_status;
		if (charge_state == POWER_SUPPLY_STATUS_CHARGING)
			*status = 1;
		pr_err("%s:line%d change status = %d\n", __func__, __LINE__, *status);

		ret = get_ibat_avg_uA(cm, charger_cur);
		pr_err("%s:line%d ret = %d, charger_cur = %d\n", __func__, __LINE__, ret, *charger_cur);
		if (ret) {
			dev_err(cm->dev, "get charger_cur error.\n");
		}
	} else {
		pr_err("%s:line%d no cm found!!!\n", __func__, __LINE__);
	}
}
EXPORT_SYMBOL(charger_cur_callback);
#endif
/* mag sensor get changer current -by hulang start */

MODULE_AUTHOR("MyungJoo Ham <myungjoo.ham@samsung.com>");
MODULE_DESCRIPTION("Charger Manager");
MODULE_LICENSE("GPL");
