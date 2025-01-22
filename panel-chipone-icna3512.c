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

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0)
		return ret;

	usleep_range(10000, 20000);

	ret = mipi_dsi_dcs_set_pixel_format(dsi, MIPI_DCS_PIXEL_FMT_24BIT << 4);
	if (ret < 0) {
		dev_err(dev, "failed to set pixel format: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_column_address(dsi, 0, icna3512->mode->hdisplay - 1);
	if (ret < 0) {
		dev_err(dev, "failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0, icna3512->mode->vdisplay - 1);
	if (ret < 0) {
		dev_err(dev, "failed to set page address: %d\n", ret);
		return ret;
	}

	/*
	 * BIT(5) BCTRL = 1 Backlight Control Block On, Brightness registers
	 *                  are active
	 * BIT(3) BL = 1    Backlight Control On
	 * BIT(2) DD = 0    Display Dimming is Off
	 */
	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				 (u8[]){ 0x24 }, 1);
	if (ret < 0) {
		dev_err(dev, "failed to write control display: %d\n", ret);
		return ret;
	}

	/* CABC off */
	ret = mipi_dsi_dcs_write(dsi, MIPI_DCS_WRITE_POWER_SAVE,
				 (u8[]){ 0x00 }, 1);
	if (ret < 0) {
		dev_err(dev, "failed to set cabc off: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "failed to set exit sleep mode: %d\n", ret);
		return ret;
	}

	msleep(120);

	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x00}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set mcap: %d\n", ret);
		return ret;
	}

	mdelay(10);

	/* Interface setting, video mode */
	ret = mipi_dsi_generic_write(dsi, (u8[])
				     {0xB3, 0x26, 0x08, 0x00, 0x20, 0x00}, 6);
	if (ret < 0) {
		dev_err(dev, "failed to set display interface setting: %d\n"
			, ret);
		return ret;
	}

	mdelay(20);

	ret = mipi_dsi_generic_write(dsi, (u8[]){0xB0, 0x03}, 2);
	if (ret < 0) {
		dev_err(dev, "failed to set default values for mcap: %d\n"
			, ret);
		return ret;
	}

	return 0;
}

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

	ret = regulator_bulk_enable(ARRAY_SIZE(icna3512->supplies), icna3512->supplies);
	if (ret < 0) {
		dev_err(dev, "regulator enable failed, %d\n", ret);
		return ret;
	}

	msleep(20);

	gpiod_set_value_cansleep(icna3512->dcdc_en_gpio, 1);
	usleep_range(10, 20);

	gpiod_set_value_cansleep(icna3512->reset_gpio, 0);
	usleep_range(10, 20);

	ret = icna3512_panel_init(icna3512);
	if (ret < 0) {
		dev_err(dev, "failed to init panel: %d\n", ret);
		goto poweroff;
	}

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
		.clock = 155493,
		.hdisplay = 1200,
		.hsync_start = 1200 + 48,
		.hsync_end = 1200 + 48 + 32,
		.htotal = 1200 + 48 + 32 + 60,
		.vdisplay = 1920,
		.vsync_start = 1920 + 3,
		.vsync_end = 1920 + 3 + 5,
		.vtotal = 1920 + 3 + 5 + 6,
		.flags = 0,
};

static int icna3512_panel_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
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

	connector->display_info.width_mm = 95;
	connector->display_info.height_mm = 151;

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
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
			   MIPI_DSI_CLOCK_NON_CONTINUOUS;

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
