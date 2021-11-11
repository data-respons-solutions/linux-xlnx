#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/component.h>
#include <linux/pm_runtime.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_of.h>

#include "video-vdma.h"

struct panel_vdma
{
	struct vdma_channel *vdma;
	struct drm_display_mode *mode;
	struct drm_panel panel;
	struct platform_device *pdev;
	struct drm_connector connector;
	struct drm_encoder encoder;
};

static const struct component_ops panel_component_ops;
static const struct drm_connector_helper_funcs panel_vdma_connector_helper_funcs;
static const struct drm_encoder_funcs panel_vdma_encoder_funcs;
static const struct drm_encoder_helper_funcs panel_vdma_encoder_helper_funcs;
static const struct drm_connector_funcs panel_vdma_connector_funcs;
static const struct drm_panel_funcs panel_funcs;
static const struct of_device_id panel_of_table[];

static struct platform_driver panel_driver;

static int panel_remove(struct platform_device *pdev);
static int panel_probe(struct platform_device *pdev);
static int panel_get_modes(struct drm_panel *panel);
static int panel_enable(struct drm_panel *panel);
static int panel_disable(struct drm_panel *panel);
static int panel_prepare(struct drm_panel *panel);
static int panel_unprepare(struct drm_panel *panel);
static int panel_vdma_get_modes(struct drm_connector *connector);
static int panel_vdma_bind(struct device *dev, struct device *master, void *data);
static int panel_vdma_encoder_atomic_check(struct drm_encoder *encoder,
		struct drm_crtc_state *crtc_state, struct drm_connector_state *conn_state);
static void panel_vdma_encoder_disable(struct drm_encoder *encoder);
static void panel_vdma_encoder_enable(struct drm_encoder *encoder);
static void panel_vdma_unbind(struct device *dev, struct device *master, void *data);

static int panel_unprepare(struct drm_panel *panel)
{
	(void)panel;
	return 0;
}

static int panel_prepare(struct drm_panel *panel)
{
	(void)panel;
	return 0;
}

static int panel_disable(struct drm_panel *panel)
{
	(void)panel;
	return 0;
}

static int panel_enable(struct drm_panel *panel)
{
	(void)panel;
	return 0;
}

static int panel_get_modes(struct drm_panel *panel)
{
	struct panel_vdma *vpanel;
	struct drm_connector *connector;

	u32 width;
	u32 height;
	u32 bpp;
	u32 ppi;

	int rc;
	u32 bus_format;

	vpanel = container_of(panel, struct panel_vdma, panel);
	connector = vpanel->panel.connector;
	vpanel->mode = drm_mode_create(vpanel->panel.drm);
	if (vpanel->mode == NULL) {
		dev_err(&vpanel->pdev->dev, "drm mode create failed\n");
		return 0;
	}
	if (IS_ERR(vpanel->mode)) {
		dev_err(&vpanel->pdev->dev, "drm mode create failed\n");
		return 0;
	}
	width  = vdma_mm2s_get_px_width(vpanel->vdma);
	height = vdma_mm2s_get_px_height(vpanel->vdma);
	bpp = vdma_mm2s_get_bit_per_px(vpanel->vdma);
	ppi = vdma_mm2s_get_px_per_inch(vpanel->vdma);
	rc  = vdma_get_px_format(vpanel->vdma, &bus_format);
	if (rc != 0) {
		dev_err(&vpanel->pdev->dev, "cannot get drm pixel format\n");
		return 0;
	}
	vpanel->mode->clock    = 20000;
	vpanel->mode->vrefresh = 60;

	vpanel->mode->hdisplay    = width;
	vpanel->mode->hsync_start = width;
	vpanel->mode->hsync_end   = width;
	vpanel->mode->htotal      = width;

	vpanel->mode->vdisplay    = height;
	vpanel->mode->vsync_start = height;
	vpanel->mode->vsync_end   = height;
	vpanel->mode->vtotal      = height;

	vpanel->mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, vpanel->mode);

	connector->display_info.width_mm  = (width * 25) / ppi;
	connector->display_info.height_mm = (height * 25) / ppi;
	connector->display_info.bus_flags = DRM_BUS_FLAG_DATA_LSB_TO_MSB;
	drm_display_info_set_bus_formats(&connector->display_info, &bus_format, 1);
	dev_info(&vpanel->pdev->dev, "configured\n");
	return 1;
}

static int panel_vdma_get_modes(struct drm_connector *connector)
{
	struct panel_vdma *vpanel;

	vpanel = container_of(connector, struct panel_vdma, connector);
	return drm_panel_get_modes(&vpanel->panel);
}

static void panel_vdma_encoder_enable(struct drm_encoder *encoder)
{
	struct panel_vdma *vpanel;

	vpanel = container_of(encoder, struct panel_vdma, encoder);
	drm_panel_prepare(&vpanel->panel);
	drm_panel_enable(&vpanel->panel);
}

static void panel_vdma_encoder_disable(struct drm_encoder *encoder)
{
	struct panel_vdma *vpanel;

	vpanel = container_of(encoder, struct panel_vdma, encoder);
	drm_panel_disable(&vpanel->panel);
	drm_panel_unprepare(&vpanel->panel);
}

static int panel_vdma_encoder_atomic_check(struct drm_encoder *encoder,
						struct drm_crtc_state *crtc_state,
						struct drm_connector_state *conn_state)
{
	(void)encoder;
	(void)crtc_state;
	(void)conn_state;
	return 0;
}

