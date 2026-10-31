// SPDX-License-Identifier: GPL-2.0：
// Copyright (C) 2019 Spreadtrum Communications Inc.

#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include <linux/power/charger-manager.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/pwm.h>
#include <linux/seq_file.h>

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
#include <sqc_common.h>
#include <vendor/common/zte_misc.h>

int sqc_notify_daemon_changed(int chg_id, int msg_type, int msg_val);
static bool jeita_lev4_discharge = 0;
#endif
static int pc_charge_type = 0;

#define MAX_CURRENT		2000000
#define PWM_SCALE		100
#define SC2721_BATTERY_NAME		"sc27xx-fgu"

struct sprd_pwm_chg_info {
	struct device *dev;
	struct pwm_device *pwm;
	struct usb_phy *usb_phy;
	struct notifier_block usb_notify;
	struct mutex lock;
	struct power_supply *psy_usb;
	bool charging;
	bool is_charger_online;
	u32 scale;
	u32 max_current;
	u32 limit;
	int icl_ma;
	int fcc_ma;
	int fcv_mv;
	bool enable_powerptah;
	bool fcc_change;
};

#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
static int sqc_chg_type = SQC_NONE_TYPE;
extern int zte_sqc_set_prop_by_name(const char *name, enum zte_power_supply_property psp, int data);
#endif

static int sprd_pwm_chg_set_duty(struct sprd_pwm_chg_info *info, u64 duty_cycle)
{
	struct pwm_state state;

	pwm_get_state(info->pwm, &state);

	if (duty_cycle >= 0) {
		duty_cycle *= state.period;
		do_div(duty_cycle, info->scale);
		state.duty_cycle = duty_cycle;
		state.enabled = true;
	} else {
		state.duty_cycle = 0;
		state.enabled = false;
	}
	dev_info(info->dev, "duty_cycle = %d, state: duty(period) = %d(%d)\n", duty_cycle, state.duty_cycle, state.period);
	pwm_apply_state(info->pwm, &state);

	return 0;
}

static int sprd_pwm_chg_get_current(struct sprd_pwm_chg_info *info, u32* curr_mA)
{
	*curr_mA = info->fcc_ma;

	return 0;
}

static int sprd_pwm_chg_set_current(struct sprd_pwm_chg_info *info, u32 curr_mA)
{
	u64 duty_cycle = 0;
	u32 curr_uA = 0;

	curr_uA = curr_mA * 1000;

	if (curr_uA > info->max_current)
		curr_uA = info->max_current;

	if (curr_uA > 2000000) {
		duty_cycle = 0;
	} else if (curr_uA > 1900000 && curr_uA <= 2000000) {
		duty_cycle = 3;
	} else if (curr_uA > 1800000 && curr_uA <= 1900000) {
		duty_cycle = 10;
	} else if (curr_uA > 1700000 && curr_uA <= 1800000) {
		duty_cycle = 14;
	} else if (curr_uA > 1600000 && curr_uA <= 1700000) {
		duty_cycle = 17;
	} else if (curr_uA > 1500000 && curr_uA <= 1600000) {
		duty_cycle = 20;
	} else if (curr_uA > 1400000 && curr_uA <= 1500000) {
		duty_cycle = 25;
	} else if (curr_uA > 1300000 && curr_uA <= 1400000) {
		duty_cycle = 28;
	} else if (curr_uA > 1200000 && curr_uA <= 1300000) {
		duty_cycle = 32;
	} else if (curr_uA > 1100000 && curr_uA <= 1200000) {
		duty_cycle = 36;
	} else if (curr_uA > 1000000 && curr_uA <= 1100000) {
		duty_cycle = 39;
	} else if (curr_uA > 900000 && curr_uA <= 1000000) {
		duty_cycle = 42;
	} else if (curr_uA > 800000 && curr_uA <= 900000) {
		duty_cycle = 46;
	} else if (curr_uA > 700000 && curr_uA <= 800000) {
		duty_cycle = 49;
	} else if (curr_uA > 600000 && curr_uA <= 700000) {
		duty_cycle = 53;
	} else if (curr_uA > 500000 && curr_uA <= 600000) {
		duty_cycle = 56;
	} else if (curr_uA > 400000 && curr_uA <= 500000) {
		duty_cycle = 65;
	} else if (curr_uA > 300000 && curr_uA <= 400000) {
		duty_cycle = 68;
	} else if (curr_uA > 200000 && curr_uA <= 300000) {
		duty_cycle = 72;
	} else {
		duty_cycle = 100;
	}

	sprd_pwm_chg_set_duty(info, duty_cycle);
	dev_info(info->dev, "set curr_mA = %d, duty = %d(%d)\n", curr_mA, duty_cycle, info->scale);

	return 0;
}

