#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/media-bus-format.h>

#include "video-vdma.h"

#define VDMA_MM2S_CONTROL_REGISTER		0x00
#define VDMA_MM2S_STATUS_REGISTER		0x04
#define VDMA_VERSION_REGISTER			0x2C
#define VDMA_S2MM_CONTROL_REGISTER		0x30
#define VDMA_S2MM_STATUS_REGISTER		0x34
#define VDMA_MM2S_VSIZE_REGISTER		0x50
#define VDMA_MM2S_HSIZE_REGISTER		0x54
#define VDMA_MM2S_FRMDLY_STRIDE_REGISTER	0x58
#define VDMA_MM2S_START_ADDRESS_REGISTER(x)	(0x5C + ((x) * 4))
#define VDMA_S2MM_VSIZE_REGISTER		0xA0
#define VDMA_S2MM_HSIZE_REGISTER		0xA4
#define VDMA_S2MM_FRMDLY_STRIDE_REGISTER	0xA8
#define VDMA_S2MM_START_ADDRESS_REGISTER	0xAC

#define VDMA_MM2S_ERR_IRQ_EN_BITMASK		(1 << 14)
#define VDMA_MM2S_FRAME_CNT_IRQ_EN_BITMASK	(1 << 12)
#define VDMA_MM2S_CONTROL_GENLOCK_BITMASK	(1 <<  7)
#define VDMA_MM2S_CIRCULAR_PARK_BITMASK		(1 <<  1)
#define VDMA_MM2S_CONTROL_RS_BITMASK		(1 <<  0)

#define VDMA_MM2S_STATUS_ERR_IRQ_BITMASK	(1 << 14)
#define VDMA_MM2S_STATUS_FRAME_CNT_BITMASK	(1 << 12)
#define VDMA_MM2S_STATUS_SOF_EARLY_ERR_BITMASK	(1 <<  7)
#define VDMA_MM2S_STATUS_DEC_ERR_BITMASK	(1 <<  6)
#define VDMA_MM2S_STATUS_SLV_ERR_BITMASK	(1 <<  5)
#define VDMA_MM2S_STATUS_INT_ERR_BITMASK	(1 <<  4)
#define VDMA_MM2S_STATUS_HALTED_BITMASK		(1 <<  0)

#define VDMA_S2MM_ERR_IRQ_EN_BITMASK            (1 << 14)
#define VDMA_S2MM_FRAME_CNT_EN_BITMASK          (1 << 12)
#define VDMA_S2MM_CONTROL_GENLOCK_BITMASK	(1 <<  7)
#define VDMA_S2MM_CONTROL_RS_BITMASK		(1 <<  0)

#define VDMA_S2MM_STATUS_EOL_LATE_ERR_BITMASK	(1 << 15)
#define VDMA_S2MM_STATUS_ERR_BITMASK		(1 << 14)
#define VDMA_S2MM_STATUS_FRAME_CNT_BITMASK      (1 << 12)
#define VDMA_S2MM_STATUS_SOF_LATE_ERR_BITMASK	(1 << 11)
#define VDMA_S2MM_STATUS_EOL_EARLY_ERR_BITMASK	(1 <<  8)
#define VDMA_S2MM_STATUS_SOF_EARLY_ERR_BITMASK	(1 <<  7)
#define VDMA_S2MM_STATUS_DEC_ERR_BITMASK	(1 <<  6)
#define VDMA_S2MM_STATUS_SLV_ERR_BITMASK	(1 <<  5)
#define VDMA_S2MM_STATUS_INT_ERR_BITMASK	(1 <<  4)
#define VDMA_S2MM_STATUS_HALTED_BITMASK		(1 <<  0)

#define VDMA_VERSION_MAJOR_BITMASK	(0xF    << 28)
#define VDMA_VERSION_MINOR_BITMASK	(0xFF   << 20)
#define VDMA_VERSION_REVISION_BITMASK	(0xF    << 16)
#define VDMA_VERSION_XIL_INTERN_BITMASK	(0xFFFF <<  0)

#define VDMA_MM2S_VSIZE_LINES_BITMASK	(0xFFF)
#define VDMA_MM2S_HSIZE_BYTES_BITMASK	(0xFFFF)

#define VDMA_MM2S_STRIDE_BYTES_BITMASK	(0xFFFF)

#define VDMA_S2MM_VSIZE_LINES_BITMASK	(0xFFF)
#define VDMA_S2MM_HSIZE_BYTES_BITMASK	(0xFFFF)

#define VDMA_S2MM_STRIDE_BYTES_BITMASK	(0xFFFF)

#define VDMA_CMD_SYNC_BUFFERS	0x445202
#define VDMA_CMD_GET_STATUS	0x445201

MODULE_LICENSE("PROPRIETARY");

#define MAX_VDMA_CHANNELS	8

struct vdma_status {
	char name[64];

	struct {
		u8 major;
		u8 minor;
		u8 revision;
		u16 xil_internal;
	} version;

	struct {
		u32 err;
		u32 sof_early_err;
		u32 decode_err;
		u32 slave_err;
		u32 internal_err;
		u32 frame_cnt;

		bool halted;

		struct {
			u32 horizontal;
			u32 stride;
			u32 vertical;
		} size;
	} mm2s;

	struct {
		u32 eol_late_err;
		u32 err;
		u32 sof_late_err;
		u32 eol_early_err;
		u32 sof_early_err;
		u32 decode_err;
		u32 slave_err;
		u32 internal_err;
		u32 frame_cnt;

		bool halted;

		struct {
			u32 horizontal;
			u32 stride;
			u32 vertical;
		} size;
	} s2mm;
};

struct vdma_channel {
	struct {
		u8 *buffer;
		dma_addr_t phys_addr_offset;
		dma_addr_t phys_addr;

		bool interlaced;
		u8 *buffer_even;
		u8 *buffer_odd;
		dma_addr_t phys_addr_even;
		dma_addr_t phys_addr_odd;

