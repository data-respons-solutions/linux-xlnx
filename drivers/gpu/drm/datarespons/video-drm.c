#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_of.h>
#include <drm/drm_probe_helper.h>

#include <linux/component.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>

#include "video-drm.h"

struct video_fbdev {
	struct drm_fb_helper fb_helper;
	struct drm_framebuffer *fb;
	u32 align;
	u32 vres_mult;
};

struct video_drm {
	struct drm_device *drm;
	struct video_crtc *crtc;
	struct drm_fb_helper *fb;
	struct platform_device *master;
	struct drm_atomic_state *suspend_state;
	u32 master_count;
};

static const struct dev_pm_ops video_pm_ops;
static const struct file_operations video_fops;
static const struct drm_mode_config_funcs video_mode_config_funcs;
static const struct component_master_ops video_master_ops;

static struct platform_driver video_driver;
static struct drm_driver video_drm_driver;
static struct drm_fb_helper_funcs video_fb_helper_funcs;
static struct drm_framebuffer_funcs video_fb_funcs;
static struct fb_ops video_fbdev_ops;

static int video_pm_resume(struct device *dev);
static int video_drm_release(struct inode *inode, struct file *filp);
static int video_drm_get_fb_addr(struct drm_device *drm, dma_addr_t *paddr, void **vaddr);
static int video_fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg);
static int video_drm_open(struct drm_device *dev, struct drm_file *file);
static int video_platform_remove(struct platform_device *pdev);
static int video_platform_probe(struct platform_device *pdev);
static int video_compare_of(struct device *dev, void *data);
static int video_bind(struct device *dev);
static int video_fbdev_create(struct drm_fb_helper *fb_helper,
					struct drm_fb_helper_surface_size *size);
static int video_gem_cma_dumb_create(struct drm_file *file_priv, struct drm_device *drm,
							struct drm_mode_create_dumb *args);
static int video_of_component_probe(struct device *master_dev,
					int (*compare_of)(struct device *, void *),
					const struct component_master_ops *m_ops);

static u32 video_get_align(struct drm_device *drm);
static u32 video_get_format(struct drm_device *drm);

static void video_unbind(struct device *dev);
static void video_lastclose(struct drm_device *drm);
static void video_mode_config_init(struct drm_device *drm);
static void video_output_poll_changed(struct drm_device *drm);
static void video_platform_shutdown(struct platform_device *pdev);

static struct drm_framebuffer *video_fb_create(struct drm_device *drm,
                struct drm_file *file_priv, const struct drm_mode_fb_cmd2 *mode_cmd);
static struct drm_framebuffer *video_fb_gem_fb_alloc(struct drm_device *drm,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **obj,
		u32 num_planes, const struct drm_framebuffer_funcs *funcs);
static struct drm_framebuffer *video_fb_gem_fbdev_fb_create(struct drm_device *drm,
		struct drm_fb_helper_surface_size *size, u32 pitch_align,
		struct drm_gem_object *obj, const struct drm_framebuffer_funcs *funcs);
static struct drm_fb_helper *video_fb_init(struct drm_device *drm, int preferred_bpp,
						u32 max_conn_count, u32 align, u32 vres_mult);

static int video_fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg)
{
	struct drm_fb_helper *fb_helper;
	struct drm_mode_set *mode_set;
	struct drm_crtc *crtc;
	int rc;

	fb_helper = info->par;
	switch (cmd) {
		case FBIO_WAITFORVSYNC:
			rc = 0;
			drm_client_for_each_modeset(mode_set, &fb_helper->client) {
				crtc = mode_set->crtc;
				rc = drm_crtc_vblank_get(crtc);
				if (rc == 0) {
					drm_crtc_wait_one_vblank(crtc);
					drm_crtc_vblank_put(crtc);
				}
			}
			return rc;

		default:
			return -ENOTTY;
	}
	return 0;
}

