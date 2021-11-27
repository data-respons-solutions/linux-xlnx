#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/fb.h>
#include <linux/of_address.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/media-bus-format.h>

#include "video-vdma.h"

#define MAX_VDMA_CONSOLIDATORS 1

struct vdma_consolidator
{
	struct platform_device *pdev;
	struct cdev cdev;
	struct device *dev;
	struct class *pclass;

	dev_t node;

	char name[32];

	u32 width;
	u32 width_mm;
	u32 height;
	u32 height_mm;
	u32 bpp;
	u32 stride;
	u32 fmt;

	dma_addr_t paddr;
};

static int consolidators_probed = 0;
struct vdma_consolidator *consolidators[MAX_VDMA_CONSOLIDATORS];
static struct platform_driver vdma_consolidator_driver;
static struct file_operations vdma_consolidator_fops;
static const struct of_device_id vdma_consolidator_of_ids[];

static int vdma_consolidator_probe(struct platform_device *pdev);
static int vdma_consolidator_init_cdevice(struct vdma_consolidator *consolidator);
static int vdma_consolidator_remove(struct platform_device *pdev);
static int vdma_consolidator_open(struct inode *ino, struct file *file);
static int vdma_consolidator_release(struct inode *ino, struct file *file);
static int vdma_consolidator_mmap(struct file *file_p, struct vm_area_struct *vma);
static long vdma_consolidator_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static int vdma_consolidator_open(struct inode *ino, struct file *file)
{
	u32 i;

	for (i = 0; i < consolidators_probed; ++i) {
		if (ino->i_rdev == consolidators[i]->node) {
			file->private_data = consolidators[i];
			return 0;
		}
	}
	return -ENOENT;
}

static int vdma_consolidator_release(struct inode *ino, struct file *file)
{
	(void)ino;
	(void)file;
	return 0;
}

static int vdma_consolidator_mmap(struct file *file_p, struct vm_area_struct *vma)
{
	struct vdma_consolidator *consolidator;

	u32 recv_size;
	u32 expected_size;

	consolidator = (struct vdma_consolidator *)file_p->private_data;
	recv_size = vma->vm_end - vma->vm_start;
	expected_size = consolidator->stride * consolidator->height;
	if (recv_size != expected_size) {
		dev_err(&consolidator->pdev->dev, "invalid map size received (%u/%u)\n",
								recv_size, expected_size);
		return -EINVAL;
	}
	return vm_iomap_memory(vma, consolidator->paddr, expected_size);
}

static long vdma_consolidator_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vdma_consolidator *consolidator;

	struct fb_fix_screeninfo *finfo;
	struct fb_var_screeninfo *vinfo;

	consolidator = (struct vdma_consolidator *)file->private_data;
	switch (cmd) {
		case FBIOGET_FSCREENINFO:
			finfo = (struct fb_fix_screeninfo *)arg;

			(void)memset(finfo, 0, sizeof(*finfo));
			(void)strcpy(finfo->id, "DR consolidator");
			finfo->smem_start  = consolidator->paddr;
			finfo->smem_len    = consolidator->stride * consolidator->height;
			finfo->line_length = consolidator->stride;
			finfo->type   = FB_TYPE_PACKED_PIXELS;
			finfo->visual = FB_VISUAL_TRUECOLOR;
			return 0;

		case FBIOGET_VSCREENINFO:
			vinfo = (struct fb_var_screeninfo *)arg;

			(void)memset(vinfo, 0, sizeof(*vinfo));
			vinfo->xres = consolidator->width;
			vinfo->yres = consolidator->height;
			vinfo->xres_virtual = consolidator->width;
			vinfo->yres_virtual = consolidator->height;
			vinfo->width  = consolidator->width_mm;
			vinfo->height = consolidator->height_mm;

			vinfo->bits_per_pixel = consolidator->bpp;
			switch (consolidator->fmt) {
				case MEDIA_BUS_FMT_ARGB8888_1X32:
					vinfo->red.offset    = 0;
					vinfo->red.length    = 8;
					vinfo->green.offset  = 8;
					vinfo->green.length  = 8;
					vinfo->blue.offset   = 16;
					vinfo->blue.length   = 8;
					vinfo->transp.offset = 24;
					vinfo->transp.length = 8;
					break;
			}
			vinfo->pixclock = 80;
			vinfo->sync     = FB_SYNC_EXT;
			vinfo->vmode    = FB_VMODE_NONINTERLACED;
			return 0;
	}
	return -ENOTSUPP;
}

