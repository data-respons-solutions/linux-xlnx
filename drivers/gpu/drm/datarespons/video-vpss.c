#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/cdev.h>
#include <linux/slab.h>

#define VIDEO_VPSS_DEINTERLACE_CTRL_REGISTER		0x00
#define VIDEO_VPSS_DEINTERLACE_WIDTH_REGISTER		0x10
#define VIDEO_VPSS_DEINTERLACE_HEIGHT_REGISTER		0x18
#define VIDEO_VPSS_DEINTERLACE_READ_FB1_REGISTER	0x20
#define VIDEO_VPSS_DEINTERLACE_COLOR_FORMAT_REGISTER	0x30
#define VIDEO_VPSS_DEINTERLACE_ALGORITHM_REGISTER	0x38
#define VIDEO_VPSS_DEINTERLACE_READ_FB2_REGISTER	0x50

#define VIDEO_VPSS_VSCALER_CTRL_REGISTER	0x000
#define VIDEO_VPSS_VSCALER_HEIGHT_IN_REGISTER	0x010
#define VIDEO_VPSS_VSCALER_WIDTH_REGISTER	0x018
#define VIDEO_VPSS_VSCALER_HEIGHT_OUT_REGISTER	0x020
#define VIDEO_VPSS_VSCALER_LINE_RATE_REGISTER	0x028
#define VIDEO_VPSS_VSCALER_COLOR_MODE_REGISTER	0x030
#define VIDEO_VPSS_VSCALER_COEFF_REGISTER	0x800
#define VIDEO_VPSS_VSCALER_COEFF_REGISTER_IDX(x) \
		(VIDEO_VPSS_VSCALER_COEFF_REGISTER + ((x) * 0x4))

#define VIDEO_VPSS_HSCALER_CTRL_REGISTER		0x0000
#define VIDEO_VPSS_HSCALER_HEIGHT_REGISTER		0x0010
#define VIDEO_VPSS_HSCALER_WIDTH_IN_REGISTER		0x0018
#define VIDEO_VPSS_HSCALER_WIDTH_OUT_REGISTER		0x0020
#define VIDEO_VPSS_HSCALER_COLOR_MODE_IN_REGISTER	0x0028
#define VIDEO_VPSS_HSCALER_PIXEL_RATE_REGISTER		0x0030
#define VIDEO_VPSS_HSCALER_COLOR_MODE_OUT_REGISTER	0x0038
#define VIDEO_VPSS_HSCALER_COEFF_REGISTER		0x0800
#define VIDEO_VPSS_HSCALER_PHASES_REGISTER		0x4000
#define VIDEO_VPSS_HSCALER_COEFF_REGISTER_IDX(x) \
		(VIDEO_VPSS_HSCALER_COEFF_REGISTER + ((x) * 0x4))
#define VIDEO_VPSS_HSCALER_PHASES_REGISTER_IDX(x) \
		(VIDEO_VPSS_HSCALER_PHASES_REGISTER + ((x) * 0x4))

#define VIDEO_VPSS_LETTERBOX_CTRL_REGISTER		0x00
#define VIDEO_VPSS_LETTERBOX_WIDTH_REGISTER		0x10
#define VIDEO_VPSS_LETTERBOX_HEIGHT_REGISTER		0x18
#define VIDEO_VPSS_LETTERBOX_VIDEO_FORMAT_REGISTER	0x20
#define VIDEO_VPSS_LETTERBOX_COLUMN_START_REGISTER	0x28
#define VIDEO_VPSS_LETTERBOX_COLUMN_END_REGISTER	0x30
#define VIDEO_VPSS_LETTERBOX_ROW_START_REGISTER		0x38
#define VIDEO_VPSS_LETTERBOX_ROW_END_REGISTER		0x40
#define VIDEO_VPSS_LETTERBOX_YR_VALUE_REGISTER		0x48
#define VIDEO_VPSS_LETTERBOX_CBG_VALUE_REGISTER		0x50
#define VIDEO_VPSS_LETTERBOX_CRB_VALUE_REGISTER		0x58

#define VIDEO_VPSS_CHROMA_CTRL_REGISTER			0x00
#define VIDEO_VPSS_CHROMA_WIDTH_REGISTER		0x10
#define VIDEO_VPSS_CHROMA_HEIGHT_REGISTER		0x18
#define VIDEO_VPSS_CHROMA_COLOR_FORMAT_IN_REGISTER	0x20
#define VIDEO_VPSS_CHROMA_COLOR_FORMAT_OUT_REGISTER	0x28
#define VIDEO_VPSS_CHROMA_COEFF_REGISTER		0x30
#define VIDEO_VPSS_CHROMA_COEFF_REGISTER_IDX(x) \
		(VIDEO_VPSS_CHROMA_COEFF_REGISTER + ((x) * 0x08))

#define VIDEO_VPSS_CSC_CTRL_REGISTER			0x00
#define VIDEO_VPSS_CSC_VIDEO_FORMAT_IN_REGISTER		0x10
#define VIDEO_VPSS_CSC_VIDEO_FORMAT_OUT_REGISTER	0x18
#define VIDEO_VPSS_CSC_WIDTH_REGISTER			0x20
#define VIDEO_VPSS_CSC_HEIGHT_REGISTER			0x28
#define VIDEO_VPSS_CSC_COEFF_REGISTER			0x50
#define VIDEO_VPSS_CSC_COEFF_REGISTER_IDX(x) \
		(VIDEO_VPSS_CSC_COEFF_REGISTER + ((x) * 0x08))

