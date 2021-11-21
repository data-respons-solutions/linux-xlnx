#ifndef VIDEO_DRM_H
#define VIDEO_DRM_H

struct video_crtc {
	struct drm_crtc crtc;
	u32 (*get_align)(struct video_crtc *crtc);
	u32 (*get_format)(struct video_crtc *crtc);
	int (*get_max_width)(struct video_crtc *crtc);
	int (*get_max_height)(struct video_crtc *crtc);
	int (*get_fb_addr)(struct video_crtc *crtc, dma_addr_t *paddr, void **vaddr);
};
struct drm_device;

struct platform_device *video_drm_pipeline_init(struct platform_device *parent);

void video_crtc_register(struct drm_device *drm, struct video_crtc *crtc);

#endif
