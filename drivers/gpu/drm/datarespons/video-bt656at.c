#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/fs.h>

#define MAX_TRANSCODERS 4

#define BT656AT_CONTROL_REGISTER       	0x00
#define BT656AT_STATUS_REGISTER        	0x04
#define BT656AT_DETECTED_SIZE_REGISTER 	0x08
#define BT656AT_FMT_ERR_REGISTER	0x0C

#define BT656AT_CONTROL_RUN_BITMASK		(1 << 0)
#define BT656AT_CONTROL_RESET_BITMASK		(1 << 1)
#define BT656AT_CONTROL_RESET_SIZE_BITMASK	(1 << 2)

#define BT656AT_STATUS_FIFO_EMPTY_BITMASK	(1 << 0)
#define BT656AT_STATUS_OVERFLOW_BITMASK		(1 << 2)
#define BT656AT_STATUS_HFMT_ERROR_BITMASK	(1 << 3)
#define BT656AT_STATUS_VFMT_ERROR_BITMASK	(1 << 4)

#define BT656AT_DETECTED_SIZE_WIDTH_BITMASK	0x00000FFF
#define BT656AT_DETECTED_SIZE_HEIGHT_BITMASK	0x0FFF0000

#define BT656AT_CMD_GET_STATUS 	0x445201
#define BT656AT_CMD_START_STOP	0x445202
#define BT656AT_CMD_RESET	0x445203
#define BT656AT_CMD_SIZE_RESET	0x445204

struct bt656at_status
{
	char name[64];
	bool running;
	bool fifo_empty;
	bool fifo_overflow;
	bool hfmt_error;
	bool vfmt_error;
	u32 width;
	u32 height;
	u32 fmt_errors;
};

struct bt656at
{
	void __iomem *regs;

	dev_t node;

	struct platform_device *pdev;
	struct class *pclass;
	struct device *dev;
	struct cdev cdev;
} *transcoders[MAX_TRANSCODERS];

static const struct of_device_id bt656at_of_ids[];
static struct platform_driver bt656at_driver;
static struct file_operations bt656at_fops;

static int bt656at_open(struct inode *ino, struct file *file);
static int bt656at_release(struct inode *ino, struct file *file);
static long bt656at_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int bt656at_init_cdevice(struct bt656at *t);
static int bt656at_probe(struct platform_device *pdev);
static int bt656at_remove(struct platform_device *pdev);

static u32 probed_transcoders = 0;

static int bt656at_open(struct inode *ino, struct file *file)
{
	u32 i;

	for (i = 0; i < probed_transcoders; ++i) {
		if (ino->i_rdev == transcoders[i]->node) {
			file->private_data = transcoders[i];
			return 0;
		}
	}
	return -ENOENT;
}

static int bt656at_release(struct inode *ino, struct file *file)
{
	(void)ino;
	(void)file;

	return 0;
}

static long bt656at_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct bt656at *t;
	struct bt656at_status *status;

	u32 reg;

	t = (struct bt656at *)file->private_data;
	switch(cmd)
	{
		case BT656AT_CMD_GET_STATUS:
			status = (struct bt656at_status *)arg;
			(void)strncpy(status->name, t->pdev->name, sizeof(status->name));

			reg = ioread32(t->regs + BT656AT_CONTROL_REGISTER);
			status->running = ((reg & BT656AT_CONTROL_RUN_BITMASK) != 0);

			reg = ioread32(t->regs + BT656AT_STATUS_REGISTER);
			status->fifo_empty = ((reg & BT656AT_STATUS_FIFO_EMPTY_BITMASK) != 0);
			status->fifo_overflow = ((reg &	BT656AT_STATUS_OVERFLOW_BITMASK) != 0);
			status->hfmt_error = ((reg & BT656AT_STATUS_HFMT_ERROR_BITMASK) != 0);
			status->vfmt_error = ((reg & BT656AT_STATUS_VFMT_ERROR_BITMASK) != 0);

			reg = ioread32(t->regs + BT656AT_DETECTED_SIZE_REGISTER);
			status->width = (reg & BT656AT_DETECTED_SIZE_WIDTH_BITMASK);
			status->height = ((reg & BT656AT_DETECTED_SIZE_HEIGHT_BITMASK) >> 16);

			status->fmt_errors = ioread32(t->regs + BT656AT_FMT_ERR_REGISTER);
			return 0;

		case BT656AT_CMD_START_STOP:
			reg = ioread32(t->regs + BT656AT_CONTROL_REGISTER);
			if (arg != 0) {
				reg |= BT656AT_CONTROL_RUN_BITMASK;
			} else {
				reg &= ~BT656AT_CONTROL_RUN_BITMASK;
			}
			iowrite32(reg, t->regs + BT656AT_CONTROL_REGISTER);
			return 0;

		case BT656AT_CMD_RESET:
			reg = ioread32(t->regs + BT656AT_CONTROL_REGISTER);
			if (arg != 0) {
				reg |= BT656AT_CONTROL_RESET_BITMASK;
			} else {
				reg &= ~BT656AT_CONTROL_RESET_BITMASK;
			}
			iowrite32(reg, t->regs + BT656AT_CONTROL_REGISTER);
			return 0;

		case BT656AT_CMD_SIZE_RESET:
			reg = ioread32(t->regs + BT656AT_CONTROL_REGISTER);
			if (arg != 0) {
				reg |= BT656AT_CONTROL_RESET_SIZE_BITMASK;
			} else {
				reg &= ~BT656AT_CONTROL_RESET_SIZE_BITMASK;
			}
			iowrite32(reg, t->regs + BT656AT_CONTROL_REGISTER);
			return 0;

		default:
			dev_err(&t->pdev->dev, "ioctl command not supported: 0x%x\n", cmd);
			return -ENOTSUPP;
	}
	return -EINVAL;
}