#define VIDEO_VPSS_COLOR_FORMAT_RGB444		0x00
#define VIDEO_VPSS_COLOR_FORMAT_YCBCR444	0x01
#define VIDEO_VPSS_COLOR_FORMAT_YCBCR422	0x02
#define VIDEO_VPSS_COLOR_FORMAT_YCBCR420	0x03

#define VPSS_CMD_SET_BRIGHTNESS		0x445201
#define VPSS_CMD_GET_BRIGHTNESS		0x445202
#define VPSS_CMD_SET_CONTRAST		0x445203
#define VPSS_CMD_GET_CONTRAST		0x445204
#define VPSS_CMD_GET_COEFFICIENTS	0x445205
#define VPSS_CMD_SET_COEFFICIENTS	0x445206

#define VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK	(1 << 7)
#define VIDEO_VPSS_CTRL_AP_START_BITMASK	(1 << 0)

#define VIDEO_VPSS_ENABLE_OUTPUT_BITMASK	(1 << 1)
#define VIDEO_VPSS_CORE_START_BITMASK 		(1 << 0)

MODULE_LICENSE("PROPRIETARY");

static const u32 csc_ycbcr444_to_rgb444_coefficients[] = {
	0x000012A1, 0x00000000, 0x00001973, 0x000012A1, 0xFFFFF9BB, 0xFFFFF2FE, 0x000012A1,
	0x00002046, 0x00000000, 0xFFFFFF21, 0x00000070, 0xFFFFFEEB, 0x00000000, 0x000000FF
};

static const u32 csc_ycbcr422_to_rgb444_coefficients[] = {
	0x000012A1, 0x00000000, 0x00001973, 0x000012A1, 0xFFFFF9BB, 0xFFFFF2FE, 0x000012A1,
	0x00002046, 0x00000000, 0xFFFFFF21, 0x00000070, 0xFFFFFEEB, 0x00000000, 0x000000FF
};

static const u32 csc_rgb444_to_ycbcr444_coefficients[] = {
	0x0000041B, 0x00000810, 0x00000190, 0xFFFFFDA1, 0xFFFFFB59, 0x00000707, 0x00000707,
	0xFFFFFA1E, 0xFFFFFEDC, 0x00000010, 0x00000080, 0x00000080, 0x00000000, 0x000000FF
};

static const u32 chroma_422_444_coefficients[] = {
	0x00000000, 0x00000000, 0x00001000, 0x00000000, 0x000001FA, 0x00000606, 0x00000606,
	0x00000000, 0x00000000, 0x000001FA
};

static const u32 chroma_444_422_coefficients[] = {
	0x00000000, 0x00000400, 0x00000800, 0x00000400, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000
};

static const u32 scaling_coefficients[] = {
	0x00000000, 0x00001000, 0x00000000, 0xFFD80000, 0x002A1003, 0xFFFB0000, 0xFFB3FFFF,
	0x00571001, 0xFFF7FFFF, 0xFF91FFFE, 0x00860FFC, 0xFFF1FFFE, 0xFF71FFFC, 0x00B80FF2,
	0xFFEDFFFC, 0xFF53FFFA, 0x00ED0FE4, 0xFFE9FFF9, 0xFF37FFF8, 0x01240FD3, 0xFFE4FFF6,
	0xFF1EFFF5, 0x015E0FBD, 0xFFDFFFF3, 0xFF08FFF2, 0x019B0FA3, 0xFFDAFFEE, 0xFEF3FFEF,
	0x01DA0F86, 0xFFD5FFE9, 0xFEE1FFEB, 0x021B0F64, 0xFFD1FFE4, 0xFED1FFE8, 0x02600F3F,
	0xFFCAFFDE, 0xFEC3FFE4, 0x02A60F16, 0xFFC6FFD7, 0xFEB7FFE0, 0x02EF0EE9, 0xFFC2FFCF,
	0xFEADFFDB, 0x033A0EB8, 0xFFBFFFC7, 0xFEA5FFD7, 0x03870E84, 0xFFBAFFBF, 0xFE9FFFD3,
	0x03D60E4D, 0xFFB6FFB5, 0xFE9AFFCE, 0x04270E12, 0xFFB3FFAC, 0xFE97FFCA, 0x047A0DD3,
	0xFFB1FFA1, 0xFE96FFC6, 0x04CE0D92, 0xFFAEFF96, 0xFE97FFC2, 0x05250D4E, 0xFFA9FF8B,
	0xFE99FFBE, 0x057C0D07, 0xFFA6FF80, 0xFE9CFFBA, 0x05D50CBD, 0xFFA4FF74, 0xFEA1FFB6,
	0x062F0C71, 0xFFA2FF67, 0xFEA6FFB3, 0x06890C22, 0xFFA1FF5B, 0xFEADFFAF, 0x06E50BD1,
	0xFFA0FF4E, 0xFEB5FFAC, 0x07410B7E, 0xFF9FFF41, 0xFEBEFFA9, 0x079E0B2A, 0xFF9DFF34,
	0xFEC7FFA7, 0x07FB0AD3, 0xFF9DFF27, 0xFED2FFA4, 0x08580A7B, 0xFF9DFF1A, 0xFEDCFFA2,
	0x08B40A22, 0xFF9FFF0D, 0xFEE8FFA1, 0x091109C8, 0xFF9EFF00, 0xFEF4FF9F, 0x096D096D,
	0xFF9FFEF4, 0xFF00FF9F, 0x09C80911, 0xFFA0FEE8, 0xFF0DFF9E, 0x0A2208B4, 0xFFA3FEDC,
	0xFF1AFF9E, 0x0A7B0858, 0xFFA3FED2, 0xFF27FF9E, 0x0AD307FB, 0xFFA6FEC7, 0xFF34FF9E,
	0x0B2A079E, 0xFFA8FEBE, 0xFF41FF9F, 0x0B7E0741, 0xFFACFEB5, 0xFF4EFFA0, 0x0BD106E5,
	0xFFAFFEAD, 0xFF5BFFA1, 0x0C220689, 0xFFB3FEA6, 0xFF67FFA3, 0x0C71062F, 0xFFB5FEA1,
	0xFF74FFA5, 0x0CBD05D5, 0xFFB9FE9C, 0xFF80FFA7, 0x0D07057C, 0xFFBDFE99, 0xFF8BFFAA,
	0x0D4E0525, 0xFFC1FE97, 0xFF96FFAD, 0x0D9204CE, 0xFFC7FE96, 0xFFA1FFB0, 0x0DD3047A,
	0xFFCBFE97, 0xFFACFFB3, 0x0E120427, 0xFFCEFE9A, 0xFFB5FFB7, 0x0E4D03D6, 0xFFD2FE9F,
	0xFFBFFFBB, 0x0E840387, 0xFFD6FEA5, 0xFFC7FFBF, 0x0EB8033A, 0xFFDBFEAD, 0xFFCFFFC3,
	0x0EE902EF, 0xFFDFFEB7, 0xFFD7FFC7, 0x0F1602A6, 0xFFE3FEC3, 0xFFDEFFCC, 0x0F3F0260,
	0xFFE6FED1, 0xFFE4FFD1, 0x0F64021B, 0xFFEBFEE1, 0xFFE9FFD5, 0x0F8601DA, 0xFFEFFEF3,
	0xFFEEFFDA, 0x0FA3019B, 0xFFF2FF08, 0xFFF3FFDF, 0x0FBD015E, 0xFFF5FF1E, 0xFFF6FFE4,
	0x0FD30124, 0xFFF8FF37, 0xFFF9FFE8, 0x0FE400ED, 0xFFFBFF53, 0xFFFCFFED, 0x0FF200B8,
	0xFFFCFF71, 0xFFFEFFF2, 0x0FFC0086, 0xFFFDFF91, 0xFFFFFFF7, 0x10010057, 0xFFFFFFB3,
	0x0000FFFB, 0x1003002A, 0x0000FFD8
};