		u32 frame_bpp;
		u32 frame_width;
		u32 frame_height;
		u32 frame_stride;
		u32 frame_ppi;

		bool active;
		bool always_on;
		bool dma_malloced;
		bool genlock;

		int irq;

		u32 err;
		u32 sof_early_err;
		u32 decode_err;
		u32 slave_err;
		u32 internal_err;
		u32 frame_cnt;
	} mm2s;

	struct {
		u8 *buffer;
		dma_addr_t phys_addr_offset;
		dma_addr_t phys_addr;

		u32 frame_bpp;
		u32 frame_width;
		u32 frame_height;
		u32 frame_stride;

		bool active;
		bool always_on;
		bool dma_malloced;
		bool genlock;

		int irq;

		u32 eol_late_err;
		u32 err;
		u32 sof_late_err;
		u32 eol_early_err;
		u32 sof_early_err;
		u32 decode_err;
		u32 slave_err;
		u32 internal_err;
		u32 frame_cnt;
	} s2mm;

	void __iomem *regs;

	struct platform_device *pdev;
	struct device *dev;
	struct cdev cdev;
	struct class *pclass;

	dev_t node;

	u32 red_offset;
	u32 green_offset;
	u32 blue_offset;
	u32 alpha_offset;

	bool red_support;
	bool green_support;
	bool blue_support;
	bool alpha_support;
};

static u32 vdma_channels_probed = 0;
static struct vdma_channel *channels[MAX_VDMA_CHANNELS];
static struct platform_driver vdma_driver;
static struct file_operations vdma_fops;
static const struct of_device_id vdma_of_ids[];

static int vdma_init_cdevice(struct vdma_channel *ch);
static irqreturn_t mm2s_irq_handler(int irq, void *data);
static irqreturn_t s2mm_irq_handler(int irq, void *data);
static long vdma_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t vdma_read(struct file *file, char __user *buff, size_t len, loff_t *offset);
static int vdma_release(struct inode *ino, struct file *file);
static int vdma_open(struct inode *ino, struct file *file);
static void vdma_s2mm_stop(struct vdma_channel *ch);
static void vdma_mm2s_stop(struct vdma_channel *ch);
static void vdma_s2mm_start(struct vdma_channel *ch);
static void vdma_mm2s_start(struct vdma_channel *ch);
static void vdma_mm2s_clear_buffer(struct vdma_channel *ch);
static int vdma_mmap(struct file *file_p, struct vm_area_struct *vma);
static int vdma_probe(struct platform_device *pdev);
static int vdma_s2mm_probe(struct platform_device *pdev, struct vdma_channel *vdma,
								struct device_node *child);
static int vdma_mm2s_probe(struct platform_device *pdev, struct vdma_channel *vdma,
								struct device_node *child);

static int vdma_mmap(struct file *file_p, struct vm_area_struct *vma)
{
	struct vdma_channel *ch;

	u32 recv_size;
	u32 expected_size;

	ch = (struct vdma_channel *)file_p->private_data;
	if (ch->mm2s.active) {
		recv_size = vma->vm_end - vma->vm_start;
		expected_size = ch->mm2s.frame_stride *	ch->mm2s.frame_height;
		if (recv_size != expected_size) {
			dev_err(&ch->pdev->dev,	"invalid map size received (%u/%u)\n",
								recv_size, expected_size);
			return -EINVAL;
		}
		if (ch->mm2s.dma_malloced) {
			return dma_mmap_coherent(&ch->pdev->dev, vma, ch->mm2s.buffer,
							ch->mm2s.phys_addr, expected_size);
		}
		return vm_iomap_memory(vma, ch->mm2s.phys_addr, expected_size);
	}
	return -ENOTSUPP;
}

static void vdma_mm2s_clear_buffer(struct vdma_channel *ch)
{
	u32 i;
	u32 size;
	u32 *b;

	if (ch->mm2s.active) {
		b = (u32 *)ch->mm2s.buffer;
		size = (ch->mm2s.frame_stride * ch->mm2s.frame_height) / 4;
		for (i = 0; i < size; ++i) {
			b[i] = 0x00000000;
		}
	}
}

static void vdma_mm2s_start(struct vdma_channel *ch)
{
	u32 ctrl;

	if (ch->mm2s.active) {
		ctrl = 0;
		if (ch->mm2s.irq != -1) {
			ctrl |= VDMA_MM2S_ERR_IRQ_EN_BITMASK | VDMA_MM2S_FRAME_CNT_IRQ_EN_BITMASK;
		}
		if (ch->mm2s.interlaced) {
			ctrl |= VDMA_MM2S_CIRCULAR_PARK_BITMASK;
		}
		if (ch->mm2s.genlock) {
			ctrl |= VDMA_MM2S_CONTROL_GENLOCK_BITMASK;
		}
		ctrl |= VDMA_MM2S_CONTROL_RS_BITMASK;
		iowrite32(ctrl, ch->regs + VDMA_MM2S_CONTROL_REGISTER);
		iowrite32((ch->mm2s.frame_bpp / 8) * ch->mm2s.frame_width,
					ch->regs + VDMA_MM2S_HSIZE_REGISTER);
		if (ch->mm2s.interlaced) {
			iowrite32(ch->mm2s.frame_stride * 2,
					ch->regs + VDMA_MM2S_FRMDLY_STRIDE_REGISTER);
			iowrite32(ch->mm2s.phys_addr_even - ch->mm2s.phys_addr_offset,
					ch->regs + VDMA_MM2S_START_ADDRESS_REGISTER(0));
			iowrite32(ch->mm2s.phys_addr_odd - ch->mm2s.phys_addr_offset,
					ch->regs + VDMA_MM2S_START_ADDRESS_REGISTER(1));
			iowrite32(ch->mm2s.frame_height / 2, ch->regs + VDMA_MM2S_VSIZE_REGISTER);
		} else {
			iowrite32(ch->mm2s.frame_stride,
					ch->regs + VDMA_MM2S_FRMDLY_STRIDE_REGISTER);
			iowrite32(ch->mm2s.phys_addr - ch->mm2s.phys_addr_offset,
					ch->regs + VDMA_MM2S_START_ADDRESS_REGISTER(0));
			iowrite32(ch->mm2s.frame_height, ch->regs + VDMA_MM2S_VSIZE_REGISTER);
		}
	}
}

