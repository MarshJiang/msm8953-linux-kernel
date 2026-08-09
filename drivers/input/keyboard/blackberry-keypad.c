// SPDX-License-Identifier: GPL-2.0-only
/*
 * BlackBerry KEYone keyboard controller driver
 *
 * Copyright (C) 2015 BlackBerry Limited
 * Copyright (C) 2026 MarshJiang
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm_wakeirq.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#define BBRY_KEYPAD_REG_CHIP_ID		0x00
#define BBRY_KEYPAD_REG_INT_CTRL	0x04
#define BBRY_KEYPAD_REG_INT_EN		0x06
#define BBRY_KEYPAD_REG_INT_STATUS	0x08
#define BBRY_KEYPAD_REG_ROW		0x30
#define BBRY_KEYPAD_REG_COL		0x31
#define BBRY_KEYPAD_REG_CTRL_LOW	0x33
#define BBRY_KEYPAD_REG_CTRL_MID	0x34
#define BBRY_KEYPAD_REG_CTRL_HIGH	0x35
#define BBRY_KEYPAD_REG_CMD		0x36
#define BBRY_KEYPAD_REG_DATA		0x3a

#define BBRY_KEYPAD_CHIP_ID		0xc1

#define BBRY_KEYPAD_INT_KEY		BIT(1)
#define BBRY_KEYPAD_INT_OVERFLOW	BIT(2)
#define BBRY_KEYPAD_INT_GLOBAL_ENABLE	BIT(0)

#define BBRY_KEYPAD_CMD_SCAN_ENABLE	BIT(0)
#define BBRY_KEYPAD_FIFO_LENGTH		5
#define BBRY_KEYPAD_FIFO_EVENTS		3
#define BBRY_KEYPAD_FIFO_EMPTY_MASK	GENMASK(6, 3)
#define BBRY_KEYPAD_FIFO_RELEASE	BIT(7)

#define BBRY_KEYPAD_MAX_ROWS		8
#define BBRY_KEYPAD_MAX_COLS		8
#define BBRY_KEYPAD_KEYMAP_SIZE		64
#define BBRY_KEYPAD_MAX_FIFO_READS	16

struct blackberry_keypad {
	struct input_dev *input;
	struct regmap *regmap;
	unsigned short keymap[BBRY_KEYPAD_KEYMAP_SIZE];
	unsigned int rows;
	unsigned int cols;
	unsigned int row_shift;
	u8 row_mask;
	u16 col_mask;
};

static const struct regmap_config blackberry_keypad_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = BBRY_KEYPAD_REG_DATA + BBRY_KEYPAD_FIFO_LENGTH - 1,
};

static int blackberry_keypad_configure(struct blackberry_keypad *keypad)
{
	u8 columns[] = { keypad->col_mask, keypad->col_mask >> 8 };
	int error;

	error = regmap_write(keypad->regmap, BBRY_KEYPAD_REG_CMD, 0);
	if (error)
		return error;

	error = regmap_write(keypad->regmap, BBRY_KEYPAD_REG_ROW,
			     keypad->row_mask);
	if (error)
		return error;

	error = regmap_bulk_write(keypad->regmap, BBRY_KEYPAD_REG_COL,
				  columns, sizeof(columns));
	if (error)
		return error;

	/* Two scans per key, 10 debounce periods and a 60 Hz scan rate. */
	error = regmap_write(keypad->regmap, BBRY_KEYPAD_REG_CTRL_LOW, 2 << 4);
	if (error)
		return error;

	error = regmap_write(keypad->regmap, BBRY_KEYPAD_REG_CTRL_MID, 10 << 1);
	if (error)
		return error;

	error = regmap_write(keypad->regmap, BBRY_KEYPAD_REG_CTRL_HIGH, 0);
	if (error)
		return error;

	error = regmap_write(keypad->regmap, BBRY_KEYPAD_REG_INT_EN,
			     BBRY_KEYPAD_INT_KEY | BBRY_KEYPAD_INT_OVERFLOW);
	if (error)
		return error;

	error = regmap_write(keypad->regmap, BBRY_KEYPAD_REG_INT_CTRL,
			     BBRY_KEYPAD_INT_GLOBAL_ENABLE);
	if (error)
		return error;

	return regmap_write(keypad->regmap, BBRY_KEYPAD_REG_CMD,
			    BBRY_KEYPAD_CMD_SCAN_ENABLE);
}

static int blackberry_keypad_read_fifo(struct blackberry_keypad *keypad)
{
	u8 data[BBRY_KEYPAD_FIFO_LENGTH];
	bool reported = false;
	int error;
	int batch;
	int i;

	for (batch = 0; batch < BBRY_KEYPAD_MAX_FIFO_READS; batch++) {
		bool events = false;

		error = regmap_bulk_read(keypad->regmap, BBRY_KEYPAD_REG_DATA,
					 data, sizeof(data));
		if (error)
			return error;

		for (i = 0; i < BBRY_KEYPAD_FIFO_EVENTS; i++) {
			unsigned int code;
			unsigned int col;
			unsigned int row;
			bool pressed;

			if ((data[i] & BBRY_KEYPAD_FIFO_EMPTY_MASK) ==
			    BBRY_KEYPAD_FIFO_EMPTY_MASK)
				continue;
			events = true;

			row = data[i] & GENMASK(2, 0);
			col = (data[i] >> 3) & GENMASK(3, 0);
			if (row >= keypad->rows || col >= keypad->cols)
				continue;

			code = MATRIX_SCAN_CODE(row, col, keypad->row_shift);
			if (keypad->keymap[code] == KEY_RESERVED)
				continue;

			pressed = !(data[i] & BBRY_KEYPAD_FIFO_RELEASE);
			if (pressed == test_bit(keypad->keymap[code],
						keypad->input->key))
				continue;

			input_event(keypad->input, EV_MSC, MSC_SCAN, code);
			input_report_key(keypad->input, keypad->keymap[code], pressed);
			reported = true;
		}

		if (!events)
			break;
	}

	if (reported)
		input_sync(keypad->input);

	return 0;
}