static int sprd_pwm_chg_start_charge(struct sprd_pwm_chg_info *info)
{
	int ret = 0;

	dev_info(info->dev, "%s\n", __func__);
	ret = sprd_pwm_chg_set_current(info, info->fcc_ma);

	return ret;
}

static int sprd_pwm_chg_stop_charge(struct sprd_pwm_chg_info *info)
{
	int ret;

	dev_info(info->dev, "%s\n", __func__);
	ret = sprd_pwm_chg_set_current(info, 0);
	if (ret)
		dev_err(info->dev, "failed to stop charge\n");

	return ret;
}

static int sprd_pwm_chg_get_health(struct sprd_pwm_chg_info *info,
				     u32 *health)
{
	*health = POWER_SUPPLY_HEALTH_GOOD;

	return 0;
}

static int sprd_pwm_chg_get_status(struct sprd_pwm_chg_info *info)
{
	if (info->charging == true)
		return POWER_SUPPLY_STATUS_CHARGING;
	else
		return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int sprd_pwm_chg_set_status(struct sprd_pwm_chg_info *info,
				       int val)
{
	int ret = 0;

	if (!val && info->charging) {
		sprd_pwm_chg_stop_charge(info);
		info->charging = false;
	} else if (val && !info->charging) {
		ret = sprd_pwm_chg_start_charge(info);
		if (ret)
			dev_err(info->dev, "start charge failed\n");
		else
			info->charging = true;
	}

	return ret;
}

static int pwm_charger_usb_change(struct notifier_block *nb,
				     unsigned long limit, void *data)
{
	struct sprd_pwm_chg_info *info =
		container_of(nb, struct sprd_pwm_chg_info, usb_notify);

	info->limit = limit;

	if (info->usb_phy->chg_type == SDP_TYPE || info->usb_phy->chg_type == CDP_TYPE)
		pc_charge_type = 1;
	else
		pc_charge_type = 0;

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	jeita_lev4_discharge = 0;
#endif

	return NOTIFY_OK;
}

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
extern struct sqc_bc1d2_proto_ops sqc_sfcp_ops_node;

static int sfcp_status_init(void)
{
	struct sprd_pwm_chg_info *info =
		(struct sprd_pwm_chg_info *)sqc_sfcp_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	return SQC_ADAPTER_OK;
}

static int sfcp_status_end(void)
{
	struct sprd_pwm_chg_info *info =
		(struct sprd_pwm_chg_info *)sqc_sfcp_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: info is null\n", __func__);
		return SQC_ADAPTER_ERROR;
	}

	pr_err("[SQC-HW]: %s\n", __func__);

	return SQC_ADAPTER_OK;
}

static int sfcp_get_charger_type(int *chg_type)
{
	struct sprd_pwm_chg_info *info =
		(struct sprd_pwm_chg_info *)sqc_sfcp_ops_node.arg;

	if (!info) {
		pr_err("[SQC-HW]: [%s] info is null\n", __func__);
		*chg_type = SQC_NONE_TYPE;
		return 0;
	}

	if (!info->limit && !info->usb_phy->chg_type) {
		if (info->usb_phy->chg_state == USB_CHARGER_PRESENT) {
			*chg_type = SQC_FLOAT_TYPE;
		} else {
			*chg_type = SQC_NONE_TYPE;
			goto out_loop;
		}
	}

	if (info->usb_phy->chg_type == DCP_TYPE) {
		*chg_type = SQC_DCP_TYPE;
	} else if (info->usb_phy->chg_type == SDP_TYPE) {
		*chg_type = SQC_SDP_TYPE;
	} else if (info->usb_phy->chg_type == CDP_TYPE) {
		*chg_type = SQC_CDP_TYPE;
	} else if (info->usb_phy->chg_type == ACA_TYPE) {
		*chg_type = SQC_FLOAT_TYPE;
	} else {
		*chg_type = SQC_FLOAT_TYPE;
	}

	if ((sqc_chg_type == SQC_SLEEP_MODE_TYPE)
			&& (*chg_type == SQC_NONE_TYPE)) {
		sqc_chg_type = *chg_type;
		zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 1);
	} else if (sqc_chg_type == SQC_SLEEP_MODE_TYPE) {
		*chg_type = sqc_chg_type;
	}

