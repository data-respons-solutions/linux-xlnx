#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/fs.h>
#include <linux/component.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_fourcc.h>

#include "video-drm.h"
#include "video-vdma.h"

#define VIDEO_SYNC_CONTROL_REGISTER		0x00
#define VIDEO_SYNC_STATUS_REGISTER		0x04
#define VIDEO_SYNC_TOTAL_SIZE_REGISTER		0x08
#define VIDEO_SYNC_ACTIVE_SIZE_REGISTER		0x0C
#define VIDEO_SYNC_DELAY_LINES_REGISTER		0x10
#define VIDEO_SYNC_IRQ_ENABLE_REGISTER		0x14
#define VIDEO_SYNC_GLOBAL_ALPHA_REGISTER	0x18
#define VIDEO_SYNC_FW_VERSION_REGISTER		0x1C

#define VIDEO_SYNC_CTRL_RUN_MASK	(1 <<  0)
#define VIDEO_SYNC_CTRL_EXT_SYNC_MASK	(1 <<  1)
#define VIDEO_SYNC_CTRL_ILACE_MASK	(1 <<  2)
#define VIDEO_SYNC_CTRL_IRQEN_MASK	(1 <<  3)
#define VIDEO_SYNC_CTRL_VIDEO_EN_MASK	(1 <<  4)
#define VIDEO_SYNC_CTRL_DYN_BLEND_MASK	(1 <<  5)
#define VIDEO_SYNC_CTRL_SLAVE_VDMA_MASK	(1 <<  6)
#define VIDEO_SYNC_CTRL_AUXRST_MASK	(1 << 29)
#define VIDEO_SYNC_CTRL_CLRIRQ_MASK	(1 << 30)
#define VIDEO_SYNC_CTRL_SW_RST_MASK	(1 << 31)

#define VIDEO_SYNC_STATUS_PL_ERR_MASK		(1    <<  0)
#define VIDEO_SYNC_STATUS_SOF_ERR_MASK		(1    <<  1)
#define VIDEO_SYNC_STATUS_URUN_MASK		(1    <<  2)
#define VIDEO_SYNC_STATUS_SYNC_TO_MASK		(1    <<  3)
#define VIDEO_SYNC_STATUS_FRAME_DONE_MASK	(1    <<  4)
#define VIDEO_SYNC_STATUS_SHORT_LINE_ERR_MASK	(1    <<  5)
#define VIDEO_SYNC_STATUS_LONG_LINE_ERR_MASK	(1    <<  6)
#define VIDEO_SYNC_STATUS_SYNC_TX_MASK		(1    <<  7)
#define VIDEO_SYNC_STATUS_SYNC_FIELD_MASK	(1    <<  8)
#define VIDEO_SYNC_STATUS_COMPLETED_FIELD_MASK	(1    <<  9)
#define VIDEO_SYNC_STATUS_FIELD_EXT_MASK	(1    << 10)
#define VIDEO_SYNC_STATUS_DMA_BUF_INDEX_MASK	(0x1F << 11)
#define VIDEO_SYNC_STATUS_PL_LATE_MASK		(1    << 24)

#define VIDEO_SYNC_TOTAL_SIZE_WIDTH_MASK	(0xFFF <<  0)
#define VIDEO_SYNC_TOTAL_SIZE_HEIGHT_MASK	(0xFFF << 16)

#define VIDEO_SYNC_ACTIVE_SIZE_WIDTH_MASK	(0xFFF <<  0)
#define VIDEO_SYNC_ACTIVE_SIZE_HEIGHT_MASK	(0xFFF << 16)

#define VIDEO_SYNC_DELAY_EXT_SYNC_MASK	(0xFFF <<  0)
#define VIDEO_SYNC_DELAY_PL_MASK	(0xFFF << 16)

#define VIDEO_SYNC_IRQ_PL_ERR_MASK	(1 << 0)
#define VIDEO_SYNC_IRQ_SOF_ERR_MASK	(1 << 1)
#define VIDEO_SYNC_IRQ_URUN_MASK	(1 << 2)
#define VIDEO_SYNC_IRQ_SYNC_TO_MASK	(1 << 3)
#define VIDEO_SYNC_IRQ_FRAME_DONE_MASK	(1 << 4)
#define VIDEO_SYNC_IRQ_ERR_SHORT_MASK	(1 << 5)
#define VIDEO_SYNC_IRQ_ERR_LONG_MASK	(1 << 6)
#define VIDEO_SYNC_IRQ_FRAME_SYNC_MASK	(1 << 7)

#define MAX_VIDEO_SYNCHRONIZERS	4

#define VIDEO_SYNC_CMD_GET_INFO		0x445201
#define VIDEO_SYNC_CMD_SET_INFO		0x445202
#define VIDEO_SYNC_CMD_CTL_AUXRST	0x445203
#define VIDEO_SYNC_CMD_CTL_SW_RST	0x445204

MODULE_LICENSE("PROPRIETARY");

struct video_sync_info {
	char name[64];

	bool running;
	bool ext_sync;
	bool ilace;
	bool irqen;
	bool videoen;
	bool dyn_blend;
	bool slave_vdma;
	bool sync_field;
	bool field_ext;
	bool pl_late;

	bool pl_err_irq_enabled;
	bool sof_err_irq_enabled;
	bool urun_err_irq_enabled;
	bool sync_to_err_irq_enabled;
	bool frame_done_irq_enabled;
	bool err_short_irq_enabled;
	bool err_long_irq_enabled;
	bool frame_sync_irq_enabled;

	u32 version;

	u32 pl_err;
	u32 sof_err;
	u32 urun_err;
	u32 sync_to_err;
	u32 short_line_err;
	u32 long_line_err;
	u32 frames_done;
	u32 even_frames_done;
	u32 odd_frames_done;

	u32 dma_buf_index;

	u32 total_width;
	u32 total_height;
	u32 active_width;
	u32 active_height;
	u32 delay_ext_sync;
	u32 delay_pl;
};

struct video_sync_plane {
	struct drm_plane base;
	struct vdma_channel *vdma;
	u32 format;
};

struct video_sync {
	void __iomem *regs;
	struct platform_device *pdev;

	int irq;
	dev_t node;
	struct cdev cdev;
	struct device *dev;
	struct class *pclass;

	bool interlaced;
	bool vblank_enabled;

	u32 pl_err;
	u32 sof_err;
	u32 urun_err;
	u32 sync_to_err;
	u32 short_line_err;
	u32 long_line_err;
	u32 frames_done;
	u32 even_frames_done;
	u32 odd_frames_done;

	struct platform_device *master;
	struct video_crtc crtc;
	struct drm_device *drm;
	struct vdma_channel *video_vdma;

	struct video_sync_plane overlay_plane;
	struct drm_pending_vblank_event *vblank_event;
};