static irqreturn_t blackberry_keypad_irq(int irq, void *data)
{
	struct blackberry_keypad *keypad = data;
	unsigned int status;
	int error;

	error = regmap_read(keypad->regmap, BBRY_KEYPAD_REG_INT_STATUS, &status);
	if (error)
		return IRQ_NONE;

	if (status & BBRY_KEYPAD_INT_OVERFLOW)
		dev_warn_ratelimited(keypad->input->dev.parent,
				     "keyboard FIFO overflow\n");

	if (!(status & BBRY_KEYPAD_INT_KEY))
		return IRQ_HANDLED;

	error = blackberry_keypad_read_fifo(keypad);
	if (error) {
		dev_err_ratelimited(keypad->input->dev.parent,
				    "failed to read keyboard FIFO: %d\n", error);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int blackberry_keypad_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct blackberry_keypad *keypad;
	struct gpio_desc *reset_gpio;
	unsigned int chip_id;
	int error;
	int i;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EOPNOTSUPP;

	keypad = devm_kzalloc(dev, sizeof(*keypad), GFP_KERNEL);
	if (!keypad)
		return -ENOMEM;

	keypad->regmap = devm_regmap_init_i2c(client,
					      &blackberry_keypad_regmap_config);
	if (IS_ERR(keypad->regmap))
		return dev_err_probe(dev, PTR_ERR(keypad->regmap),
				     "failed to initialize regmap\n");

	error = devm_regulator_get_enable(dev, "i2c");
	if (error)
		return dev_err_probe(dev, error,
				     "failed to enable I2C supply\n");

	reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return dev_err_probe(dev, PTR_ERR(reset_gpio),
				     "failed to get reset GPIO\n");

	keypad->input = devm_input_allocate_device(dev);
	if (!keypad->input)
		return -ENOMEM;

	keypad->input->name = "BlackBerry KEYone Keyboard";
	keypad->input->id.bustype = BUS_I2C;
	keypad->input->dev.parent = dev;

	error = matrix_keypad_parse_properties(dev, &keypad->rows, &keypad->cols);
	if (error)
		return error;

	if (!keypad->rows || keypad->rows > BBRY_KEYPAD_MAX_ROWS ||
	    !keypad->cols || keypad->cols > BBRY_KEYPAD_MAX_COLS)
		return dev_err_probe(dev, -EINVAL, "invalid keyboard matrix size\n");

	keypad->row_shift = get_count_order(keypad->cols);
	error = matrix_keypad_build_keymap(NULL, NULL, keypad->rows,
					   keypad->cols, keypad->keymap,
					   keypad->input);
	if (error)
		return dev_err_probe(dev, error, "failed to build keymap\n");

	for (i = 0; i < keypad->rows * keypad->cols; i++) {
		if (keypad->keymap[i] == KEY_RESERVED)
			continue;

		keypad->row_mask |= BIT(i >> keypad->row_shift);
		keypad->col_mask |= BIT(i & (BIT(keypad->row_shift) - 1));
	}

	input_set_capability(keypad->input, EV_MSC, MSC_SCAN);

	msleep(20);
	gpiod_set_value_cansleep(reset_gpio, 0);
	msleep(100);

	for (i = 0; i < 5; i++) {
		error = regmap_read(keypad->regmap, BBRY_KEYPAD_REG_CHIP_ID,
				    &chip_id);
		if (!error && chip_id == BBRY_KEYPAD_CHIP_ID)
			break;

		if (i != 4)
			msleep(500);
	}

	if (error)
		return dev_err_probe(dev, error, "failed to read chip ID\n");

	if (chip_id != BBRY_KEYPAD_CHIP_ID)
		return dev_err_probe(dev, -ENODEV, "unexpected chip ID %#x\n",
				     chip_id);

	error = blackberry_keypad_configure(keypad);
	if (error)
		return dev_err_probe(dev, error, "failed to configure keyboard\n");

	error = input_register_device(keypad->input);
	if (error)
		return dev_err_probe(dev, error, "failed to register input device\n");

	error = devm_request_threaded_irq(dev, client->irq, NULL,
					  blackberry_keypad_irq, IRQF_ONESHOT,
					  dev_name(dev), keypad);
	if (error)
		return dev_err_probe(dev, error, "failed to request interrupt\n");

	error = dev_pm_set_wake_irq(dev, client->irq);
	if (error)
		dev_warn(dev, "failed to configure wake IRQ: %d\n", error);

	i2c_set_clientdata(client, keypad);

	return 0;
}

static void blackberry_keypad_remove(struct i2c_client *client)
{
	dev_pm_clear_wake_irq(&client->dev);
}

static const struct of_device_id blackberry_keypad_of_match[] = {
	{ .compatible = "blackberry,mercury-keypad" },
	{ }
};
MODULE_DEVICE_TABLE(of, blackberry_keypad_of_match);

static struct i2c_driver blackberry_keypad_driver = {
	.driver = {
		.name = "blackberry-keypad",
		.of_match_table = blackberry_keypad_of_match,
	},
	.probe = blackberry_keypad_probe,
	.remove = blackberry_keypad_remove,
};
module_i2c_driver(blackberry_keypad_driver);

MODULE_AUTHOR("BlackBerry Limited");
MODULE_AUTHOR("MarshJiang");
MODULE_DESCRIPTION("BlackBerry KEYone keyboard controller driver");
MODULE_LICENSE("GPL");