static const u32 scaling_phases[] = {
	0x1300100, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160,
	0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160, 0x1300140, 0x1500160
};

const u32 letterbox_coefficients[] = {
	0x00000000, 0x00000080, 0x00000080
};

#define MAX_VPSS_CHANNELS 8

struct vpss_properties
{
	void __iomem *regs;

	int brightness;
	int contrast;

	bool csc_supported;
	u32 csc_output;
	u32 csc_offset;
	u32 csc_coefficients[14];
};

struct vpss_channel {
	dev_t node;

	struct platform_device *pdev;
	struct device *dev;
	struct cdev cdev;
	struct class *pclass;

	struct vpss_properties prop;
};

static struct file_operations video_vpss_fops;
static const struct of_device_id video_vpss_of_ids[];
static struct platform_driver video_vpss_driver;

static int vpss_open(struct inode *ino, struct file *file);
static int vpss_release(struct inode *ino, struct file *file);
static void vpss_csc_set_coefficients(struct vpss_channel *ch);
static long vpss_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int vpss_init_cdevice(struct vpss_channel *ch, struct platform_device *pdev,
									const char *name);
static int video_vpss_probe(struct platform_device *pdev);
static int video_vpss_remove(struct platform_device *pdev);
static int video_vpss_configure_router(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs);
static int video_vpss_reset_sel_axis(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs);
static int video_vpss_reset_sel_axi_mm(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs);
static int video_vpss_reset_sel_scaler_gpio(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs);
static int video_vpss_configure_csc(struct platform_device *pdev, struct vpss_channel *ch,
						struct device_node *node, void __iomem *regs);
static int video_vpss_configure_chroma(struct platform_device *pdev,
			struct device_node *node, void __iomem *regs);
static int video_vpss_configure_letterbox(struct platform_device *pdev,
			struct device_node *node, void __iomem *regs);
static int video_vpss_configure_hscaler(struct platform_device *pdev,
			struct device_node *node, void __iomem *regs);
static int video_vpss_configure_vscaler(struct platform_device *pdev,
			struct device_node *node, void __iomem *regs);
static int video_vpss_configure_deinterlace(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs);

static u32 vpss_channels_probed = 0;
static struct vpss_channel *channels[MAX_VPSS_CHANNELS];

static int video_vpss_configure_router(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs)
{
	int rc;

	u32 offset;

	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in xbar router\n");
		return rc;
	}

	iowrite32(0x0, regs + offset + 0x00);
	iowrite32(0x8, regs + offset + 0x40);
	iowrite32(0x9, regs + offset + 0x44);
	iowrite32(0x1, regs + offset + 0x48);
	iowrite32(0x2, regs + offset + 0x50);
	iowrite32(0x4, regs + offset + 0x54);
	iowrite32(0x5, regs + offset + 0x60);
	iowrite32(0x0, regs + offset + 0x64);
	iowrite32(0x2, regs + offset + 0x00);

	dev_info(&pdev->dev, "xbar router configured\n");
	return 0;
}

static int video_vpss_reset_sel_axis(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs)
{
	int rc;

	u32 offset;

	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in reset sel axis\n");
		return rc;
	}
	iowrite32(VIDEO_VPSS_ENABLE_OUTPUT_BITMASK | VIDEO_VPSS_CORE_START_BITMASK, regs + offset);

	dev_info(&pdev->dev, "axis configured\n");
	return 0;
}

