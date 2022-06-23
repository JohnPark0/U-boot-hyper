/*
 * MIPI-DSI based s6e8fa0 AMOLED LCD 5.3 inch panel driver.
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd
 *
 * Inki Dae, <inki.dae@samsung.com>
 * Donghwa Lee, <dh09.lee@samsung.com>
 * Joongmock Shin <jmock.shin@samsung.com>
 * Eunchul Kim <chulspro.kim@samsung.com>
 * Tomasz Figa <t.figa@samsung.com>
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_mode.h>
#include <drm/drm_connector.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>


#define S6E8FA0_MIN_BRIGHTNESS      0
#define S6E8FA0_MAX_BRIGHTNESS      255
#define S6E8FA0_DEFAULT_BRIGHTNESS  240

static const u32 s6e8fa0_panel_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

struct s6e8fa0_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct gpio_desc *reset;
	struct backlight_device *bl_dev;

	bool prepared;
	bool enabled;

	struct videomode vm;
	u32 width_mm;
	u32 height_mm;
};

static void s6e8fa0_dcs_write(struct mipi_dsi_device *dsi, const void *data, size_t len)
{
	// len : 1=MIPI_DSI_DCS_SHORT_WRITE, 2=MIPI_DSI_DCS_SHORT_WRITE_PARAM, else=MIPI_DSI_DCS_LONG_WRITE)
	mipi_dsi_dcs_write_buffer(dsi, data, len);
}

#define s6e8fa0_dcs_write_seq_static(dsi, seq...) \
({\
	static const u8 d[] = { seq };\
	s6e8fa0_dcs_write(dsi, d, ARRAY_SIZE(d));\
})

static inline struct s6e8fa0_panel *to_s6e8fa0_panel(struct drm_panel *panel)
{
	return container_of(panel, struct s6e8fa0_panel, base);
}

static int s6e8fa0_panel_prepare(struct drm_panel *drm_panel)
{
	struct s6e8fa0_panel *panel = to_s6e8fa0_panel(drm_panel);
	struct device *dev = &panel->dsi->dev;	

	dev_info(dev, "[LCD] %s\n", __func__);

	if(panel->prepared)
		return 0;

	if(panel->reset != NULL) {
		gpiod_set_value(panel->reset, 0);
		usleep_range(5000, 10000);
		gpiod_set_value(panel->reset, 1);
		usleep_range(20000, 25000);
	}
	panel->prepared = true;

	return 0;
}

static int s6e8fa0_panel_unprepare(struct drm_panel *drm_panel)
{
	struct s6e8fa0_panel *panel = to_s6e8fa0_panel(drm_panel);
	struct device *dev = &panel->dsi->dev;

	dev_info(dev, "[LCD] %s\n", __func__);

	if (!panel->prepared)
		return 0;

	if (panel->enabled) {
		dev_err(dev, "Panel still enabled!\n");
		return -EPERM;
	}

	if (panel->reset != NULL) {
		gpiod_set_value(panel->reset, 0);
		usleep_range(15000, 17000);
		gpiod_set_value(panel->reset, 1);
	}

	panel->prepared = false;

	return 0;
}

static int s6e8fa0_panel_enable(struct drm_panel *drm_panel)
{
	struct s6e8fa0_panel *panel = to_s6e8fa0_panel(drm_panel);
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;
	//u16 brightness;
	int ret;

	dev_info(dev, "[LCD] %s\n", __func__);

	if (panel->enabled)
		return 0;

	if (!panel->prepared) {
		dev_err(dev, "Panel not prepared!\n");
		return -EPERM;
	}

	/* Software reset */
	ret = mipi_dsi_dcs_soft_reset(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to do Software Reset (%d)\n", ret);
		goto fail;
	}

	usleep_range(15000, 17000);

	/* Exit sleep mode */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);	/* 0x11 */
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode (%d)\n", ret);
		goto fail;
	}
	usleep_range(20000, 21000);

	s6e8fa0_dcs_write_seq_static(dsi, 0x53, 0x28);
	usleep_range(5000, 6000);
	s6e8fa0_dcs_write_seq_static(dsi, 0x51, 0xff);
	usleep_range(10000, 11000);

    ret = mipi_dsi_dcs_set_display_on(dsi);		/* 0x29 */
	if (ret < 0) {
		dev_err(dev, "Failed to set display ON (%d)\n", ret);
		goto fail;
	}
	usleep_range(20000, 21000);

	backlight_enable(panel->bl_dev);

	panel->enabled = true;

	return 0;

fail:
	if (panel->reset != NULL)
		gpiod_set_value(panel->reset, 0);

	return ret;
}

