/*
 * dev - Device functions
 *
 * Copyright (C) 2015 Javier González <javier@cnexlabs.com>
 * Copyright (C) 2015 Matias Bjørling <matias@cnexlabs.com>
 * Copyright (C) 2016 Simon A. F. Lund <slund@cnexlabs.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <libudev.h>
#include <linux/lightnvm.h>
#include <liblightnvm.h>
#include <nvm.h>
#include <nvm_debug.h>

static inline int cmd_ioctl_idf(struct nvm_dev *dev, struct nvm_ret *ret)
{
	struct nvm_passthru_vio ctl;
	struct spec_idf *idf;
	int err;

	idf = nvm_buf_alloc(nvm_dev_get_geo(dev), sizeof(*idf));
	if (!idf) {
		errno = ENOMEM;
		return -1;
	}

	memset(&ctl, 0, sizeof(ctl));
	ctl.opcode = S12_OPC_IDF;
	ctl.addr = (uint64_t)idf;
	ctl.data_len = sizeof(*idf);

	err = ioctl(dev->fd, NVME_NVM_IOCTL_ADMIN_VIO, &ctl);
	if (ret) {
		ret->result = ctl.result;
		ret->status = ctl.status;
	}
	if (err) {
		errno = EIO;
		free(idf);
		return -1;
	}

	/*
	printf("idf(%lu), ppaf(%lu), lbaf(%lu), group(%lu)\n",
		sizeof(struct spec_12_idf),
		sizeof(struct spec_ppaf_nand),
		sizeof(struct spec_lbaf),
		sizeof(struct spec_cgrp)
	);
	*/

	switch (idf->verid) {
	case 0x1:
	case 0x2:
		spec_idf_pr(idf);
		break;
	default:
		printf("Unsupported Version ID(%d)", idf->verid);
		errno = ENOSYS;
		return -1;
	}

	// TODO: Fill something out

	free(idf);

	return 0;
}

/*
 * Searches the udev 'subsystem' for device named 'dev_name' of type 'devtype'
 *
 * NOTE: Caller is responsible for calling `udev_device_unref` on the returned
 * udev_device
 *
 * @returns First device in 'subsystem' of given 'devtype' with given 'dev_name'
 */
struct udev_device *udev_dev_find(struct udev *udev, const char *subsystem,
				  const char *devtype, const char *dev_name)
{
	struct udev_device *dev = NULL;

	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;

	enumerate = udev_enumerate_new(udev);	/* Search 'subsystem' */
	udev_enumerate_add_match_subsystem(enumerate, subsystem);
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *path;
		int path_len;

		path = udev_list_entry_get_name(dev_list_entry);
		if (!path) {
			NVM_DEBUG("FAILED: retrieving path from entry\n");
			continue;
		}
		path_len = strlen(path);

		if (dev_name) {			/* Compare name */
			int dev_name_len = strlen(dev_name);
			int match = strcmp(dev_name,
					   path + path_len-dev_name_len);
			if (match != 0) {
				continue;
			}
		}
						/* Get the udev object */
		dev = udev_device_new_from_syspath(udev, path);
		if (!dev) {
			NVM_DEBUG("FAILED: retrieving device from path\n");
			continue;
		}

		if (devtype) {			/* Compare device type */
			const char *sys_devtype;
			int sys_devtype_match;

			sys_devtype = udev_device_get_devtype(dev);
			if (!sys_devtype) {
				NVM_DEBUG("FAILED: sys_devtype(%s)", sys_devtype);
				udev_device_unref(dev);
				dev = NULL;
				continue;
			}

			sys_devtype_match = strcmp(devtype, sys_devtype);
			if (sys_devtype_match != 0) {
				NVM_DEBUG("FAILED: %s != %s\n", devtype, sys_devtype);
				udev_device_unref(dev);
				dev = NULL;
				continue;
			}
		}

		break;
	}

	return dev;
}

struct udev_device *udev_nvmdev_find(struct udev *udev, const char *dev_name)
{
	struct udev_device *dev;

	dev  = udev_dev_find(udev, "block", NULL, dev_name);
	if (dev)
		return dev;

	NVM_DEBUG("FAILED: NOTHING FOUND\n");
	return NULL;
}

