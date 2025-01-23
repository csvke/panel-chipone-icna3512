#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

static const char * const regulator_names[] = {
	"vddp",
	"iovcc"
};

struct icna3512_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct regulator_bulk_data supplies[ARRAY_SIZE(regulator_names)];

	struct gpio_desc *reset_gpio;
	struct gpio_desc *dcdc_en_gpio;
	struct backlight_device *backlight;

	bool prepared;
	bool enabled;

	const struct drm_display_mode *mode;
};

static inline struct icna3512_panel *to_icna3512_panel(struct drm_panel *panel)
{
	return container_of(panel, struct icna3512_panel, base);
}

static int icna3512_panel_init(struct icna3512_panel *icna3512)
{
    struct mipi_dsi_device *dsi = icna3512->dsi;
    struct device *dev = &icna3512->dsi->dev;
    int ret;

    dev_info(dev, "Sending initial code\n");

    // Command 1 - Vendor Specific? - same as given by vender
    u8 cmd1[] = {0xA5, 0xA5};
    ret = mipi_dsi_dcs_write(dsi, 0x9C, cmd1, sizeof(cmd1));
    if (ret < 0) {
        return ret;
    }

    // Command 2 - Vendor Specific? - same as given by vender
    u8 cmd2[] = {0x5A, 0x5A};
    ret = mipi_dsi_dcs_write(dsi, 0xFD, cmd2, sizeof(cmd2));
    if (ret < 0) {
        return ret;
    }

    // Command 3 - Vendor Specific? - same as given by vender
    u8 cmd3[] = {0x03};
    ret = mipi_dsi_dcs_write(dsi, 0x48, cmd3, sizeof(cmd3));
    if (ret < 0) {
        return ret;
    }

    // Command 4 - Vendor Specific? - same as given by vender
    u8 cmd4[] = {0xE0};
    ret = mipi_dsi_dcs_write(dsi, 0x53, cmd4, sizeof(cmd4));
    if (ret < 0) {
        return ret;
    }

    // Command 5 - Vendor Specific? - same as given by vender
    u8 cmd5[] = {0x00, 0x00};
    ret = mipi_dsi_dcs_write(dsi, 0x51, cmd5, sizeof(cmd5));
    if (ret < 0) {
        return ret;
    }
	/***********************************************************************/

    // Command 6 - MIPI_DCS_SET_TEAR_ON - rk3288 init cmd appears to be setting tearing off? (15 00 02 35 00 vs 05 00 01 35)
	u8 cmd6[] = {0x00};
	ret = mipi_dsi_dcs_write(dsi, 0x35, cmd6, sizeof(cmd6));
	if (ret < 0) {
		return ret;
	}

    // Command 7 - SLP OUT - rk3288 init cmd appears to be setting SLP OUT differently
	u8 cmd7[] = {0x01, 0x11};
	ret = mipi_dsi_dcs_write(dsi, 0xFF, cmd7, sizeof(cmd7));
	if (ret < 0) {
		return ret;
	}

	// Delay 120ms
	msleep(120);
	
    // Command 8 - MIPI_DCS_SET_DISPLAY_BRIGHTNESS (MIPI DCS 1.3) - rk3288 init cmd appears to be setting a different display brightness
    // u8 cmd8[] = {0x0D, 0xBB}; // LV=800 of 1000, 4.2v power rail @ grey=0.790W, noise=1.143W ~ 1.152W
	u8 cmd8[] = {0x05, 0x55};
    ret = mipi_dsi_dcs_write(dsi, 0x51, cmd8, sizeof(cmd8));
    if (ret < 0) {
        return ret;
    }

    // Command 9 - Vendor Specific? - same as given by vender
    u8 cmd9[] = {0x0F};
    ret = mipi_dsi_dcs_write(dsi, 0x9F, cmd9, sizeof(cmd9));
    if (ret < 0) {
        return ret;
    }

    // Command 10 - Vendor Specific? - same as given by vender
    u8 cmd10[] = {0x22};
    ret = mipi_dsi_dcs_write(dsi, 0xCE, cmd10, sizeof(cmd10));
    if (ret < 0) {
        return ret;
    }

	// Command 11 - not seen from given by vendor
	u8 cmd11[] = {0x01};
	ret = mipi_dsi_dcs_write(dsi, 0x9F, cmd11, sizeof(cmd11));

	// Command 12 - not seen from given by vendor
	u8 cmd12[] = {0x01};
	ret = mipi_dsi_dcs_write(dsi, 0xC5, cmd12, sizeof(cmd12));

    // Command 13 - DISP ON - rk3288 init cmd appears to be setting DISP on differently
    u8 cmd13[] = {0x01, 0x29};
	ret = mipi_dsi_dcs_write(dsi, 0xFF, cmd13, sizeof(cmd13));
	if (ret < 0) {
		return ret;
	}

    dev_info(dev, "initial code sent\n");

    return 0;
}