static int bt656at_init_cdevice(struct bt656at *t)
{
	int rc;
	size_t i;
	const char *dev_name = NULL;
	struct class *pclass = NULL;

	for (i = 0; i < strlen(t->pdev->name); ++i) {
		// EX format: b0100000.bt656a0
		if (t->pdev->name[i] == '.') {
			dev_name = &t->pdev->name[i + 1];
		}
	}
	for (i = 0; i < MAX_TRANSCODERS; ++i) {
		if (transcoders[i] && transcoders[i]->pclass) {
			pclass = transcoders[i]->pclass;
			break;
		}
	}
	if (dev_name == NULL) {
		dev_name = t->pdev->name;
	}
	rc = alloc_chrdev_region(&t->node, 0, 1, dev_name);
	if (rc != 0) {
		dev_err(&t->pdev->dev, "unable to get a char device number\n");
		return rc;
	}
	cdev_init(&t->cdev, &bt656at_fops);
	t->cdev.owner = THIS_MODULE;

	rc = cdev_add(&t->cdev, t->node, 1);
	if (rc != 0) {
		dev_err(&t->pdev->dev, "unable to add char device\n");
		return rc;
	}
	if (pclass != NULL) {
		t->pclass = pclass;
	} else {
		t->pclass = class_create(THIS_MODULE, "bt656at");
	}
	if (t->pclass == NULL) {
		dev_err(&t->pdev->dev, "unable to create the device class\n");
		return -ENOMEM;
	}
	if (IS_ERR(t->pclass)) {
		dev_err(&t->pdev->dev, "unable to create the device class\n");
		return PTR_ERR(t->pclass);
	}
	t->dev = device_create(t->pclass, NULL, t->node, NULL, dev_name);
	if (t->dev == NULL) {
		dev_err(&t->pdev->dev, "unable to create the char device\n");
		return -ENOMEM;
	}
	if (IS_ERR(t->dev)) {
		dev_err(&t->pdev->dev, "unable to create the char device\n");
		return PTR_ERR(t->dev);
	}
	return 0;
}

static int bt656at_probe(struct platform_device *pdev)
{
	int rc;
	void __iomem *regs;

	if (probed_transcoders >= MAX_TRANSCODERS) {
		dev_err(&pdev->dev, "bt656 axis transcoder is out of bounds\n");
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
	transcoders[probed_transcoders] = kzalloc(sizeof(struct bt656at), GFP_KERNEL);
	if (transcoders[probed_transcoders] == NULL) {
		dev_err(&pdev->dev,
			"cannot allocate memory for bt656 axis transcoder %u\n",
			probed_transcoders);
		return -ENOMEM;
	}
	if (IS_ERR(transcoders[probed_transcoders])) {
		dev_err(&pdev->dev,
			"cannot allocate memory for bt656 axis transcoder %u\n",
			probed_transcoders);
		return PTR_ERR(transcoders[probed_transcoders]);
	}
	transcoders[probed_transcoders]->pdev = pdev;
	transcoders[probed_transcoders]->regs = regs;

	iowrite32(BT656AT_CONTROL_RUN_BITMASK, regs + BT656AT_CONTROL_REGISTER);

	rc = bt656at_init_cdevice(transcoders[probed_transcoders]);
	if (rc != 0) {
		return rc;
	}

	dev_info(&pdev->dev, "%u initialized\n", probed_transcoders);
	++probed_transcoders;
	return 0;
}

static int bt656at_remove(struct platform_device *pdev)
{
	u32 i;
	u32 reg;

	for (i = 0; i < probed_transcoders; ++i) {
		if (transcoders[i]->pdev == pdev) {
			reg = ioread32(transcoders[i]->regs + BT656AT_CONTROL_REGISTER);
			reg &= ~BT656AT_CONTROL_RUN_BITMASK;
			iowrite32(reg, transcoders[i]->regs + BT656AT_CONTROL_REGISTER);
		}
	}
	return 0;
}

static const struct of_device_id bt656at_of_ids[] = {
	{ .compatible = "datarespons,bt656-axis-transcoder", },
	{}
};

static struct platform_driver bt656at_driver = {
	.driver = {
		.name = "bt656at_driver",
		.owner = THIS_MODULE,
		.of_match_table = bt656at_of_ids,
	},
	.probe = bt656at_probe,
	.remove = bt656at_remove,
};

static struct file_operations bt656at_fops = {
	.owner    = THIS_MODULE,
	.open     = bt656at_open,
	.release  = bt656at_release,
	.unlocked_ioctl = bt656at_ioctl,
};

module_platform_driver(bt656at_driver);

MODULE_AUTHOR("Data Respons");
MODULE_DESCRIPTION("BT656 AXIS Transcoder");
MODULE_LICENSE("PROPRIETARY");