static struct drm_crtc_helper_funcs video_sync_crtc_helper_funcs;
static struct drm_crtc_funcs video_sync_crtc_funcs;
static struct drm_plane_funcs overlay_plane_funcs;
static struct file_operations video_sync_fops;
static struct platform_driver video_sync_driver;

static const struct drm_plane_helper_funcs overlay_plane_helper_funcs;
static const struct component_ops video_sync_component_ops;
static const struct of_device_id video_sync_of_ids[];

static irqreturn_t irq_handler(int irq, void *data);
static long video_sync_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int video_sync_open(struct inode *ino, struct file *file);
static int video_sync_release(struct inode *ino, struct file *file);
static int video_sync_crtc_get_max_width(struct video_crtc *crtc);
static int video_sync_crtc_get_max_height(struct video_crtc *crtc);
static int video_sync_crtc_get_fb_addr(struct video_crtc *crtc, dma_addr_t *paddr, void **vaddr);
static int init_cdevice(struct video_sync *synchronizer);
static int video_sync_crtc_create(struct video_sync *sync);
static int video_sync_probe(struct platform_device *pdev);
static int video_sync_remove(struct platform_device *pdev);
static int video_sync_plane_create(struct device *dev, struct video_sync *sync);
static int video_sync_bind(struct device *dev, struct device *master, void *data);
static int video_sync_plane_prepare_fb(struct drm_plane *plane, struct drm_plane_state *new_state);
static int video_sync_plane_atomic_check(struct drm_plane *plane, struct drm_plane_state *state);
static int video_sync_plane_atomic_async_check(struct drm_plane *plane,
						struct drm_plane_state *state);
static int video_sync_plane_atomic_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
	struct drm_framebuffer *fb, int crtc_x, int crtc_y, u32 crtc_w,
	u32 crtc_h, u32 src_x, u32 src_y, u32 src_w,
	u32 src_h, struct drm_modeset_acquire_ctx *ctx);
static int video_sync_plane_atomic_set_property(struct drm_plane *base_plane,
		struct drm_plane_state *state, struct drm_property *property, u64 val);
static int video_sync_plane_atomic_get_property(struct drm_plane *base_plane,
	const struct drm_plane_state *state, struct drm_property *property, u64 *val);
static int video_sync_disp_crtc_atomic_set_property(struct drm_crtc *crtc,
	struct drm_crtc_state *state, struct drm_property *property, u64 val);
static int video_sync_disp_crtc_atomic_get_property(struct drm_crtc *crtc,
	const struct drm_crtc_state *state, struct drm_property *property, u64 *val);
static int video_sync_crtc_enable_vblank(struct drm_crtc *base_crtc);
static int video_sync_crtc_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *state);
static void video_sync_plane_cleanup_fb(struct drm_plane *plane, struct drm_plane_state *old_state);
static void video_sync_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state);
static void video_sync_plane_atomic_disable(struct drm_plane *plane,
					struct drm_plane_state *old_state);
static void video_sync_plane_atomic_async_update(struct drm_plane *plane,
					struct drm_plane_state *new_state);
static void video_sync_crtc_destroy(struct drm_crtc *base_crtc);
static void video_sync_crtc_disable_vblank(struct drm_crtc *base_crtc);
static void video_sync_crtc_atomic_enable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state);
static void video_sync_crtc_atomic_disable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state);
static void video_sync_crtc_mode_set_nofb(struct drm_crtc *crtc);
static void video_sync_crtc_atomic_begin(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state);
static void video_sync_unbind(struct device *dev, struct device *master, void *data);
static u32 video_sync_crtc_get_format(struct video_crtc *crtc);
static u32 video_sync_crtc_get_align(struct video_crtc *crtc);

static struct video_sync *synchronizers[MAX_VIDEO_SYNCHRONIZERS];
static u32 synchronizers_probed = 0;