// csvke: BIST, may not need in the end
// static int icna3512_panel_bist_test(struct mipi_dsi_device *dsi)
// {
//     struct device *dev = &dsi->dev;
//     int ret;

//     // BIST Command
//     u8 bist_cmd[] = {0x39, 0x00, 0x05, 0xD6, 0x01, 0x00, 0x1F, 0x01};
//     dev_info(dev, "Sending BIST command 0x39 with data 0x00, 0x05, 0xD6, 0x01, 0x00, 0x1F, 0x01\n");
//     ret = mipi_dsi_dcs_write(dsi, bist_cmd[0], &bist_cmd[1], sizeof(bist_cmd) - 1);
//     if (ret < 0) {
//         dev_err(dev, "Failed to send BIST command: %d\n", ret);
//         return ret;
//     }
//     dev_info(dev, "BIST command sent successfully\n");

//     return 0;
// }

static int icna3512_panel_on(struct icna3512_panel *icna3512)
{
	struct mipi_dsi_device *dsi = icna3512->dsi;
	struct device *dev = &icna3512->dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0)
		dev_err(dev, "failed to set display on: %d\n", ret);

	return ret;
}

static void icna3512_panel_off(struct icna3512_panel *icna3512)
{
	struct mipi_dsi_device *dsi = icna3512->dsi;
	struct device *dev = &icna3512->dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0)
		dev_err(dev, "failed to set display off: %d\n", ret);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0)
		dev_err(dev, "failed to enter sleep mode: %d\n", ret);

	msleep(100);
}

static int icna3512_panel_disable(struct drm_panel *panel)
{
	struct icna3512_panel *icna3512 = to_icna3512_panel(panel);

	if (!icna3512->enabled)
		return 0;

	backlight_disable(icna3512->backlight);

	icna3512->enabled = false;

	return 0;
}

static int icna3512_panel_unprepare(struct drm_panel *panel)
{
	struct icna3512_panel *icna3512 = to_icna3512_panel(panel);
	struct device *dev = &icna3512->dsi->dev;
	int ret;

	if (!icna3512->prepared)
		return 0;

	icna3512_panel_off(icna3512);

	ret = regulator_bulk_disable(ARRAY_SIZE(icna3512->supplies), icna3512->supplies);
	if (ret < 0)
		dev_err(dev, "regulator disable failed, %d\n", ret);

	gpiod_set_value_cansleep(icna3512->reset_gpio, 1);

	gpiod_set_value_cansleep(icna3512->dcdc_en_gpio, 0);

	icna3512->prepared = false;

	return 0;
}

