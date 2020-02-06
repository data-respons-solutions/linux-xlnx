#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include <linux/nvmem-provider.h>

#define CADENCE_BROKEN
#define RAM_SIZE 0x10000

struct micro_23lcv512_priv {
	struct nvmem_config conf;
	struct spi_device *spi;
	u8 *tx;
	u8 *rx;
};

static int micro_23lcv512_read(void* handle, unsigned int offset,
		void *val, size_t bytes)
{
	struct micro_23lcv512_priv *priv = handle;
	int rc;
	struct spi_transfer tfr;
	if (offset + bytes > RAM_SIZE)
		return -EINVAL;

	tfr.rx_buf = priv->rx;
	tfr.len = 3+bytes;
	tfr.tx_buf = priv->tx;
	tfr.bits_per_word = 8;

	priv->tx[0] = 0x3;
	priv->tx[1] = (offset >> 8) & 0xff;
	priv->tx[2] = offset & 0xff;
	rc = spi_sync_transfer(priv->spi, &tfr, 1);
	if (rc < 0)
		dev_err(&priv->spi->dev, "%s failed for offs %d, sz %d return %d",
				__func__, offset, (int)bytes, rc);
	memcpy(val, priv->rx+3, bytes);
	return rc;
}

static int micro_23lcv512_read_slow(void* handle, unsigned int offset,
		void *val, size_t bytes)
{
	struct micro_23lcv512_priv *priv = handle;
	int rc=0;
	int n;
	unsigned index=offset;
	u8 *rp=val;

	priv->tx[0] = 0x3;
	for (n=0 ; n < bytes; n++, index++, rp++) {
		priv->tx[1] = (index >> 8) & 0xff;
		priv->tx[2] = index & 0xff;
		rc = spi_write_then_read(priv->spi, priv->tx, 3, rp, 1);
		if (rc < 0)
			break;
	}
	if (rc < 0)
		dev_err(&priv->spi->dev, "%s failed for offs %d, sz %d return %d",
				__func__, offset, (int)bytes, rc);
	return rc;
}

static int micro_23lcv512_write_slow(void* handle, unsigned int offset,
		void *val, size_t bytes)
{
	struct micro_23lcv512_priv *priv = handle;
	int rc=0;
	int n;
	unsigned index=offset;
	u8 *rp=val;

	priv->tx[0] = 0x2;
	for (n=0 ; n < bytes; n++, index++, rp++) {
		priv->tx[1] = (index >> 8) & 0xff;
		priv->tx[2] = index & 0xff;
		priv->tx[3] = *rp;
		rc = spi_write_then_read(priv->spi, priv->tx, 4, priv->rx, 0);
		if (rc < 0)
			break;
	}
	if (rc < 0)
		dev_err(&priv->spi->dev, "%s failed for offs %d, sz %d return %d",
				__func__, offset, (int)bytes, rc);
	return rc;
}

static int micro_23lcv512_write(void* handle, unsigned int offset,
		void *val, size_t bytes)
{
	struct micro_23lcv512_priv *priv = handle;
	int rc;
	struct spi_transfer tfr;
	if (offset + bytes > RAM_SIZE)
		return -EINVAL;

	tfr.rx_buf = priv->rx;
	tfr.len = 3+bytes;
	tfr.tx_buf = priv->tx;
	tfr.bits_per_word = 8;

	priv->tx[0] = 0x2;
	priv->tx[1] = (offset >> 8) & 0xff;
	priv->tx[2] = offset & 0xff;
	memcpy(priv->tx + 3, val, bytes);
	rc = spi_sync_transfer(priv->spi, &tfr, 1);
	if (rc < 0)
		dev_err(&priv->spi->dev, "%s failed %d", __func__, rc);
	return rc;
}

static int micro_23lcv512_spi_probe(struct spi_device *spi)
{
	struct nvmem_device *nvmem;
	int rc;
	u8 buf[2];
	struct micro_23lcv512_priv *priv = devm_kzalloc(&spi->dev,
			sizeof(struct micro_23lcv512_priv), GFP_KERNEL);
	if (priv == 0)
		return -ENOMEM;

	priv->tx = devm_kzalloc(&spi->dev, RAM_SIZE+32, GFP_KERNEL);
	priv->rx = devm_kzalloc(&spi->dev, RAM_SIZE+32, GFP_KERNEL);
	if (priv->tx == 0 || priv->rx == 0) {
		dev_err(&spi->dev, "Failed to alloc buffers\n");
		return -ENOMEM;
	}
	priv->conf.dev = &spi->dev;
	priv->conf.owner = THIS_MODULE;
	priv->conf.priv = priv;
	priv->conf.size = RAM_SIZE;
	priv->conf.name = "micro-nvram",
	priv->conf.stride = 1,
	priv->conf.word_size = 1,
#ifdef CADENCE_BROKEN
	priv->conf.reg_read = micro_23lcv512_read_slow;
	priv->conf.reg_write = micro_23lcv512_write_slow,
#else
	priv->conf.reg_read = micro_23lcv512_read,
	priv->conf.reg_write = micro_23lcv512_write,
#endif

	priv->spi = spi;
	spi_set_drvdata(spi, priv);

	rc = spi_w8r8(spi, 0x5);
	if (rc >= 0) {
		dev_info(&spi->dev, "Found NVRAM in mode %d\n", (rc & 0xc0) >> 6);
	}
	else {
		dev_err(&spi->dev, "No nvram found (%d)", rc);
		return rc;
	}
#ifdef CADENCE_BROKEN
	/* Set byte mode */
	buf[0] = 0x1;
	buf[1] = 0x0;
	spi_write(spi, buf, 2);
#endif
	nvmem = devm_nvmem_register(&spi->dev, &priv->conf);
	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id micro_23lcv512_spi_of_match[] = {
	{
		.compatible = "micro,23lcv512",
	},
	{},
};

MODULE_DEVICE_TABLE(of, micro_23lcv512_spi_of_match);

static struct spi_driver micro_23lcv512_driver = {
	.driver = {
		.name = "micro_23lcv512",
		.of_match_table = of_match_ptr(micro_23lcv512_spi_of_match),
	},
	.probe = micro_23lcv512_spi_probe,
};
module_spi_driver(micro_23lcv512_driver);

MODULE_AUTHOR("Hans Christian Lonstad <hcl@datarespons.no>");
MODULE_DESCRIPTION("Microsemi SPI nvram driver");
MODULE_LICENSE("GPL v2");
