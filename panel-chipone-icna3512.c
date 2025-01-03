// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2019 Radxa Limited
 * Copyright (c) 2022 Edgeble AI Technologies Pvt. Ltd.
 *
 * Author:
 * - Jagan Teki <jagan@amarulasolutions.com>
 * - Stephen Chen <stephen@radxa.com>
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#define ICNA3512_INIT_CMD_LEN		2

struct icna3512_init_cmd {
    u8 data[ICNA3512_INIT_CMD_LEN];
};

struct icna3512_panel_desc {
    const struct drm_display_mode mode;
    unsigned int lanes;
    enum mipi_dsi_pixel_format format;
    const struct icna3512_init_cmd *init_cmds;
    u32 num_init_cmds;
};

struct icna3512 {
    struct drm_panel panel;
    struct mipi_dsi_device *dsi;
    const struct icna3512_panel_desc *desc;

    struct regulator *vdd;
    struct regulator *vccio;
    struct gpio_desc *reset;
};

static inline struct icna3512 *panel_to_icna3512(struct drm_panel *panel)
{
    return container_of(panel, struct icna3512, panel);
}

static int icna3512_enable(struct drm_panel *panel)
{
    struct device *dev = panel->dev;
    struct icna3512 *icna3512 = panel_to_icna3512(panel);
    const struct icna3512_panel_desc *desc = icna3512->desc;
    struct mipi_dsi_device *dsi = icna3512->dsi;
    unsigned int i;
    int err;

    msleep(10);

    for (i = 0; i < desc->num_init_cmds; i++) {
        const struct icna3512_init_cmd *cmd = &desc->init_cmds[i];

        err = mipi_dsi_dcs_write_buffer(dsi, cmd->data, ICNA3512_INIT_CMD_LEN);
        if (err < 0)
            return err;
    }

    msleep(120);

    err = mipi_dsi_dcs_exit_sleep_mode(dsi);
    if (err < 0)
        DRM_DEV_ERROR(dev, "failed to exit sleep mode ret = %d\n", err);

    err =  mipi_dsi_dcs_set_display_on(dsi);
    if (err < 0)
        DRM_DEV_ERROR(dev, "failed to set display on ret = %d\n", err);

    return 0;
}

static int icna3512_disable(struct drm_panel *panel)
{
    struct device *dev = panel->dev;
    struct icna3512 *icna3512 = panel_to_icna3512(panel);
    int ret;

    ret = mipi_dsi_dcs_set_display_off(icna3512->dsi);
    if (ret < 0)
        DRM_DEV_ERROR(dev, "failed to set display off: %d\n", ret);

    ret = mipi_dsi_dcs_enter_sleep_mode(icna3512->dsi);
    if (ret < 0)
        DRM_DEV_ERROR(dev, "failed to enter sleep mode: %d\n", ret);

    return 0;
}

static int icna3512_prepare(struct drm_panel *panel)
{
    struct icna3512 *icna3512 = panel_to_icna3512(panel);
    int ret;

    ret = regulator_enable(icna3512->vccio);
    if (ret)
        return ret;

    ret = regulator_enable(icna3512->vdd);
    if (ret)
        return ret;

    gpiod_set_value(icna3512->reset, 1);
    msleep(5);

    gpiod_set_value(icna3512->reset, 0);
    msleep(10);

    gpiod_set_value(icna3512->reset, 1);
    msleep(120);

    return 0;
}

static int icna3512_unprepare(struct drm_panel *panel)
{
    struct icna3512 *icna3512 = panel_to_icna3512(panel);

    gpiod_set_value(icna3512->reset, 1);
    msleep(120);

    regulator_disable(icna3512->vdd);
    regulator_disable(icna3512->vccio);

    return 0;
}

static int icna3512_get_modes(struct drm_panel *panel,
                struct drm_connector *connector)
{
    struct icna3512 *icna3512 = panel_to_icna3512(panel);
    const struct drm_display_mode *desc_mode = &icna3512->desc->mode;
    struct drm_display_mode *mode;