static int s6e8fa0_panel_disable(struct drm_panel *drm_panel)
{
	struct s6e8fa0_panel *panel= to_s6e8fa0_panel(drm_panel);
	struct mipi_dsi_device *dsi = panel->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dev_info(dev, "[LCD] %s\n", __func__);

	if (!panel->enabled)
		return 0;

	backlight_disable(panel->bl_dev);

	usleep_range(10000, 15000);

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display OFF (%d)\n", ret);
		return ret;
	}

	usleep_range(5000, 10000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	panel->enabled = false;

	return 0;
}

static int s6e8fa0_panel_get_modes(struct drm_panel *drm_panel,
										struct drm_connector *connector)
{
	struct s6e8fa0_panel *panel = to_s6e8fa0_panel(drm_panel);
	struct device *dev = &panel->dsi->dev;
	struct drm_display_mode *mode;
	u32 *bus_flags = &connector->display_info.bus_flags;
	int ret;

	dev_info(dev, "[LCD] %s\n", __func__);

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		dev_err(dev, "Failed to create display mode!\n");
		return 0;
	}

	drm_display_mode_from_videomode(&panel->vm, mode);
	mode->width_mm  = panel->width_mm;
	mode->height_mm = panel->height_mm;

	connector->display_info.width_mm  = panel->width_mm;
	connector->display_info.height_mm = panel->height_mm;

	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	if (panel->vm.flags & DISPLAY_FLAGS_DE_HIGH)
		*bus_flags |= DRM_BUS_FLAG_DE_HIGH;
	if (panel->vm.flags & DISPLAY_FLAGS_DE_LOW)
		*bus_flags |= DRM_BUS_FLAG_DE_LOW;
	if (panel->vm.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_SAMPLE_NEGEDGE;
	if (panel->vm.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_SAMPLE_POSEDGE;

	ret = drm_display_info_set_bus_formats(&connector->display_info,
              s6e8fa0_panel_bus_formats, ARRAY_SIZE(s6e8fa0_panel_bus_formats));
	if (ret)
		return ret;

	drm_mode_probed_add(connector, mode);

	return 1;
}

static int s6e8fa0_bl_get_brightness(struct backlight_device *bl_dev)
{
	return bl_dev->props.brightness;
}

static int s6e8fa0_bl_update_status(struct backlight_device *bl_dev)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl_dev);
	struct s6e8fa0_panel *panel = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret = 0;

	dev_info(dev, "[LCD] %s : brightness=%d\n", __func__,  bl_dev->props.brightness);

	if (!panel->prepared)
        return 0;

	ret = mipi_dsi_dcs_set_display_brightness(dsi, bl_dev->props.brightness);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct backlight_ops s6e8fa0_bl_ops = {
	.update_status  = s6e8fa0_bl_update_status,
	.get_brightness = s6e8fa0_bl_get_brightness,
};

static const struct drm_panel_funcs s6e8fa0_drm_funcs = {
	.prepare   = s6e8fa0_panel_prepare,
	.unprepare = s6e8fa0_panel_unprepare,
	.enable    = s6e8fa0_panel_enable,
	.disable   = s6e8fa0_panel_disable,
	.get_modes = s6e8fa0_panel_get_modes,
};

/*
 * The clock might range from 66MHz (30Hz refresh rate)
 * to 132MHz (60Hz refresh rate)
 */
static const struct display_timing s6e8fa0_default_timing = {
	.pixelclock = { 66000000, 132000000, 132000000 },
	.hactive = { 1080, 1080, 1080 },
	.hfront_porch = { 30, 30, 30 },
	.hsync_len = { 10, 10, 10 },
	.hback_porch = { 14, 14, 14 },
	.vactive = { 1920, 1920, 1920 },
	.vfront_porch = { 6, 6, 6 },
	.vsync_len = { 2, 2, 2 },
	.vback_porch = { 6, 6, 6 },
	.flags = DISPLAY_FLAGS_HSYNC_LOW |
	         DISPLAY_FLAGS_VSYNC_LOW |
	         DISPLAY_FLAGS_DE_LOW |
	         DISPLAY_FLAGS_PIXDATA_NEGEDGE,
};