out_loop:
	pr_info("[SQC-HW]: [%s] limit: %d, sprd_type: %d, chg_type: %d\n",
		__func__, info->limit, info->usb_phy->chg_type, *chg_type);

	return 0;
}


struct sqc_bc1d2_proto_ops sqc_sfcp_ops_node = {
	.status_init = sfcp_status_init,
	.status_remove = sfcp_status_end,
	.get_charger_type = sfcp_get_charger_type,
};

static int sqc_mp_get_chg_type(void *arg, unsigned int *chg_type)
{
	arg = arg ? arg : NULL;

	*chg_type = SQC_PMIC_TYPE_BUCK;

	return 0;
}

static int sqc_mp_get_vbat(void *arg, u32 *mV)
{
	union power_supply_propval batt_vol_uv;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(SC2721_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, &batt_vol_uv);

	power_supply_put(fuel_gauge);
	if (ret1) {
		pr_err("%s: get POWER_SUPPLY_PROP_VOLTAGE_NOW failed!\n", __func__);
		return ret1;
	}

	*mV = batt_vol_uv.intval / 1000;

	return 0;
}

static int sqc_mp_set_enbale_chging(void *arg, unsigned int en)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;
	u32 vbat_now = 0;

	if (en && info->enable_powerptah) {
		pr_info("%s powerptah enabled, force disable chging\n", __func__);
		en = false;
	}

	if (info->fcv_mv == 4100) {
		sqc_mp_get_vbat(arg, &vbat_now);
		pr_info("%s vbat_now=%d info->fcv_mv=%d jeita_lev4_discharge=%d\n", __func__, vbat_now, info->fcv_mv, jeita_lev4_discharge);
		if (!jeita_lev4_discharge) {
			if (vbat_now > 4100) {
				en = false;
				jeita_lev4_discharge = 1;
			}
		} else {
			en = false;
			if (vbat_now < 3850) {
				jeita_lev4_discharge = 0;
				en = true;
			}
		}
	} else {
		jeita_lev4_discharge = 0;
	}

	if (en) {
		sprd_pwm_chg_start_charge(info);
		pm_wakeup_event(info->dev, MSEC_PER_SEC * 7);
		info->charging = true;
	} else {
		sprd_pwm_chg_stop_charge(info);
		info->charging = false;
	}

	pr_info("%s %d\n", __func__, en);

	return 0;
}

static int sqc_mp_get_enbale_chging(void *arg, unsigned int *en)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;

	*en = info->charging;

	pr_info("%s %d\n", __func__, *en);

	return 0;
}

static int sqc_mp_get_aicr(void *arg, u32 *aicr_ma)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;

	*aicr_ma = info->icl_ma;

	pr_info("%s %d\n", __func__, *aicr_ma);

	return 0;
}

static int sqc_mp_set_aicr(void *arg, u32 aicr_ma)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;

	info->icl_ma = aicr_ma;

	pr_info("%s %d\n", __func__, aicr_ma);

	return 0;
}

static int sqc_mp_get_ichg(void *arg, u32 *ichg_ma)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;
	int ret = 0;

	*ichg_ma = info->fcc_ma;

	pr_info("%s %d\n", __func__, *ichg_ma);

	return ret;
}

static int sqc_mp_set_ichg(void *arg, u32 mA)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;
	int ret = 0;

	info->fcc_ma = mA;
	if (pc_charge_type) {
		pr_info("chg_type is SDP or CDP, set fcc 500mA\n");
		info->fcc_ma = 500;
	}


	pr_info("%s %d\n", __func__, mA);

	return ret;
}

static int sqc_mp_get_cv(void *arg, u32 *cv_mv)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;
	int ret = 0;

	*cv_mv = info->fcv_mv;

	pr_info("%s %d\n", __func__, *cv_mv);

	return ret;
}

static int sqc_mp_set_cv(void *arg, u32 cv_mv)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;
	int ret = 0;

	info->fcv_mv = cv_mv;

	pr_info("%s %d\n", __func__, cv_mv);

	return ret;
}

static int sqc_mp_get_ibat(void *arg, u32 *mA)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(SC2721_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CURRENT_NOW, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		pr_err("%s: get POWER_SUPPLY_PROP_CURRENT_NOW failed!\n", __func__);
		return ret1;
	}

	*mA = val.intval / 1000;

	return 0;
}