static int sysattr2int(struct udev_device *dev, const char *attr, int *val)
{
	const char *dev_path;
	char path[4096];
	char buf[4096];
	char c;
	FILE *fp;
	int i;

	memset(buf, 0, sizeof(char)*4096);

	dev_path = udev_device_get_syspath(dev);
	if (!dev_path)
		return -ENODEV;

	sprintf(path, "%s/%s", dev_path, attr);
	fp = fopen(path, "rb");
	if (!fp)
		return -ENODEV;

	i = 0;
	while (((c = getc(fp)) != EOF) && i < 4096) {
		buf[i] = c;
		++i;
	}
	fclose(fp);

	*val = atoi(buf);
	return 0;
}

static int sysattr2fmt(struct udev_device *dev, const char *attr,
		   struct nvm_addr_fmt  *fmt, struct nvm_addr_fmt_mask *mask)
{
	const char *dev_path;
	char path[4096];
	char buf[4096];
	char buf_fmt[3];
	char c;
	FILE *fp;
	int i;

	memset(buf, 0, sizeof(char)*4096);

	dev_path = udev_device_get_syspath(dev);
	if (!dev_path)
		return -ENODEV;

	sprintf(path, "%s/%s", dev_path, attr);
	fp = fopen(path, "rb");
	if (!fp)
		return -ENODEV;

	i = 0;
	while (((c = getc(fp)) != EOF) && i < 4096) {
		buf[i] = c;
		++i;
	}
	fclose(fp);

	if (strlen(buf) != 27) { // len !matching "0x380830082808001010102008\n"
		return -1;
	}

	for (i = 0; i < 12; ++i) {
		buf_fmt[0] = buf[2 + i*2];	// offset in bits
		buf_fmt[1] = buf[2 + i*2 + 1];	// number of bits
		buf_fmt[2] = '\0';
		fmt->a[i] = strtol(buf_fmt, NULL, 16);

		if ((i % 2)) {
			// i-1 = offset
			// i = width
			mask->a[i/2] = (((uint64_t)1<< fmt->a[i])-1) << fmt->a[i-1];
		}
	}

	return 0;
}

uint64_t ilog2(uint64_t x)
{
  uint64_t val = 0;

  while (x >>= 1)
	val++;

  return val;
}