static int video_vpss_reset_sel_axi_mm(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs)
{
	int rc;

	u32 offset;

	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in reset sel axi mm\n");
		return rc;
	}
	iowrite32(VIDEO_VPSS_CORE_START_BITMASK, regs + offset);

	dev_info(&pdev->dev, "mm axis configured\n");
	return 0;
}

static int video_vpss_reset_sel_scaler_gpio(struct platform_device *pdev,
				struct device_node *node, void __iomem *regs)
{
	int rc;

	u32 offset;

	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in reset sel scaler\n");
		return rc;
	}
	iowrite32(VIDEO_VPSS_ENABLE_OUTPUT_BITMASK, regs + offset);

	dev_info(&pdev->dev, "scaler reset configured\n");
	return 0;
}

static int video_vpss_configure_csc
	(struct platform_device *pdev, struct vpss_channel *ch,
	struct device_node *node, void __iomem *regs)
{
	int rc;
	int i;

	u32 color_format_in;
	u32 color_format_out;
	u32 offset;
	u32 width;
	u32 height;

	rc = of_property_read_u32(node, "color-format-in", &color_format_in);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format-in in color space conversion\n");
		return rc;
	}
	rc = of_property_read_u32(node, "color-format-out", &color_format_out);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format-out in color space conversion\n");
		return rc;
	}
	rc = of_property_read_u32(node, "width", &width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing width in color space conversion\n");
		return rc;
	}
	rc = of_property_read_u32(node, "height", &height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing height in color space conversion\n");
		return rc;
	}
	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in color space conversion\n");
		return rc;
	}
	if ((color_format_in == VIDEO_VPSS_COLOR_FORMAT_YCBCR444) &&
		(color_format_out == VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
		(void)memcpy(ch->prop.csc_coefficients, csc_ycbcr444_to_rgb444_coefficients,
						sizeof(csc_ycbcr444_to_rgb444_coefficients));
	} else if ((color_format_in == VIDEO_VPSS_COLOR_FORMAT_RGB444) &&
		(color_format_out == VIDEO_VPSS_COLOR_FORMAT_YCBCR444)) {
		(void)memcpy(ch->prop.csc_coefficients, csc_rgb444_to_ycbcr444_coefficients,
						sizeof(csc_rgb444_to_ycbcr444_coefficients));
	} else if ((color_format_in == VIDEO_VPSS_COLOR_FORMAT_YCBCR422) &&
		(color_format_out == VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
		(void)memcpy(ch->prop.csc_coefficients, csc_ycbcr422_to_rgb444_coefficients,
						sizeof(csc_ycbcr422_to_rgb444_coefficients));
	} else {
		dev_err(&pdev->dev, "color space conversion is not supported\n");
		return -ENOTSUPP;
	}
	ch->prop.csc_offset = offset;
	ch->prop.csc_output = color_format_out;

	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK,
			regs + offset + VIDEO_VPSS_CSC_CTRL_REGISTER);
	iowrite32(color_format_in,  regs + offset + VIDEO_VPSS_CSC_VIDEO_FORMAT_IN_REGISTER);
	iowrite32(color_format_out, regs + offset + VIDEO_VPSS_CSC_VIDEO_FORMAT_OUT_REGISTER);
	iowrite32(width,            regs + offset + VIDEO_VPSS_CSC_WIDTH_REGISTER);
	iowrite32(height,           regs + offset + VIDEO_VPSS_CSC_HEIGHT_REGISTER);

	for(i = 0; i < 14; ++i) {
		iowrite32(ch->prop.csc_coefficients[i],
				regs + offset + VIDEO_VPSS_CSC_COEFF_REGISTER_IDX(i));
	}
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK | VIDEO_VPSS_CTRL_AP_START_BITMASK,
						regs + offset + VIDEO_VPSS_CSC_CTRL_REGISTER);

	dev_info(&pdev->dev, "color space conversion %ux%u @color %x->%x\n",
			width, height, color_format_in, color_format_out);
	return 0;
}

static int video_vpss_configure_chroma
	(struct platform_device *pdev, struct device_node *node, void __iomem *regs)
{
	int rc;
	int i;

	u32 color_format_out;
	u32 color_format_in;
	u32 offset;
	u32 width;
	u32 height;

	const u32 *coef = NULL;

	bool predefined_coefficients;

	rc = of_property_read_u32(node, "color-format-out", &color_format_out);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format-out in chroma\n");
		return rc;
	}
	rc = of_property_read_u32(node, "color-format-in", &color_format_in);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format-in in chroma\n");
		return rc;
	}
	rc = of_property_read_u32(node, "width", &width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing width in chroma\n");
		return rc;
	}
	rc = of_property_read_u32(node, "height", &height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing height in chroma\n");
		return rc;
	}
	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in chroma\n");
		return rc;
	}
	predefined_coefficients = of_property_read_bool(node, "predefined-coefficients");

	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK,
			regs + offset + VIDEO_VPSS_CHROMA_CTRL_REGISTER);
	iowrite32(width,            regs + offset + VIDEO_VPSS_CHROMA_WIDTH_REGISTER);
	iowrite32(height,           regs + offset + VIDEO_VPSS_CHROMA_HEIGHT_REGISTER);
	iowrite32(color_format_in,  regs + offset + VIDEO_VPSS_CHROMA_COLOR_FORMAT_IN_REGISTER);
	iowrite32(color_format_out, regs + offset + VIDEO_VPSS_CHROMA_COLOR_FORMAT_OUT_REGISTER);

	if ((color_format_in == VIDEO_VPSS_COLOR_FORMAT_YCBCR422) &&
		(color_format_out == VIDEO_VPSS_COLOR_FORMAT_YCBCR444)) {
		coef = chroma_422_444_coefficients;
	} else if ((color_format_in == VIDEO_VPSS_COLOR_FORMAT_YCBCR444) &&
		(color_format_out == VIDEO_VPSS_COLOR_FORMAT_YCBCR422)) {
		coef = chroma_444_422_coefficients;
	} else {
		dev_err(&pdev->dev, "chroma conversion not supported\n");
		return -EINVAL;
	}

	if (predefined_coefficients == false) {
		for(i = 0; i < 10; ++i) {
			iowrite32(coef[i], regs + offset + VIDEO_VPSS_CHROMA_COEFF_REGISTER_IDX(i));
		}
	}
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK | VIDEO_VPSS_CTRL_AP_START_BITMASK,
						regs + offset + VIDEO_VPSS_CHROMA_CTRL_REGISTER);

	dev_info(&pdev->dev, "chroma %ux%u @color %x->%x (%s) coefficients\n",
			width, height, color_format_in, color_format_out,
			predefined_coefficients ? "predefined" : "user-defined");
	return 0;
}