static struct drm_framebuffer *video_fb_gem_fb_alloc(struct drm_device *drm,
		const struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **obj,
		u32 num_planes, const struct drm_framebuffer_funcs *funcs)
{
	struct drm_framebuffer *fb;
	int rc;
	int i;

	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (fb == NULL) {
		dev_err(drm->dev, "failed to alloc fb\n");
		return ERR_PTR(-ENOMEM);
	}
	if (IS_ERR(fb)) {
		dev_err(drm->dev, "failed to alloc fb\n");
		return fb;
	}
	drm_helper_mode_fill_fb_struct(drm, fb, mode_cmd);
	for (i = 0; i < num_planes; ++i) {
		fb->obj[i] = obj[i];
	}
	rc = drm_framebuffer_init(drm, fb, funcs);
	if (rc != 0) {
		dev_err(drm->dev, "failed to init framebuffer: %d\n", rc);
		return ERR_PTR(rc);
	}
	return fb;
}

static struct drm_framebuffer *video_fb_gem_fbdev_fb_create(struct drm_device *drm,
		struct drm_fb_helper_surface_size *size, u32 pitch_align,
		struct drm_gem_object *obj, const struct drm_framebuffer_funcs *funcs)
{
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };

	mode_cmd.width = size->surface_width;
	mode_cmd.height = size->surface_height;
	mode_cmd.pitches[0] = size->surface_width * DIV_ROUND_UP(size->surface_bpp, 8);
	if (pitch_align != 0) {
		mode_cmd.pitches[0] = roundup(mode_cmd.pitches[0], pitch_align);
	}
	mode_cmd.pixel_format = drm_driver_legacy_fb_format(drm,
					size->surface_bpp, size->surface_depth);
	if (obj->size < (size_t)mode_cmd.pitches[0] * mode_cmd.height) {
		return ERR_PTR(-EINVAL);
	}
	return video_fb_gem_fb_alloc(drm, &mode_cmd, &obj, 1, funcs);
}

static int video_fbdev_create(struct drm_fb_helper *fb_helper,
				struct drm_fb_helper_surface_size *size)
{
	struct video_fbdev *fbdev;
	struct drm_device *drm;
	struct drm_gem_cma_object *obj;
	struct drm_framebuffer *fb;
	u32 bytes_per_pixel;
	u32 format;
	u64 offset;
	struct fb_info *fbi;
	const struct drm_format_info *info;
	size_t bytes;
	int rc;

	fbdev = container_of(fb_helper, struct video_fbdev, fb_helper);
	drm = fb_helper->dev;

	size->surface_height *= fbdev->vres_mult;
	bytes_per_pixel = DIV_ROUND_UP(size->surface_bpp, 8);
	bytes = ALIGN((size_t)size->surface_width * bytes_per_pixel, fbdev->align);
	bytes *= size->surface_height;

	obj = drm_gem_cma_create(drm, bytes);
	if (obj == NULL) {
		dev_err(drm->dev, "failed to allocate framebuffer gem\n");
		return -ENOMEM;
	}
	if (IS_ERR(obj)) {
		dev_err(drm->dev, "failed to allocate framebuffer gem\n");
		return PTR_ERR(obj);
	}
	dma_free_wc(obj->base.dev->dev, obj->base.size, obj->vaddr, obj->paddr);
	rc = video_drm_get_fb_addr(drm, &obj->paddr, &obj->vaddr);
	if (rc != 0) {
		dev_err(drm->dev, "failed to get vdma fb address\n");
		return rc;
	}
	fbi = framebuffer_alloc(0, drm->dev);
	if (fbi == NULL) {
		dev_err(drm->dev, "failed to allocate framebuffer info.\n");
		return -ENOMEM;
	}
	if (IS_ERR(fbi)) {
		dev_err(drm->dev, "failed to allocate framebuffer info.\n");
		return PTR_ERR(fbi);
	}
	format = video_get_format(drm);
	info = drm_format_info(format);
	if (size->surface_bpp == info->cpp[0] * 8) {
		size->surface_depth = info->depth;
	}
	fbdev->fb = video_fb_gem_fbdev_fb_create(drm, size, fbdev->align,
							&obj->base, &video_fb_funcs);
	if (fbdev->fb == NULL) {
		dev_err(drm->dev, "failed to allocate drm framebuffer.\n");
		return -ENOMEM;
	}
	if (IS_ERR(fbdev->fb)) {
		dev_err(drm->dev, "failed to allocate drm framebuffer.\n");
		return PTR_ERR(fbdev->fb);
	}
	fb = fbdev->fb;
	fb_helper->fb = fb;
	fb_helper->fbdev = fbi;
	fbi->flags = FBINFO_FLAG_DEFAULT;
	fbi->fbops = &video_fbdev_ops;