static int dev_attr_fill(struct nvm_dev *dev)
{
	struct udev *udev;
	struct udev_device *udev_dev;
	struct nvm_geo *geo;
	int val;

	udev = udev_new();
	if (!udev) {
		NVM_DEBUG("FAILED: udev_new for name(%s)\n", dev->name);
		errno = ENOMEM;
		return -1;
	}

	/* Get a handle on udev / sysfs */
	udev_dev = udev_nvmdev_find(udev, dev->name);
	if (!udev_dev) {
		NVM_DEBUG("FAILED: udev_nvmdev_find for name(%s)\n", dev->name);
		udev_unref(udev);
		errno = ENODEV;
		return -1;
	}

	/* Extract ppa_format from sysfs via libudev */
	if (sysattr2fmt(udev_dev, "lightnvm/ppa_format", &dev->fmt, &dev->mask)) {
		NVM_DEBUG("FAILED: ppa_format for name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}

	/*
	 * Extract geometry from sysfs via libudev
	 */
	geo = &(dev->geo);

	if (sysattr2int(udev_dev, "lightnvm/num_channels", &val)) {
		NVM_DEBUG("FAILED: num_channels for dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->nchannels = val;

	if (sysattr2int(udev_dev, "lightnvm/num_luns", &val)) {
		NVM_DEBUG("FAILED: num_luns for dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->nluns = val;

	if (sysattr2int(udev_dev, "lightnvm/num_planes", &val)) {
		NVM_DEBUG("FAILED: num_planes for dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->nplanes = val;

	if (sysattr2int(udev_dev, "lightnvm/num_blocks", &val)) {
		NVM_DEBUG("FAILED: num_blocks for dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->nblocks = val;

	if (sysattr2int(udev_dev, "lightnvm/num_pages", &val)) {
		NVM_DEBUG("FAILED: num_pages for dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->npages = val;

	if (sysattr2int(udev_dev, "lightnvm/page_size", &val)) {
		NVM_DEBUG("FAILED: page_size for dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->page_nbytes = val;

	if (sysattr2int(udev_dev, "lightnvm/hw_sector_size", &val)) {
		NVM_DEBUG("FAILED: hw_sector_size for dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->sector_nbytes = val;

	if (sysattr2int(udev_dev, "lightnvm/oob_sector_size", &val)) {
		NVM_DEBUG("FAILED: oob_sector_size dev->name(%s)\n", dev->name);
		errno = EIO;
		return -1;
	}
	geo->meta_nbytes = val;

	udev_device_unref(udev_dev);
	udev_unref(udev);

	// WARN: HOTFIX for reports of unrealisticly large OOB area
	if (geo->meta_nbytes > 100) {
		geo->meta_nbytes = 16;	// Naively hope this is right
	}

	// Derive number of sectors
	geo->nsectors = geo->page_nbytes / geo->sector_nbytes;

	/* Derive total number of bytes on device */
	geo->tbytes = geo->nchannels * geo->nluns * geo->nplanes * \
		      geo->nblocks * geo->npages * geo->nsectors * geo->sector_nbytes;

	/* Derive number of bytes occupied by a virtual block/page */
	geo->vblk_nbytes = geo->nplanes * geo->npages * geo->nsectors * \
			   geo->sector_nbytes;
	geo->vpg_nbytes = geo->nplanes * geo->nsectors * geo->sector_nbytes;

	/* Derive the sector-shift-width for LBA mapping */
	dev->ssw = ilog2(geo->sector_nbytes);

	/* Derive a default plane mode */
	switch(geo->nplanes) {
		case 4:
			dev->pmode = NVM_FLAG_PMODE_QUAD;
			break;
		case 2:
			dev->pmode = NVM_FLAG_PMODE_DUAL;
			break;
		case 1:
			dev->pmode = NVM_FLAG_PMODE_SNGL;
			break;

		default:
			errno = EINVAL;
			return -1;
	}

	dev->erase_naddrs_max = NVM_NADDR_MAX;
	dev->write_naddrs_max = NVM_NADDR_MAX;
	dev->read_naddrs_max = NVM_NADDR_MAX;

	dev->meta_mode = NVM_META_MODE_NONE;

	return 0;
}

struct nvm_dev *nvm_dev_new(void)
{
	struct nvm_dev *dev;

	dev = malloc(sizeof(*dev));
	if (dev)
		memset(dev, 0, sizeof(*dev));

	return dev;
}

void nvm_dev_pr(struct nvm_dev *dev)
{
	if (!dev) {
		printf("dev { NULL }\n");
		return;
	}

	printf("dev {\n path(%s), name(%s), fd(%d), ssw(%lu), pmode(%d),\n",
	       dev->path, dev->name, dev->fd, dev->ssw, dev->pmode);
	printf(" erase_naddrs_max(%d), read_naddrs_max(%d), write_naddrs_max(%d),\n",
	       dev->erase_naddrs_max,
	       dev->read_naddrs_max,
	       dev->write_naddrs_max);
	printf(" meta_mode(%d),\n", dev->meta_mode);
	printf(" bbts_cached(%d)\n}\n", dev->bbts_cached);
	printf("dev-"); nvm_geo_pr(&dev->geo);
	printf("dev-"); nvm_addr_fmt_pr(&dev->fmt);
	printf("dev-"); nvm_addr_fmt_mask_pr(&dev->mask);
}

int nvm_dev_attr_nchannels(struct nvm_dev *dev)
{
	return dev->geo.nchannels;
}

int nvm_dev_attr_nluns(struct nvm_dev *dev)
{
	return dev->geo.nluns;
}

int nvm_dev_attr_nplanes(struct nvm_dev *dev)
{
	return dev->geo.nplanes;
}

int nvm_dev_attr_nblocks(struct nvm_dev *dev)
{
	return dev->geo.nblocks;
}

int nvm_dev_attr_npages(struct nvm_dev *dev)
{
	return dev->geo.npages;
}

int nvm_dev_attr_nsectors(struct nvm_dev *dev)
{
	return dev->geo.nsectors;
}

int nvm_dev_attr_nbytes(struct nvm_dev *dev)
{
	return dev->geo.sector_nbytes;
}

int nvm_dev_attr_vblk_nbytes(struct nvm_dev *dev)
{
	return dev->geo.vblk_nbytes;
}

int nvm_dev_attr_vpage_nbytes(struct nvm_dev *dev)
{
	return dev->geo.vpg_nbytes;
}

const struct nvm_geo * nvm_dev_get_geo(struct nvm_dev *dev)
{
	return &dev->geo;
}

int nvm_dev_get_pmode(struct nvm_dev *dev)
{
        return dev->pmode;
}

int nvm_dev_get_meta_mode(struct nvm_dev *dev)
{
	return dev->meta_mode;
}

int nvm_dev_set_meta_mode(struct nvm_dev *dev, int meta_mode)
{
	switch (meta_mode) {
		case NVM_META_MODE_NONE:
		case NVM_META_MODE_ALPHA:
		case NVM_META_MODE_CONST:
			break;
		default:
			errno = EINVAL;
			return -1;
	}

	dev->meta_mode = meta_mode;

	return 0;
}

int nvm_dev_get_erase_naddrs_max(struct nvm_dev *dev)
{
	return dev->erase_naddrs_max;
}

int nvm_dev_get_read_naddrs_max(struct nvm_dev *dev)
{
	return dev->read_naddrs_max;
}

int nvm_dev_get_write_naddrs_max(struct nvm_dev *dev)
{
	return dev->write_naddrs_max;
}

int nvm_dev_set_erase_naddrs_max(struct nvm_dev *dev, int naddrs)
{
	if (naddrs > NVM_NADDR_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (naddrs < 1) {
		errno = EINVAL;
		return -1;
	}
	if (naddrs % dev->geo.nplanes) {
		errno = EINVAL;
		return -1;
	}

	dev->erase_naddrs_max = naddrs;

	return 0;
}

int nvm_dev_get_bbts_cached(struct nvm_dev *dev)
{
	return dev->bbts_cached;
}

int nvm_dev_set_bbts_cached(struct nvm_dev *dev, int bbts_cached)
{
	switch(bbts_cached) {
	case 0:
	case 1:
		break;
	default:
		errno = EINVAL;
		return -1;
	}

	dev->bbts_cached = bbts_cached;

	return 0;
}

int nvm_dev_set_read_naddrs_max(struct nvm_dev *dev, int naddrs)
{
	if (naddrs > NVM_NADDR_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (naddrs < 1) {
		errno = EINVAL;
		return -1;
	}
	if (naddrs % (dev->geo.nplanes * dev->geo.nsectors)) {
		errno = EINVAL;
		return -1;
	}

	dev->read_naddrs_max = naddrs;

	return 0;
}

int nvm_dev_set_write_naddrs_max(struct nvm_dev *dev, int naddrs)
{
	if (naddrs > NVM_NADDR_MAX) {
		errno = EINVAL;
		return -1;
	}
	if (naddrs < 1) {
		errno = EINVAL;
		return -1;
	}
	if (naddrs % (dev->geo.nplanes * dev->geo.nsectors)) {
		errno = EINVAL;
		return -1;
	}

	dev->write_naddrs_max = naddrs;

	return 0;
}

struct nvm_dev *nvm_dev_open(const char *dev_path)
{
	struct nvm_dev *dev;
	struct nvm_ret ret = {};
	int err;
	
	if (strlen(dev_path) > NVM_DEV_PATH_LEN) {
		NVM_DEBUG("FAILED: Device path too long\n");
		errno = EINVAL;
		return NULL;
	}

	dev = nvm_dev_new();
	if (!dev) {
		NVM_DEBUG("FAILED: nvm_dev_new.\n");
		return NULL;
	}

	strncpy(dev->path, dev_path, NVM_DEV_PATH_LEN);
	strncpy(dev->name, dev_path+5, NVM_DEV_NAME_LEN);

	dev->fd = open(dev->path, O_RDWR | O_DIRECT);
	if (dev->fd < 0) {
		NVM_DEBUG("FAILED: open dev->path(%s) dev->fd(%d)\n",
			  dev->path, dev->fd);

		free(dev);

		return NULL;
	}

	err = dev_attr_fill(dev);
	if (err) {
		NVM_DEBUG("FAILED: dev_attr_fill, err(%d)\n", err);
		close(dev->fd);
		free(dev);
		return NULL;
	}

	dev->bbts_cached = 0;
	dev->nbbts = dev->geo.nchannels * dev->geo.nluns;
	dev->bbts = malloc(sizeof(*dev->bbts) * dev->nbbts);
	for (size_t i = 0; i < dev->nbbts; ++i)
		dev->bbts[i] = NULL;

	err = cmd_ioctl_idf(dev, &ret);
	if (err) {
		perror("cmd_ioctl_idf");
		NVM_DEBUG("FAILED: cmd_ioctl_idf, err(%d)", err);
		nvm_ret_pr(&ret);
		close(dev->fd);
		free(dev);
		return NULL;
	}

	return dev;
}

void nvm_dev_close(struct nvm_dev *dev)
{
	if (!dev)
		return;

	nvm_bbt_flush_all(dev, NULL);
	free(dev->bbts);

	close(dev->fd);
	free(dev);
}

