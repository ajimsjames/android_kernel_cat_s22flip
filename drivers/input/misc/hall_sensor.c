/*
 * Cat S22 Flip Hall Sensor Driver (Lid Open / Close)
 * Compatible with "hall-switch"
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

struct hall_sensor_data {
	struct input_dev *input_dev;
	int gpio_int;
	int irq;
	struct delayed_work work;
	struct regulator *vddio;
};

static void hall_sensor_work(struct work_struct *work)
{
	struct hall_sensor_data *data = container_of(to_delayed_work(work),
					struct hall_sensor_data, work);
	int state = gpio_get_value(data->gpio_int);

	/* SW_LID: 1 = closed, 0 = open */
	input_report_switch(data->input_dev, SW_LID, !state);
	input_sync(data->input_dev);
}

static irqreturn_t hall_sensor_irq_handler(int irq, void *dev_id)
{
	struct hall_sensor_data *data = dev_id;

	schedule_delayed_work(&data->work, msecs_to_jiffies(10));
	return IRQ_HANDLED;
}

static int hall_sensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hall_sensor_data *data;
	int rc;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->gpio_int = of_get_named_gpio(np, "linux,gpio-int", 0);
	if (!gpio_is_valid(data->gpio_int)) {
		dev_err(dev, "failed to get gpio-int\n");
		return -EINVAL;
	}

	rc = devm_gpio_request_one(dev, data->gpio_int, GPIOF_DIR_IN, "hall_sensor");
	if (rc) {
		dev_err(dev, "failed to request gpio %d\n", data->gpio_int);
		return rc;
	}

	data->vddio = devm_regulator_get_optional(dev, "vddio");
	if (!IS_ERR(data->vddio)) {
		rc = regulator_enable(data->vddio);
		if (rc)
			dev_warn(dev, "failed to enable vddio: %d\n", rc);
	}

	data->input_dev = devm_input_allocate_device(dev);
	if (!data->input_dev)
		return -ENOMEM;

	data->input_dev->name = "hall_sensor";
	data->input_dev->phys = "hall_sensor/input0";
	data->input_dev->id.bustype = BUS_HOST;

	input_set_capability(data->input_dev, EV_SW, SW_LID);

	rc = input_register_device(data->input_dev);
	if (rc) {
		dev_err(dev, "failed to register input device\n");
		return rc;
	}

	INIT_DELAYED_WORK(&data->work, hall_sensor_work);

	data->irq = gpio_to_irq(data->gpio_int);
	rc = devm_request_threaded_irq(dev, data->irq, NULL,
				       hall_sensor_irq_handler,
				       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				       "hall_sensor", data);
	if (rc) {
		dev_err(dev, "failed to request irq %d\n", data->irq);
		return rc;
	}

	device_init_wakeup(dev, of_property_read_bool(np, "linux,wakeup"));
	platform_set_drvdata(pdev, data);

	/* Read initial state */
	schedule_delayed_work(&data->work, 0);

	dev_info(dev, "hall_sensor probed successfully (gpio=%d)\n", data->gpio_int);
	return 0;
}

static int hall_sensor_remove(struct platform_device *pdev)
{
	struct hall_sensor_data *data = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&data->work);
	return 0;
}

static const struct of_device_id hall_sensor_of_match[] = {
	{ .compatible = "hall-switch", },
	{ }
};
MODULE_DEVICE_TABLE(of, hall_sensor_of_match);

static struct platform_driver hall_sensor_driver = {
	.probe = hall_sensor_probe,
	.remove = hall_sensor_remove,
	.driver = {
		.name = "hall_sensor",
		.of_match_table = hall_sensor_of_match,
	},
};
module_platform_driver(hall_sensor_driver);

MODULE_DESCRIPTION("Cat S22 Flip Hall Effect Sensor Driver");
MODULE_LICENSE("GPL v2");