	rc = fb_alloc_cmap(&fbi->cmap, 256, 0);
	if (rc != 0) {
		dev_err(drm->dev, "failed to allocate color map.\n");
		return -ENOMEM;
	}
	drm_fb_helper_fill_info(fbi, fb_helper, size);
	fbi->var.yres = fb->height / fbdev->vres_mult;

	offset = (u64)fbi->var.xoffset * bytes_per_pixel;
	offset += fbi->var.yoffset * fb->pitches[0];

	drm->mode_config.fb_base = (resource_size_t)obj->paddr;
	fbi->screen_base = (char __iomem *)(obj->vaddr + offset);
	fbi->fix.smem_start = (u64)(obj->paddr + offset);
	fbi->screen_size = bytes;
	fbi->fix.smem_len = bytes;
	return 0;
}

static struct drm_fb_helper *video_fb_init(struct drm_device *drm, int preferred_bpp,
						u32 max_conn_count, u32 align, u32 vres_mult)
{
	struct video_fbdev *fbdev;
	struct drm_fb_helper *fb_helper;
	int rc;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (fbdev == NULL) {
		dev_err(drm->dev, "failed to allocate fbdev\n");
		return ERR_PTR(-ENOMEM);
	}
	if (IS_ERR(fbdev)) {
		dev_err(drm->dev, "failed to allocate fbdev\n");
		return (void *)fbdev;
	}
	fbdev->vres_mult = vres_mult;
	fbdev->align = align;
	fb_helper = &fbdev->fb_helper;
	drm_fb_helper_prepare(drm, fb_helper, &video_fb_helper_funcs);

	rc = drm_fb_helper_init(drm, fb_helper, max_conn_count);
	if (rc < 0) {
		dev_err(drm->dev, "failed to initialize drm fb helper.\n");
		return ERR_PTR(rc);
	}
	rc = drm_fb_helper_single_add_all_connectors(fb_helper);
	if (rc < 0) {
		dev_err(drm->dev, "failed to add connectors.\n");
		return ERR_PTR(rc);
	}
	rc = drm_fb_helper_initial_config(fb_helper, preferred_bpp);
	if (rc < 0) {
		dev_err(drm->dev, "failed to set initial hw configuration.\n");
		return ERR_PTR(rc);
	}
	return fb_helper;
}

static u32 video_get_align(struct drm_device *drm)
{
	struct video_drm *video_drm;

	video_drm = drm->dev_private;
	return video_drm->crtc->get_align(video_drm->crtc);
}

static u32 video_get_format(struct drm_device *drm)
{
	struct video_drm *video_drm;

	video_drm = drm->dev_private;
	return video_drm->crtc->get_format(video_drm->crtc);
}

static void video_output_poll_changed(struct drm_device *drm)
{
	struct video_drm *video_drm;

	video_drm = drm->dev_private;
	if (video_drm->fb != NULL) {
		drm_fb_helper_hotplug_event(video_drm->fb);
	}
}

static int video_drm_get_fb_addr(struct drm_device *drm, dma_addr_t *paddr, void **vaddr)
{
	struct video_drm *video_drm;

	video_drm = drm->dev_private;
	return video_drm->crtc->get_fb_addr(video_drm->crtc, paddr, vaddr);
}

static struct drm_framebuffer *video_fb_create(struct drm_device *drm,
		struct drm_file *file_priv, const struct drm_mode_fb_cmd2 *mode_cmd)
{
	return drm_gem_fb_create_with_funcs(drm, file_priv, mode_cmd, &video_fb_funcs);
}

static void video_mode_config_init(struct drm_device *drm)
{
	struct video_drm *video_drm;

	video_drm = drm->dev_private;
	drm->mode_config.min_width = 0;
	drm->mode_config.min_height = 0;
	drm->mode_config.max_width = video_drm->crtc->get_max_width(video_drm->crtc);
	drm->mode_config.max_height = video_drm->crtc->get_max_height(video_drm->crtc);
	drm->mode_config.cursor_width = 0;
	drm->mode_config.cursor_height = 0;
}