static int vdma_consolidator_init_cdevice(struct vdma_consolidator *consolidator)
{
	int rc;
	bool scan;
	size_t i;
	size_t j;
	struct class *pclass = NULL;

	j = 0;
	scan = false;
	for (i = 0; i < strlen(consolidator->pdev->name); ++i) {
		// EX format: amba_pl@0:v_vdmacs0@0
		if(consolidator->pdev->name[i] == ':') {
			scan = true;
			continue;
		}
		if (scan && (consolidator->pdev->name[i] == '@')) {
			break;
		}
		if (scan && (j < (sizeof(consolidator->name) - 1))) {
			consolidator->name[j] = consolidator->pdev->name[i];
			++j;
		}
	}
	for(i = 0; i < MAX_VDMA_CONSOLIDATORS; ++i) {
		if ((consolidators[i] != NULL) && (consolidators[i]->pclass != NULL)) {
			pclass = consolidators[i]->pclass;
			break;
		}
	}
	consolidator->name[j] = '\0';
	if (j == 0) {
		(void)strncpy(consolidator->name, consolidator->pdev->name,
						sizeof(consolidator->name));
		consolidator->name[sizeof(consolidator->name) - 1] = '\0';
	}
	rc = alloc_chrdev_region(&consolidator->node, 0, 1, consolidator->name);
	if (rc != 0) {
		dev_err(&consolidator->pdev->dev, "unable to get a char device number\n");
		return rc;
	}
	cdev_init(&consolidator->cdev, &vdma_consolidator_fops);
	consolidator->cdev.owner = THIS_MODULE;
	rc = cdev_add(&consolidator->cdev, consolidator->node, 1);
	if (rc != 0) {
		dev_err(&consolidator->pdev->dev, "unable to add char device\n");
		return rc;
	}
	if (pclass != NULL) {
		consolidator->pclass = pclass;
	} else {
		consolidator->pclass = class_create(THIS_MODULE, "consolidator");
	}
	if (IS_ERR(consolidator->pclass)) {
		dev_err(&consolidator->pdev->dev, "unable to create the class\n");
		return PTR_ERR(consolidator->pclass);
	}
	if (consolidator->pclass == NULL) {
		dev_err(&consolidator->pdev->dev, "unable to create the class\n");
		return -ENOMEM;
	}
	consolidator->dev = device_create(consolidator->pclass, NULL,
					consolidator->node, NULL, consolidator->name);
	if (IS_ERR(consolidator->dev)) {
		dev_err(&consolidator->pdev->dev, "unable to create the device\n");
		return PTR_ERR(consolidator->dev);
	}
	if (consolidator->dev == NULL) {
		dev_err(&consolidator->pdev->dev, "unable to create the device\n");
		return -ENOMEM;
	}
	return 0;
}

