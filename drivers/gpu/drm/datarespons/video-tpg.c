#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/io.h>

#define VIDEO_TPG_CONTROL_REGISTER 	0x00
#define VIDEO_TPG_HEIGHT_REGISTER	0x10
#define VIDEO_TPG_WIDTH_REGISTER	0x18
#define VIDEO_BACKGROUND_PTRN_REGISTER	0x20
#define VIDEO_COLOR_FORMAT_REGISTER	0x40
#define VIDEO_INTERLACE_CONFIG_REGISTER	0xD0

MODULE_LICENSE("PROPRIETARY");

static const struct of_device_id video_tpg_of_ids[];
static struct platform_driver video_tpg_driver;

static int video_tpg_probe(struct platform_device *pdev);
static int video_tpg_remove(struct platform_device *pdev);

static int video_tpg_probe(struct platform_device *pdev)
{
	int rc;

	u32 width;
	u32 height;
	u32 background_pattern;
	u32 color_format;
	u32 interlace_config;

	void __iomem *regs;
	struct device_node *node;

	node = pdev->dev.of_node;
	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs) || (regs == NULL)) {
		dev_err(&pdev->dev, "cannot map registers\n");
		return PTR_ERR(regs);
	}
	rc = of_property_read_u32(node, "frame-width", &width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing frame-width\n");
		return rc;
	}
	rc = of_property_read_u32(node, "frame-height", &height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing frame-height\n");
		return rc;
	}
	rc = of_property_read_u32(node, "background-pattern", &background_pattern);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing background-pattern\n");
		return rc;
	}
	rc = of_property_read_u32(node, "color-format", &color_format);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format\n");
		return rc;
	}
	rc = of_property_read_u32(node, "interlace-config", &interlace_config);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing interlace-config\n");
		return rc;
	}

	dev_info(&pdev->dev, "Output %ux%u bptrn=%x fmt=%x interlace=%x\n",
			width, height, background_pattern, color_format, interlace_config);

	iowrite32(0x80, regs + VIDEO_TPG_CONTROL_REGISTER);
	iowrite32(width, regs + VIDEO_TPG_WIDTH_REGISTER);
	iowrite32(height, regs + VIDEO_TPG_HEIGHT_REGISTER);
	iowrite32(background_pattern, regs + VIDEO_BACKGROUND_PTRN_REGISTER);
	iowrite32(color_format, regs + VIDEO_COLOR_FORMAT_REGISTER);
	iowrite32(interlace_config, regs + VIDEO_INTERLACE_CONFIG_REGISTER);
	iowrite32(0x81, regs + VIDEO_TPG_CONTROL_REGISTER);

	dev_info(&pdev->dev, "initialized\n");
	return 0;
}

static int video_tpg_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id video_tpg_of_ids[] = {
	{ .compatible = "datarespons,video-tpg",},
	{}
};

static struct platform_driver video_tpg_driver = {
	.driver = {
		.name = "video_tpg_driver",
		.owner = THIS_MODULE,
		.of_match_table = video_tpg_of_ids,
	},
	.probe = video_tpg_probe,
	.remove = video_tpg_remove,
};

module_platform_driver(video_tpg_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("Video TPG");
MODULE_LICENSE("PROPRIETARY");
