#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h> // Include this header for gpiod_* functions
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "focaltech_core_test.h"

// Dummy functions to satisfy linker
int fts_ts_probe_entry(struct fts_ts_data *ts_data) {
    printk(KERN_INFO "Dummy fts_ts_probe_entry called\n");
    return 0;
}

void fts_ts_remove_entry(struct fts_ts_data *ts_data) {
    printk(KERN_INFO "Dummy fts_ts_remove_entry called\n");
}

// Define the global variable fts_data
struct fts_ts_data *fts_data;

// Define the BUS_TYPE_I2C constant
#define BUS_TYPE_I2C 1

// GPIO and power control functions
static int fts_gpio_setup(struct fts_ts_data *ts_data)
{
    dev_info(ts_data->dev, "Requesting reset GPIO\n");
    ts_data->reset_gpio = devm_gpiod_get(ts_data->dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(ts_data->reset_gpio)) {
        dev_err(ts_data->dev, "Failed to request reset GPIO\n");
        return PTR_ERR(ts_data->reset_gpio);
    } else {
        dev_info(ts_data->dev, "Successfully requested reset GPIO\n");
    }

    dev_info(ts_data->dev, "Requesting irq GPIO\n");
    ts_data->irq_gpio = devm_gpiod_get(ts_data->dev, "irq", GPIOD_IN);
    if (IS_ERR(ts_data->irq_gpio)) {
        dev_err(ts_data->dev, "Failed to request irq GPIO\n");
        return PTR_ERR(ts_data->irq_gpio);
    } else {
        dev_info(ts_data->dev, "Successfully requested irq GPIO\n");
    }

    return 0;
}

static int fts_power_control(struct fts_ts_data *ts_data, bool on)
{
    if (on) {
        // Power on sequence
        dev_info(ts_data->dev, "Powering on the device\n");
        gpiod_set_value(ts_data->reset_gpio, 0); // Set reset pin low
        dev_info(ts_data->dev, "Set reset pin low\n");
        gpiod_direction_output(ts_data->irq_gpio, 0); // Set IRQ pin low
        dev_info(ts_data->dev, "Set IRQ pin low\n");
        msleep(1); // Wait for 1ms (Tprt)

        gpiod_direction_input(ts_data->irq_gpio); // Set IRQ pin input high
        dev_info(ts_data->dev, "Set IRQ pin input high\n");
        msleep(1); // Wait for 1ms (Tprt)

        gpiod_set_value(ts_data->reset_gpio, 1); // Set reset pin high
        dev_info(ts_data->dev, "Set reset pin high\n");
        msleep(1); // Wait for 1ms (Tprt)

        msleep(6); // Maintain IRQ input high for another 6ms (Trio)
        dev_info(ts_data->dev, "Maintain IRQ input high for 6ms\n");

        gpiod_direction_output(ts_data->irq_gpio, 1); // Set IRQ pin output high
        dev_info(ts_data->dev, "Set IRQ pin output high\n");
        // TBD: Wait for the specified duration (TBD ms)
        msleep(200); // Example wait time, adjust as needed
        dev_info(ts_data->dev, "Wait for 200ms\n");
    } else {
        // Power off sequence
        dev_info(ts_data->dev, "Powering off the device\n");
        gpiod_set_value(ts_data->reset_gpio, 0); // Set reset pin low
        dev_info(ts_data->dev, "Set reset pin low\n");
    }

    return 0;
}

static int fts_ts_probe(struct i2c_client *client)
{
    int ret = 0;
    struct fts_ts_data *ts_data = NULL;

    printk(KERN_INFO "Touch Screen(I2C BUS) driver probe...\n");
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        printk(KERN_ERR "I2C not supported\n");
        return -ENODEV;
    }

    ts_data = (struct fts_ts_data *)kzalloc(sizeof(*ts_data), GFP_KERNEL);
    if (!ts_data) {
        printk(KERN_ERR "allocate memory for fts_data fail\n");
        return -ENOMEM;
    }

    fts_data = ts_data;
    ts_data->client = client;
    ts_data->dev = &client->dev;
    ts_data->log_level = 1;
    ts_data->fw_is_running = 0;
    ts_data->bus_type = BUS_TYPE_I2C;
    i2c_set_clientdata(client, ts_data);

    dev_info(ts_data->dev, "Calling fts_gpio_setup\n");
    ret = fts_gpio_setup(ts_data);
    if (ret) {
        printk(KERN_ERR "Failed to setup GPIOs\n");
        kfree(ts_data);
        return ret;
    }

    dev_info(ts_data->dev, "Calling fts_power_control\n");
    ret = fts_power_control(ts_data, true);
    if (ret) {
        printk(KERN_ERR "Failed to power on device\n");
        kfree(ts_data);
        return ret;
    }

    dev_info(ts_data->dev, "Calling fts_ts_probe_entry\n");
    ret = fts_ts_probe_entry(ts_data);
    if (ret) {
        printk(KERN_ERR "Touch Screen(I2C BUS) driver probe fail\n");
        fts_power_control(ts_data, false);
        kfree(ts_data);
        return ret;
    }

    printk(KERN_INFO "Touch Screen(I2C BUS) driver probe successfully\n");
    return 0;
}

static void fts_ts_remove(struct i2c_client *client)
{
    struct fts_ts_data *ts_data = i2c_get_clientdata(client);

    fts_power_control(ts_data, false);
    fts_ts_remove_entry(ts_data);
    kfree(ts_data);
}

static const struct i2c_device_id fts_ts_id[] = {
    {FTS_DRIVER_NAME, 0},
    {},
};
MODULE_DEVICE_TABLE(i2c, fts_ts_id);

static const struct of_device_id fts_dt_match[] = {
    {.compatible = "focaltech,test", },
    {},
};
MODULE_DEVICE_TABLE(of, fts_dt_match);

static struct i2c_driver fts_ts_driver = {
    .probe = fts_ts_probe,
    .remove = fts_ts_remove,
    .driver = {
        .name = FTS_DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(fts_dt_match),
    },
    .id_table = fts_ts_id,
};

static int __init focaltech_core_test_init(void)
{
    int ret = 0;

    printk(KERN_INFO "Focaltech core test module init\n");
    ret = i2c_add_driver(&fts_ts_driver);
    if (ret != 0) {
        printk(KERN_ERR "Focaltech touch screen driver init failed!\n");
    }
    return ret;
}

static void __exit focaltech_core_test_exit(void)
{
    i2c_del_driver(&fts_ts_driver);
    printk(KERN_INFO "Focaltech core test module exit\n");
}

module_init(focaltech_core_test_init);
module_exit(focaltech_core_test_exit);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("FocalTech Core Test Module");
MODULE_LICENSE("GPL");