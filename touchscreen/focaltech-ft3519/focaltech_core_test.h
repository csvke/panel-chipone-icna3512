/*
 *
 * FocalTech TouchScreen driver test header.
 *
 * Copyright (c) 2012-2020, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_FOCALTECH_CORE_TEST_H__
#define __LINUX_FOCALTECH_CORE_TEST_H__

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/gpio/consumer.h> // Include this header for gpiod_* functions

/* Define the driver name */
#define FTS_DRIVER_NAME "focaltech_ts"

/* Data structure for the touch screen data */
struct fts_ts_data {
    struct i2c_client *client;
    struct device *dev;
    int log_level;
    int fw_is_running;
    int bus_type;
    struct gpio_desc *reset_gpio; // Use struct gpio_desc * for GPIO descriptors
    struct gpio_desc *irq_gpio;   // Use struct gpio_desc * for GPIO descriptors
};

/* Dummy function declarations for testing */
int fts_ts_probe_entry(struct fts_ts_data *ts_data);
void fts_ts_remove_entry(struct fts_ts_data *ts_data);

#endif /* __LINUX_FOCALTECH_CORE_TEST_H__ */