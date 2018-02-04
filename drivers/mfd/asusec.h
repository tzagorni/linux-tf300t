/*
 * Driver for ASUS Transformer Pad embedded controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASUSEC_H__
#define __ASUSEC_H__

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/power_supply.h>

struct asusec_data {
	struct work_struct input_work;
	struct work_struct dock_detect_work;

	struct i2c_client *client;
	struct i2c_client *dockram_client;
	struct gpio_desc *request_gpio;
	struct gpio_desc *dock_detect_gpio;
	int irq;
	bool has_keyboard;
	bool is_dock;
	bool present;
#ifdef CONFIG_KEYBOARD_ASUSEC
	struct input_dev *indev;
#endif
#ifdef CONFIG_BATTERY_ASUSEC
	struct power_supply_config bat_cfg;
	struct power_supply_config ac_cfg;
	struct power_supply_desc bat_desc;
	struct power_supply_desc ac_desc;

	struct power_supply* bat;
	struct power_supply* ac;

	struct gpio_desc *irq_test_gpio;
#endif
};

#define ASUSEC_BATTERY_INFO_SIZE 32
extern int asusec_read_battery_info(struct asusec_data *ec,
				    unsigned char *data);

#ifdef CONFIG_BATTERY_ASUSEC
extern int asusec_battery_init(struct asusec_data *ec);
extern int asusec_battery_enable(struct asusec_data *ec);
extern void asusec_battery_disable(struct asusec_data *ec);
#else
static inline int asusec_battery_init(struct asusec_data *ec) { return 0; }
static inline int asusec_battery_enable(struct asusec_data *ec) { return 0; }
static inline void asusec_battery_disable(struct asusec_data *ec) { }
#endif /* CONFIG_BATTERY_ASUSEC */

#ifdef CONFIG_KEYBOARD_ASUSEC
extern int asusec_keyboard_init(struct asusec_data *ec);
extern int asusec_keyboard_enable(struct asusec_data *ec);
extern void asusec_keyboard_disable(struct asusec_data *ec);

extern void asusec_keyboard_f_key(struct asusec_data *ec,
				  const unsigned char *data);
extern void asusec_keyboard_key(struct asusec_data *ec,
				const unsigned char *data);
#else
static inline int asusec_keyboard_init(struct asusec_data *ec) { return 0; }
static inline int asusec_keyboard_enable(struct asusec_data *ec) { return 0; }
static inline void asusec_keyboard_disable(struct asusec_data *ec) { }

static inline void asusec_keyboard_f_key(struct asusec_data *ec,
					 const unsigned char *data) { }
static inline void asusec_keyboard_key(struct asusec_data *ec,
				       const unsigned char *data) { }
#endif /* CONFIG_KEYBOARD_ASUSEC */

#endif /* __ASUSEC_H__ */