static int icna3512_panel_prepare(struct drm_panel *panel)
{
	struct icna3512_panel *icna3512 = to_icna3512_panel(panel);
	struct device *dev = &icna3512->dsi->dev;
	int ret;

	if (icna3512->prepared)
		return 0;

    // // csvke: Set the prepare_prev_first flag to ensure DSI interface is in LP-11 mode, https://forums.raspberrypi.com/viewtopic.php?p=2276942&hilit=LP+11#p2276316
    // icna3512->base.prepare_prev_first = true;
    // dev_info(dev, "Set panel prepare_prev_first to true\n");
	
	ret = regulator_bulk_enable(ARRAY_SIZE(icna3512->supplies), icna3512->supplies);
	if (ret < 0) {
		dev_err(dev, "regulator enable failed, %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(icna3512->dcdc_en_gpio, 1);
    usleep_range(10, 20);

	// Trigger the reset pin according to the datasheet
    // Set nRESET to low for at least T1 (10ms)
    dev_info(panel->dev, "Setting reset GPIO low for 10ms (T1)\n");
    gpiod_set_value(icna3512->reset_gpio, 1);
    usleep_range(10000, 11000); // Sleep for 10ms

    // Set nRESET to high for at least T3 (3ms)
    dev_info(panel->dev, "Setting reset GPIO high for 3ms (T3)\n");
    gpiod_set_value(icna3512->reset_gpio, 0);
    usleep_range(3000, 4000); // Sleep for 3ms

    // Set nRESET to low for at least T4 (7ms)
    dev_info(panel->dev, "Keeping reset GPIO low for 7ms (T4)\n");
    gpiod_set_value(icna3512->reset_gpio, 1);
    usleep_range(7000, 8000); // Sleep for 7ms

    // Set nRESET to high
    dev_info(panel->dev, "Setting reset GPIO high\n");
    gpiod_set_value(icna3512->reset_gpio, 0);

    // Set a delay of 15ms (T4)
    usleep_range(15000, 16000); // Sleep for 15ms

    ret = icna3512_panel_init(icna3512);
    if (ret < 0) {
        dev_err(dev, "failed to init panel: %d\n", ret);
        goto poweroff;
    }

    // ret = icna3512_panel_bist_test(icna3512->dsi);
    // if (ret < 0) {
    //     dev_err(dev, "failed to send BIST command: %d\n", ret);
    //     goto poweroff;
    // }

    ret = icna3512_panel_on(icna3512);
    if (ret < 0) {
        dev_err(dev, "failed to set panel on: %d\n", ret);
        goto poweroff;
    }

    icna3512->prepared = true;

    return 0;

poweroff:
    ret = regulator_bulk_disable(ARRAY_SIZE(icna3512->supplies), icna3512->supplies);
    if (ret < 0)
        dev_err(dev, "regulator disable failed, %d\n", ret);

    gpiod_set_value_cansleep(icna3512->reset_gpio, 1);

    gpiod_set_value_cansleep(icna3512->dcdc_en_gpio, 0);

    return ret;
}

static int icna3512_panel_enable(struct drm_panel *panel)
{
	struct icna3512_panel *icna3512 = to_icna3512_panel(panel);
	
	if (icna3512->enabled)
		return 0;

	backlight_enable(icna3512->backlight);

	icna3512->enabled = true;

	return 0;
}

static const struct drm_display_mode default_mode = {
        .clock		= 150000, // csvke

        .hdisplay	= 1080, // Hadr in datasheet
        .hsync_start	= 1080 + 156, // HAdr + HFP
        .hsync_end	= 1080 + 156 + 1, // HAdr + HFP + Hsync
        .htotal		= 1080 + 156 + 1 + 23, // HAdr + HFP + Hsync + HBP

        .vdisplay	= 1920, // VAdr in datasheet
        .vsync_start	= 1920 + 20, // Vadr + VFP
        .vsync_end	= 1920 + 20 + 1, // Vadr + VFP + Vsync
        .vtotal		= 1920 + 20 + 1 + 15, // Vadr + VFP + Vsync + VBP
		.flags = 0, // csvke: ??? maybe 0xA03
};

static int icna3512_panel_get_modes(struct drm_panel *panel, struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct icna3512_panel *icna3512 = to_icna3512_panel(panel);
	struct device *dev = &icna3512->dsi->dev;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode) {
		dev_err(dev, "failed to add mode %ux%ux@%u\n",
			default_mode.hdisplay, default_mode.vdisplay,
			drm_mode_vrefresh(&default_mode));
		return -ENOMEM;
	}

	drm_mode_set_name(mode);

	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 87;
	connector->display_info.height_mm = 155;

	return 1;
}

static int dsi_dcs_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;
	u16 brightness = bl->props.brightness;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness & 0xff;
}