static void vdma_s2mm_start(struct vdma_channel *ch)
{
	u32 ctrl;

	if (ch->s2mm.active) {
		ctrl = 0;
		if (ch->s2mm.irq != -1) {
			ctrl |= VDMA_S2MM_ERR_IRQ_EN_BITMASK | VDMA_S2MM_FRAME_CNT_EN_BITMASK;
		}
		if (ch->s2mm.genlock) {
			ctrl |= VDMA_S2MM_CONTROL_GENLOCK_BITMASK;
		}
		ctrl |= VDMA_S2MM_CONTROL_RS_BITMASK;
		iowrite32(ctrl, ch->regs + VDMA_S2MM_CONTROL_REGISTER);
		iowrite32(ch->s2mm.phys_addr - ch->s2mm.phys_addr_offset,
					ch->regs + VDMA_S2MM_START_ADDRESS_REGISTER);
		iowrite32(ch->s2mm.frame_stride, ch->regs + VDMA_S2MM_FRMDLY_STRIDE_REGISTER);
		iowrite32((ch->s2mm.frame_bpp / 8) * ch->s2mm.frame_width,
					ch->regs + VDMA_S2MM_HSIZE_REGISTER);
		iowrite32(ch->s2mm.frame_height, ch->regs + VDMA_S2MM_VSIZE_REGISTER);
	}
}

static void vdma_mm2s_stop(struct vdma_channel *ch)
{
	if (ch->mm2s.active) {
		iowrite32(0x0, ch->regs + VDMA_MM2S_CONTROL_REGISTER);
	}
}

static void vdma_s2mm_stop(struct vdma_channel *ch)
{
	if (ch->mm2s.active) {
		iowrite32(0x0, ch->regs + VDMA_S2MM_CONTROL_REGISTER);
	}
}

static int vdma_open(struct inode *ino, struct file *file)
{
	u32 i;

	for (i = 0; i < vdma_channels_probed; ++i) {
		if (ino->i_rdev == channels[i]->node) {
			file->private_data = channels[i];
			if (!channels[i]->mm2s.always_on) {
				vdma_mm2s_clear_buffer(channels[i]);
				vdma_mm2s_start(channels[i]);
			}
			if (!channels[i]->s2mm.always_on) {
				vdma_s2mm_start(channels[i]);
			}
			return 0;
		}
	}
	return -ENOENT;
}

static int vdma_release(struct inode *ino, struct file *file)
{
	struct vdma_channel *ch;

	ch = (struct vdma_channel *)file->private_data;
	if (!ch->mm2s.always_on) {
		vdma_mm2s_stop(ch);
	}
	if (!ch->s2mm.always_on) {
		vdma_s2mm_stop(ch);
	}
	return 0;
}

static ssize_t vdma_read(struct file *file, char __user *buff, size_t len, loff_t *offset)
{
	struct vdma_channel *ch;
	unsigned long rc;

	ch = (struct vdma_channel *)file->private_data;
	if (ch->s2mm.active) {
		if (len != (ch->s2mm.frame_stride * ch->s2mm.frame_height)) {
			return -EINVAL;
		}
		rc = copy_to_user(buff, ch->s2mm.buffer, len);
		if (rc != 0) {
			return -EFAULT;
		}
		*offset += len;
		return len;
	}
	return -ENOTSUPP;
}