    mode = drm_mode_duplicate(connector->dev, desc_mode);
    if (!mode) {
        DRM_DEV_ERROR(&icna3512->dsi->dev, "failed to add mode %ux%ux@%u\n",
                  desc_mode->hdisplay, desc_mode->vdisplay,
                  drm_mode_vrefresh(desc_mode));
        return -ENOMEM;
    }

    drm_mode_set_name(mode);
    drm_mode_probed_add(connector, mode);

    connector->display_info.width_mm = mode->width_mm;
    connector->display_info.height_mm = mode->height_mm;

    return 1;
}

static const struct drm_panel_funcs icna3512_funcs = {
    .disable = icna3512_disable,
    .unprepare = icna3512_unprepare,
    .prepare = icna3512_prepare,
    .enable = icna3512_enable,
    .get_modes = icna3512_get_modes,
};

// csvke: dxq7d0023 init_cmds based on panel-init-sequence of rk3288_mipi_7D0_amoled.dts given by vendor
// csvke: DCS Write command info: https://forum.armsom.org/t/rk3588-mipi-display-debugging-rk3588-mipi-dsi-lcd-power-up-initialization-sequence/147
// WIP: csvke: need to verify the correctness of the init_cmds as vendor provided multiple init_cmds
static const struct icna3512_init_cmd dxq7d0023_init_cmds[] = {
    // 9C,A5,A5
    { .data = { 0x39, 0x00 } },
    { .data = { 0x03, 0x9C } },
    { .data = { 0xA5, 0xA5 } },

    //FD,5A,5A
    { .data = { 0x39, 0x00 } },
    { .data = { 0x03, 0xFD } },
    { .data = { 0x5A, 0x5A } },
    
    //48,03
    //53,E0
    { .data = { 0x15, 0x00 } },
    { .data = { 0x02, 0x48 } },
    { .data = { 0x03, 0x15 } },
    { .data = { 0x00, 0x02 } },
    { .data = { 0x53, 0xE0 } },
    
    //51,00,00
    { .data = { 0x39, 0x00 } },
    { .data = { 0x03, 0x51 } },
    { .data = { 0x00, 0x00 } },
    
    //
    { .data = { 0x15, 0x00 } },
    { .data = { 0x02, 0x35 } },
    
    { .data = { 0x00, 0x05 } },
    { .data = { 0xFF, 0x01 } },
    { .data = { 0x11, 0x39 } }, //
    { .data = { 0x00, 0x03 } },
    { .data = { 0x51, 0x05 } },
    { .data = { 0x55, 0x15 } },
    { .data = { 0x00, 0x02 } },
    { .data = { 0x9F, 0x0F } },
    { .data = { 0x15, 0x00 } },
    { .data = { 0x02, 0xCE } },
    { .data = { 0x22, 0x15 } },
    { .data = { 0x00, 0x02 } },
    { .data = { 0x9F, 0x01 } },
    { .data = { 0x15, 0x00 } },
    { .data = { 0x02, 0xC5 } },
    { .data = { 0x01, 0x05 } },
    { .data = { 0xFF, 0x01 } },

    { .data = { 0x29, 0x39 } }, // csvke: likely to be the start of Auto BIST (Built-In Self-Test) from rk3288_mipi_7D0_amoled.dts 39 00 05 d6 01 00 1F 01   //auto BIST
    { .data = { 0x00, 0x05 } },
    { .data = { 0xD6, 0x01 } },
    { .data = { 0x00, 0x1F } },
    { .data = { 0x01, 0x00 } },
};