static int video_vpss_configure_letterbox
	(struct platform_device *pdev, struct device_node *node, void __iomem *regs)
{
	int rc;

	u32 width;
	u32 height;
	u32 offset;
	u32 color_format;

	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in letterbox\n");
		return rc;
	}
	rc = of_property_read_u32(node, "color-format", &color_format);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format in letterbox\n");
		return rc;
	}
	rc = of_property_read_u32(node, "width", &width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing width in letterbox\n");
		return rc;
	}
	rc = of_property_read_u32(node, "height", &height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing height in letterbox\n");
		return rc;
	}
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK,
			regs + offset + VIDEO_VPSS_LETTERBOX_CTRL_REGISTER);
	iowrite32(width,        regs + offset + VIDEO_VPSS_LETTERBOX_WIDTH_REGISTER);
	iowrite32(height,       regs + offset + VIDEO_VPSS_LETTERBOX_HEIGHT_REGISTER);
	iowrite32(color_format, regs + offset + VIDEO_VPSS_LETTERBOX_VIDEO_FORMAT_REGISTER);
	iowrite32(0x0,          regs + offset + VIDEO_VPSS_LETTERBOX_COLUMN_START_REGISTER);
	iowrite32(width,        regs + offset + VIDEO_VPSS_LETTERBOX_COLUMN_END_REGISTER);
	iowrite32(0x0,          regs + offset + VIDEO_VPSS_LETTERBOX_ROW_START_REGISTER);
	iowrite32(height,       regs + offset + VIDEO_VPSS_LETTERBOX_ROW_END_REGISTER);

	iowrite32(letterbox_coefficients[0],
			regs + offset + VIDEO_VPSS_LETTERBOX_YR_VALUE_REGISTER);
	iowrite32(letterbox_coefficients[1],
			regs + offset + VIDEO_VPSS_LETTERBOX_CBG_VALUE_REGISTER);
	iowrite32(letterbox_coefficients[2],
			regs + offset + VIDEO_VPSS_LETTERBOX_CRB_VALUE_REGISTER);

	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK | VIDEO_VPSS_CTRL_AP_START_BITMASK,
					regs + offset + VIDEO_VPSS_LETTERBOX_CTRL_REGISTER);

	dev_info(&pdev->dev, "letterbox %ux%u @color %x\n", width, height, color_format);
	return 0;
}

static int video_vpss_configure_hscaler
	(struct platform_device *pdev, struct device_node *node, void __iomem *regs)
{
	int rc;

	u32 offset;
	u32 color_format;
	u32 width_in;
	u32 width_out;
	u32 height;
	u32 i;

	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in horizontal scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "color-format", &color_format);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format in horizontal scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "width-in", &width_in);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing width-in in horizontal scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "width-out", &width_out);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing width-out in horizontal scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "height", &height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing height in horizontal scaler\n");
		return rc;
	}
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK,
			regs + offset + VIDEO_VPSS_HSCALER_CTRL_REGISTER);
	for(i = 0; i < sizeof(scaling_coefficients) / sizeof(scaling_coefficients[0]); ++i) {
		iowrite32(scaling_coefficients[i],
			regs + offset + VIDEO_VPSS_HSCALER_COEFF_REGISTER_IDX(i));
	}
	for(i = 0; i < sizeof(scaling_phases) / sizeof(scaling_phases[0]); ++i) {
		iowrite32(scaling_phases[i],
			regs + offset + VIDEO_VPSS_HSCALER_PHASES_REGISTER_IDX(i));
	}
	iowrite32(height,       regs + offset + VIDEO_VPSS_HSCALER_HEIGHT_REGISTER);
	iowrite32(width_in,     regs + offset + VIDEO_VPSS_HSCALER_WIDTH_IN_REGISTER);
	iowrite32(width_out,    regs + offset + VIDEO_VPSS_HSCALER_WIDTH_OUT_REGISTER);
	iowrite32(color_format, regs + offset + VIDEO_VPSS_HSCALER_COLOR_MODE_IN_REGISTER);

	iowrite32(width_in * 0x10000 / width_out,
			regs + offset + VIDEO_VPSS_HSCALER_PIXEL_RATE_REGISTER);

	iowrite32(color_format, regs + offset + VIDEO_VPSS_HSCALER_COLOR_MODE_OUT_REGISTER);
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK | VIDEO_VPSS_CTRL_AP_START_BITMASK,
					regs + offset + VIDEO_VPSS_HSCALER_CTRL_REGISTER);

	dev_info(&pdev->dev, "horizontal scaler %ux%u->%ux%u @color %x\n",
				width_in, height, width_out, height, color_format);
	return 0;
}