static int dsi_dcs_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl->props.brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops dsi_bl_ops = {
	.update_status = dsi_dcs_bl_update_status,
	.get_brightness = dsi_dcs_bl_get_brightness,
};

static struct backlight_device *
drm_panel_create_dsi_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct backlight_properties props;

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.brightness = 255;
	props.max_brightness = 255;

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &dsi_bl_ops, &props);
}

static const struct drm_panel_funcs icna3512_panel_funcs = {
	.disable = icna3512_panel_disable,
	.unprepare = icna3512_panel_unprepare,
	.prepare = icna3512_panel_prepare,
	.enable = icna3512_panel_enable,
	.get_modes = icna3512_panel_get_modes,
};

static const struct of_device_id icna3512_of_match[] = {
    {
        .compatible = "dxq,dxq7d0023",
    },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, icna3512_of_match);

static int icna3512_panel_add(struct icna3512_panel *icna3512)
{
	struct device *dev = &icna3512->dsi->dev;
	int ret;
	unsigned int i;

	icna3512->mode = &default_mode;

	for (i = 0; i < ARRAY_SIZE(icna3512->supplies); i++)
		icna3512->supplies[i].supply = regulator_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(icna3512->supplies),
				      icna3512->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to init regulator, ret=%d\n", ret);

	icna3512->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(icna3512->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(icna3512->reset_gpio),
				     "cannot get reset-gpios %d\n", ret);

	icna3512->dcdc_en_gpio = devm_gpiod_get(dev, "dcdc-en", GPIOD_OUT_LOW);
	if (IS_ERR(icna3512->dcdc_en_gpio))
		return dev_err_probe(dev, PTR_ERR(icna3512->dcdc_en_gpio),
				     "cannot get dcdc-en-gpio %d\n", ret);

	icna3512->backlight = drm_panel_create_dsi_backlight(icna3512->dsi);
	if (IS_ERR(icna3512->backlight))
		return dev_err_probe(dev, PTR_ERR(icna3512->backlight),
				     "failed to register backlight %d\n", ret);

	icna3512->base.prepare_prev_first = true;
	drm_panel_init(&icna3512->base, &icna3512->dsi->dev, &icna3512_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&icna3512->base);

	return 0;
}

static void icna3512_panel_del(struct icna3512_panel *icna3512)
{
	if (icna3512->base.dev)
		drm_panel_remove(&icna3512->base);
}

static int icna3512_panel_probe(struct mipi_dsi_device *dsi)
{
	struct icna3512_panel *icna3512;
	int ret;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	// dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
	// 		   MIPI_DSI_CLOCK_NON_CONTINUOUS;
	// dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_VSYNC_FLUSH | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	// dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET | MIPI_DSI_CLOCK_NON_CONTINUOUS;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	icna3512 = devm_kzalloc(&dsi->dev, sizeof(*icna3512), GFP_KERNEL);
	if (!icna3512)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, icna3512);

	icna3512->dsi = dsi;

	ret = icna3512_panel_add(icna3512);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		icna3512_panel_del(icna3512);
		return ret;
	}

	return 0;
}

static void icna3512_panel_remove(struct mipi_dsi_device *dsi)
{
	struct icna3512_panel *icna3512 = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = icna3512_panel_disable(&icna3512->base);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to disable panel: %d\n", ret);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n",
			ret);

	icna3512_panel_del(icna3512);
}

static void icna3512_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct icna3512_panel *icna3512 = mipi_dsi_get_drvdata(dsi);

	icna3512_panel_disable(&icna3512->base);
}

static struct mipi_dsi_driver icna3512_panel_driver = {
	.driver = {
		.name = "panel-chipone-icna3512",
		.of_match_table = icna3512_of_match,
	},
	.probe = icna3512_panel_probe,
	.remove = icna3512_panel_remove,
	.shutdown = icna3512_panel_shutdown,
};
module_mipi_dsi_driver(icna3512_panel_driver);

MODULE_AUTHOR("Frankie Yuen <frankie.yuen@me.com>");
MODULE_DESCRIPTION("Chipone ICNA3512 AMOLED Display Driver IC");
MODULE_LICENSE("GPL v2");