static int video_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct video_drm *video_drm;
	bool is_primary_client;

	video_drm = dev->dev_private;
	is_primary_client = (drm_is_primary_client(file) && (dev->master == NULL));
	if ((is_primary_client == false) && (file->is_master == false) &&
							capable(CAP_SYS_ADMIN)) {
		file->is_master = 1;
		++video_drm->master_count;
	}
	return 0;
}

static int video_drm_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file;
	struct drm_minor *minor;
	struct drm_device *drm;
	struct video_drm *video_drm;

	file = filp->private_data;
	minor = file->minor;
	drm = minor->dev;
	video_drm = drm->dev_private;
	if (file->is_master && (video_drm->master_count != 0)) {
		--video_drm->master_count;
		file->is_master = 0;
	}
	return drm_release(inode, filp);
}

static void video_lastclose(struct drm_device *drm)
{
	struct video_drm *video_drm;

	video_drm = drm->dev_private;
	if (video_drm->fb != NULL) {
		drm_fb_helper_restore_fbdev_mode_unlocked(video_drm->fb);
	}
}

static int video_gem_cma_dumb_create(struct drm_file *file_priv, struct drm_device *drm,
					struct drm_mode_create_dumb *args)
{
	int pitch;
	u32 align;

	pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	align = video_get_align(drm);
	if ((args->pitch == 0) || (IS_ALIGNED(args->pitch, align) == false)) {
		args->pitch = ALIGN(pitch, align);
	}
	return drm_gem_cma_dumb_create_internal(file_priv, drm, args);
}

static int video_bind(struct device *dev)
{
	struct video_drm *video_drm;
	struct drm_device *drm;
	const struct drm_format_info *info;
	struct platform_device *master;
	struct platform_device *pdev;
	int rc;
	u32 align;
	u32 format;

	master = to_platform_device(dev);
	pdev = to_platform_device(dev->parent);
	drm = drm_dev_alloc(&video_drm_driver, &pdev->dev);
	if (IS_ERR(drm)) {
		dev_err(&pdev->dev, "failed to allocate drm device\n");
		return PTR_ERR(drm);
	}
	if (drm == NULL) {
		dev_err(&pdev->dev, "failed to allocate drm device\n");
		return -ENOMEM;
	}
	video_drm = devm_kzalloc(drm->dev, sizeof(*video_drm), GFP_KERNEL);
	if (video_drm == NULL) {
		dev_err(&pdev->dev, "failed to allocate video drm device\n");
		return -ENOMEM;
	}
	if (IS_ERR(video_drm)) {
		dev_err(&pdev->dev, "failed to allocate video drm device\n");
		return PTR_ERR(video_drm);
	}
	drm_mode_config_init(drm);
	drm->mode_config.funcs = &video_mode_config_funcs;

	rc = drm_vblank_init(drm, 1);
	if (rc != 0) {
		dev_err(&pdev->dev, "failed to initialize vblank\n");
		return rc;
	}
	drm->irq_enabled = 1;
	drm->dev_private = video_drm;
	video_drm->drm = drm;
	video_drm->master = master;
	drm_kms_helper_poll_init(drm);
	platform_set_drvdata(master, video_drm);

	video_drm->crtc = NULL;
	rc = component_bind_all(&master->dev, drm);
	if (rc != 0) {
		dev_err(&pdev->dev, "failed to bind all\n");
		return rc;
	}
	if (video_drm->crtc == NULL) {
		dev_err(&pdev->dev, "crtc is null after bind all\n");
		return rc;
	}
	video_mode_config_init(drm);
	drm_mode_config_reset(drm);
	dma_set_mask(drm->dev, DMA_BIT_MASK(sizeof(dma_addr_t) * 8));

	format = video_drm->crtc->get_format(video_drm->crtc);
	info = drm_format_info(format);
	if ((info != NULL) && (info->depth != 0) && (info->cpp[0] != 0)) {
		align = video_drm->crtc->get_align(video_drm->crtc);
		video_drm->fb = video_fb_init(drm, info->cpp[0] * 8, 1, align, 2);
		if ((IS_ERR(video_drm->fb)) || (video_drm->fb == NULL)) {
			dev_err(&pdev->dev, "failed to initialize drm fb\n");
			video_drm->fb = NULL;
		}
	} else {
		dev_info(&pdev->dev, "fbdev is not initialized\n");
	}
	rc = drm_dev_register(drm, 0);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to register drm\n");
		return rc;
	}
	return 0;
}

