#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/io.h>

#define VIDEO_MIXER_CONTROL_REGISTER 		0x000
#define VIDEO_MIXER_WIDTH_REGISTER		0x010
#define VIDEO_MIXER_HEIGHT_REGISTER		0x018
#define VIDEO_MIXER_PLANE_BMSK_REGISTER		0x040
#define VIDEO_MIXER_YCBCR_RGB_CSC_COEFFICIENTS	0x048
#define VIDEO_MIXER_RGB_YCBCR_CSC_COEFFICIENTS	0x140
#define VIDEO_MIXER_PLANE_OPAC_REGISTER		0x000
#define VIDEO_MIXER_PLANE_WDTH_REGISTER		0x018
#define VIDEO_MIXER_PLANE_STRD_REGISTER 	0x020
#define VIDEO_MIXER_PLANE_HGHT_REGISTER		0x028

#define VIDEO_MIXER_YCBCR_RGB_CSC_COEFFICIENTS_IDX(x) \
	(VIDEO_MIXER_YCBCR_RGB_CSC_COEFFICIENTS + (x * 0x08))
#define VIDEO_MIXER_RGB_YCBCR_CSC_COEFFICIENTS_IDX(x) \
	(VIDEO_MIXER_RGB_YCBCR_CSC_COEFFICIENTS + (x * 0x08))

#define VIDEO_MIXER_CTRL_AUTO_RESTART_BITMASK	(1 << 7)
#define VIDEO_MIXER_CTRL_AP_START_BITMASK	(1 << 0)

MODULE_LICENSE("PROPRIETARY");

static const u32 csc_ycbcr_to_rgb_coefficients[] = {
	0x000012A1, 0x00000000, 0x00001973, 0x000012A1, 0xFFFFF9BB, 0xFFFFF2FE,
	0x000012A1, 0x00002046, 0x00000000, 0xFFFFFF21, 0x00000088, 0xFFFFFEEB
};

static const u32 csc_rgb_to_ycbcr_coefficients[] = {
	0x0000041B, 0x00000810, 0x00000190, 0xFFFFFDA1, 0xFFFFFB59, 0x00000707,
	0x00000707, 0xFFFFFA1E, 0xFFFFFEDC, 0x00000010, 0x00000080, 0x00000080
};

static const struct of_device_id video_mixer_of_ids[];
static struct platform_driver video_mixer_driver;

static int video_mixer_probe(struct platform_device *pdev);
static int video_mixer_remove(struct platform_device *pdev);

static int video_mixer_probe(struct platform_device *pdev)
{
	int rc;

	u32 width;
	u32 height;
	u32 bpp;
	u32 opacity;
	u32 plane_bitmask;
	u32 reg_offset;
	u32 i;

	void __iomem *regs;
	struct device_node *node;
	struct device_node *child;

	bool csc;

	node = pdev->dev.of_node;
	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs) || (regs == NULL)) {
		dev_err(&pdev->dev, "cannot map registers\n");
		return PTR_ERR(regs);
	}
	rc = of_property_read_u32(node, "output-frame-width", &width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing output-frame-width\n");
		return rc;
	}
	rc = of_property_read_u32(node, "output-frame-height", &height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing output-frame-height\n");
		return rc;
	}
	rc = of_property_read_u32(node, "plane-bitmask", &plane_bitmask);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing plane-bitmask\n");
		return rc;
	}
	csc = of_property_read_bool(node, "csc");

	iowrite32(VIDEO_MIXER_CTRL_AUTO_RESTART_BITMASK, regs + VIDEO_MIXER_CONTROL_REGISTER);
	iowrite32(width, regs + VIDEO_MIXER_WIDTH_REGISTER);
	iowrite32(height, regs + VIDEO_MIXER_HEIGHT_REGISTER);
	iowrite32(plane_bitmask, regs + VIDEO_MIXER_PLANE_BMSK_REGISTER);

	if (csc) {
		for (i = 0; i < 12; ++i) {
			iowrite32(csc_ycbcr_to_rgb_coefficients[i],
				regs + VIDEO_MIXER_YCBCR_RGB_CSC_COEFFICIENTS_IDX(i));
			iowrite32(csc_rgb_to_ycbcr_coefficients[i],
				regs + VIDEO_MIXER_RGB_YCBCR_CSC_COEFFICIENTS_IDX(i));
		}
	}
	dev_info(&pdev->dev, "output %ux%u pmask=%x. csc %s\n",
			width, height, plane_bitmask, (csc ? "true" : "false"));

	for_each_child_of_node(node, child) {
		rc = of_property_read_u32(child, "reg-offset", &reg_offset);
		if (rc < 0) {
			dev_err(&pdev->dev, "missing reg-offset\n");
			return rc;
		}
		rc = of_property_read_u32(child, "frame-opacity", &opacity);
		if (rc < 0) {
			dev_err(&pdev->dev, "missing frame-opacity\n");
			return rc;
		}
		rc = of_property_read_u32(child, "frame-width", &width);
		if (rc < 0) {
			dev_err(&pdev->dev, "missing frame-width\n");
			return rc;
		}
		rc = of_property_read_u32(child, "frame-height", &height);
		if (rc < 0) {
			dev_err(&pdev->dev, "missing frame-height\n");
			return rc;
		}
		rc = of_property_read_u32(child, "frame-bpp", &bpp);
		if (rc < 0) {
			dev_err(&pdev->dev, "missing frame-bpp\n");
			return rc;
		}
		dev_info(&pdev->dev, "input @%u %ux%u %ubpp op=%x\n",
				(reg_offset / 0x100) - 1, width, height, bpp, opacity);

		iowrite32(opacity, regs + reg_offset + VIDEO_MIXER_PLANE_OPAC_REGISTER);
		iowrite32(width, regs + reg_offset + VIDEO_MIXER_PLANE_WDTH_REGISTER);
		iowrite32(width * (bpp / 8), regs + reg_offset + VIDEO_MIXER_PLANE_STRD_REGISTER);
		iowrite32(height, regs + reg_offset + VIDEO_MIXER_PLANE_HGHT_REGISTER);
	}
	iowrite32(VIDEO_MIXER_CTRL_AUTO_RESTART_BITMASK | VIDEO_MIXER_CTRL_AP_START_BITMASK,
							regs + VIDEO_MIXER_CONTROL_REGISTER);

	dev_info(&pdev->dev, "initialized\n");
	return 0;
}

static int video_mixer_remove(struct platform_device *pdev)
{
	(void)pdev;
	return 0;
}

static const struct of_device_id video_mixer_of_ids[] = {
	{ .compatible = "datarespons,video-mixer",},
	{}
};

static struct platform_driver video_mixer_driver = {
	.driver = {
		.name = "video_mixer_driver",
		.owner = THIS_MODULE,
		.of_match_table = video_mixer_of_ids,
	},
	.probe = video_mixer_probe,
	.remove = video_mixer_remove,
};

module_platform_driver(video_mixer_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("Video Mixer Driver");
MODULE_LICENSE("PROPRIETARY");