static int panel_vdma_bind(struct device *dev, struct device *master, void *data)
{
	struct panel_vdma *vpanel;
	struct drm_encoder *encoder;
	struct drm_connector *connector;
	struct drm_device *drm_dev;

	int rc;

	vpanel = dev_get_drvdata(dev);
	drm_dev = data;
	encoder = &vpanel->encoder;
	connector = &vpanel->connector;

	encoder->possible_crtcs = 1;
	rc = drm_encoder_init(drm_dev, encoder, &panel_vdma_encoder_funcs,
							DRM_MODE_ENCODER_NONE, NULL);
	if (rc < 0) {
		dev_err(dev, "failed to initialize encoder\n");
		return rc;
	}
	drm_encoder_helper_add(encoder, &panel_vdma_encoder_helper_funcs);

	connector->dpms = DRM_MODE_DPMS_OFF;
	rc = drm_connector_init(drm_dev, connector, &panel_vdma_connector_funcs,
							DRM_MODE_CONNECTOR_VIRTUAL);
	if (rc < 0) {
		dev_err(dev, "failed to initialize connector\n");
		return rc;
	}
	drm_connector_helper_add(connector, &panel_vdma_connector_helper_funcs);
	rc = drm_connector_attach_encoder(connector, encoder);
	if (rc < 0) {
		dev_err(dev, "failed to attach encoder\n");
		return rc;
	}
	rc = drm_panel_attach(&vpanel->panel, connector);
	if (rc < 0) {
		dev_err(dev, "failed to attach panel\n");
		return rc;
	}
	pm_runtime_enable(dev);
	dev_info(dev, "bound crtc: %08x\n", encoder->possible_crtcs);
	return 0;
}

static void panel_vdma_unbind(struct device *dev, struct device *master, void *data)
{
	struct panel_vdma *vpanel;

	vpanel = dev_get_drvdata(dev);
	drm_panel_disable(&vpanel->panel);
	drm_panel_unprepare(&vpanel->panel);
	drm_panel_detach(&vpanel->panel);
	pm_runtime_disable(dev);
	drm_connector_cleanup(&vpanel->connector);
	drm_encoder_cleanup(&vpanel->encoder);
}

static int panel_probe(struct platform_device *pdev)
{
	int rc;

	struct panel_vdma *vpanel;
	struct device_node *node;
	struct platform_device *vdma_pdev;
	struct device_node *vdma_node;

	node = pdev->dev.of_node;
	vpanel = devm_kzalloc(&pdev->dev, sizeof(struct panel_vdma), GFP_KERNEL);
	if (vpanel == NULL) {
		dev_err(&pdev->dev, "cannot allocate memory\n");
		return -ENOMEM;
	}
	if (IS_ERR(vpanel)) {
		dev_err(&pdev->dev, "cannot allocate memory\n");
		return PTR_ERR(vpanel);
	}
	vdma_node = of_parse_phandle(node, "vdma", 0);
	if (vdma_node == NULL) {
		dev_err(&pdev->dev, "no vdma handle provided\n");
		return -EINVAL;
	}
	if (IS_ERR(vdma_node)) {
		dev_err(&pdev->dev, "no vdma handle provided\n");
		return PTR_ERR(vdma_node);
	}
	vdma_pdev = of_find_device_by_node(vdma_node);
	if (vdma_pdev == NULL) {
		dev_err(&pdev->dev, "no vdma found for platform device\n");
		return -ENOMEM;
	}
	if (IS_ERR(vdma_pdev)) {
		dev_err(&pdev->dev, "no vdma found for platform device\n");
		return PTR_ERR(vdma_pdev);
	}
	vpanel->vdma = platform_get_drvdata(vdma_pdev);
	vpanel->pdev = pdev;

	drm_panel_init(&vpanel->panel);
	vpanel->panel.dev = &pdev->dev;
	vpanel->panel.funcs = &panel_funcs;
	rc = drm_panel_add(&vpanel->panel);
	if (rc < 0) {
		dev_err(&pdev->dev, "cannot add drm panel: %d\n", rc);
		return rc;
	}
	dev_set_drvdata(&pdev->dev, vpanel);
	rc = component_add(&pdev->dev, &panel_component_ops);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to add component\n");
		return rc;
	}
	dev_info(&pdev->dev, "initialized. vdma: %s\n", vdma_get_name(vpanel->vdma));
	return 0;
}

static int panel_remove(struct platform_device *pdev)
{
	struct panel_vdma *vpanel;

	vpanel = dev_get_drvdata(&pdev->dev);
	drm_panel_remove(&vpanel->panel);
	panel_disable(&vpanel->panel);
	return 0;
}

static const struct of_device_id panel_of_table[] = {
	{ .compatible = "datarespons,panel-vdma", },
	{ },
};

static struct platform_driver panel_driver = {
	.probe		= panel_probe,
	.remove		= panel_remove,
	.driver		= {
		.name	= "panel-vdma",
		.of_match_table = panel_of_table,
	},
};

static const struct drm_panel_funcs panel_funcs = {
	.disable   = panel_disable,
	.unprepare = panel_unprepare,
	.prepare   = panel_prepare,
	.enable    = panel_enable,
	.get_modes = panel_get_modes,
};

static const struct drm_connector_funcs panel_vdma_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy    = drm_connector_cleanup,
	.reset      = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_encoder_helper_funcs panel_vdma_encoder_helper_funcs = {
	.enable       = panel_vdma_encoder_enable,
	.disable      = panel_vdma_encoder_disable,
	.atomic_check = panel_vdma_encoder_atomic_check,
};

static const struct drm_encoder_funcs panel_vdma_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static const struct drm_connector_helper_funcs panel_vdma_connector_helper_funcs = {
	.get_modes = panel_vdma_get_modes,
};

static const struct component_ops panel_component_ops = {
	.bind   = panel_vdma_bind,
	.unbind = panel_vdma_unbind,
};

module_platform_driver(panel_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("VDMA Panel driver");
MODULE_LICENSE("Proprietary");