static irqreturn_t irq_handler(int irq, void *data)
{
	struct video_sync *synchronizer;
	struct drm_pending_vblank_event *event;

	u32 ctrl;
	u32 status;

	bool even;

	unsigned long flags;

	synchronizer = data;
	if (irq != synchronizer->irq) {
		return IRQ_NONE;
	}
	status = ioread32(synchronizer->regs + VIDEO_SYNC_STATUS_REGISTER);
	if ((status & VIDEO_SYNC_STATUS_PL_ERR_MASK) != 0) {
		dev_err(&synchronizer->pdev->dev, "pipeline delay too short\n");
		++synchronizer->pl_err;
	}
	if ((status & VIDEO_SYNC_STATUS_SOF_ERR_MASK) != 0) {
		dev_err(&synchronizer->pdev->dev, "sof not in sync\n");
		++synchronizer->sof_err;
	}
	if ((status & VIDEO_SYNC_STATUS_URUN_MASK) != 0) {
		dev_err(&synchronizer->pdev->dev, "underrun\n");
		++synchronizer->urun_err;
	}
	if ((status & VIDEO_SYNC_STATUS_SYNC_TO_MASK) != 0) {
		dev_err(&synchronizer->pdev->dev, "timeout using external sync\n");
		++synchronizer->sync_to_err;
	}
	if ((status & VIDEO_SYNC_STATUS_SHORT_LINE_ERR_MASK) != 0) {
		dev_err(&synchronizer->pdev->dev, "short line error\n");
		++synchronizer->short_line_err;
	}
	if ((status & VIDEO_SYNC_STATUS_LONG_LINE_ERR_MASK) != 0) {
		dev_err(&synchronizer->pdev->dev, "long line error\n");
		++synchronizer->long_line_err;
	}
	if ((status & VIDEO_SYNC_STATUS_FRAME_DONE_MASK) != 0) {
		++synchronizer->frames_done;
		if (synchronizer->vblank_enabled) {
			drm_crtc_handle_vblank(&synchronizer->crtc.crtc);
			spin_lock_irqsave(&synchronizer->drm->event_lock, flags);
			event = synchronizer->vblank_event;
			synchronizer->vblank_event = NULL;
			if (event != NULL) {
				drm_crtc_send_vblank_event(&synchronizer->crtc.crtc, event);
				drm_crtc_vblank_put(&synchronizer->crtc.crtc);
			}
			spin_unlock_irqrestore(&synchronizer->drm->event_lock, flags);
		}
	}
	if (synchronizer->interlaced &&
		((status & VIDEO_SYNC_STATUS_SYNC_TX_MASK) != 0)) {
		even = ((status & VIDEO_SYNC_STATUS_SYNC_FIELD_MASK) == 0);
		if (even) {
			++synchronizer->even_frames_done;
		} else {
			++synchronizer->odd_frames_done;
		}
		vdma_toggle_interlaced_buffer(synchronizer->overlay_plane.vdma, even);
		vdma_toggle_interlaced_buffer(synchronizer->video_vdma, even);
	}
	ctrl = ioread32(synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
	ctrl |= VIDEO_SYNC_CTRL_CLRIRQ_MASK;
	iowrite32(ctrl, synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
	ctrl &= ~VIDEO_SYNC_CTRL_CLRIRQ_MASK;
	ctrl |= VIDEO_SYNC_CTRL_IRQEN_MASK;
	iowrite32(ctrl, synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
	return IRQ_HANDLED;
}

static long video_sync_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct video_sync *synchronizer;
	struct video_sync_info *info;

	u32 reg;

	synchronizer = (struct video_sync *)file->private_data;
	switch (cmd)
	{
		case VIDEO_SYNC_CMD_GET_INFO:
			info = (struct video_sync_info *)arg;

			(void)strncpy(info->name, synchronizer->pdev->name, sizeof(info->name));

			reg = ioread32(synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			info->running    = ((reg & VIDEO_SYNC_CTRL_RUN_MASK)        != 0);
			info->ext_sync   = ((reg & VIDEO_SYNC_CTRL_EXT_SYNC_MASK)   != 0);
			info->ilace      = ((reg & VIDEO_SYNC_CTRL_ILACE_MASK)      != 0);
			info->irqen      = ((reg & VIDEO_SYNC_CTRL_IRQEN_MASK)      != 0);
			info->videoen    = ((reg & VIDEO_SYNC_CTRL_VIDEO_EN_MASK)   != 0);
			info->dyn_blend  = ((reg & VIDEO_SYNC_CTRL_DYN_BLEND_MASK)  != 0);
			info->slave_vdma = ((reg & VIDEO_SYNC_CTRL_SLAVE_VDMA_MASK) != 0);

			reg = ioread32(synchronizer->regs + VIDEO_SYNC_STATUS_REGISTER);
			if (synchronizer->irq != -1) {
				info->pl_err         = synchronizer->pl_err;
				info->sof_err        = synchronizer->sof_err;
				info->urun_err       = synchronizer->urun_err;
				info->sync_to_err    = synchronizer->sync_to_err;
				info->short_line_err = synchronizer->short_line_err;
				info->long_line_err  = synchronizer->long_line_err;
				info->frames_done    = synchronizer->frames_done;
			} else {
				info->pl_err =
					(((reg & VIDEO_SYNC_STATUS_PL_ERR_MASK) != 0) ? 1 : 0);
				info->sof_err =
					(((reg & VIDEO_SYNC_STATUS_SOF_ERR_MASK) != 0) ? 1 : 0);
				info->urun_err =
					(((reg & VIDEO_SYNC_STATUS_URUN_MASK) != 0) ? 1 : 0);
				info->sync_to_err =
					(((reg & VIDEO_SYNC_STATUS_SYNC_TO_MASK) != 0) ? 1 : 0);
				info->short_line_err =
					(((reg & VIDEO_SYNC_STATUS_SHORT_LINE_ERR_MASK) != 0) ? 1 : 0);
				info->long_line_err =
					(((reg & VIDEO_SYNC_STATUS_LONG_LINE_ERR_MASK) != 0) ? 1 : 0);
				info->frames_done = 0;
			}
			info->even_frames_done = synchronizer->even_frames_done;
			info->odd_frames_done  = synchronizer->odd_frames_done;
			info->sync_field = ((reg & VIDEO_SYNC_STATUS_SYNC_FIELD_MASK) != 0);
			info->field_ext  = ((reg & VIDEO_SYNC_STATUS_FIELD_EXT_MASK) != 0);
			info->pl_late    = ((reg & VIDEO_SYNC_STATUS_PL_LATE_MASK) != 0);
			info->dma_buf_index = ((reg &
					VIDEO_SYNC_STATUS_DMA_BUF_INDEX_MASK) >> 11);

			reg = ioread32(synchronizer->regs + VIDEO_SYNC_TOTAL_SIZE_REGISTER);
			info->total_width  =  (reg & VIDEO_SYNC_TOTAL_SIZE_WIDTH_MASK);
			info->total_height = ((reg & VIDEO_SYNC_TOTAL_SIZE_HEIGHT_MASK) >> 16);

			reg = ioread32(synchronizer->regs + VIDEO_SYNC_ACTIVE_SIZE_REGISTER);
			info->active_width = (reg & VIDEO_SYNC_ACTIVE_SIZE_WIDTH_MASK);
			info->active_height = ((reg &
					VIDEO_SYNC_ACTIVE_SIZE_HEIGHT_MASK) >> 16);

			reg = ioread32(synchronizer->regs + VIDEO_SYNC_DELAY_LINES_REGISTER);
			info->delay_ext_sync = (reg & VIDEO_SYNC_DELAY_EXT_SYNC_MASK);
			info->delay_pl = ((reg & VIDEO_SYNC_DELAY_PL_MASK) >> 16);

			reg = ioread32(synchronizer->regs + VIDEO_SYNC_IRQ_ENABLE_REGISTER);
			info->pl_err_irq_enabled = ((reg & VIDEO_SYNC_IRQ_PL_ERR_MASK) != 0);
			info->sof_err_irq_enabled = ((reg & VIDEO_SYNC_IRQ_SOF_ERR_MASK) != 0);
			info->urun_err_irq_enabled = ((reg & VIDEO_SYNC_IRQ_URUN_MASK) != 0);
			info->sync_to_err_irq_enabled =
						((reg & VIDEO_SYNC_IRQ_SYNC_TO_MASK) != 0);
			info->frame_done_irq_enabled =
						((reg & VIDEO_SYNC_IRQ_FRAME_DONE_MASK) != 0);
			info->err_short_irq_enabled =
						((reg & VIDEO_SYNC_IRQ_ERR_SHORT_MASK) != 0);
			info->err_long_irq_enabled =
						((reg & VIDEO_SYNC_IRQ_ERR_LONG_MASK) != 0);
			info->frame_sync_irq_enabled =
						((reg & VIDEO_SYNC_IRQ_FRAME_SYNC_MASK) != 0);

			info->version = ioread32(synchronizer->regs +
							VIDEO_SYNC_FW_VERSION_REGISTER);
			return 0;

		case VIDEO_SYNC_CMD_SET_INFO:
			info = (struct video_sync_info *)arg;
			reg = ioread32(synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			reg &= ~VIDEO_SYNC_CTRL_RUN_MASK;
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);

			reg = 0;
			if (info->frame_done_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_FRAME_DONE_MASK;
			}
			if (info->sync_to_err_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_SYNC_TO_MASK;
			}
			if (info->urun_err_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_URUN_MASK;
			}
			if (info->sof_err_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_SOF_ERR_MASK;
			}
			if (info->pl_err_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_PL_ERR_MASK;
			}
			if (info->err_short_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_ERR_SHORT_MASK;
			}
			if (info->err_long_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_ERR_LONG_MASK;
			}
			if (info->frame_sync_irq_enabled) {
				reg |= VIDEO_SYNC_IRQ_FRAME_SYNC_MASK;
			}
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_IRQ_ENABLE_REGISTER);

			reg  = ((info->delay_pl << 16) & VIDEO_SYNC_DELAY_PL_MASK);
			reg |=  (info->delay_ext_sync  & VIDEO_SYNC_DELAY_EXT_SYNC_MASK);
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_DELAY_LINES_REGISTER);

			reg  = ((info->active_height << 16) &
							VIDEO_SYNC_ACTIVE_SIZE_HEIGHT_MASK);
			reg |= (info->active_width & VIDEO_SYNC_ACTIVE_SIZE_WIDTH_MASK);
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_ACTIVE_SIZE_REGISTER);

			reg  = ((info->total_height << 16) & VIDEO_SYNC_TOTAL_SIZE_HEIGHT_MASK);
			reg |=  (info->total_width         & VIDEO_SYNC_TOTAL_SIZE_WIDTH_MASK);
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_TOTAL_SIZE_REGISTER);

			reg = 0;
			if (info->dyn_blend) {
				reg |= VIDEO_SYNC_CTRL_DYN_BLEND_MASK;
			}
			if (info->videoen) {
				reg |= VIDEO_SYNC_CTRL_VIDEO_EN_MASK;
			}
			if (info->irqen) {
				reg |= VIDEO_SYNC_CTRL_IRQEN_MASK;
			}
			if (info->ilace) {
				reg |= VIDEO_SYNC_CTRL_ILACE_MASK;
			}
			if (info->ext_sync) {
				reg |= VIDEO_SYNC_CTRL_EXT_SYNC_MASK;
			}
			if (info->slave_vdma) {
				reg |= VIDEO_SYNC_CTRL_SLAVE_VDMA_MASK;
			}
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			if (info->running) {
				reg |= VIDEO_SYNC_CTRL_RUN_MASK;
				iowrite32(reg, synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			}
			return 0;

		case VIDEO_SYNC_CMD_CTL_AUXRST:
			reg = ioread32(synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			if (arg != 0) {
				reg |= VIDEO_SYNC_CTRL_AUXRST_MASK;
			} else {
				reg &= ~VIDEO_SYNC_CTRL_AUXRST_MASK;
			}
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			return 0;

		case VIDEO_SYNC_CMD_CTL_SW_RST:
			reg = ioread32(synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			if (arg != 0) {
				reg |= VIDEO_SYNC_CTRL_SW_RST_MASK;
			} else {
				reg &= ~VIDEO_SYNC_CTRL_SW_RST_MASK;
			}
			iowrite32(reg, synchronizer->regs + VIDEO_SYNC_CONTROL_REGISTER);
			return 0;

		default:
			dev_err(&synchronizer->pdev->dev, "ioctl command not supported: 0x%x\n", cmd);
			return -ENOTSUPP;
	}
}

static int video_sync_open(struct inode *ino, struct file *file)
{
	u32 i;

	for (i = 0; i < synchronizers_probed; ++i) {
		if (ino->i_rdev == synchronizers[i]->node) {
			file->private_data = synchronizers[i];
			return 0;
		}
	}
	return -ENOENT;
}

static int video_sync_release(struct inode *ino, struct file *file)
{
	(void)ino;
	(void)file;
	return 0;
}

static int video_sync_plane_prepare_fb(struct drm_plane *plane,
					struct drm_plane_state *new_state)
{
	(void)plane;
	(void)new_state;
	return 0;
}

static void video_sync_plane_cleanup_fb(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	(void)plane;
	(void)old_state;
}

static int video_sync_plane_atomic_check(struct drm_plane *plane,
						struct drm_plane_state *state)
{
	(void)plane;
	(void)state;
	return 0;
}

static void video_sync_plane_atomic_update(struct drm_plane *plane,
					struct drm_plane_state *old_state)
{
	(void)plane;
	(void)old_state;
}

static void video_sync_plane_atomic_disable(struct drm_plane *plane,
						struct drm_plane_state *old_state)
{
	(void)plane;
	(void)old_state;
}

static int video_sync_plane_atomic_async_check(struct drm_plane *plane,
						struct drm_plane_state *state)
{
	(void)plane;
	(void)state;
	return 0;
}

static void video_sync_plane_atomic_async_update(struct drm_plane *plane,
						struct drm_plane_state *new_state)
{
	struct drm_plane_state *old_state;

	old_state = drm_atomic_get_old_plane_state(new_state->state, plane);
	swap(plane->state->fb, new_state->fb);
	plane->state->crtc = new_state->crtc;
	plane->state->crtc_x = new_state->crtc_x;
	plane->state->crtc_y = new_state->crtc_y;
	plane->state->crtc_w = new_state->crtc_w;
	plane->state->crtc_h = new_state->crtc_h;
	plane->state->src_x = new_state->src_x;
	plane->state->src_y = new_state->src_y;
	plane->state->src_w = new_state->src_w;
	plane->state->src_h = new_state->src_h;
	plane->state->state = new_state->state;
	video_sync_plane_atomic_update(plane, old_state);
}

static int video_sync_plane_atomic_update_plane(struct drm_plane *plane,
	struct drm_crtc *crtc, struct drm_framebuffer *fb, int crtc_x, int crtc_y,
	u32 crtc_w, u32 crtc_h, u32 src_x, u32 src_y,
	u32 src_w, u32 src_h, struct drm_modeset_acquire_ctx *ctx)
{
	int rc;
	struct video_sync_plane *overlay_plane;
	struct video_sync *sync;
	struct drm_atomic_state *state;
	struct drm_plane_state *plane_state;

	overlay_plane = container_of(plane, struct video_sync_plane, base);
	sync = container_of(overlay_plane, struct video_sync, overlay_plane);
	state = drm_atomic_state_alloc(plane->dev);
	if (state == NULL) {
		dev_err(&sync->pdev->dev, "cannot allocate memory for drm state\n");
		return -ENOMEM;
	}
	if (IS_ERR(state)) {
		dev_err(&sync->pdev->dev, "cannot allocate memory for drm state\n");
		return PTR_ERR(state);
	}
	plane_state = drm_atomic_get_plane_state(state, plane);
	if (IS_ERR(plane_state)) {
		dev_err(&sync->pdev->dev, "cannot get plane state\n");
		return PTR_ERR(plane_state);
	}
	rc = drm_atomic_set_crtc_for_plane(plane_state, crtc);
	if (rc != 0) {
		dev_err(&sync->pdev->dev, "cannot set crtc for plane\n");
		return rc;
	}
	drm_atomic_set_fb_for_plane(plane_state, fb);
	plane_state->crtc_x = crtc_x;
	plane_state->crtc_y = crtc_y;
	plane_state->crtc_w = crtc_w;
	plane_state->crtc_h = crtc_h;
	plane_state->src_x = src_x;
	plane_state->src_y = src_y;
	plane_state->src_w = src_w;
	plane_state->src_h = src_h;

	state->async_update = !drm_atomic_helper_async_check(plane->dev, state);
	rc = drm_atomic_commit(state);
	if (rc != 0) {
		dev_err(&sync->pdev->dev, "cannot atomic commit state\n");
		return rc;
	}
	drm_atomic_state_put(state);
	return 0;
}

static int video_sync_plane_atomic_set_property(struct drm_plane *base_plane,
			struct drm_plane_state *state, struct drm_property *property, u64 val)
{
	(void)base_plane;
	(void)state;
	(void)property;
	(void)val;
	return -EINVAL;
}

static int video_sync_plane_atomic_get_property(struct drm_plane *base_plane,
	const struct drm_plane_state *state, struct drm_property *property, u64 *val)
{
	(void)base_plane;
	(void)state;
	(void)property;
	(void)val;
	return -EINVAL;
}

static void video_sync_crtc_destroy(struct drm_crtc *base_crtc)
{
	drm_crtc_cleanup(base_crtc);
}

static int video_sync_disp_crtc_atomic_set_property(struct drm_crtc *crtc,
		struct drm_crtc_state *state, struct drm_property *property, u64 val)
{
	(void)crtc;
	(void)state;
	(void)property;
	(void)val;
	return 0;
}

static int video_sync_disp_crtc_atomic_get_property(struct drm_crtc *crtc,
		const struct drm_crtc_state *state, struct drm_property *property, u64 *val)
{
	(void)crtc;
	(void)state;
	(void)property;
	(void)val;
	return 0;
}

static int video_sync_crtc_enable_vblank(struct drm_crtc *base_crtc)
{
	struct video_sync *sync;
	struct video_crtc *crtc;

	crtc = container_of(base_crtc, struct video_crtc, crtc);
	sync = container_of(crtc, struct video_sync, crtc);
	sync->vblank_enabled = true;

	return 0;
}

static void video_sync_crtc_disable_vblank(struct drm_crtc *base_crtc)
{
	struct video_sync *sync;
	struct video_crtc *crtc;

	crtc = container_of(base_crtc, struct video_crtc, crtc);
	sync = container_of(crtc, struct video_sync, crtc);
	sync->vblank_enabled = false;
}

static void video_sync_crtc_atomic_enable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state)
{
	struct video_sync *sync;
	struct video_crtc *xcrtc;
	struct drm_display_mode *adjusted_mode;
	int vrefresh;

	adjusted_mode = &crtc->state->adjusted_mode;
	if((adjusted_mode->clock == 0) || (adjusted_mode->vtotal == 0) ||
		(adjusted_mode->htotal == 0)) {
		xcrtc = container_of(crtc, struct video_crtc, crtc);
		sync = container_of(xcrtc, struct video_sync, crtc);
		dev_err(&sync->pdev->dev, "invalid mode parameter: %ux%u @%u\n",
			adjusted_mode->htotal, adjusted_mode->vtotal, adjusted_mode->clock);
		return;
	}
	vrefresh = ((adjusted_mode->clock * 1000) /
			(adjusted_mode->vtotal * adjusted_mode->htotal));
	msleep(3 * 1000 / vrefresh);
}

static void video_sync_crtc_atomic_disable(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state)
{
	if (crtc->state->event) {
		complete_all(crtc->state->event->base.completion);
		crtc->state->event = NULL;
	}
	drm_crtc_vblank_off(crtc);
}

static void video_sync_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	(void)crtc;
}