static int sqc_mp_get_vbus(void *arg, u32 *mV)
{
	union power_supply_propval val;
	struct power_supply *fuel_gauge = NULL;
	int ret1 = 0;

	fuel_gauge = power_supply_get_by_name(SC2721_BATTERY_NAME);
	if (!fuel_gauge)
		return -ENODEV;

	ret1 = power_supply_get_property(fuel_gauge,
				POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);

	power_supply_put(fuel_gauge);
	if (ret1) {
		pr_err("%s: get POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE failed!\n", __func__);
		return ret1;
	}

	*mV = val.intval / 1000;

	return 0;
}

static int sqc_mp_get_ibus(void *arg, u32 *mA)
{
	arg = arg ? arg : NULL;

	*mA = 0;

	return 0;
}

static int sqc_enable_powerpath_set(void *arg, unsigned int enabled)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;

	info->enable_powerptah = !!enabled;

	return 0;
}

static int sqc_enable_powerpath_get(void *arg, unsigned int *enabled)
{
	struct sprd_pwm_chg_info *info = (struct sprd_pwm_chg_info *)arg;

	*enabled = info->enable_powerptah;

	return 0;
}

static struct sqc_pmic_chg_ops pwm_sqc_chg_ops = {

	.init_pmic_charger = NULL,

	.get_chg_type = sqc_mp_get_chg_type,

	.chg_enable = sqc_mp_set_enbale_chging,
	.chg_enable_get = sqc_mp_get_enbale_chging,

	.set_chging_icl = sqc_mp_set_aicr,
	.get_chging_icl = sqc_mp_get_aicr,

	.set_chging_fcv = sqc_mp_set_cv,
	.get_chging_fcv = sqc_mp_get_cv,
	.set_chging_fcc = sqc_mp_set_ichg,
	.get_chging_fcc = sqc_mp_get_ichg,

	.batt_ibat_get = sqc_mp_get_ibat,
	.batt_vbat_get = sqc_mp_get_vbat,

	.usb_ibus_get = sqc_mp_get_ibus,
	.usb_vbus_get = sqc_mp_get_vbus,

	.enable_path_set = sqc_enable_powerpath_set,
	.enable_path_get = sqc_enable_powerpath_get,
};
#endif


#if IS_ENABLED(CONFIG_ZTE_POWER_SUPPLY)
int sqc_sleep_node_set(const char *val, const void *arg)
{
	int sleep_mode_enable = 0;

	if (sscanf(val, "%d", &sleep_mode_enable) != 1) {
		pr_err("%s sscanf failed\n", __func__);
		return 0;
	}

	pr_info("%s, line:%d sleep_mode_enable = %d\n", __func__, __LINE__, sleep_mode_enable);

	if (sleep_mode_enable) {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			pr_info("%s, line:%d sleep on status!\n", __func__, __LINE__);

			/*disabel sqc-daemon*/
			sqc_chg_type = SQC_SLEEP_MODE_TYPE;
			sqc_notify_daemon_changed(SQC_NOTIFY_USB, SQC_NOTIFY_USB_STATUS_CHANGED, 1);
#ifdef CONFIG_VENDOR_SQC_BQ2560X
			pr_info("%s, line:%d disable bq2660x watchdog!\n", __func__, __LINE__);
			zte_sqc_set_prop_by_name("zte_bq2560x_charger", POWER_SUPPLY_PROP_FEED_WATCHDOG, 0);
#endif
			/*mtk enter sleep mode*/
			zte_sqc_set_prop_by_name("zte_battery", POWER_SUPPLY_PROP_BATTERY_CHARGING_ENABLED, 0);
		}
	} else {
		if (sqc_chg_type != SQC_SLEEP_MODE_TYPE) {
			pr_info("%s, line:%d sleep off status!\n", __func__, __LINE__);
			sqc_chg_type = SQC_NONE_TYPE;
		}
	}

	return 0;
}

int sqc_sleep_node_get(char *val, const void *arg)
{
	int sleep_mode = 0;

	if (sqc_chg_type == SQC_SLEEP_MODE_TYPE)
		sleep_mode = 1;

	return snprintf(val, PAGE_SIZE, "%u", sleep_mode);
}

static struct zte_misc_ops qc3dp_sleep_mode_node = {
	.node_name = "qc3dp_sleep_mode",
	.set = sqc_sleep_node_set,
	.get = sqc_sleep_node_get,
	.free = NULL,
	.arg = NULL,
};
#endif

