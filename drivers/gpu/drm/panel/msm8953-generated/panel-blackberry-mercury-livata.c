// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 MarshJiang
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
// Copyright (c) 2013, The Linux Foundation. All rights reserved.

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct livata {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;
	bool prepared;
};

static const struct regulator_bulk_data livata_supplies[] = {
	{ .supply = "vsn" },
	{ .supply = "vsp" },
};

static inline struct livata *to_livata(struct drm_panel *panel)
{
	return container_of(panel, struct livata, panel);
}

static void livata_reset(struct livata *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int livata_on(struct livata *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x07, 0x01);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x80);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xff, 0x87, 0x07);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa3);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x06, 0x54, 0x00, 0x10);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb4);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xc0, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 1000, 2000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xa0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6,
					 0x0c, 0x0a, 0x08, 0x09, 0x0f, 0x0f, 0x14, 0x15,
					 0x10, 0x1b, 0x16, 0x12);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xb0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6,
					 0xc5, 0x80, 0x88, 0x78, 0x7d, 0x7a, 0x7b, 0x76,
					 0x94, 0x97, 0xb2, 0xdd);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xc0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6,
					 0x84, 0x80, 0x8a, 0x7d, 0x81, 0x80, 0x9c, 0x8f,
					 0x93, 0xa0, 0x9a, 0x99);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0xd0);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6,
					 0x9a, 0x8a, 0x8d, 0x9a, 0x80, 0x80, 0x98, 0x9b,
					 0xaa, 0xaa, 0xa6, 0x94);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x00, 0x00);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0x91, 0x80);

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 34);

	return dsi_ctx.accum_err;
}

static int livata_off(struct livata *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 34);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int livata_prepare(struct drm_panel *panel)
{
	struct livata *ctx = to_livata(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(livata_supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}
	livata_reset(ctx);

	ret = livata_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(livata_supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int livata_unprepare(struct drm_panel *panel)
{
	struct livata *ctx = to_livata(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = livata_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(livata_supplies), ctx->supplies);
	ctx->prepared = false;

	return 0;
}

static const struct drm_display_mode livata_mode = {
	.clock = (1080 + 10 + 4 + 10) * (1620 + 400 + 1 + 14) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 10,
	.hsync_end = 1080 + 10 + 4,
	.htotal = 1080 + 10 + 4 + 10,
	.vdisplay = 1620,
	.vsync_start = 1620 + 400,
	.vsync_end = 1620 + 400 + 1,
	.vtotal = 1620 + 400 + 1 + 14,
	.width_mm = 63,
	.height_mm = 95,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int livata_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &livata_mode);
}

static const struct drm_panel_funcs livata_panel_funcs = {
	.prepare = livata_prepare,
	.unprepare = livata_unprepare,
	.get_modes = livata_get_modes,
};

static int livata_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct livata *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(livata_supplies),
					    livata_supplies,
					    &ctx->supplies);
	if (ret < 0)
		return ret;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_MODE_VIDEO_HSE | MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &livata_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void livata_remove(struct mipi_dsi_device *dsi)
{
	struct livata *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id livata_of_match[] = {
	{ .compatible = "blackberry,livata" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, livata_of_match);

static struct mipi_dsi_driver livata_driver = {
	.probe = livata_probe,
	.remove = livata_remove,
	.driver = {
		.name = "panel-livata",
		.of_match_table = livata_of_match,
	},
};
module_mipi_dsi_driver(livata_driver);

MODULE_AUTHOR("MarshJiang");
MODULE_DESCRIPTION("DRM driver for Livata video mode DSI panel");
MODULE_LICENSE("GPL v2");