static int video_sync_crtc_atomic_check(struct drm_crtc *crtc,
					struct drm_crtc_state *state)
{
	return drm_atomic_add_affected_planes(state->state, crtc);
}

static void video_sync_crtc_atomic_begin(struct drm_crtc *crtc,
					struct drm_crtc_state *old_crtc_state)
{
	struct video_crtc *xcrtc;
	struct video_sync *sync;

	drm_crtc_vblank_on(crtc);
	if (crtc->state->event != NULL) {
		xcrtc = container_of(crtc, struct video_crtc, crtc);
		sync = container_of(xcrtc, struct video_sync, crtc);

		crtc->state->event->pipe = drm_crtc_index(crtc);
		WARN_ON(drm_crtc_vblank_get(crtc) != 0);
		sync->vblank_event = crtc->state->event;
		crtc->state->event = NULL;
	}
}

static int video_sync_crtc_get_max_width(struct video_crtc *crtc)
{
	struct video_sync *sync;

	sync = container_of(crtc, struct video_sync, crtc);
	return vdma_mm2s_get_px_width(sync->overlay_plane.vdma);
}

static int video_sync_crtc_get_max_height(struct video_crtc *crtc)
{
	struct video_sync *sync;

	sync = container_of(crtc, struct video_sync, crtc);
	return vdma_mm2s_get_px_height(sync->overlay_plane.vdma);
}

