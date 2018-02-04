/*
 * Driver for ASUS Transformer Pad embedded controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/power_supply.h>
#include "asusec.h"

#define PROP_STATUS		 1
#define PROP_TEMPERATURE	 7
#define PROP_VOLTAGE		 9
#define PROP_CURRENT		11
#define PROP_CAPACITY		13
#define PROP_REMAINING_CAPACITY	15
#define PROP_AVG_TIME_TO_EMPTY	17
#define PROP_AVG_TIME_TO_FULL	19

#define STATUS_CHARGING		0x40
#define STATUS_FULL_CHARGED	0x20
#define STATUS_FULL_DISCHARGED	0x10

static unsigned short asusec_read_property(struct asusec_data *ec, int prop)
{
	unsigned char data[ASUSEC_BATTERY_INFO_SIZE];
	if (asusec_read_battery_info(ec, data))
	{
		dev_err(&ec->client->dev, "error reading battery info\n");
		return 0;
	}

	return (data[prop + 1] << 8) | data[prop];
}

/*
 * This is what the original ASUS source does, probably to ensure the
 * computer has enough time to shutdown on low battery.
 */
static int asusec_get_corrected_capacity(struct asusec_data *ec)
{
	int cap = asusec_read_property(ec, PROP_CAPACITY);

	if (cap > 100)
		cap = 100;
	if (cap <= 80)
		cap--;
	if (cap <= 70)
		cap--;
	if (cap <= 60)
		cap--;
	if (cap <= 50)
		cap--;
	if (cap <= 30)
		cap--;
	if (cap < 0)
		cap = 0;
	return cap;
}

static int asusec_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct asusec_data *ec = psy->drv_data;
	int prop;

	if (!ec->present) {
		val->intval = 0;
		return 0;
	}

	switch(psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		prop = asusec_read_property(ec, PROP_STATUS);
		if (prop & STATUS_FULL_CHARGED) {
			val->intval = POWER_SUPPLY_STATUS_FULL;
		} else if (prop & STATUS_CHARGING) {
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		} else if (prop & STATUS_FULL_DISCHARGED) {
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = asusec_read_property(ec, PROP_VOLTAGE);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = asusec_get_corrected_capacity(ec);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = asusec_read_property(ec, PROP_TEMPERATURE);
		val->intval -= 2731;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int asusec_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp,	union power_supply_propval *val)
{
	struct asusec_data *ec = psy->drv_data;
	union power_supply_propval bat_val;

	if (!ec->present) {
		val->intval = 0;
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		asusec_battery_get_property(ec->bat, POWER_SUPPLY_PROP_STATUS,
			&bat_val);

		if (bat_val.intval == POWER_SUPPLY_STATUS_CHARGING
		    || bat_val.intval == POWER_SUPPLY_STATUS_FULL) {
			val->intval = 1;
		} else {
			val->intval = 0;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}


int asusec_battery_init(struct asusec_data *ec)
{
	static enum power_supply_property bat_props[] = {
		POWER_SUPPLY_PROP_PRESENT,
		POWER_SUPPLY_PROP_STATUS,
		POWER_SUPPLY_PROP_TECHNOLOGY,
		POWER_SUPPLY_PROP_VOLTAGE_NOW,
		POWER_SUPPLY_PROP_CAPACITY,
		POWER_SUPPLY_PROP_TEMP,
	};
	static enum power_supply_property ac_props[] = {
		POWER_SUPPLY_PROP_PRESENT,
		POWER_SUPPLY_PROP_ONLINE,
	};
	static char *ac_supplicants[] = { "Battery" };
	static char *dock_ac_supplicants[] = { "DockBattery" };

	ec->bat_cfg.drv_data = ec;
	ec->ac_cfg.drv_data = ec;

	ec->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	ec->bat_desc.properties = bat_props;
	ec->bat_desc.num_properties = ARRAY_SIZE(bat_props);
	ec->bat_desc.get_property = asusec_battery_get_property;

	ec->ac_desc.type = POWER_SUPPLY_TYPE_MAINS;
	ec->ac_desc.properties = ac_props;
	ec->ac_desc.num_properties = ARRAY_SIZE(ac_props);
	ec->ac_desc.get_property = asusec_ac_get_property;

	if (ec->is_dock) {
		ec->bat_desc.name = "DockBattery";
		ec->ac_desc.name = "DockAC";
		ec->ac_cfg.num_supplicants = ARRAY_SIZE(dock_ac_supplicants);
		ec->ac_cfg.supplied_to = dock_ac_supplicants;
	} else {
		ec->bat_desc.name = "Battery";
		ec->ac_desc.name = "AC";
		ec->ac_cfg.num_supplicants = ARRAY_SIZE(ac_supplicants);
		ec->ac_cfg.supplied_to = ac_supplicants;
	}

	ec->bat = devm_power_supply_register_no_ws(&ec->client->dev,
		&ec->bat_desc, &ec->bat_cfg);
	ec->ac = devm_power_supply_register_no_ws(&ec->client->dev,
		&ec->ac_desc, &ec->ac_cfg);

	if (IS_ERR_OR_NULL(ec->bat)) {
		return PTR_ERR(ec->bat);
	} else if (IS_ERR_OR_NULL(ec->ac)) {
		return PTR_ERR(ec->ac);
	} else {
		return 0;
	}
}

int asusec_battery_enable(struct asusec_data *ec)
{
	return 0;
}

void asusec_battery_disable(struct asusec_data *ec)
{
}
