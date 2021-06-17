#ifndef VIDEO_VDMA_H
#define VIDEO_VDMA_H

struct vdma_channel;

u32 vdma_mm2s_get_px_width(struct vdma_channel *ch);
u32 vdma_mm2s_get_px_height(struct vdma_channel *ch);
u32 vdma_mm2s_get_bit_per_px(struct vdma_channel *ch);
u32 vdma_mm2s_get_px_per_inch(struct vdma_channel *ch);
u32 vdma_mm2s_get_stride(struct vdma_channel *ch);
int vdma_mm2s_set_data(struct vdma_channel *ch, const void *buffer, u32 size);
int vdma_mm2s_get_fb_addr(struct vdma_channel *ch, dma_addr_t *paddr, void **vaddr);

int vdma_get_px_format(struct vdma_channel *ch, u32 *fmt);
const char *vdma_get_name(struct vdma_channel *ch);

#endif