static u32 video_sync_crtc_get_format(struct video_crtc *crtc)
{
	struct video_sync *sync;

	sync = container_of(crtc, struct video_sync, crtc);
	return sync->overlay_plane.format;
}

static u32 video_sync_crtc_get_align(struct video_crtc *crtc)
{
	struct video_sync_plane *overlay_plane;
	struct video_sync *sync;

	overlay_plane = container_of(crtc->crtc.primary, struct video_sync_plane, base);
	sync = container_of(overlay_plane, struct video_sync, overlay_plane);
	return vdma_mm2s_get_stride(sync->overlay_plane.vdma);
}

static int video_sync_crtc_get_fb_addr(struct video_crtc *crtc, dma_addr_t *paddr, void **vaddr)
{
	struct video_sync_plane *overlay_plane;
	struct video_sync *sync;

	overlay_plane = container_of(crtc->crtc.primary, struct video_sync_plane, base);
	sync = container_of(overlay_plane, struct video_sync, overlay_plane);
	return vdma_mm2s_get_fb_addr(sync->overlay_plane.vdma, paddr, vaddr);
}

static int init_cdevice(struct video_sync *synchronizer)
{
	int rc;
	size_t i;
	const char *dev_name = NULL;
	struct class *pclass = NULL;

	for (i = 0; i < strlen(synchronizer->pdev->name); ++i) {
		// EX format: b0100000.v_sync
		if (synchronizer->pdev->name[i] == '.') {
			dev_name = &synchronizer->pdev->name[i + 1];
		}
	}
	if(dev_name == NULL) {
		dev_name = synchronizer->pdev->name;
	}
	for (i = 0; i < MAX_VIDEO_SYNCHRONIZERS; ++i) {
		if ((synchronizers[i] != NULL) && (synchronizers[i]->pclass != NULL)) {
			pclass = synchronizers[i]->pclass;
			break;
		}
	}
	rc = alloc_chrdev_region(&synchronizer->node, 0, 1, dev_name);
	if (rc != 0) {
		dev_err(&synchronizer->pdev->dev, "unable to get a char device number\n");
		return rc;
	}
	cdev_init(&synchronizer->cdev, &video_sync_fops);
	synchronizer->cdev.owner = THIS_MODULE;

	rc = cdev_add(&synchronizer->cdev, synchronizer->node, 1);
	if (rc != 0) {
		dev_err(&synchronizer->pdev->dev, "unable to add char device\n");
		return rc;
	}
	if (pclass != NULL) {
		synchronizer->pclass = pclass;
	} else {
		synchronizer->pclass = class_create(THIS_MODULE, "sync");
	}
	if (synchronizer->pclass == NULL) {
		dev_err(&synchronizer->pdev->dev, "unable to create class\n");
		return -ENOMEM;
	}
	if (IS_ERR(synchronizer->pclass)) {
		dev_err(&synchronizer->pdev->dev, "unable to create class\n");
		return PTR_ERR(synchronizer->pclass);
	}
	synchronizer->dev = device_create(synchronizer->pclass, NULL,
						synchronizer->node, NULL, dev_name);
	if (synchronizer->dev == NULL) {
		dev_err(&synchronizer->pdev->dev, "unable to create the device\n");
		return -ENOMEM;
	}
	if (IS_ERR(synchronizer->dev)) {
		dev_err(&synchronizer->pdev->dev, "unable to create the device\n");
		return PTR_ERR(synchronizer->dev);
	}
	return 0;
}