static long vdma_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vdma_channel *ch;

	struct vdma_status *status;
	struct fb_fix_screeninfo *finfo;
	struct fb_var_screeninfo *vinfo;

	u32 reg;

	ch = (struct vdma_channel *)file->private_data;
	switch (cmd) {
		case FBIOGET_FSCREENINFO:
			finfo = (struct fb_fix_screeninfo *)arg;

			(void)memset(finfo, 0, sizeof(*finfo));
			(void)strcpy(finfo->id, "VDMA DR driver");
			if (ch->mm2s.active) {
				finfo->smem_start = ch->mm2s.phys_addr;
				finfo->smem_len = ch->mm2s.frame_stride * ch->mm2s.frame_height;
				finfo->type = FB_TYPE_PACKED_PIXELS;
				finfo->visual = FB_VISUAL_TRUECOLOR;
				finfo->line_length = ch->mm2s.frame_stride;
			}
			return 0;

		case FBIOGET_VSCREENINFO:
			vinfo = (struct fb_var_screeninfo *)arg;

			(void)memset(vinfo, 0, sizeof(*vinfo));
			if (ch->mm2s.active) {
				vinfo->xres         = ch->mm2s.frame_width;
				vinfo->yres         = ch->mm2s.frame_height;
				vinfo->xres_virtual = ch->mm2s.frame_width;
				vinfo->yres_virtual = ch->mm2s.frame_height;

				vinfo->bits_per_pixel = ch->mm2s.frame_bpp;

				vinfo->red.offset    = ch->red_offset;
				vinfo->red.length    = (ch->red_support ? 8 : 0);
				vinfo->green.offset  = ch->green_offset;
				vinfo->green.length  = (ch->green_support ? 8 : 0);
				vinfo->blue.offset   = ch->blue_offset;
				vinfo->blue.length   = (ch->blue_support ? 8 : 0);
				vinfo->transp.offset = ch->alpha_offset;
				vinfo->transp.length = (ch->alpha_support ? 8 : 0);

				if (ch->mm2s.frame_ppi != 0) {
					vinfo->height = (ch->mm2s.frame_height * 25) /
										ch->mm2s.frame_ppi;
					vinfo->width = (ch->mm2s.frame_width * 25) /
										ch->mm2s.frame_ppi;
				} else {
					vinfo->height = 0;
					vinfo->width = 0;
				}
				vinfo->pixclock = 80;
				vinfo->sync     = FB_SYNC_EXT;
				vinfo->vmode    = FB_VMODE_NONINTERLACED;
			}
			return 0;

		case FBIOBLANK:
			switch(arg) {
				case VESA_POWERDOWN:
					if (!ch->mm2s.always_on) {
						vdma_mm2s_stop(ch);
					}
					if (!ch->s2mm.always_on) {
						vdma_s2mm_stop(ch);
					}
					break;

				case VESA_NO_BLANKING:
					if (!ch->mm2s.always_on) {
						vdma_mm2s_start(ch);
					}
					if (!ch->s2mm.always_on) {
						vdma_s2mm_start(ch);
					}
					break;

				default:
					dev_err(&ch->pdev->dev, "unsupported arg for fbioblank: %lx\n", arg);
					return -ENOTSUPP;
			}
			return 0;

		case VDMA_CMD_GET_STATUS:
			status = (struct vdma_status *)arg;
			(void)strncpy(status->name, ch->pdev->name, sizeof(status->name));

			reg = ioread32(ch->regs + VDMA_MM2S_STATUS_REGISTER);
			if (ch->mm2s.irq != -1) {
				status->mm2s.err = ch->mm2s.err;
				status->mm2s.sof_early_err = ch->mm2s.sof_early_err;
				status->mm2s.decode_err = ch->mm2s.decode_err;
				status->mm2s.slave_err = ch->mm2s.slave_err;
				status->mm2s.internal_err = ch->mm2s.internal_err;
				status->mm2s.frame_cnt = ch->mm2s.frame_cnt;
			} else {
				status->mm2s.err =
					(((reg & VDMA_MM2S_STATUS_ERR_IRQ_BITMASK) != 0) ? 1 : 0);
				status->mm2s.sof_early_err =
					(((reg & VDMA_MM2S_STATUS_SOF_EARLY_ERR_BITMASK) != 0) ? 1 : 0);
				status->mm2s.decode_err =
					(((reg & VDMA_MM2S_STATUS_DEC_ERR_BITMASK) != 0) ? 1 : 0);
				status->mm2s.slave_err =
					(((reg & VDMA_MM2S_STATUS_SLV_ERR_BITMASK) != 0) ? 1 : 0);
				status->mm2s.internal_err =
					(((reg & VDMA_MM2S_STATUS_INT_ERR_BITMASK) != 0) ? 1 : 0);
				status->mm2s.frame_cnt = 0;
			}
			status->mm2s.halted = ((reg &
						VDMA_MM2S_STATUS_HALTED_BITMASK) != 0);

			reg = ioread32(ch->regs + VDMA_VERSION_REGISTER);
			status->version.major = ((reg & VDMA_VERSION_MAJOR_BITMASK) >> 28);
			status->version.minor = ((reg & VDMA_VERSION_MINOR_BITMASK) >> 20);
			status->version.revision = ((reg &
						VDMA_VERSION_REVISION_BITMASK) >> 16);
			status->version.xil_internal = (reg & VDMA_VERSION_XIL_INTERN_BITMASK);

			reg = ioread32(ch->regs + VDMA_S2MM_STATUS_REGISTER);
			if (ch->s2mm.irq != -1) {
				status->s2mm.eol_late_err = ch->s2mm.eol_late_err;
				status->s2mm.err = ch->s2mm.err;
				status->s2mm.sof_late_err = ch->s2mm.sof_late_err;
				status->s2mm.eol_early_err = ch->s2mm.eol_early_err;
				status->s2mm.sof_early_err = ch->s2mm.sof_early_err;
				status->s2mm.decode_err = ch->s2mm.decode_err;
				status->s2mm.slave_err = ch->s2mm.slave_err;
				status->s2mm.internal_err = ch->s2mm.internal_err;
				status->s2mm.frame_cnt = ch->s2mm.frame_cnt;
			} else {
				status->s2mm.eol_late_err =
					(((reg & VDMA_S2MM_STATUS_EOL_LATE_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.err =
					(((reg & VDMA_S2MM_STATUS_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.sof_late_err =
					(((reg & VDMA_S2MM_STATUS_SOF_LATE_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.eol_early_err =
					(((reg & VDMA_S2MM_STATUS_EOL_EARLY_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.sof_early_err =
					(((reg & VDMA_S2MM_STATUS_SOF_EARLY_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.decode_err =
					(((reg & VDMA_S2MM_STATUS_DEC_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.slave_err =
					(((reg & VDMA_S2MM_STATUS_SLV_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.internal_err =
					(((reg & VDMA_S2MM_STATUS_INT_ERR_BITMASK) != 0) ? 1 : 0);
				status->s2mm.frame_cnt = 0;
			}
			status->s2mm.halted = ((reg &
						VDMA_S2MM_STATUS_HALTED_BITMASK) != 0);

			reg = ioread32(ch->regs + VDMA_MM2S_VSIZE_REGISTER);
			status->mm2s.size.vertical = (reg & VDMA_MM2S_VSIZE_LINES_BITMASK);

			reg = ioread32(ch->regs + VDMA_MM2S_HSIZE_REGISTER);
			status->mm2s.size.horizontal = (reg & VDMA_MM2S_HSIZE_BYTES_BITMASK);

			reg = ioread32(ch->regs + VDMA_MM2S_FRMDLY_STRIDE_REGISTER);
			status->mm2s.size.stride = (reg & VDMA_MM2S_STRIDE_BYTES_BITMASK);

			reg = ioread32(ch->regs + VDMA_S2MM_VSIZE_REGISTER);
			status->s2mm.size.vertical = (reg & VDMA_S2MM_VSIZE_LINES_BITMASK);

			reg = ioread32(ch->regs + VDMA_S2MM_HSIZE_REGISTER);
			status->s2mm.size.horizontal = (reg & VDMA_S2MM_HSIZE_BYTES_BITMASK);

			reg = ioread32(ch->regs + VDMA_S2MM_FRMDLY_STRIDE_REGISTER);
			status->s2mm.size.stride = (reg & VDMA_S2MM_STRIDE_BYTES_BITMASK);

			return 0;
	}
	dev_err(&ch->pdev->dev, "unsupported ioctl command: %x\n", cmd);
	return -ENOTSUPP;
}

static irqreturn_t s2mm_irq_handler(int irq, void *data)
{
	struct vdma_channel *ch;

	u32 status;
	u32 clr_status;
	u32 ctrl;

	ch = data;
	if (irq != ch->s2mm.irq) {
		return IRQ_NONE;
	}
	clr_status = 0;
	status = ioread32(ch->regs + VDMA_S2MM_STATUS_REGISTER);
	if ((status & VDMA_S2MM_STATUS_EOL_LATE_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm eol late error\n");
		++ch->s2mm.eol_late_err;
		clr_status |= VDMA_S2MM_STATUS_EOL_LATE_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm error\n");
		++ch->s2mm.err;
		clr_status |= VDMA_S2MM_STATUS_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_SOF_LATE_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm sof late\n");
		++ch->s2mm.sof_late_err;
		clr_status |= VDMA_S2MM_STATUS_SOF_LATE_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_EOL_EARLY_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm eol early\n");
		++ch->s2mm.eol_early_err;
		clr_status |= VDMA_S2MM_STATUS_EOL_EARLY_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_SOF_EARLY_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm sof early\n");
		++ch->s2mm.sof_early_err;
		clr_status |= VDMA_S2MM_STATUS_SOF_EARLY_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_DEC_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm decode error\n");
		++ch->s2mm.decode_err;
		clr_status |= VDMA_S2MM_STATUS_DEC_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_SLV_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm slave error\n");
		++ch->s2mm.slave_err;
		clr_status |= VDMA_S2MM_STATUS_SLV_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_INT_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "s2mm internal error\n");
		++ch->s2mm.internal_err;
		clr_status |= VDMA_S2MM_STATUS_INT_ERR_BITMASK;
	}
	if ((status & VDMA_S2MM_STATUS_FRAME_CNT_BITMASK) != 0) {
		++ch->s2mm.frame_cnt;
		clr_status |= VDMA_S2MM_STATUS_FRAME_CNT_BITMASK;
	}
	iowrite32(clr_status, ch->regs + VDMA_S2MM_STATUS_REGISTER);
	ctrl = ioread32(ch->regs + VDMA_S2MM_CONTROL_REGISTER);
	ctrl |= VDMA_S2MM_CONTROL_RS_BITMASK;
	iowrite32(ctrl, ch->regs + VDMA_S2MM_CONTROL_REGISTER);
	return IRQ_HANDLED;
}

static irqreturn_t mm2s_irq_handler(int irq, void *data)
{
	struct vdma_channel *ch;

	u32 status;
	u32 ctrl;
	u32 clr_status;

	ch = data;
	if (irq != ch->mm2s.irq) {
		return IRQ_NONE;
	}
	clr_status = 0;
	status = ioread32(ch->regs + VDMA_MM2S_STATUS_REGISTER);
	if ((status & VDMA_MM2S_STATUS_ERR_IRQ_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "mm2s error\n");
		++ch->mm2s.err;
		clr_status |= VDMA_MM2S_STATUS_ERR_IRQ_BITMASK;
	}
	if ((status & VDMA_MM2S_STATUS_SOF_EARLY_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "mm2s sof early error\n");
		++ch->mm2s.sof_early_err;
		clr_status |= VDMA_MM2S_STATUS_SOF_EARLY_ERR_BITMASK;
	}
	if ((status & VDMA_MM2S_STATUS_DEC_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "mm2s decode error\n");
		++ch->mm2s.decode_err;
		clr_status |= VDMA_MM2S_STATUS_DEC_ERR_BITMASK;
	}
	if ((status & VDMA_MM2S_STATUS_SLV_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "mm2s slave error\n");
		++ch->mm2s.slave_err;
		clr_status |= VDMA_MM2S_STATUS_SLV_ERR_BITMASK;
	}
	if ((status & VDMA_MM2S_STATUS_INT_ERR_BITMASK) != 0) {
		dev_err(&ch->pdev->dev, "mm2s internal error\n");
		++ch->mm2s.internal_err;
		clr_status |= VDMA_MM2S_STATUS_INT_ERR_BITMASK;
	}
	if ((status & VDMA_MM2S_STATUS_FRAME_CNT_BITMASK) != 0) {
		++ch->mm2s.frame_cnt;
		clr_status |= VDMA_MM2S_STATUS_FRAME_CNT_BITMASK;
	}
	iowrite32(clr_status, ch->regs + VDMA_MM2S_STATUS_REGISTER);
	ctrl = ioread32(ch->regs + VDMA_MM2S_CONTROL_REGISTER);
	ctrl |= VDMA_MM2S_CONTROL_RS_BITMASK;
	iowrite32(ctrl, ch->regs + VDMA_MM2S_CONTROL_REGISTER);
	return IRQ_HANDLED;
}

static int vdma_init_cdevice(struct vdma_channel *ch)
{
	int rc;
	size_t i;
	const char *dev_name = NULL;
	struct class *pclass = NULL;

	for (i = 0; i < strlen(ch->pdev->name); ++i) {
		// EX format: b0100000.v_vdma
		if(ch->pdev->name[i] == '.') {
			dev_name = &ch->pdev->name[i + 1];
		}
	}
	for (i = 0; i < MAX_VDMA_CHANNELS; ++i) {
		if ((channels[i] != NULL) && (channels[i]->pclass != NULL)) {
			pclass = channels[i]->pclass;
			break;
		}
	}
	if (dev_name == NULL) {
		dev_name = ch->pdev->name;
	}

	rc = alloc_chrdev_region(&ch->node, 0, 1, dev_name);
	if (rc != 0) {
		dev_err(&ch->pdev->dev, "unable to get a char device number\n");
		return rc;
	}
	cdev_init(&ch->cdev, &vdma_fops);
	ch->cdev.owner = THIS_MODULE;

	rc = cdev_add(&ch->cdev, ch->node, 1);
	if (rc != 0) {
		dev_err(&ch->pdev->dev, "unable to add char device\n");
		return rc;
	}

	if (pclass != NULL) {
		ch->pclass = pclass;
	} else {
		ch->pclass = class_create(THIS_MODULE, "vdma");
	}
	if (IS_ERR(ch->pclass)) {
		dev_err(&ch->pdev->dev, "unable to create class\n");
		return PTR_ERR(ch->pclass);
	}
	if (ch->pclass == NULL) {
		dev_err(&ch->pdev->dev, "unable to create class\n");
		return -ENOMEM;
	}
	ch->dev = device_create(ch->pclass, NULL, ch->node, NULL, dev_name);
	if (IS_ERR(ch->dev)) {
		dev_err(&ch->pdev->dev, "unable to create the device\n");
		return PTR_ERR(ch->dev);
	}
	if (ch->dev == NULL) {
		dev_err(&ch->pdev->dev, "unable to create the device\n");
		return -ENOMEM;
	}
	return 0;
}

static int vdma_mm2s_probe(struct platform_device *pdev, struct vdma_channel *vdma,
								struct device_node *child)
{
	int rc;

	bool irq;

	struct device_node *mem_node;
	struct resource mem_res;
	u32 mem_size;

	vdma->mm2s.active = true;
	vdma->mm2s.err = 0;
	vdma->mm2s.sof_early_err = 0;
	vdma->mm2s.decode_err = 0;
	vdma->mm2s.slave_err = 0;
	vdma->mm2s.internal_err = 0;
	vdma->mm2s.frame_cnt = 0;

	rc = of_property_read_u32(child, "frame-bpp", &vdma->mm2s.frame_bpp);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing mm2s frame-bpp property\n");
		return rc;
	}
	rc = of_property_read_u32(child, "frame-width", &vdma->mm2s.frame_width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing mm2s frame-width property\n");
		return rc;
	}
	rc = of_property_read_u32(child, "frame-stride", &vdma->mm2s.frame_stride);
	if (rc < 0) {
		vdma->mm2s.frame_stride = vdma->mm2s.frame_width * (vdma->mm2s.frame_bpp / 8);
	}
	rc = of_property_read_u32(child, "frame-height", &vdma->mm2s.frame_height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing mm2s frame-height property\n");
		return rc;
	}
	rc = of_property_read_u32(child, "frame-ppi", &vdma->mm2s.frame_ppi);
	if (rc < 0) {
		vdma->mm2s.frame_ppi = 0;
	}
	vdma->mm2s.always_on = of_property_read_bool(child, "always-on");
	vdma->mm2s.interlaced = of_property_read_bool(child, "interlaced");
	vdma->mm2s.genlock = of_property_read_bool(child, "genlock");
	vdma->mm2s.phys_addr_offset = 0;
	mem_node = of_parse_phandle(child, "memory-block", 0);
	if ((mem_node != NULL) && (IS_ERR(mem_node) == false)) {
		rc = of_address_to_resource(mem_node, 0, &mem_res);
		if (rc == 0) {
			vdma->mm2s.phys_addr_offset = mem_res.start;
		}
	}
	mem_node = of_parse_phandle(child, "memory-addr", 0);
	mem_size = vdma->mm2s.frame_stride * vdma->mm2s.frame_height;
	if ((mem_node != NULL) && (IS_ERR(mem_node) == false)) {
		rc = of_address_to_resource(mem_node, 0, &mem_res);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot map memory resource for mm2s\n");
			return rc;
		}
		if (resource_size(&mem_res) != mem_size) {
			dev_err(&pdev->dev,
				"invalid mapped mm2s memory size: %llu/%u\n",
				resource_size(&mem_res), mem_size);
			return -EINVAL;
		}
		vdma->mm2s.phys_addr = mem_res.start;
		vdma->mm2s.buffer = (u8 *)memremap(
						mem_res.start, mem_size, MEMREMAP_WB);
		vdma->mm2s.dma_malloced = false;
	} else {
		vdma->mm2s.buffer = (u8 *)dmam_alloc_coherent(&pdev->dev,
				mem_size, &vdma->mm2s.phys_addr, GFP_KERNEL);
		vdma->mm2s.dma_malloced = true;
	}
	if (vdma->mm2s.buffer == NULL) {
		dev_err(&pdev->dev, "cannot allocate vdma mm2s memory\n");
		return -ENOMEM;
	}
	if (IS_ERR(vdma->mm2s.buffer)) {
		dev_err(&pdev->dev, "cannot allocate vdma mm2s memory\n");
		return PTR_ERR(vdma->mm2s.buffer);
	}
	if (vdma->mm2s.interlaced) {
		vdma->mm2s.buffer_even = vdma->mm2s.buffer;
		vdma->mm2s.buffer_odd = vdma->mm2s.buffer + vdma->mm2s.frame_stride;
		vdma->mm2s.phys_addr_even = vdma->mm2s.phys_addr;
		vdma->mm2s.phys_addr_odd = vdma->mm2s.phys_addr + (dma_addr_t)vdma->mm2s.frame_stride;
	} else {
		vdma->mm2s.buffer_even = NULL;
		vdma->mm2s.buffer_odd = NULL;
		vdma->mm2s.phys_addr_even = 0;
		vdma->mm2s.phys_addr_odd = 0;
	}
	irq = of_property_read_bool(child, "interrupts");
	if (irq) {
		vdma->mm2s.irq = irq_of_parse_and_map(child, 0);
		rc = request_irq(vdma->mm2s.irq, mm2s_irq_handler, IRQF_SHARED,
							"datarespons-mm2s", vdma);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot map interrupt\n");
			return rc;
		}
	} else {
		vdma->mm2s.irq = -1;
	}
	dev_info(&pdev->dev, "mm2s frame %u(%u)x%u %ubpp%s%s%s%s\n",
		vdma->mm2s.frame_width, vdma->mm2s.frame_stride, vdma->mm2s.frame_height,
		vdma->mm2s.frame_bpp, vdma->mm2s.interlaced ? " interlaced" : "",
		vdma->mm2s.always_on ? " ON" : "", irq ? " IRQ" : "",
		vdma->mm2s.genlock ? " genlock" : "");
	return 0;
}

static int vdma_s2mm_probe(struct platform_device *pdev, struct vdma_channel *vdma,
								struct device_node *child)
{
	int rc;

	bool irq;

	struct device_node *mem_node;
	struct resource mem_res;
	u32 mem_size;

	vdma->s2mm.active = true;
	vdma->s2mm.eol_late_err = 0;
	vdma->s2mm.err = 0;
	vdma->s2mm.sof_late_err = 0;
	vdma->s2mm.eol_early_err = 0;
	vdma->s2mm.sof_early_err = 0;
	vdma->s2mm.decode_err = 0;
	vdma->s2mm.slave_err = 0;
	vdma->s2mm.internal_err = 0;
	vdma->s2mm.frame_cnt = 0;

	rc = of_property_read_u32(child, "frame-bpp", &vdma->s2mm.frame_bpp);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing s2mm frame-bpp property\n");
		return rc;
	}
	rc = of_property_read_u32(child, "frame-width", &vdma->s2mm.frame_width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing s2mm frame-width property\n");
		return rc;
	}
	rc = of_property_read_u32(child, "frame-stride", &vdma->s2mm.frame_stride);
	if (rc < 0) {
		vdma->s2mm.frame_stride = vdma->s2mm.frame_width * (vdma->s2mm.frame_bpp / 8);
	}
	rc = of_property_read_u32(child, "frame-height", &vdma->s2mm.frame_height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing s2mm frame-height property\n");
		return rc;
	}
	vdma->s2mm.always_on = of_property_read_bool(child, "always-on");
	vdma->s2mm.genlock = of_property_read_bool(child, "genlock");
	vdma->s2mm.phys_addr_offset = 0;
	mem_node = of_parse_phandle(child, "memory-block", 0);
	if ((mem_node != NULL) && (IS_ERR(mem_node) == false)) {
		rc = of_address_to_resource(mem_node, 0, &mem_res);
		if (rc == 0) {
			vdma->s2mm.phys_addr_offset = mem_res.start;
		}
	}
	mem_node = of_parse_phandle(child, "memory-addr", 0);
	mem_size = vdma->s2mm.frame_stride * vdma->s2mm.frame_height;
	if ((mem_node != NULL) && (IS_ERR(mem_node) == false)) {
		rc = of_address_to_resource(mem_node, 0, &mem_res);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot map memory resource for s2mm\n");
			return rc;
		}
		if (resource_size(&mem_res) != mem_size) {
			dev_err(&pdev->dev,
				"invalid s2mm memory size: %llu/%u\n",
				resource_size(&mem_res), mem_size);
			return -EINVAL;
		}
		vdma->s2mm.phys_addr = mem_res.start;
		vdma->s2mm.buffer = (u8 *)memremap(
					mem_res.start, mem_size, MEMREMAP_WB);
		vdma->s2mm.dma_malloced = false;
	} else {
		vdma->s2mm.buffer = (u8 *)dmam_alloc_coherent(&pdev->dev,
					mem_size, &vdma->s2mm.phys_addr, GFP_KERNEL);
		vdma->s2mm.dma_malloced = true;
	}
	if (vdma->s2mm.buffer == NULL) {
		dev_err(&pdev->dev, "cannot allocate vdma s2mm memory\n");
		return -ENOMEM;
	}
	if (IS_ERR(vdma->s2mm.buffer)) {
		dev_err(&pdev->dev, "cannot allocate vdma s2mm memory\n");
		return PTR_ERR(vdma->s2mm.buffer);
	}
	irq = of_property_read_bool(child, "interrupts");
	if (irq) {
		vdma->s2mm.irq = irq_of_parse_and_map(child, 0);
		rc = request_irq(vdma->s2mm.irq, s2mm_irq_handler, IRQF_SHARED,
							"datarespons-s2mm", vdma);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot map interrupt\n");
			return rc;
		}
	} else {
		vdma->s2mm.irq = -1;
	}
	dev_info(&pdev->dev, "s2mm frame %u(%u)x%u %ubpp%s%s%s\n",
			vdma->s2mm.frame_width, vdma->s2mm.frame_stride, vdma->s2mm.frame_height,
			vdma->s2mm.frame_bpp, vdma->s2mm.always_on ? " on" : "",
			irq ? " irq" : "", vdma->s2mm.genlock ? " genlock" : "");
	return 0;
}

static int vdma_probe(struct platform_device *pdev)
{
	int rc;
	int i;
	const char *color_format;

	struct vdma_channel *vdma;
	struct device_node *node;
	struct device_node *child;

	node = pdev->dev.of_node;
	if (vdma_channels_probed >= MAX_VDMA_CHANNELS) {
		dev_err(&pdev->dev, "vdma channel is out of bounds\n");
		return -ERANGE;
	}
	vdma = kzalloc(sizeof(struct vdma_channel), GFP_KERNEL);
	if (IS_ERR(vdma)) {
		dev_err(&pdev->dev, "cannot allocate memory for vdma driver\n");
		return PTR_ERR(vdma);
	}
	if (vdma == NULL) {
		dev_err(&pdev->dev, "cannot allocate memory for vdma driver\n");
		return -ENOMEM;
	}
	vdma->pdev = pdev;
	vdma->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(vdma->regs) || (vdma->regs == NULL)) {
		dev_err(&pdev->dev, "cannot map registers\n");
		return -EINVAL;
	}
	child = of_get_child_by_name(node, "mm2s");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = vdma_mm2s_probe(pdev, vdma, child);
		if (rc != 0) {
			return rc;
		}
	}
	child = of_get_child_by_name(node, "s2mm");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = vdma_s2mm_probe(pdev, vdma, child);
		if (rc != 0) {
			return rc;
		}
	}
	rc = of_property_read_string(node, "color-format", &color_format);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format property\n");
		return rc;
	}
	vdma->red_offset   = 0;
	vdma->green_offset = 0;
	vdma->blue_offset  = 0;
	vdma->alpha_offset = 0;
	vdma->red_support   = false;
	vdma->green_support = false;
	vdma->blue_support  = false;
	vdma->alpha_support = false;
	for (i = 0; i < strlen(color_format); ++i) {
		switch(color_format[i])
		{
			case 'r':
				vdma->red_offset  = (i * 8);
				vdma->red_support = true;
				break;
			case 'g':
				vdma->green_offset  = (i * 8);
				vdma->green_support = true;
				break;
			case 'b':
				vdma->blue_offset  = (i * 8);
				vdma->blue_support = true;
				break;
			case 'a':
				vdma->alpha_offset  = (i * 8);
				vdma->alpha_support = true;
				break;
		}
	}
	dev_info(&pdev->dev, "color-format %s(%u) %s(%u) %s(%u) %s(%u)\n",
			(vdma->red_support   ? "r" : "-"), vdma->red_offset,
			(vdma->green_support ? "g" : "-"), vdma->green_offset,
			(vdma->blue_support  ? "b" : "-"), vdma->blue_offset,
			(vdma->alpha_support ? "a" : "-"), vdma->alpha_offset);
	if ((vdma->red_offset >= 32) || (vdma->green_offset >= 32) ||
		(vdma->blue_offset >= 32) || (vdma->alpha_offset >= 32)) {
		dev_err(&pdev->dev, "color-format is not supported\n");
		return -ENOTSUPP;
	}
	if (vdma->s2mm.always_on) {
		vdma_s2mm_start(vdma);
	}
	if (vdma->mm2s.always_on) {
		vdma_mm2s_start(vdma);
	}
	platform_set_drvdata(pdev, vdma);
	rc = vdma_init_cdevice(vdma);
	if (rc != 0) {
		dev_err(&pdev->dev, "cannot init char device\n");
		return rc;
	}
	dev_info(&pdev->dev, "vdma %u ready\n", vdma_channels_probed);

	channels[vdma_channels_probed] = vdma;
	++vdma_channels_probed;
	return 0;
}

static int vdma_remove(struct platform_device *pdev)
{
	u32 i;

	for (i = 0; i < vdma_channels_probed; ++i) {
		if (channels[i]->pdev == pdev) {
			vdma_mm2s_stop(channels[i]);
			vdma_s2mm_stop(channels[i]);
		}
	}
	return 0;
}

u32 vdma_mm2s_get_px_width(struct vdma_channel *ch)
{
	return ch->mm2s.frame_width;
}

u32 vdma_mm2s_get_stride(struct vdma_channel *ch)
{
	return ch->mm2s.frame_stride;
}

u32 vdma_mm2s_get_px_height(struct vdma_channel *ch)
{
	return ch->mm2s.frame_height;
}

u32 vdma_mm2s_get_bit_per_px(struct vdma_channel *ch)
{
	return ch->mm2s.frame_bpp;
}

u32 vdma_mm2s_get_px_per_inch(struct vdma_channel *ch)
{
	return ch->mm2s.frame_ppi;
}

int vdma_mm2s_get_fb_addr(struct vdma_channel *ch, dma_addr_t *paddr, void **vaddr)
{
	*paddr = ch->mm2s.phys_addr;
	*vaddr = ch->mm2s.buffer;
	return 0;
}

int vdma_get_px_format(struct vdma_channel *ch, u32 *fmt)
{
	if (ch->red_support && ch->green_support && ch->blue_support && ch->alpha_support) {
		if ((ch->red_offset == 0) && (ch->green_offset == 8) &&
		    (ch->blue_offset == 16) && (ch->alpha_offset == 24)) {
			*fmt = MEDIA_BUS_FMT_ARGB8888_1X32;
			return 0;
		}
	}
	dev_err(&ch->pdev->dev, "unrecognized drm pixel format r(%d)g(%d)b(%d)a(%d)\n",
					(ch->red_support   ? ch->red_offset   : -1),
					(ch->green_support ? ch->green_offset : -1),
					(ch->blue_support  ? ch->blue_offset  : -1),
					(ch->alpha_support ? ch->alpha_offset : -1));
	return -EINVAL;
}

int vdma_mm2s_set_data(struct vdma_channel *ch, const void *buffer, u32 size)
{
	u32 expected_size;

	expected_size = ch->mm2s.frame_width * ch->mm2s.frame_height *
						(ch->mm2s.frame_bpp / 8);
	if (expected_size != size) {
		dev_err(&ch->pdev->dev, "unexpected size %u/%u\n", size, expected_size);
		return -EINVAL;
	}
	(void)memcpy(ch->mm2s.buffer, buffer, size);
	return 0;
}

const char *vdma_get_name(struct vdma_channel *ch)
{
	return ch->pdev->name;
}

static struct file_operations vdma_fops = {
	.owner   = THIS_MODULE,
	.open    = vdma_open,
	.release = vdma_release,
	.mmap    = vdma_mmap,
	.read    = vdma_read,
	.unlocked_ioctl = vdma_ioctl
};

static const struct of_device_id vdma_of_ids[] = {
	{ .compatible = "datarespons,vdma",},
	{}
};

static struct platform_driver vdma_driver = {
	.driver = {
		.name = "vdma_driver",
		.owner = THIS_MODULE,
		.of_match_table = vdma_of_ids,
	},
	.probe = vdma_probe,
	.remove = vdma_remove,
};

module_platform_driver(vdma_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("VDMA Driver");
MODULE_LICENSE("PROPRIETARY");
