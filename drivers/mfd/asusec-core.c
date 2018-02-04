/*
 * Driver for ASUS Transformer Pad embedded controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/gpio.h>
#include "asusec.h"

#define ASUSEC_IRQ_OBF		0x01
#define ASUSEC_IRQ_KEY		0x04
#define ASUSEC_IRQ_KBC		0x08
#define ASUSEC_IRQ_AUX		0x20
#define ASUSEC_IRQ_SCI		0x40
#define ASUSEC_IRQ_SMI		0x80

#define ASUSEC_SMI_HANDSHAKING	0x50
#define ASUSEC_SMI_RESET	0x5f

static void asusec_check_dock(struct asusec_data *ec);
static int asusec_chip_init(struct asusec_data *ec, bool send_request);


static void asusec_request_ec(struct asusec_data *ec)
{
	gpiod_set_value(ec->request_gpio, 1);
	msleep(50);
	gpiod_set_value(ec->request_gpio, 0);
	msleep(100);

}

char *asusec_error_string = "";
int print_counter = 0;

static irqreturn_t asusec_irq(int irq, void *_dev)
{
	struct asusec_data *ec = _dev;
	int ret;
	u8 data[8];

	if (!ec->present)
	{
		//dev_info(&ec->client->dev, "IRQ happened but EC not present\n");
		msleep(25);
		return IRQ_HANDLED;
	}

	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	ret = i2c_smbus_read_i2c_block_data(ec->client, 0x6A, 8, &data[0]);

	//dev_info(&client->dev, "read i2c\n");
	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	if (ret < 0)
	{
		dev_err(&ec->client->dev, "unable to read from i2c\n");
		//asusec_check_dock(ec);
		return IRQ_HANDLED;
	}

	if (data[1] & ASUSEC_IRQ_OBF) {
		if (data[1] & ASUSEC_IRQ_SMI) {
			// SMI
			dev_info(&ec->client->dev, "SMI\n");
			switch (data[2]) {
			case ASUSEC_SMI_HANDSHAKING:
				dev_info(&ec->client->dev, "HANDSHAKING\n");
				asusec_chip_init(ec, false);
				break;
			case ASUSEC_SMI_RESET:
				dev_info(&ec->client->dev, "RESET\n");
				if (ec->is_dock)
					asusec_check_dock(ec);
				else
					asusec_chip_init(ec, true);
				break;
			default:
				dev_info(&ec->client->dev,
					 "unknown SMI, doing nothing\n");
			}
		} else if (data[1] & ASUSEC_IRQ_AUX) {
			// Touchpad
			dev_info(&ec->client->dev, "AUX not implemented\n");
		} else if (data[1] & ASUSEC_IRQ_KBC) {
			// ACK for LED Control?
			dev_info(&ec->client->dev, "KBC not implemented\n");
		} else if (data[1] & ASUSEC_IRQ_SCI) {
			asusec_keyboard_f_key(ec, data);
		} else if (data[1] & ASUSEC_IRQ_KEY) {
			//dev_info(&ec->client->dev, "Keyboard interrupt\n");
			if (ec->has_keyboard)
				asusec_keyboard_key(ec, data);
		}
	} else {
		msleep(25);
		//dev_info(&ec->client->dev, "Unknown IRQ\n");
		//dev_err(&ec->client->dev, "Unknown IRQ, requesting EC\n");
		//asusec_chip_init(ec, true);
	}
	return IRQ_HANDLED;
}

static irqreturn_t asusec_dock_detect_irq(int irq, void *_dev)
{
	struct asusec_data *ec = _dev;
	dev_info(&ec->client->dev, "Dock detect\n");
	asusec_check_dock(ec);
	return IRQ_HANDLED;
}


int asusec_read_battery_info(struct asusec_data *ec,
			     unsigned char *data)
{
	int ret, cap;
	ret = i2c_smbus_read_i2c_block_data(ec->dockram_client, 0x14, 32, data);
	if (ret < 0) {
		dev_err(&ec->client->dev, "error reading battery capacity\n");
		return ret;
	}
	cap = (data[14] << 8) | data[13];
	//dev_info(&ec->client->dev, "battery capacity: %u\n", cap);

	return 0;
}


static void enter_normal_mode(struct asusec_data *ec)
{
	int ret;
	unsigned char data[32];

	dev_info(&ec->client->dev, "enter_normal_mode\n");
	//dev_info(&ec->client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	ret = i2c_smbus_read_i2c_block_data(ec->dockram_client, 0x0A, 32, data);
	if (ret < 0) {
		dev_err(&ec->client->dev,
			"enter_normal_mode: cannot read i2c\n");
		return;
	}

	//dev_info(&ec->client->dev, "read i2c\n");
	//dev_info(&ec->client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	data[0] = 8;
	data[5] &= 0xBF;

	ret = i2c_smbus_write_i2c_block_data(ec->dockram_client, 0x0A, 9, data);
	if (ret < 0) {
		dev_err(&ec->client->dev,
			"enter_normal_mode: cannot write i2c\n");
		return;
	}
	//dev_info(&ec->client->dev, "wrote i2c\n");
	//dev_info(&ec->client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));
}


static void asusec_clear_i2c_buffer(struct asusec_data *ec)
{
	u8 data[8];
	int i;

	for (i = 0; i < 8; i++) {
		if (i2c_smbus_read_i2c_block_data(ec->client, 0x6A, 8, data) < 0) {
			dev_err(&ec->client->dev, "error reading data\n");
			//return ret_val;
		}
	}
}


static int asusec_get_response(struct asusec_data *ec, int irq_mask, int response)
{
	u8 data[8];
	int retry;

	for (retry = 0; retry < 3; retry++) {
		i2c_smbus_read_i2c_block_data(ec->client, 0x6A, 8, data);
		if ((data[1] & ASUSEC_IRQ_OBF) && (data[1] & irq_mask)
					&& (data[2] == response)) {
			return 0;
		}

		msleep(10);
	}

	dev_err(&ec->client->dev, "failed to get touchpad/keyboard response");
	return -EIO;
}


static int asusec_acked_command(struct asusec_data *ec, int command,
				int irq_mask, int num_retries, int sleep_before)
{
	int retry;

	for (retry = 0; retry < num_retries; retry++) {
		i2c_smbus_write_word_data(ec->client, 0x64, command);
		msleep(sleep_before);
		if (asusec_get_response(ec, irq_mask, 0xFA) == 0) { // PS2 ACK
			return 0;
		}
	}

	dev_err(&ec->client->dev, "failed to disable touchpad\n");
	return -EIO;
}


static int asusec_touchpad_hw_disable(struct asusec_data *ec)
{
	return asusec_acked_command(ec, 0xF5D4, ASUSEC_IRQ_AUX, 5, 500);
}


static int asusec_keypad_hw_disable(struct asusec_data *ec)
{
	return asusec_acked_command(ec, 0xF500, ASUSEC_IRQ_KBC, 3, 0);
}


int asusec_keypad_hw_enable(struct asusec_data *ec)
{
	return asusec_acked_command(ec, 0xF400, ASUSEC_IRQ_KBC, 3, 0);
}


static int asusec_chip_init(struct asusec_data *ec, bool send_request)
{
	struct i2c_client *client = ec->client;
	int i;
	int ret_val = 0;
	char dockram_data[32];

	// TODO: maybe IRQ should be disabled

	if (send_request) {
		dev_info(&ec->client->dev, "asusec_chip_init(true)\n");
		//dev_info(&ec->client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));
		asusec_request_ec(ec);
		//msleep(200);
	} else {

		dev_info(&ec->client->dev, "asusec_chip_init(false)\n");
		//dev_info(&ec->client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	//disable_irq_nosync(ec->irq);
	//dev_info(&ec->client->dev, "disabled IRQ\n");


	for (i = 0; i < 10; i++) {
		ret_val = i2c_smbus_write_word_data(client, 0x64, 0);
		if (ret_val < 0)
			msleep(300);
		else
			break;
	}


	if (ret_val < 0) {
		dev_err(&client->dev, "error accessing ec\n");
		return ret_val;
	}

	asusec_clear_i2c_buffer(ec);

	//dev_info(&ec->client->dev, "read i2c\n");
	//dev_info(&ec->client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));
	i2c_smbus_read_i2c_block_data(ec->dockram_client, 0x01, 32, dockram_data);
	dev_info(&ec->client->dev, "model name: %s\n", dockram_data);

	i2c_smbus_read_i2c_block_data(ec->dockram_client, 0x02, 32, dockram_data);
	dev_info(&ec->client->dev, "ec firmware version: %s\n", dockram_data);

	i2c_smbus_read_i2c_block_data(ec->dockram_client, 0x03, 32, dockram_data);
	dev_info(&ec->client->dev, "ec config format: %s\n", dockram_data);

	i2c_smbus_read_i2c_block_data(ec->dockram_client, 0x04, 32, dockram_data);
	dev_info(&ec->client->dev, "pid/pcba version: %s\n", dockram_data);

	if (ec->has_keyboard) {
		i2c_smbus_read_i2c_block_data(ec->dockram_client, 0x0A, 32, dockram_data);
		dev_info(&ec->client->dev, "dock device\n");

		// if 201
		msleep(750);
		asusec_clear_i2c_buffer(ec);
		asusec_touchpad_hw_disable(ec);
		asusec_keypad_hw_disable(ec);

		// 1
		asusec_clear_i2c_buffer(ec);

		//..... TODO


		asusec_keypad_hw_enable(ec);
		asusec_clear_i2c_buffer(ec);
	} else {
		enter_normal_mode(ec);
	}

	// Test: "Handle IRQ"
	//i2c_smbus_read_i2c_block_data(ec->client, 0x6A, 8, data[0]);
	}
	return 0;
}


void asusec_check_dock(struct asusec_data *ec)
{
	int value;

	value = gpiod_get_value(ec->dock_detect_gpio);
	if (value < 0) {
		dev_err(&ec->client->dev, "Failed to get dock detect value\n");
		asusec_error_string = "Failed to get dock detect value\n";
		return;
	}
	if (value) {
		msleep(200);
		if (asusec_chip_init(ec, true) == 0) {
			if (!ec->present) {
				dev_info(&ec->client->dev, "Dock in\n");

				ec->present = true;
				if (ec->has_keyboard)
					asusec_keyboard_enable(ec);
				asusec_battery_enable(ec);
			}
		} else {
			dev_err(&ec->client->dev, "asusec_chip_init failed in asusec_check_dock\n");
			asusec_error_string = "asusec_chip_init failed in asusec_check_dock\n";
		}
	} else {
		if (ec->present) {
			dev_info(&ec->client->dev, "Dock out\n");

			ec->present = false;
			if (ec->has_keyboard)
				asusec_keyboard_disable(ec);
			asusec_battery_disable(ec);
		}
	}
}


static int asusec_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct device_node *master_node, *dockram_node, *node;
	struct i2c_client *master_client;
	struct asusec_data *ec, *master_ec;
	//u32 i2c_functionality = I2C_FUNC_SMBUS_READ_BLOCK_DATA;
	int dock_detect_irq, error;

	/*if (!i2c_check_functionality(client->adapter, i2c_functionality)) {
		dev_err(&client->dev,
			"insufficient i2c adapter functionality\n");
		return -ENXIO;
	}*/

	ec = devm_kzalloc(&client->dev, sizeof(struct asusec_data), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	ec->client = client;
	node = client->dev.of_node;

	ec->irq_test_gpio = gpio_to_desc(146);
	if (ec->irq_test_gpio == NULL)
	{
		dev_err(&client->dev, "Could not get IRQ GPIO\n");
	}
	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	master_node = of_parse_phandle(node, "asus,master", 0);
	if (master_node != NULL) {
		if (!of_device_is_compatible(master_node, "asus,ec")) {
			of_node_put(master_node);
			dev_err(&client->dev,
				"Master is not an asusec device\n");
			return -EINVAL;
		}

		master_client = of_find_i2c_device_by_node(master_node);
		of_node_put(master_node);
		if (master_client == NULL) {
			dev_err(&client->dev,
				"Failed to get master client device\n");
			return -ENODEV;
		}

		master_ec = dev_get_drvdata(&master_client->dev);
		if (master_ec == NULL) {
			dev_info(&client->dev,
				 "Master is not initialized yet\n");
			return -EPROBE_DEFER;
		}

		if (master_ec->is_dock) {
			dev_info(&client->dev,
				 "Master EC must be non-removable\n");
			return -EINVAL;
		}
	}

	dockram_node = of_parse_phandle(node, "asus,dockram", 0);
	if (dockram_node == NULL) {
		dev_err(&client->dev, "No dockram device specified\n");
		return -EINVAL;
	}

	if (!of_device_is_compatible(dockram_node, "asus,ec-dockram"))
	{
		of_node_put(dockram_node);
		dev_err(&client->dev, "Dockram is not a dockram device\n");
		return -EINVAL;
	}

	ec->dockram_client = of_find_i2c_device_by_node(dockram_node);
	of_node_put(dockram_node);
	if (ec->dockram_client == NULL) {
		dev_err(&client->dev, "Failed to get dockram device\n");
		return -ENODEV;
	}

	//dev_info(&client->dev, "setting up gpios\n");
	ec->request_gpio = devm_gpiod_get(&client->dev, "ec-request",
					  GPIOD_OUT_HIGH);
	if (IS_ERR(ec->request_gpio)) {
		dev_err(&client->dev, "Error getting ec request gpio\n");
		return PTR_ERR(ec->request_gpio);
	}
	if (!ec->request_gpio) {
		dev_err(&client->dev, "No ec request gpio specified\n");
		return -EINVAL;
	}

	ec->dock_detect_gpio = devm_gpiod_get(&client->dev, "dock-detect",
					      GPIOD_IN);
	if (IS_ERR(ec->dock_detect_gpio)) {
		if (PTR_ERR(ec->dock_detect_gpio) != -ENOENT) {
			dev_err(&client->dev,
				"Error getting dock detect gpio\n");
			return PTR_ERR(ec->dock_detect_gpio);
		} else {
		}
	} else {
		ec->is_dock = true;
	}

	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	error = asusec_battery_init(ec);
	if (error) {
		dev_err(&client->dev, "Failed to initialize battery\n");
		return error;
	}

	ec->has_keyboard = of_find_property(node, "asus,has-keyboard", NULL);
	if (ec->has_keyboard) {
		error = asusec_keyboard_init(ec);
		if (error) {
			dev_err(&client->dev,
				"Failed to initialize keyboard\n");
			return error;
		}
	}
	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	//dev_info(&client->dev, "setting up irq\n");
	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, asusec_irq, IRQF_ONESHOT,
					  client->name, ec);
	if (error) {
		dev_err(&client->dev, "Failed to register irq\n");
		return error;
	}
	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	//dev_info(&client->dev, "initializing EC\n");
	if (ec->is_dock) {
		asusec_check_dock(ec);
	} else {
		disable_irq(client->irq);
		ec->present = true;
		error = asusec_chip_init(ec, true);
		if (error) {
			dev_err(&client->dev, "Failed to initialize EC\n");
			return error;
		}
		if (ec->has_keyboard)
			asusec_keyboard_enable(ec);
		enable_irq(client->irq);
	}
	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));

	if (ec->is_dock) {
		dock_detect_irq = gpiod_to_irq(ec->dock_detect_gpio);
		if (dock_detect_irq < 0)
		{
			dev_err(&client->dev,
				"Failed to get dock detect irq\n");
			return dock_detect_irq;
		}
		error = devm_request_threaded_irq(&client->dev, dock_detect_irq,
						  NULL, asusec_dock_detect_irq,
						  IRQF_ONESHOT
						  | IRQF_TRIGGER_RISING
						  | IRQF_TRIGGER_FALLING,
						  client->name, ec);
		if (error) {
			dev_err(&client->dev, "Failed to register dock irq\n");
			return error;
		}
	}

	//dev_info(&client->dev, "IRQ GPIO state: %i\n", gpiod_get_value(ec->irq_test_gpio));
	//dev_info(&client->dev, "setting drvdata\n");
	dev_set_drvdata(&client->dev, ec);
	ec->irq = client->irq;

	dev_info(&client->dev, "driver init complete\n");
	return 0;
}


static struct of_device_id asusec_of_match[] = {
	{ .compatible = "asus,ec" },
	{}
}
MODULE_DEVICE_TABLE(of, of_match);

static struct i2c_device_id asusec_id[] = {
	{"asus,ec", 0},
	{}
}
MODULE_DEVICE_TABLE(i2c, asusec_id);

static struct i2c_driver asusec_driver = {
	.driver = {
		.name = "asusec",
		.of_match_table = asusec_of_match,
		.owner = THIS_MODULE,
	},
	.id_table = asusec_id,
	.probe = asusec_probe,
};
module_i2c_driver(asusec_driver);

MODULE_DESCRIPTION("ASUS Transformer Pad EC MFD driver");