static void video_unbind(struct device *dev)
{
	struct video_drm *video_drm;
	struct drm_device *drm;

	video_drm = dev_get_drvdata(dev);
	drm = video_drm->drm;

	drm_dev_unregister(drm);
	component_unbind_all(&video_drm->master->dev, drm);
	drm_kms_helper_poll_fini(drm);
	drm_mode_config_cleanup(drm);
	drm_dev_put(drm);
}

static int video_of_component_probe(struct device *master_dev,
				   int (*compare_of)(struct device *, void *),
				   const struct component_master_ops *m_ops)
{
	struct device *dev;
	struct device_node *ep;
	struct device_node *port;
	struct device_node *remote;
	struct device_node *parent;
	struct component_match *match;
	int i;

	dev = master_dev->parent;
	match = NULL;

	if (dev->of_node == NULL) {
		return -EINVAL;
	}
	component_match_add(master_dev, &match, compare_of, dev->of_node);

	for (i = 0; ; ++i) {
		port = of_parse_phandle(dev->of_node, "ports", i);
		if ((port == NULL) || (IS_ERR(port))) {
			break;
		}
		parent = port->parent;
		if (of_node_cmp(parent->name, "ports") == 0) {
			parent = parent->parent;
		}
		parent = of_node_get(parent);
		if (of_device_is_available(parent) == false) {
			of_node_put(parent);
			of_node_put(port);
			continue;
		}
		component_match_add(master_dev, &match, compare_of, parent);
		of_node_put(parent);
		of_node_put(port);
	}
	parent = dev->of_node;
	for (i = 0; ; ++i) {
		parent = of_node_get(parent);
		if (of_device_is_available(parent) == false) {
			of_node_put(parent);
			continue;
		}
		for_each_endpoint_of_node(parent, ep) {
			remote = of_graph_get_remote_port_parent(ep);
			if ((remote == NULL) || (of_device_is_available(remote) == false) ||
			    (remote == dev->of_node)) {
				of_node_put(remote);
				continue;
			} else if (of_device_is_available(remote->parent) == false) {
				dev_warn(dev, "parent dev of %s unavailable\n", remote->full_name);
				of_node_put(remote);
				continue;
			}
			component_match_add(master_dev, &match, compare_of, remote);
			of_node_put(remote);
		}
		of_node_put(parent);
		port = of_parse_phandle(dev->of_node, "ports", i);
		if ((port == NULL) || (IS_ERR(port))) {
			break;
		}
		parent = port->parent;
		if (of_node_cmp(parent->name, "ports") == 0) {
			parent = parent->parent;
		}
		of_node_put(port);
	}
	return component_master_add_with_match(master_dev, m_ops, match);
}

static int video_compare_of(struct device *dev, void *data)
{
	return (dev->of_node == data);
}

static int video_platform_probe(struct platform_device *pdev)
{
	return video_of_component_probe(&pdev->dev, video_compare_of,
				       &video_master_ops);
}

static int video_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &video_master_ops);
	return 0;
}

static void video_platform_shutdown(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &video_master_ops);
}

static int __maybe_unused video_pm_suspend(struct device *dev)
{
	struct video_drm *video_drm;
	struct drm_device *drm;

	video_drm = dev_get_drvdata(dev);
	drm = video_drm->drm;

	drm_kms_helper_poll_disable(drm);
	video_drm->suspend_state = drm_atomic_helper_suspend(drm);
	if (IS_ERR(video_drm->suspend_state)) {
		drm_kms_helper_poll_enable(drm);
		return PTR_ERR(video_drm->suspend_state);
	}
	return 0;
}

static int __maybe_unused video_pm_resume(struct device *dev)
{
	struct video_drm *video_drm;
	struct drm_device *drm;

	video_drm = dev_get_drvdata(dev);
	drm = video_drm->drm;
	drm_atomic_helper_resume(drm, video_drm->suspend_state);
	drm_kms_helper_poll_enable(drm);
	return 0;
}