static int video_sync_crtc_create(struct video_sync *sync)
{
	struct video_crtc *crtc;
	int rc;

	crtc = &sync->crtc;
	rc = drm_crtc_init_with_planes(sync->drm, &crtc->crtc, &sync->overlay_plane.base,
							NULL, &video_sync_crtc_funcs, NULL);
	if (rc != 0) {
		dev_err(&sync->pdev->dev, "cannot init crtc\n");
		return rc;
	}
	dev_info(&sync->pdev->dev, "crtc index: %08x\n", crtc->crtc.index);
	drm_crtc_helper_add(&crtc->crtc, &video_sync_crtc_helper_funcs);
	crtc->get_max_width = &video_sync_crtc_get_max_width;
	crtc->get_max_height = &video_sync_crtc_get_max_height;
	crtc->get_align = &video_sync_crtc_get_align;
	crtc->get_fb_addr = &video_sync_crtc_get_fb_addr;
	crtc->get_format = &video_sync_crtc_get_format;
	video_crtc_register(sync->drm, crtc);
	return 0;
}

static int video_sync_plane_create(struct device *dev, struct video_sync *sync)
{
	int rc;
	u32 fmt;

	rc = vdma_get_px_format(sync->overlay_plane.vdma, &fmt);
	if (rc != 0) {
		dev_err(&sync->pdev->dev, "cannot get pixel format\n");
		return rc;
	}
	switch (fmt)
	{
		case MEDIA_BUS_FMT_ARGB8888_1X32:
			sync->overlay_plane.format = DRM_FORMAT_ARGB8888;
			break;

		default:
			dev_err(&sync->pdev->dev, "unsupported drm format\n");
			return -EINVAL;
	}
	rc = drm_universal_plane_init(sync->drm, &sync->overlay_plane.base, 1,
			&overlay_plane_funcs, &sync->overlay_plane.format , 1,
			NULL, DRM_PLANE_TYPE_PRIMARY, NULL);
	if (rc != 0) {
		dev_err(&sync->pdev->dev, "failed to initialize overlay plane: %d\n", rc);
		return rc;
	}
	drm_plane_helper_add(&sync->overlay_plane.base, &overlay_plane_helper_funcs);
	return 0;
}

static int video_sync_bind(struct device *dev, struct device *master,
				void *data)
{
	struct video_sync *sync;
	int rc;

	sync = dev_get_drvdata(dev);
	sync->drm = data;
	rc = video_sync_plane_create(dev, sync);
	if (rc != 0) {
		return rc;
	}
	rc = video_sync_crtc_create(sync);
	if (rc != 0) {
		return rc;
	}
	return 0;
}

static void video_sync_unbind(struct device *dev, struct device *master,
				void *data)
{
	(void)dev;
	(void)master;
	(void)data;
}