static int video_vpss_configure_vscaler
	(struct platform_device *pdev, struct device_node *node, void __iomem *regs)
{
	int rc;

	u32 offset;
	u32 color_format;
	u32 width;
	u32 height_in;
	u32 height_out;
	u32 i;

	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in vertical scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "color-format", &color_format);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color-format in vertical scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "width", &width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing width in vertical scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "height-in", &height_in);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing height-in in vertical scaler\n");
		return rc;
	}
	rc = of_property_read_u32(node, "height-out", &height_out);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing height-out in vertical scaler\n");
		return rc;
	}
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK,
			regs + offset + VIDEO_VPSS_VSCALER_CTRL_REGISTER);
	for(i = 0; i < sizeof(scaling_coefficients) / sizeof(scaling_coefficients[0]); ++i) {
		iowrite32(scaling_coefficients[i],
			regs + offset + VIDEO_VPSS_VSCALER_COEFF_REGISTER_IDX(i));
	}
	iowrite32(height_in,  regs + offset + VIDEO_VPSS_VSCALER_HEIGHT_IN_REGISTER);
	iowrite32(width,      regs + offset + VIDEO_VPSS_VSCALER_WIDTH_REGISTER);
	iowrite32(height_out, regs + offset + VIDEO_VPSS_VSCALER_HEIGHT_OUT_REGISTER);

	iowrite32(height_in * 0x10000 / height_out,
				regs + offset + VIDEO_VPSS_VSCALER_LINE_RATE_REGISTER);

	iowrite32(color_format, regs + offset + VIDEO_VPSS_VSCALER_COLOR_MODE_REGISTER);
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK | VIDEO_VPSS_CTRL_AP_START_BITMASK,
						regs + offset + VIDEO_VPSS_VSCALER_CTRL_REGISTER);

	dev_info(&pdev->dev, "vertical scaler %ux%u->%ux%u @color %x\n",
			width, height_in, width, height_out, color_format);
	return 0;
}

static int video_vpss_configure_deinterlace
	(struct platform_device *pdev, struct device_node *node, void __iomem *regs)
{
	int rc;

	struct device_node *mem_node;
	struct resource mem_res;
	u32 mem_size;

	u32 algorithm;
	u32 offset;
	u32 color_format;
	u32 bpp;
	u32 width;
	u32 height;

	void *fb_mem;
	dma_addr_t fb_phys_mem;

	rc = of_property_read_u32(node, "algorithm", &algorithm);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing algorithm in deinterlace\n");
		return rc;
	}
	rc = of_property_read_u32(node, "color-format", &color_format);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing color format in deinterlace\n");
		return rc;
	}
	rc = of_property_read_u32(node, "bpp", &bpp);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing bpp in deinterlace\n");
		return rc;
	}
	rc = of_property_read_u32(node, "width", &width);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing width in deinterlace\n");
		return rc;
	}
	rc = of_property_read_u32(node, "height", &height);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing height in deinterlace\n");
		return rc;
	}
	rc = of_property_read_u32(node, "offset", &offset);
	if (rc < 0) {
		dev_err(&pdev->dev, "missing offset in deinterlace\n");
		return rc;
	}
	mem_node = of_parse_phandle(node, "memory-addr", 0);
	mem_size = (bpp / 8) * width * height;
	if ((mem_node != NULL) && (IS_ERR(mem_node) == false)) {
		rc = of_address_to_resource(mem_node, 0, &mem_res);
		if (rc != 0) {
			dev_err(&pdev->dev, "cannot map memory resource\n");
			return rc;
		}
		if (resource_size(&mem_res) != mem_size) {
			dev_err(&pdev->dev,
				"invalid deinterlace memory size: %llu/%u\n",
				resource_size(&mem_res), mem_size);
			return -EINVAL;
		}
		fb_phys_mem = mem_res.start;
		fb_mem = memremap(mem_res.start, mem_size, MEMREMAP_WB);
	} else {
		fb_mem = dmam_alloc_coherent(&pdev->dev, mem_size, &fb_phys_mem, GFP_KERNEL);
	}
	if (IS_ERR(fb_mem)) {
		dev_err(&pdev->dev, "fb allocation error\n");
		return PTR_ERR(fb_mem);
	}
	if (fb_mem == NULL) {
		dev_err(&pdev->dev, "fb allocation error\n");
		return -ENOMEM;
	}
	mem_node = of_parse_phandle(node, "memory-block", 0);
	if ((mem_node != NULL) && (IS_ERR(mem_node) == false)) {
		rc = of_address_to_resource(mem_node, 0, &mem_res);
		if (rc == 0) {
			if (fb_phys_mem < mem_res.start) {
				dev_err(&pdev->dev,
					"invalid offset in deinterlace memory\n");
				return -EINVAL;
			}
			fb_phys_mem -= mem_res.start;
		}
	}
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK,
			regs + offset + VIDEO_VPSS_DEINTERLACE_CTRL_REGISTER);
	iowrite32(width,        regs + offset + VIDEO_VPSS_DEINTERLACE_WIDTH_REGISTER);
	iowrite32(height / 2,   regs + offset + VIDEO_VPSS_DEINTERLACE_HEIGHT_REGISTER);
	iowrite32(fb_phys_mem,  regs + offset + VIDEO_VPSS_DEINTERLACE_READ_FB1_REGISTER);
	iowrite32(color_format, regs + offset + VIDEO_VPSS_DEINTERLACE_COLOR_FORMAT_REGISTER);
	iowrite32(algorithm,    regs + offset + VIDEO_VPSS_DEINTERLACE_ALGORITHM_REGISTER);
	iowrite32(fb_phys_mem,  regs + offset + VIDEO_VPSS_DEINTERLACE_READ_FB2_REGISTER);
	iowrite32(VIDEO_VPSS_CTRL_AUTO_RESTART_BITMASK | VIDEO_VPSS_CTRL_AP_START_BITMASK,
					regs + offset + VIDEO_VPSS_DEINTERLACE_CTRL_REGISTER);

	dev_info(&pdev->dev, "deinterlace %ux%u algorithm %x @color %x %ubpp\n",
					width, height, algorithm, color_format, bpp);
	return 0;
}