struct platform_device *video_drm_pipeline_init(struct platform_device *pdev)
{
	static u32 video_master_ids = GENMASK(31, 0);

	struct platform_device *master;
	int id;
	int rc;

	id = ffs(video_master_ids);
	if (id == 0) {
		return ERR_PTR(-ENOSPC);
	}

	master = platform_device_alloc("video-drm", id - 1);
	if (master == NULL) {
		return ERR_PTR(-ENOMEM);
	}
	if (IS_ERR(master)) {
		return master;
	}
	master->dev.parent = &pdev->dev;
	rc = platform_device_add(master);
	if (rc != 0) {
		return ERR_PTR(rc);
	}
	WARN_ON(master->id != id - 1);
	video_master_ids &= ~BIT(master->id);
	return master;
}

void video_crtc_register(struct drm_device *drm, struct video_crtc *crtc)
{
	struct video_drm *video_drm;

	video_drm = drm->dev_private;
	video_drm->crtc = crtc;
}


static struct drm_fb_helper_funcs video_fb_helper_funcs = {
	.fb_probe = video_fbdev_create,
};

static const struct drm_mode_config_funcs video_mode_config_funcs = {
	.fb_create           = video_fb_create,
	.output_poll_changed = video_output_poll_changed,
	.atomic_check        = drm_atomic_helper_check,
	.atomic_commit       = drm_atomic_helper_commit,
};

static struct drm_framebuffer_funcs video_fb_funcs = {
	.destroy       = drm_gem_fb_destroy,
	.create_handle = drm_gem_fb_create_handle,
};

static struct fb_ops video_fbdev_ops = {
	.owner          = THIS_MODULE,
	.fb_fillrect    = sys_fillrect,
	.fb_copyarea    = sys_copyarea,
	.fb_imageblit   = sys_imageblit,
	.fb_check_var   = drm_fb_helper_check_var,
	.fb_set_par     = drm_fb_helper_set_par,
	.fb_blank       = drm_fb_helper_blank,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_setcmap     = drm_fb_helper_setcmap,
	.fb_ioctl       = video_fb_ioctl,
};

static const struct file_operations video_fops = {
	.owner          = THIS_MODULE,
	.open           = drm_open,
	.release        = video_drm_release,
	.unlocked_ioctl = drm_ioctl,
	.mmap           = drm_gem_cma_mmap,
	.poll           = drm_poll,
	.read           = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = drm_compat_ioctl,
#endif
	.llseek         = noop_llseek,
};

static struct drm_driver video_drm_driver = {
	.driver_features           = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC,
	.open                      = video_drm_open,
	.lastclose                 = video_lastclose,
	.prime_handle_to_fd        = drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle        = drm_gem_prime_fd_to_handle,
	.gem_prime_export          = drm_gem_prime_export,
	.gem_prime_import          = drm_gem_prime_import,
	.gem_prime_get_sg_table    = drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap            = drm_gem_cma_prime_vmap,
	.gem_prime_vunmap          = drm_gem_cma_prime_vunmap,
	.gem_prime_mmap            = drm_gem_cma_prime_mmap,
	.gem_free_object           = drm_gem_cma_free_object,
	.gem_vm_ops                = &drm_gem_cma_vm_ops,
	.dumb_create               = video_gem_cma_dumb_create,
	.dumb_destroy              = drm_gem_dumb_destroy,
	.fops                      = &video_fops,
	.name                      = "datarespons",
	.desc                      = "Data Respons DRM Driver",
	.date                      = "20211120",
	.major                     = 1,
	.minor                     = 0,
};

static const struct component_master_ops video_master_ops = {
	.bind   = video_bind,
	.unbind = video_unbind,
};

static const struct dev_pm_ops video_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(video_pm_suspend, video_pm_resume)
};

static struct platform_driver video_driver = {
	.probe        = video_platform_probe,
	.remove       = video_platform_remove,
	.shutdown     = video_platform_shutdown,
	.driver       = {
		.name = "video-drm",
		.pm   = &video_pm_ops,
	},
};

module_platform_driver(video_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("Data Respons DRM Driver");
MODULE_LICENSE("PROPRIETARY");