// csvke: based on datasheet of dxq7d0023 
// csvke: dxq7d0023_desc based on display-timings of rk3288_mipi_7D0_amoled.dts given by vendor seems to be wrong!
static const struct icna3512_panel_desc dxq7d0023_desc = {
    .mode = {
        .clock		= 150000,

        .hdisplay	= 1080, // Hadr in datasheet
        .hsync_start	= 1080 + 156, // HAdr + HFP
        .hsync_end	= 1080 + 156 + 1, // HAdr + HFP + Hsync
        .htotal		= 1080 + 156 + 1 + 23, // HAdr + HFP + Hsync + HBP

        .vdisplay	= 1920, // VAdr in datasheet
        .vsync_start	= 1920 + 20, // Vadr + VFP
        .vsync_end	= 1920 + 20 + 1, // Vadr + VFP + Vsync
        .vtotal		= 1920 + 20 + 1 + 15, // Vadr + VFP + Vsync + VBP

        .width_mm	= 87, // based on vendor provided datasheet, p.4 Active Area (WXH)
        .height_mm	= 155, // based on vendor provided datasheet, p.4 Active Area (WXH)
        .type		= DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
    },
    .lanes = 4,
    .format = MIPI_DSI_FMT_RGB888,
    .init_cmds = dxq7d0023_init_cmds,
    .num_init_cmds = ARRAY_SIZE(dxq7d0023_init_cmds),
};

static int icna3512_dsi_probe(struct mipi_dsi_device *dsi)
{
    struct device *dev = &dsi->dev;
    const struct icna3512_panel_desc *desc;
    struct icna3512 *icna3512;
    int ret;

    icna3512 = devm_kzalloc(&dsi->dev, sizeof(*icna3512), GFP_KERNEL);
    if (!icna3512)
        return -ENOMEM;

    desc = of_device_get_match_data(dev);
    dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
              MIPI_DSI_MODE_NO_EOT_PACKET;
    dsi->format = desc->format;
    dsi->lanes = desc->lanes;

    icna3512->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
    if (IS_ERR(icna3512->reset)) {
        DRM_DEV_ERROR(&dsi->dev, "failed to get our reset GPIO\n");
        return PTR_ERR(icna3512->reset);
    }

    icna3512->vdd = devm_regulator_get(dev, "vdd");
    if (IS_ERR(icna3512->vdd)) {
        DRM_DEV_ERROR(&dsi->dev, "failed to get vdd regulator\n");
        return PTR_ERR(icna3512->vdd);
    }

    icna3512->vccio = devm_regulator_get(dev, "vccio");
    if (IS_ERR(icna3512->vccio)) {
        DRM_DEV_ERROR(&dsi->dev, "failed to get vccio regulator\n");
        return PTR_ERR(icna3512->vccio);
    }

    drm_panel_init(&icna3512->panel, dev, &icna3512_funcs,
               DRM_MODE_CONNECTOR_DSI);

    ret = drm_panel_of_backlight(&icna3512->panel);
    if (ret)
        return ret;

    drm_panel_add(&icna3512->panel);

    mipi_dsi_set_drvdata(dsi, icna3512);
    icna3512->dsi = dsi;
    icna3512->desc = desc;

    ret = mipi_dsi_attach(dsi);
    if (ret < 0)
        drm_panel_remove(&icna3512->panel);

    return ret;
}

static void icna3512_dsi_remove(struct mipi_dsi_device *dsi)
{
    struct icna3512 *icna3512 = mipi_dsi_get_drvdata(dsi);

    mipi_dsi_detach(dsi);
    drm_panel_remove(&icna3512->panel);
}

// csvke: add comptaible string for dxq7d0023
static const struct of_device_id icna3512_of_match[] = {
    {
        .compatible = "dxq,dxq7d0023",
        .data = &dxq7d0023_desc
    },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, icna3512_of_match);

static struct mipi_dsi_driver icna3512_driver = {
    .probe = icna3512_dsi_probe,
    .remove = icna3512_dsi_remove,
    .driver = {
        .name = "icna3512-dxq7d0023",
        .of_match_table = icna3512_of_match,
    },
};
module_mipi_dsi_driver(icna3512_driver);

MODULE_AUTHOR("Jagan Teki <jagan@edgeble.ai>");
MODULE_AUTHOR("Stephen Chen <stephen@radxa.com>");
MODULE_DESCRIPTION("ICNA3512 DXQ7D0023 DSI panel");
MODULE_LICENSE("GPL");