static int vdma_consolidator_probe(struct platform_device *pdev)
{
	int rc;
	int i;
	struct vdma_channel *vdma;
	struct device_node *node;
	struct device_node *vdma_node;
	struct platform_device *vdma_pdev;
	struct vdma_consolidator *consolidator;

	bool first;
	const char *name;

	u32 width;
	u32 height;
	u32 bpp;
	u32 ppi;
	u32 stride;
	u32 fmt;

	dma_addr_t paddr;
	dma_addr_t expected_paddr;
	void *vaddr;

	node = pdev->dev.of_node;
	if (consolidators_probed >= MAX_VDMA_CONSOLIDATORS) {
		dev_err(&pdev->dev, "vdma consolidator is out of bounds\n");
		return -ERANGE;
	}
	consolidator = kzalloc(sizeof(struct vdma_consolidator), GFP_KERNEL);
	if (IS_ERR(consolidator)) {
		dev_err(&pdev->dev, "cannot allocate memory for vdma consolidator\n");
		return PTR_ERR(consolidator);
	}
	if (consolidator == NULL) {
		dev_err(&pdev->dev, "cannot allocate memory for vdma consolidator\n");
		return -ENOMEM;
	}
	first = true;
	consolidator->pdev = pdev;
	for(i = 0; ; ++i) {
		vdma_node = of_parse_phandle(node, "vdma", i);
		if ((vdma_node == NULL) || IS_ERR(vdma_node)) {
			break;
		}
		vdma_pdev = of_find_device_by_node(vdma_node);
		if ((vdma_pdev == NULL) || IS_ERR(vdma_pdev)) {
			break;
		}
		vdma   = platform_get_drvdata(vdma_pdev);
		name   = vdma_get_name(vdma);
		width  = vdma_mm2s_get_px_width(vdma);
		height = vdma_mm2s_get_px_height(vdma);
		bpp    = vdma_mm2s_get_bit_per_px(vdma);
		ppi    = vdma_mm2s_get_px_per_inch(vdma);
		stride = vdma_mm2s_get_stride(vdma);
		rc = vdma_get_px_format(vdma, &fmt);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot get pixel format for %s\n", name);
			return -EINVAL;
		}
		rc = vdma_mm2s_get_fb_addr(vdma, &paddr, &vaddr);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot get buffer address for %s\n", name);
			return -EINVAL;
		}
		if (first) {
			consolidator->width     = width;
			consolidator->height    = height;
			consolidator->bpp       = bpp;
			consolidator->stride    = stride;
			consolidator->fmt       = fmt;
			consolidator->width_mm  = (width * 25) / ppi;
			consolidator->height_mm = (height * 25) / ppi;
			consolidator->paddr     = paddr;
			first = false;
			dev_info(&pdev->dev, "added %s\n", name);
			continue;
		}
		if (bpp != consolidator->bpp) {
			dev_err(&pdev->dev,
				"cannot consolidate. bpp mistmatch (%u vs %u)\n",
				bpp, consolidator->bpp);
			return -EINVAL;
		}
		if (stride != consolidator->stride) {
			dev_err(&pdev->dev,
				"cannot consolidate. stride mistmatch (%u vs %u)\n",
				stride, consolidator->stride);
			return -EINVAL;
		}
		if (fmt != consolidator->fmt) {
			dev_err(&pdev->dev,
				"cannot consolidate. format mismatch (%u vs %u)\n",
				fmt, consolidator->fmt);
			return -EINVAL;
		}
		expected_paddr = consolidator->paddr +
				(consolidator->stride * consolidator->height);
		if (paddr != expected_paddr) {
			dev_err(&pdev->dev,
			"cannot consolidate. memory is not continuous (%08llx vs %08llx)\n",
				paddr, expected_paddr);
			return -EINVAL;
		}
		consolidator->height += height;
		consolidator->height_mm += (height * 25) / ppi;
		dev_info(&pdev->dev, "added %s\n", name);
	};
	rc = vdma_consolidator_init_cdevice(consolidator);
	if (rc != 0) {
		dev_err(&pdev->dev, "cannot init char device\n");
		return rc;
	}
	consolidators[consolidators_probed] = consolidator;
	++consolidators_probed;
	dev_info(&pdev->dev, "initialized %ux%u@%ubpp[%u] (%ux%u) line:%u\n",
				consolidator->width, consolidator->height, consolidator->bpp,
				consolidator->fmt, consolidator->width_mm,
				consolidator->height_mm, consolidator->stride);
	return 0;
}

static int vdma_consolidator_remove(struct platform_device *pdev)
{
	(void)pdev;
	return 0;
}

static struct file_operations vdma_consolidator_fops = {
	.owner   = THIS_MODULE,
	.open    = vdma_consolidator_open,
	.release = vdma_consolidator_release,
	.mmap    = vdma_consolidator_mmap,
	.unlocked_ioctl = vdma_consolidator_ioctl
};

static const struct of_device_id vdma_consolidator_of_ids[] = {
	{ .compatible = "datarespons,video-vdma-consolidator", },
	{}
};

static struct platform_driver vdma_consolidator_driver = {
	.driver = {
		.name = "vdma_consolidator",
		.owner = THIS_MODULE,
		.of_match_table = vdma_consolidator_of_ids,
	},
	.probe = vdma_consolidator_probe,
	.remove = vdma_consolidator_remove,
};

module_platform_driver(vdma_consolidator_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("VDMA Consolidator Driver");
MODULE_LICENSE("PROPRIETARY");