static int vpss_open(struct inode *ino, struct file *file)
{
	u32 i;

	for (i = 0; i < vpss_channels_probed; ++i) {
		if (ino->i_rdev == channels[i]->node) {
			file->private_data = channels[i];
			return 0;
		}
	}
	return -ENOENT;
}

static int vpss_release(struct inode *ino, struct file *file)
{
	(void)ino;
	(void)file;

	return 0;
}

static void vpss_csc_set_coefficients(struct vpss_channel *ch)
{
	int K[12];
	int i;
	int contrast_factor;

	contrast_factor = (3 * ch->prop.contrast) + 1000;
	for(i = 0; i < 12; ++i) {
		K[i] = ((int)ch->prop.csc_coefficients[i] * contrast_factor) / 1000;
	}
	for(i = 9; i < 12; ++i) {
		K[i] += ch->prop.brightness * 2;
		if((K[i] & (1 << 11)) && ((K[i] & 0xFFF) < 0xE00)) {
			// Negative overflowed value
			K[i] = 0xFFFFFE00;
		}
		if(((K[i] & (1 << 11)) == 0) && ((K[i] & 0xFFF) > 0x1FF)) {
			// Positive overflowed value
			K[i] = 0x1FF;
		}
	}

	for(i = 0; i < 12; ++i) {
		iowrite32((u32)K[i],
			ch->prop.regs + ch->prop.csc_offset + VIDEO_VPSS_CSC_COEFF_REGISTER_IDX(i));
	}
}