static int sprd_pwm_chg_usb_get_property(struct power_supply *psy,
					   enum power_supply_property psp,
					   union power_supply_propval *val)
{
	struct sprd_pwm_chg_info *info = power_supply_get_drvdata(psy);
	u32 cur, health;
	int ret = 0;

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
			val->intval = sprd_pwm_chg_get_status(info);
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = sprd_pwm_chg_get_current(info, &cur);
			if (ret)
				goto out;

			val->intval = cur * 1000;
		}
		break;

	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (!info->charging) {
			val->intval = 0;
		} else {
			ret = sprd_pwm_chg_get_current(info, &cur);
			if (ret)
				goto out;
			val->intval = cur;
		}
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		if (info->charging) {
			val->intval = 0;
		} else {
			ret = sprd_pwm_chg_get_health(info, &health);
			if (ret)
				goto out;

			val->intval = health;
		}
		break;

	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&info->lock);
	return ret;
}

static int
sprd_pwm_chg_usb_set_property(struct power_supply *psy,
				enum power_supply_property psp,
				const union power_supply_propval *val)
{
	struct sprd_pwm_chg_info *info = power_supply_get_drvdata(psy);
	int ret = 0;

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	return 0;
#endif

	mutex_lock(&info->lock);

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = sprd_pwm_chg_set_current(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set charge current failed\n");
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = sprd_pwm_chg_set_status(info, val->intval);
		if (ret < 0)
			dev_err(info->dev, "set charge status failed\n");
		break;

	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = 0;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		info->is_charger_online = val->intval;
		break;

	default:
		ret = -EINVAL;
	}

	mutex_unlock(&info->lock);
	return ret;
}

static int sprd_pwm_chg_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	int ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_STATUS:
		ret = 1;
		break;

	default:
		ret = 0;
	}

	return ret;
}


static enum power_supply_property sc2721_usb_props[] = {
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc sprd_pwm_chg_desc = {
	.name			= "sprd_pwm_chg",
	.type			= POWER_SUPPLY_TYPE_UNKNOWN,
	.properties		= sc2721_usb_props,
	.num_properties		= ARRAY_SIZE(sc2721_usb_props),
	.get_property		= sprd_pwm_chg_usb_get_property,
	.set_property		= sprd_pwm_chg_usb_set_property,
	.property_is_writeable	= sprd_pwm_chg_property_is_writeable,
};

static void pwm_charger_detect_status(struct sprd_pwm_chg_info *info)
{
	unsigned int min, max;

	/*
	 * If the USB charger status has been USB_CHARGER_PRESENT before
	 * registering the notifier, we should start to charge with getting
	 * the charge current.
	 */
	pr_info("pwm: charger_detect_status %d", info->usb_phy->chg_state);
	if (info->usb_phy->chg_state != USB_CHARGER_PRESENT) {
		pc_charge_type = 0;
		return;
	}

	if (info->usb_phy->chg_type == SDP_TYPE || info->usb_phy->chg_type == CDP_TYPE)
		pc_charge_type = 1;
	else
		pc_charge_type = 0;

	usb_phy_get_charger_current(info->usb_phy, &min, &max);
	info->limit = min;
	pr_info("pwm: limit = %d pc_charge_type = %d\n", min, pc_charge_type);
}

static int sprd_pwm_chg_parse_dt(struct device *dev, struct sprd_pwm_chg_info *info)
{
	struct device_node *node = dev->of_node;
	u32 scale = 0;
	u32 max_current_mA = 0;
	int ret;

	if (!node)
		return -ENODEV;

	ret = of_property_read_u32(node, "sprd,pwm-chg-scale", &scale);
	if (!ret)
		info->scale = scale;
	else
		info->scale = PWM_SCALE;
	
	ret = of_property_read_u32(node, "sprd,pwm-chg-max-current", &max_current_mA);
	if (!ret)
		info->max_current = max_current_mA;
	else
		info->max_current = MAX_CURRENT;

	return 0;
}

static int pwm_chip_name_show(struct seq_file *m, void *v)
{
	struct sprd_pwm_chg_info *pinfo = m->private;

	if (pinfo) {
		seq_printf(m, "ETA6295\n");
	} else {
		seq_printf(m, "unknown");
	}

	return 0;
}

static int pwm_chip_name_open(struct inode *inode, struct file *file)
{
	return single_open(file, pwm_chip_name_show, PDE_DATA(inode));
}

static const struct proc_ops pwm_chip_name_node = {
	.proc_open = pwm_chip_name_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};

static int sprd_pwm_chg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct sprd_pwm_chg_info *info;
	struct power_supply_config charger_cfg = { };
	struct pwm_state state;
#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	struct sqc_pmic_chg_ops *sqc_ops = NULL;
#endif
	int ret;

