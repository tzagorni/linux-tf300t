/*
 * Driver for ASUS Transformer Pad embedded controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/input.h>
#include "asusec.h"

static const unsigned char asusec_keys[128] = {
	/*      0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
	/* 0 */ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 15, 41,  0,
	/* 1 */ 0, 56, 42, 93, 29, 16,  2,  0,  0,  0, 44, 31, 30, 17,  3,  0,
	/* 2 */ 0, 46, 45, 32, 18,  5,  4,  0,  0, 57, 47, 33, 20, 19,  6,  0,
	/* 3 */ 0, 49, 48, 35, 34, 21,  7,  0,  0,  0, 50, 36, 22,  8,  9,  0,
	/* 4 */ 0, 51, 37, 23, 24, 11, 10,  0,  0, 52, 53, 38, 39, 25, 12,  0,
	/* 5 */ 0, 89, 40,  0, 26, 13,  0,  0, 58, 54, 28, 27,  0, 43,  0, 85,
	/* 6 */ 0, 86,  0,  0, 92,  0, 14, 94,  0,  0,124,  0,  0,  0,  0,  0,
	/* 7 */ 0,  0,  0,  0,  0,  0,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,
};
static const unsigned char asusec_ext_keys[128] = {
	/*      0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
	/* 0 */ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	/* 1 */ 0,100,  0,  0, 97,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,125,
	/* 2 */ 0,  0,  0,  0,  0,  0,  0, 56,  0,  0,  0,  0,  0,  0,  0,139,
	/* 3 */ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	/* 4 */ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	/* 5 */ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	/* 6 */ 0,  0,  0,  0,  0,  0,  0,  0,  0,107,  0,105,102,  0,  0,  0,
	/* 7 */ 0,111,108,  0,106,103,  0,  0,  0,  0,109,  0,  0,104,  0,  0,
};
static const unsigned char asusec_f_keys[] = {
	/*      0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F */
	/* 0 */	0,111, 59, 60, 61, 62, 63, 64, 65,  0,  0,  0,  0,  0,  0,  0,
	/* 1 */66, 67, 68, 87, 88,113,114,115,
};


#define ASUSEC_BREAK	0xF0
#define ASUSEC_EXTEND	0xE0

void asusec_keyboard_key(struct asusec_data *ec,
				const unsigned char *data)
{
	bool extend = false;
	bool down = true;
	int pos = 2;
	unsigned char scancode, key = 0;

	if (data[pos] == ASUSEC_EXTEND) {
		pos++;
		extend = true;
	}
	if (data[pos] == ASUSEC_BREAK) {
		pos++;
		down = false;
	}

	scancode = data[pos];

	/* Some specific scancodes come at position 5 to 6. */
	if (extend && !down && (scancode == 0x12 || scancode == 0x59)) {
		down = true;
		scancode = data[6];
	}

	if (scancode < 128)
	{
		if (extend)
			key = asusec_ext_keys[scancode];
		else
			key = asusec_keys[scancode];
	}

	if (key == 0)
	{
		dev_warn(&ec->client->dev, "unknown scancode %i\n", scancode);
		return;
	}

	input_report_key(ec->indev, key, down ? 1 : 0);
	input_sync(ec->indev);
}

void asusec_keyboard_f_key(struct asusec_data *ec,
				  const unsigned char *data)
{
	unsigned char scancode, key;

	scancode = data[2];

	/*
	 * Ignore invalid scancodes
	 * The device sometimes sends a scancode 0 for no reason.
	 */
	if (scancode < sizeof(asusec_f_keys))
	{
		key = asusec_f_keys[scancode];
		if (key)
		{
			input_report_key(ec->indev, key, 1);
			input_sync(ec->indev);
			input_report_key(ec->indev, key, 0);
			input_sync(ec->indev);
		}
	}
}

static int asusec_keyboard_event(struct input_dev *dev, unsigned int type,
				 unsigned int code, int value)
{
	// TODO
	return 0;
}

int asusec_keyboard_init(struct asusec_data *ec)
{
	return 0;
}

int asusec_keyboard_enable(struct asusec_data *ec)
{
	struct input_dev *indev;
	int i;

	indev = devm_input_allocate_device(&ec->client->dev);
	if (!indev)
		return -ENOMEM;

	ec->indev = indev;

	indev->name = "ASUS EC Keyboard";
	indev->phys = ec->client->name;
	set_bit(EV_KEY, indev->evbit);
	set_bit(EV_REP, indev->evbit);
	set_bit(LED_CAPSL, indev->ledbit);

	for (i = 0; i < 128; i++)
	{
		set_bit(asusec_keys[i], indev->keybit);
		set_bit(asusec_ext_keys[i], indev->keybit);
	}
	for (i = 0; i < sizeof(asusec_f_keys); i++)
		set_bit(asusec_f_keys[i], indev->keybit);

	clear_bit(0, indev->keybit);

	indev->dev.parent = &ec->client->dev;
	indev->event = asusec_keyboard_event;

	return input_register_device(ec->indev);
}

void asusec_keyboard_disable(struct asusec_data *ec)
{
	input_unregister_device(ec->indev);
	input_free_device(ec->indev);
}