static long vpss_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vpss_channel *ch;
	int arg_int;

	u32 *coef;

	ch = (struct vpss_channel *)file->private_data;
	switch(cmd)
	{
		case VPSS_CMD_SET_BRIGHTNESS:
			if((ch->prop.csc_supported == false) ||
				(ch->prop.csc_output != VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
				dev_err(&ch->pdev->dev, "ioctl command not supported\n");
				return -ENOTSUPP;
			}
			arg_int = (int)arg;
			if((arg_int >= -127) && (arg_int <= 127)) {
				ch->prop.brightness = arg_int;
				vpss_csc_set_coefficients(ch);
				dev_info(&ch->pdev->dev, "brightness set to %d\n", arg_int);
				return 0;
			} else {
				dev_err(&ch->pdev->dev, "invalid brightness value: %d\n", arg_int);
				return -EINVAL;
			}

		case VPSS_CMD_SET_CONTRAST:
			if((ch->prop.csc_supported == false) ||
				(ch->prop.csc_output != VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
				dev_err(&ch->pdev->dev, "ioctl command not supported\n");
				return -ENOTSUPP;
			}
			arg_int = (int)arg;
			if((arg_int >= -127) && (arg_int <= 127)) {
				ch->prop.contrast = arg_int;
				vpss_csc_set_coefficients(ch);
				dev_info(&ch->pdev->dev, "contrast set to %d\n", arg_int);
				return 0;
			} else {
				dev_err(&ch->pdev->dev, "invalid contrast value: %d\n", arg_int);
				return -EINVAL;
			}

		case VPSS_CMD_GET_BRIGHTNESS:
			if((ch->prop.csc_supported == false) ||
				(ch->prop.csc_output != VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
				dev_err(&ch->pdev->dev, "ioctl command not supported\n");
				return -ENOTSUPP;
			}
			*(int *)arg = ch->prop.brightness;
			return 0;

		case VPSS_CMD_GET_CONTRAST:
			if((ch->prop.csc_supported == false) ||
				(ch->prop.csc_output != VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
				dev_err(&ch->pdev->dev, "ioctl command not supported\n");
				return -ENOTSUPP;
			}
			*(int *)arg = ch->prop.contrast;
			return 0;

		case VPSS_CMD_GET_COEFFICIENTS:
			if((ch->prop.csc_supported == false) ||
				(ch->prop.csc_output != VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
				dev_err(&ch->pdev->dev, "ioctl command not supported\n");
				return -ENOTSUPP;
			}
			coef = (u32 *)arg;
			(void)memcpy(coef, ch->prop.csc_coefficients, sizeof(ch->prop.csc_coefficients));
			return 0;

		case VPSS_CMD_SET_COEFFICIENTS:
			if((ch->prop.csc_supported == false) ||
				(ch->prop.csc_output != VIDEO_VPSS_COLOR_FORMAT_RGB444)) {
				dev_err(&ch->pdev->dev, "ioctl command not supported\n");
				return -ENOTSUPP;
			}
			coef = (u32 *)arg;
			(void)memcpy(ch->prop.csc_coefficients, coef, sizeof(ch->prop.csc_coefficients));

			vpss_csc_set_coefficients(ch);
			return 0;

		default:
			dev_err(&ch->pdev->dev, "ioctl command not supported: 0x%x(0x%lx)\n",
											cmd, arg);
			return -ENOTSUPP;
	}
}

static int vpss_init_cdevice(struct vpss_channel *ch, struct platform_device *pdev, const char *name)
{
	int rc;
	size_t i;
	const char *dev_name = NULL;
	struct class *pclass = NULL;

	for(i = 0; i < strlen(name); ++i) {
		// EX format: b0100000.v_vpss
		if(name[i] == '.') {
			dev_name = &name[i + 1];
		}
	}
	for(i = 0; i < MAX_VPSS_CHANNELS; ++i) {
		if((channels[i] != NULL) && (channels[i]->pclass != NULL)) {
			pclass = channels[i]->pclass;
			break;
		}
	}
	if(dev_name == NULL) {
		dev_name = name;
	}
	rc = alloc_chrdev_region(&ch->node, 0, 1, dev_name);
	if (rc != 0) {
		dev_err(&pdev->dev, "unable to get a char device number\n");
		return rc;
	}
	cdev_init(&ch->cdev, &video_vpss_fops);
	ch->cdev.owner = THIS_MODULE;

	rc = cdev_add(&ch->cdev, ch->node, 1);
	if (rc != 0) {
		dev_err(&pdev->dev, "unable to add char device\n");
		return rc;
	}
	if(pclass != NULL) {
		ch->pclass = pclass;
	} else {
		ch->pclass = class_create(THIS_MODULE, "vpss");
	}
	if (ch->pclass == NULL) {
		dev_err(&pdev->dev, "unable to create class\n");
		return -ENOMEM;
	}
	if (IS_ERR(ch->pclass)) {
		dev_err(&pdev->dev, "unable to create class\n");
		return PTR_ERR(ch->pclass);
	}
	ch->dev = device_create(ch->pclass, NULL, ch->node, NULL, dev_name);
	if (ch->dev == NULL) {
		dev_err(&pdev->dev, "unable to create the device\n");
		return -ENOMEM;
	}
	if (IS_ERR(ch->dev)) {
		dev_err(&pdev->dev, "unable to create the device\n");
		return PTR_ERR(ch->dev);
	}
	return 0;
}

static int video_vpss_probe(struct platform_device *pdev)
{
	int rc;

	void __iomem *regs;
	struct device_node *node;
	struct device_node *child;

	node = pdev->dev.of_node;
	if (vpss_channels_probed >= MAX_VPSS_CHANNELS) {
		dev_err(&pdev->dev, "vpss channel is out of bounds\n");
		return -ERANGE;
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

	channels[vpss_channels_probed] = kzalloc(sizeof(struct vpss_channel), GFP_KERNEL);
	if (channels[vpss_channels_probed] == NULL) {
		dev_err(&pdev->dev, "cannot allocate memory for vpss channel %u\n",
								vpss_channels_probed);
		return -ENOMEM;
	}
	if (IS_ERR(channels[vpss_channels_probed])) {
		dev_err(&pdev->dev, "cannot allocate memory for vpss channel %u\n",
								vpss_channels_probed);
		return PTR_ERR(channels[vpss_channels_probed]);
	}
	channels[vpss_channels_probed]->pdev = pdev;
	channels[vpss_channels_probed]->prop.regs = regs;
	channels[vpss_channels_probed]->prop.brightness = 0;
	channels[vpss_channels_probed]->prop.contrast = 0;
	channels[vpss_channels_probed]->prop.csc_supported = false;

	child = of_get_child_by_name(node, "reset-sel-axi-mm");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_reset_sel_axi_mm(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "reset-sel-axis");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_reset_sel_axis(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "reset-scaler-gpio");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_reset_sel_scaler_gpio(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "xbar-router");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_configure_router(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "deinterlace");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_configure_deinterlace(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "vscaler");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_configure_vscaler(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "hscaler");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_configure_hscaler(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "letterbox");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_configure_letterbox(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "chroma");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		rc = video_vpss_configure_chroma(pdev, child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	child = of_get_child_by_name(node, "csc");
	if ((child != NULL) && (IS_ERR(child) == false)) {
		channels[vpss_channels_probed]->prop.csc_supported = true;
		rc = video_vpss_configure_csc(pdev, channels[vpss_channels_probed], child, regs);
		if (rc < 0) {
			return rc;
		}
	}

	rc = vpss_init_cdevice(channels[vpss_channels_probed], pdev, pdev->name);
	if (rc < 0) {
		return rc;
	}

	dev_info(&pdev->dev, "%u initialized\n", vpss_channels_probed);
	++vpss_channels_probed;
	return 0;
}

static int video_vpss_remove(struct platform_device *pdev)
{
	(void)pdev;
	return 0;
}

static const struct of_device_id video_vpss_of_ids[] = {
	{ .compatible = "datarespons,video-vpss",},
	{}
};

static struct platform_driver video_vpss_driver = {
	.driver = {
		.name = "video_vpss_driver",
		.owner = THIS_MODULE,
		.of_match_table = video_vpss_of_ids,
	},
	.probe = video_vpss_probe,
	.remove = video_vpss_remove,
};

static struct file_operations video_vpss_fops = {
	.owner    = THIS_MODULE,
	.open     = vpss_open,
	.release  = vpss_release,
	.unlocked_ioctl = vpss_ioctl,
};

module_platform_driver(video_vpss_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("Video VPSS Driver");
MODULE_LICENSE("PROPRIETARY");