	pr_info("%s enter\n", __func__);

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	mutex_init(&info->lock);
	info->dev = &pdev->dev;

	info->usb_phy = devm_usb_get_phy_by_phandle(&pdev->dev, "phys", 0);
	if (IS_ERR(info->usb_phy)) {
		dev_err(&pdev->dev, "failed to find USB phy\n");
		return PTR_ERR(info->usb_phy);
	}

	ret = sprd_pwm_chg_parse_dt(&pdev->dev, info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to parse sprd backlight\n");
		return ret;
	}

	info->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(info->pwm)) {
		ret = PTR_ERR(info->pwm);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "unable to request PWM\n");
		return ret;
	}
	pwm_init_state(info->pwm, &state);

	ret = pwm_apply_state(info->pwm, &state);
	if (ret) {
		dev_err(&pdev->dev, "failed to apply initial PWM state: %d\n",
			ret);
		return ret;
	}

	platform_set_drvdata(pdev, info);

	charger_cfg.drv_data = info;
	charger_cfg.of_node = np;
	info->psy_usb = devm_power_supply_register(&pdev->dev,
						   &sprd_pwm_chg_desc,
						   &charger_cfg);
	if (IS_ERR(info->psy_usb)) {
		dev_err(&pdev->dev, "failed to register power supply\n");
		return PTR_ERR(info->psy_usb);
	}

	//sprd_pwm_chg_stop_charge(info);
	proc_create_data("driver/chg_name", 0644, NULL, &pwm_chip_name_node, info);
	device_init_wakeup(info->dev, true);

	info->usb_notify.notifier_call = pwm_charger_usb_change;
	ret = usb_register_notifier(info->usb_phy, &info->usb_notify);
	if (ret) {
		dev_err(&pdev->dev, "failed to register notifier:%d\n", ret);
		return ret;
	}

	pwm_charger_detect_status(info);

#if IS_ENABLED(CONFIG_VENDOR_SQC_CHARGER_SPRD)
	sqc_ops = kzalloc(sizeof(struct sqc_pmic_chg_ops), GFP_KERNEL);
	memcpy(sqc_ops, &pwm_sqc_chg_ops, sizeof(struct sqc_pmic_chg_ops));
	sqc_ops->arg = (void *)info;
	ret = sqc_hal_charger_register(sqc_ops, SQC_CHARGER_PRIMARY);
	if (ret < 0) {
		pr_err("%s register sqc hal fail(%d)\n", __func__, ret);
	}

	sqc_sfcp_ops_node.arg = (void *)info;
	dev_err(&pdev->dev, "regist bc1d2\n");
	sqc_hal_bc1d2_register(&sqc_sfcp_ops_node);
	zte_misc_register_callback(&qc3dp_sleep_mode_node, info);
#endif
	pr_info("%s success\n", __func__);

	return 0;
}

static int sprd_pwm_chg_remove(struct platform_device *pdev)
{
	struct sprd_pwm_chg_info *info = platform_get_drvdata(pdev);

	if (!info) {
		pr_err("%s:line%d: NULL pointer!!!\n", __func__, __LINE__);
		return -EINVAL;
	}

	usb_unregister_notifier(info->usb_phy, &info->usb_notify);
	device_init_wakeup(info->dev, false);
	mutex_destroy(&info->lock);

	return 0;
}

static const struct of_device_id sprd_pwm_chg_of_match[] = {
	{ .compatible = "sprd,sprd-pwm-charger", },
	{ }
};

static struct platform_driver sprd_pwm_chg_driver = {
	.driver = {
		.name = "sprd-pwm-charger",
		.of_match_table = sprd_pwm_chg_of_match,
	},
	.probe = sprd_pwm_chg_probe,
	.remove = sprd_pwm_chg_remove,
};

module_platform_driver(sprd_pwm_chg_driver);

MODULE_DESCRIPTION("Spreadtrum PWM Charger Driver");
MODULE_LICENSE("GPL v2");