static int s6e8fa0_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *np = dev->of_node;
	struct device_node *timings;
	struct s6e8fa0_panel *panel;
	struct backlight_properties bl_props;
	int ret;
	u32 video_mode;

	dev_info(dev, "[LCD] %s\n", __func__);
	
	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, panel);

	panel->dsi = dsi;

	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags =  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_VIDEO |
	                   MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ret = of_property_read_u32(np, "video-mode", &video_mode);
	if (!ret) {
		switch (video_mode) {
		case 0: /* burst mode */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST;
			break;
		case 1: /* non-burst mode with sync event */
			break;
		case 2: /* non-burst mode with sync pulse */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			break;
		default:
			dev_warn(dev, "invalid video mode %d\n", video_mode);
			break;
		}
	}

	ret = of_property_read_u32(np, "dsi-lanes", &dsi->lanes);
	if (ret < 0) {
		dev_err(dev, "Failed to get dsi-lanes property (%d)\n", ret);
		return ret;
	}

	/*
	 * 'display-timings' is optional, so verify if the node is present
	 * before calling of_get_videomode so we won't get console error
	 * messages
	 */
	timings = of_get_child_by_name(np, "display-timings");
	if (timings) {
		of_node_put(timings);
		ret = of_get_videomode(np, &panel->vm, 0);
	} else {
		videomode_from_timing(&s6e8fa0_default_timing, &panel->vm);
	}
	if (ret < 0)
		return ret;

	of_property_read_u32(np, "panel-width-mm", &panel->width_mm);
	of_property_read_u32(np, "panel-height-mm", &panel->height_mm);

	panel->reset = devm_gpiod_get(dev, "reset-gpio", GPIOD_OUT_HIGH);

	if (IS_ERR(panel->reset))
		panel->reset = NULL;
	else
		gpiod_set_value(panel->reset, 0);

	memset(&bl_props, 0, sizeof(bl_props));
	bl_props.type = BACKLIGHT_RAW;
	bl_props.brightness = S6E8FA0_DEFAULT_BRIGHTNESS;
	bl_props.max_brightness = S6E8FA0_MAX_BRIGHTNESS;
	bl_props.power = FB_BLANK_POWERDOWN;

	panel->bl_dev = devm_backlight_device_register(
	                        dev, dev_name(dev),
	                        dev, dsi,
	                        &s6e8fa0_bl_ops, &bl_props);
	if (IS_ERR(panel->bl_dev)) {
		ret = PTR_ERR(panel->bl_dev);
		dev_err(dev, "Failed to register backlight (%d)\n", ret);
		return ret;
	}

	drm_panel_init(&panel->base, dev, &s6e8fa0_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	panel->base.funcs = &s6e8fa0_drm_funcs;
	panel->base.dev = dev;
	dev_set_drvdata(dev, panel);

	drm_panel_add(&panel->base);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&panel->base);

	return ret;
}

static int s6e8fa0_panel_remove(struct mipi_dsi_device *dsi)
{
	struct s6e8fa0_panel *panel = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(dev, "Failed to detach from host (%d)\n", ret);

	drm_panel_remove(&panel->base);
	backlight_device_unregister(panel->bl_dev);

	return 0;
}

#ifdef CONFIG_PM
static int s6e8fa0_panel_suspend(struct device *dev)
{
	struct s6e8fa0_panel *panel = dev_get_drvdata(dev);

	if (!panel->reset)
		return 0;

	devm_gpiod_put(dev, panel->reset);
	panel->reset = NULL;

	return 0;
}

static int s6e8fa0_panel_resume(struct device *dev)
{
	struct s6e8fa0_panel *panel = dev_get_drvdata(dev);

	if (panel->reset)
		return 0;

	panel->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(panel->reset))
		panel->reset = NULL;

	return PTR_ERR_OR_ZERO(panel->reset);
}
#endif

static const struct dev_pm_ops s6e8fa0_pm_ops = {
	SET_RUNTIME_PM_OPS(s6e8fa0_panel_suspend, s6e8fa0_panel_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(s6e8fa0_panel_suspend, s6e8fa0_panel_resume)
};

static const struct of_device_id s6e8fa0_of_match[] = {
    { .compatible = "samsung,s6e8fa0" },
    { }
};
MODULE_DEVICE_TABLE(of, s6e8fa0_of_match);

static struct mipi_dsi_driver s6e8fa0_driver = {
	.driver = {
		.name = "panel-samsung-s6e8fa0",
		.of_match_table = s6e8fa0_of_match,
		.pm   = &s6e8fa0_pm_ops,
	},
	.probe    = s6e8fa0_panel_probe,
	.remove   = s6e8fa0_panel_remove,
};
module_mipi_dsi_driver(s6e8fa0_driver);

MODULE_DESCRIPTION("DRM Driver for Samsung S6E8FA0 MIPI DSI panel");
MODULE_AUTHOR("Microvision Co., Ltd. <khkim@mvlab.co.kr>");
MODULE_LICENSE("GPL v2");