static int video_sync_probe(struct platform_device *pdev)
{
	int rc;

	bool use_ext_sync;
	bool irq;
	bool videoen;
	bool dyn_blend;
	bool vdma_slave;

	u32 total_size;
	u32 total_width;
	u32 total_height;
	u32 active_size;
	u32 active_width;
	u32 active_height;
	u32 delay;
	u32 delay_ext_sync;
	u32 delay_pl;
	u32 ctrl;
	u32 irqreg;

	struct platform_device *vdma_pdev;
	struct device_node *vdma_node;
	struct device_node *node;

	void __iomem *regs;

	node = pdev->dev.of_node;
	if (synchronizers_probed >= MAX_VIDEO_SYNCHRONIZERS) {
		dev_err(&pdev->dev, "video synchronizer is out of range\n");
		return -ERANGE;
	}
	synchronizers[synchronizers_probed] = kzalloc(sizeof(struct video_sync), GFP_KERNEL);
	if (synchronizers[synchronizers_probed] == NULL) {
		dev_err(&pdev->dev, "cannot allocate memory for video synchonizer %u\n",
									synchronizers_probed);
		return -ENOMEM;
	}
	if (IS_ERR(synchronizers[synchronizers_probed])) {
		dev_err(&pdev->dev, "cannot allocate memory for video synchonizer %u\n",
									synchronizers_probed);
		return PTR_ERR(synchronizers[synchronizers_probed]);
	}
	regs = devm_platform_ioremap_resource(pdev, 0);
	if (regs == NULL) {
		dev_err(&pdev->dev, "cannot map registers\n");
		return -ENOMEM;
	}
	if (IS_ERR(regs)) {
		dev_err(&pdev->dev, "cannot map registers\n");
		return PTR_ERR(regs);
	}
	rc = of_property_read_u32(node, "total-width", &total_width);
	if (rc != 0) {
		dev_err(&pdev->dev, "missing property total-width\n");
		return rc;
	}
	rc = of_property_read_u32(node, "total-height", &total_height);
	if (rc != 0) {
		dev_err(&pdev->dev, "missing property total-height\n");
		return rc;
	}
	rc = of_property_read_u32(node, "active-width", &active_width);
	if (rc != 0) {
		dev_err(&pdev->dev, "missing property active-width\n");
		return rc;
	}
	rc = of_property_read_u32(node, "active-height", &active_height);
	if (rc != 0) {
		dev_err(&pdev->dev, "missing property active-height\n");
		return rc;
	}
	rc = of_property_read_u32(node, "delay-ext-sync", &delay_ext_sync);
	if (rc != 0) {
		dev_err(&pdev->dev, "missing property delay-ext-sync\n");
		return rc;
	}
	rc = of_property_read_u32(node, "delay-pl", &delay_pl);
	if (rc != 0) {
		dev_err(&pdev->dev, "missing property delay-pl\n");
		return rc;
	}
	total_size  = ((total_width  <<  0) & VIDEO_SYNC_TOTAL_SIZE_WIDTH_MASK);
	total_size |= ((total_height << 16) & VIDEO_SYNC_TOTAL_SIZE_HEIGHT_MASK);

	active_size  = ((active_width  <<  0) & VIDEO_SYNC_ACTIVE_SIZE_WIDTH_MASK);
	active_size |= ((active_height << 16) & VIDEO_SYNC_ACTIVE_SIZE_HEIGHT_MASK);

	delay  = ((delay_ext_sync <<  0) & VIDEO_SYNC_DELAY_EXT_SYNC_MASK);
	delay |= ((delay_pl       << 16) & VIDEO_SYNC_DELAY_PL_MASK);

	iowrite32(VIDEO_SYNC_CTRL_CLRIRQ_MASK, regs + VIDEO_SYNC_CONTROL_REGISTER);
	iowrite32(total_size,  regs + VIDEO_SYNC_TOTAL_SIZE_REGISTER);
	iowrite32(active_size, regs + VIDEO_SYNC_ACTIVE_SIZE_REGISTER);
	iowrite32(delay,       regs + VIDEO_SYNC_DELAY_LINES_REGISTER);

	use_ext_sync = of_property_read_bool(node, "use-external-sync");
	irq          = of_property_read_bool(node, "interrupts");
	videoen      = of_property_read_bool(node, "video-overlay");
	dyn_blend    = of_property_read_bool(node, "dynamic-blend");
	vdma_slave   = of_property_read_bool(node, "vdma-slave");

	synchronizers[synchronizers_probed]->interlaced =
			of_property_read_bool(node, "interlaced");
	synchronizers[synchronizers_probed]->regs = regs;
	synchronizers[synchronizers_probed]->pdev = pdev;
	synchronizers[synchronizers_probed]->irq  = -1;

	synchronizers[synchronizers_probed]->vblank_event   = NULL;
	synchronizers[synchronizers_probed]->vblank_enabled = false;

	synchronizers[synchronizers_probed]->pl_err           = 0;
	synchronizers[synchronizers_probed]->sof_err          = 0;
	synchronizers[synchronizers_probed]->urun_err         = 0;
	synchronizers[synchronizers_probed]->sync_to_err      = 0;
	synchronizers[synchronizers_probed]->short_line_err   = 0;
	synchronizers[synchronizers_probed]->long_line_err    = 0;
	synchronizers[synchronizers_probed]->frames_done      = 0;
	synchronizers[synchronizers_probed]->even_frames_done = 0;
	synchronizers[synchronizers_probed]->odd_frames_done  = 0;

	ctrl = 0;
	if (synchronizers[synchronizers_probed]->interlaced) {
		ctrl |= VIDEO_SYNC_CTRL_ILACE_MASK;
	}
	if (use_ext_sync) {
		ctrl |= VIDEO_SYNC_CTRL_EXT_SYNC_MASK;
	}
	if (videoen) {
		ctrl |= VIDEO_SYNC_CTRL_VIDEO_EN_MASK;
	}
	if (dyn_blend) {
		ctrl |= VIDEO_SYNC_CTRL_DYN_BLEND_MASK;
	}
	if (vdma_slave) {
		ctrl |= VIDEO_SYNC_CTRL_SLAVE_VDMA_MASK;
	}
	if (irq) {
		ctrl |= VIDEO_SYNC_CTRL_IRQEN_MASK;
		synchronizers[synchronizers_probed]->irq = irq_of_parse_and_map(node, 0);
		rc = request_irq(synchronizers[synchronizers_probed]->irq, irq_handler,
						IRQF_SHARED, "datarespons-video-sync",
						synchronizers[synchronizers_probed]);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot map interrupt\n");
			return rc;
		}
		irqreg = VIDEO_SYNC_IRQ_PL_ERR_MASK | VIDEO_SYNC_IRQ_SOF_ERR_MASK |
			VIDEO_SYNC_IRQ_URUN_MASK | VIDEO_SYNC_IRQ_SYNC_TO_MASK |
			VIDEO_SYNC_IRQ_ERR_SHORT_MASK | VIDEO_SYNC_IRQ_ERR_LONG_MASK |
			VIDEO_SYNC_IRQ_FRAME_DONE_MASK;
		if (synchronizers[synchronizers_probed]->interlaced) {
			irqreg |= VIDEO_SYNC_IRQ_FRAME_SYNC_MASK;
		}
		iowrite32(irqreg, regs + VIDEO_SYNC_IRQ_ENABLE_REGISTER);
	}
	iowrite32(ctrl, regs + VIDEO_SYNC_CONTROL_REGISTER);

	rc = init_cdevice(synchronizers[synchronizers_probed]);
	if (rc != 0) {
		return rc;
	}
	vdma_node = of_parse_phandle(node, "overlay-vdma", 0);
	if (vdma_node == NULL) {
		dev_err(&pdev->dev, "no overlay-vdma handle provided\n");
		return -EINVAL;
	}
	if (IS_ERR(vdma_node)) {
		dev_err(&pdev->dev, "no overlay-vdma handle provided\n");
		return PTR_ERR(vdma_node);
	}
	vdma_pdev = of_find_device_by_node(vdma_node);
	if (vdma_pdev == NULL) {
		dev_err(&pdev->dev, "no overlay vdma pdev found\n");
		return -EINVAL;
	}
	if (IS_ERR(vdma_pdev)) {
		dev_err(&pdev->dev, "no overlay vdma pdev found\n");
		return PTR_ERR(vdma_pdev);
	}
	synchronizers[synchronizers_probed]->overlay_plane.vdma =
					platform_get_drvdata(vdma_pdev);
	if (synchronizers[synchronizers_probed]->overlay_plane.vdma == NULL) {
		dev_err(&pdev->dev, "no overlay vdma found\n");
		return -EINVAL;
	}
	if (IS_ERR(synchronizers[synchronizers_probed]->overlay_plane.vdma)) {
		dev_err(&pdev->dev, "no overlay vdma found\n");
		return PTR_ERR(synchronizers[synchronizers_probed]->overlay_plane.vdma);
	}
	vdma_node = of_parse_phandle(node, "video-vdma", 0);
	if (vdma_node == NULL) {
		dev_err(&pdev->dev, "no video-vdma handle provided\n");
		return -EINVAL;
	}
	if (IS_ERR(vdma_node)) {
		dev_err(&pdev->dev, "no video-vdma handle provided\n");
		return PTR_ERR(vdma_node);
	}
	vdma_pdev = of_find_device_by_node(vdma_node);
	if (vdma_pdev == NULL) {
		dev_err(&pdev->dev, "no video vdma pdev found\n");
		return -EINVAL;
	}
	if (IS_ERR(vdma_pdev)) {
		dev_err(&pdev->dev, "no video vdma pdev found\n");
		return PTR_ERR(vdma_pdev);
	}
	synchronizers[synchronizers_probed]->video_vdma = platform_get_drvdata(vdma_pdev);
	if (synchronizers[synchronizers_probed]->video_vdma == NULL) {
		dev_err(&pdev->dev, "no video vdma found\n");
		return -EINVAL;
	}
	if (IS_ERR(synchronizers[synchronizers_probed]->video_vdma)) {
		dev_err(&pdev->dev, "no video vdma found\n");
		return PTR_ERR(synchronizers[synchronizers_probed]->video_vdma);
	}
	ctrl |= VIDEO_SYNC_CTRL_RUN_MASK;
	iowrite32(ctrl, regs + VIDEO_SYNC_CONTROL_REGISTER);

	platform_set_drvdata(pdev, synchronizers[synchronizers_probed]);
	rc = component_add(&pdev->dev, &video_sync_component_ops);
	if (rc != 0) {
		dev_err(&pdev->dev, "cannot register component ops\n");
		return rc;
	}
	synchronizers[synchronizers_probed]->master = video_drm_pipeline_init(pdev);
	if (synchronizers[synchronizers_probed]->master == NULL) {
		dev_err(&pdev->dev, "failed to initialize the drm pipeline\n");
		component_del(&pdev->dev, &video_sync_component_ops);
		return -ENOMEM;
	}
	if (IS_ERR(synchronizers[synchronizers_probed]->master)) {
		dev_err(&pdev->dev, "failed to initialize the drm pipeline\n");
		component_del(&pdev->dev, &video_sync_component_ops);
		return PTR_ERR(synchronizers[synchronizers_probed]->master);
	}
	dev_info(&pdev->dev, "%u total size: %ux%u\n",
			synchronizers_probed, total_width, total_height);
	dev_info(&pdev->dev, "%u active size: %ux%u\n",
			synchronizers_probed, active_width, active_height);
	dev_info(&pdev->dev, "%u delay: ext sync %u pl %u\n",
			synchronizers_probed, delay_ext_sync, delay_pl);
	dev_info(&pdev->dev, "%u ext sync: %u\n", synchronizers_probed, use_ext_sync);
	dev_info(&pdev->dev, "%u interlaced: %u (vdma slave: %u)\n",
			synchronizers_probed, synchronizers[synchronizers_probed]->interlaced,
			vdma_slave);
	dev_info(&pdev->dev, "%u overlay: %u\n", synchronizers_probed, videoen);
	dev_info(&pdev->dev, "%u dynamic blend: %u\n", synchronizers_probed, dyn_blend);
	dev_info(&pdev->dev, "%u irq: %d\n", synchronizers_probed,
				synchronizers[synchronizers_probed]->irq);
	dev_info(&pdev->dev, "%u (overlay: %s video: %s) initialized\n",
		synchronizers_probed,
		vdma_get_name(synchronizers[synchronizers_probed]->overlay_plane.vdma),
		vdma_get_name(synchronizers[synchronizers_probed]->video_vdma));

	++synchronizers_probed;
	return 0;
}

static int video_sync_remove(struct platform_device *pdev)
{
	u32 i;
	u32 ctrl;

	for (i = 0; i < synchronizers_probed; ++i) {
		if (synchronizers[i]->pdev == pdev) {
			ctrl = ioread32(synchronizers[i]->regs + VIDEO_SYNC_CONTROL_REGISTER);
			ctrl &= ~VIDEO_SYNC_CTRL_RUN_MASK;
			iowrite32(ctrl, synchronizers[i]->regs + VIDEO_SYNC_CONTROL_REGISTER);
		}
	}
	return 0;
}

static const struct of_device_id video_sync_of_ids[] = {
	{ .compatible = "datarespons,video-sync",},
	{}
};

static struct platform_driver video_sync_driver = {
	.driver = {
		.name = "video_sync_driver",
		.owner = THIS_MODULE,
		.of_match_table = video_sync_of_ids,
	},
	.probe = video_sync_probe,
	.remove = video_sync_remove,
};

static const struct component_ops video_sync_component_ops = {
	.bind   = video_sync_bind,
	.unbind = video_sync_unbind,
};

static struct file_operations video_sync_fops = {
	.owner    = THIS_MODULE,
	.open     = video_sync_open,
	.release  = video_sync_release,
	.unlocked_ioctl = video_sync_ioctl,
};

static const struct drm_plane_helper_funcs overlay_plane_helper_funcs = {
	.prepare_fb     = video_sync_plane_prepare_fb,
	.cleanup_fb     = video_sync_plane_cleanup_fb,
	.atomic_check   = video_sync_plane_atomic_check,
	.atomic_update  = video_sync_plane_atomic_update,
	.atomic_disable = video_sync_plane_atomic_disable,
	.atomic_async_check  = video_sync_plane_atomic_async_check,
	.atomic_async_update = video_sync_plane_atomic_async_update,
};

static struct drm_plane_funcs overlay_plane_funcs = {
	.update_plane  = video_sync_plane_atomic_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.atomic_set_property = video_sync_plane_atomic_set_property,
	.atomic_get_property = video_sync_plane_atomic_get_property,
	.destroy             = drm_plane_cleanup,
	.reset               = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_plane_destroy_state,
};

static struct drm_crtc_funcs video_sync_crtc_funcs = {
	.destroy    = video_sync_crtc_destroy,
	.set_config = drm_atomic_helper_set_config,
	.page_flip  = drm_atomic_helper_page_flip,
	.atomic_set_property = video_sync_disp_crtc_atomic_set_property,
	.atomic_get_property = video_sync_disp_crtc_atomic_get_property,
	.reset                  = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank          = video_sync_crtc_enable_vblank,
	.disable_vblank         = video_sync_crtc_disable_vblank,
};

static struct drm_crtc_helper_funcs video_sync_crtc_helper_funcs = {
	.atomic_enable  = video_sync_crtc_atomic_enable,
	.atomic_disable = video_sync_crtc_atomic_disable,
	.mode_set_nofb  = video_sync_crtc_mode_set_nofb,
	.atomic_check   = video_sync_crtc_atomic_check,
	.atomic_begin   = video_sync_crtc_atomic_begin,
};

module_platform_driver(video_sync_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("Video Sync Driver");
MODULE_LICENSE("PROPRIETARY");
