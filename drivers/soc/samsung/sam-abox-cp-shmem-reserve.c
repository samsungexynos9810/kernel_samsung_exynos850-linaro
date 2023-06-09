/* XXX: This is just a copy of drivers/soc/samsung/cpif/shm_ipc.c */

/*
 * Copyright (C) 2014-2019, Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/shm_ipc.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_fdt.h>
#include <linux/debug-snapshot.h>
#include <linux/version.h>

/* From cpif/modem_v1.h */
#define LOG_TAG	"mif: "
#define FUNC	(__func__)
#define CALLER	(__builtin_return_address(0))

#define mif_err_limited(fmt, ...) \
	printk_ratelimited(KERN_ERR LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define mif_err(fmt, ...) \
	pr_err(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define mif_debug(fmt, ...) \
	pr_debug(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define mif_info(fmt, ...) \
	pr_info(LOG_TAG "%s: " pr_fmt(fmt), __func__, ##__VA_ARGS__)
#define mif_trace(fmt, ...) \
	printk(KERN_DEBUG "mif: %s: %d: called(%pF): " fmt, \
		__func__, __LINE__, __builtin_return_address(0), ##__VA_ARGS__)
#define mif_dt_read_u32(np, prop, dest) \
	do { \
		u32 val; \
		if (of_property_read_u32(np, prop, &val)) { \
			mif_err("%s is not defined\n", prop); \
			return -EINVAL; \
		} \
		dest = val; \
	} while (0)
#define mif_dt_read_string(np, prop, dest) \
	do { \
		if (of_property_read_string(np, prop, \
				(const char **)&dest)) { \
			mif_err("%s is not defined\n", prop); \
			return -EINVAL; \
		} \
	} while (0)
#define mif_dt_read_bool(np, prop, dest) \
	do { \
		u32 val; \
		if (of_property_read_u32(np, prop, &val)) { \
			mif_err("%s is not defined\n", prop); \
			return -EINVAL; \
		} \
		dest = val ? true : false; \
	} while (0)

/*
 * Reserved memory
 */
#define MAX_CP_RMEM	10

struct cp_reserved_mem {
	char *name;
	u32 index;
	unsigned long p_base;
	u32 size;
};

static int _rmem_count;
static struct cp_reserved_mem _cp_rmem[MAX_CP_RMEM];

static int __init cp_rmem_setup(struct reserved_mem *rmem)
{
	const __be32 *prop;
	int len;

	if (_rmem_count >= MAX_CP_RMEM) {
		mif_err("_cp_rmem is full for %s\n", rmem->name);
		return -ENOMEM;
	}

	prop = of_get_flat_dt_prop(rmem->fdt_node, "rmem_index", &len);
	if (!prop) {
		mif_err("rmem_name is not defined for %s\n", rmem->name);
		return -ENOENT;
	}
	_cp_rmem[be32_to_cpu(prop[0])].index = be32_to_cpu(prop[0]);
	_cp_rmem[be32_to_cpu(prop[0])].name = (char *)rmem->name;
	_cp_rmem[be32_to_cpu(prop[0])].p_base = rmem->base;
	_cp_rmem[be32_to_cpu(prop[0])].size = rmem->size;
	_rmem_count++;

	mif_info("rmem %d %s 0x%08lx 0x%08x\n",
			_cp_rmem[be32_to_cpu(prop[0])].index,
			_cp_rmem[be32_to_cpu(prop[0])].name,
			_cp_rmem[be32_to_cpu(prop[0])].p_base,
			_cp_rmem[be32_to_cpu(prop[0])].size);

	return 0;
}
RESERVEDMEM_OF_DECLARE(modem_if, "exynos,modem_if", cp_rmem_setup);

/*
 * Shared memory
 */
struct cp_shared_mem {
	char *name;
	u32 index;
	u32 rmem;
	u32 cp_num;
	unsigned long p_base;
	u32 size;
	bool cached;
	void __iomem *v_base;
};

static struct cp_shared_mem _cp_shmem[MAX_CP_NUM][MAX_CP_SHMEM];

static int cp_shmem_setup(struct device *dev)
{
	struct device_node *regions = NULL;
	struct device_node *child = NULL;
	u32 cp_num;
	u32 shmem_index, rmem_index;
	u32 offset;
	u32 count = 0;

	mif_dt_read_u32(dev->of_node, "cp_num", cp_num);

	regions = of_get_child_by_name(dev->of_node, "regions");
	if (!regions) {
		mif_err("of_get_child_by_name() error:regions\n");
		return -EINVAL;
	}
	for_each_child_of_node(regions, child) {
		if (count >= MAX_CP_SHMEM) {
			mif_err("_cp_shmem is full for %d\n", count);
			return -ENOMEM;
		}
		mif_dt_read_u32(child, "region,index", shmem_index);
		_cp_shmem[cp_num][shmem_index].index = shmem_index;
		_cp_shmem[cp_num][shmem_index].cp_num = cp_num;
		mif_dt_read_string(child, "region,name", _cp_shmem[cp_num][shmem_index].name);
		mif_dt_read_u32(child, "region,rmem", _cp_shmem[cp_num][shmem_index].rmem);
		rmem_index = _cp_shmem[cp_num][shmem_index].rmem;
		mif_dt_read_u32(child, "region,offset", offset);
		_cp_shmem[cp_num][shmem_index].p_base = _cp_rmem[rmem_index].p_base + offset;
		mif_dt_read_u32(child, "region,size", _cp_shmem[cp_num][shmem_index].size);
		mif_dt_read_bool(child, "region,cached", _cp_shmem[cp_num][shmem_index].cached);
		count++;
	}

	return 0;
}

/*
 * Memory map on CP binary
 */
#define MAX_MAP_ON_CP		10
#define MAP_ON_CP_OFFSET	0xA0

struct ns_map_info {	/* Non secure map */
	u32 name;
	u32 offset;
	u32 size;
};

struct cp_mem_map {	/* Memory map on CP */
	u32 version;
	u32 secure_size;
	u32 ns_size;
	u32 ns_map_count;
	struct ns_map_info ns_map[MAX_MAP_ON_CP];
	u32 mem_guard;
};

struct cp_toc {		/* CP TOC */
	char name[12];
	u32 img_offset;
	u32 mem_offset;
	u32 size;
	u32 crc;
	u32 reserved;
} __packed;

static int _mem_map_on_cp[MAX_CP_NUM] = {};

static int cp_shmem_check_mem_map_on_cp(struct device *dev)
{
	u32 cp_num = 0;
	void __iomem *base = NULL;
	struct cp_toc *toc = NULL;
	struct cp_mem_map map = {};
	u32 name;
	int i;
	u32 rmem_index = 0;
	u32 shmem_index = 0;

	mif_dt_read_u32(dev->of_node, "cp_num", cp_num);
	base = phys_to_virt(cp_shmem_get_base(cp_num, SHMEM_CP));
	if (!base) {
		mif_err("base is null\n");
		return -ENOMEM;
	}

	toc = (struct cp_toc *)(base + sizeof(struct cp_toc));
	if (!toc) {
		mif_err("toc is null\n");
		return -ENOMEM;
	}
	mif_info("offset:0x%08x\n", toc->img_offset);

	if (toc->img_offset > (cp_shmem_get_size(cp_num, SHMEM_CP) - MAP_ON_CP_OFFSET)) {
		mif_info("Invalid img_offset:0x%08x. Use dt information\n", toc->img_offset);
		return 0;
	}

	memcpy(&map, base + toc->img_offset + MAP_ON_CP_OFFSET, sizeof(struct cp_mem_map));
	name = ntohl(map.version);
	if (strncmp((const char *)&name, "MEM\0", sizeof(name))) {
		mif_err("map.version error:0x%08x. Use dt information\n", map.version);
		return 0;
	}

	_mem_map_on_cp[cp_num] = 1;

	mif_info("secure_size:0x%08x ns_size:0x%08x count:%d\n", map.secure_size, map.ns_size, map.ns_map_count);
	_cp_shmem[cp_num][SHMEM_CP].size = map.secure_size;

	for (i = 0; i < map.ns_map_count; i++) {
		name = map.ns_map[i].name;
		if (!strncmp((const char *)&name, "CPI\0", sizeof(name)))
			shmem_index = SHMEM_IPC;
		else if (!strncmp((const char *)&name, "SSV\0", sizeof(name)))
			shmem_index = SHMEM_VSS;
#if defined(CONFIG_SOC_EXYNOS9820)
		else if (!strncmp((const char *)&name, "APV\0", sizeof(name)))
			shmem_index = SHMEM_VPA;
#endif
		else if (!strncmp((const char *)&name, "GOL\0", sizeof(name)))
			shmem_index = SHMEM_BTL;
		else if (!strncmp((const char *)&name, "B2L\0", sizeof(name)))
			shmem_index = SHMEM_L2B;
		else if (!strncmp((const char *)&name, "PKP\0", sizeof(name)))
			shmem_index = SHMEM_PKTPROC;
		else
			continue;

		rmem_index = _cp_shmem[cp_num][shmem_index].rmem;
		_cp_shmem[cp_num][shmem_index].p_base = _cp_rmem[rmem_index].p_base + map.ns_map[i].offset;
		_cp_shmem[cp_num][shmem_index].size = map.ns_map[i].size;
		mif_info("index:%d/%d base:0x%08lx offset:0x%08x size:0x%08x\n",
				shmem_index, rmem_index,
				_cp_rmem[rmem_index].p_base,
				map.ns_map[i].offset, map.ns_map[i].size);
	}

	return 0;
}

/*
 * Export functions - legacy
 */
unsigned long shm_get_msi_base(void)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	return cp_shmem_get_base(0, SHMEM_MSI);
#else
	int i;

	for (i = 0; i < MAX_CP_RMEM; i++) {
		if (!_cp_rmem[i].name)
			continue;

		if (strncmp(_cp_rmem[i].name, "cp_msi_rmem", strlen("cp_msi_rmem")) == 0) {
			mif_info("p_base:0x%08lx\n", _cp_rmem[i].p_base);
			return _cp_rmem[i].p_base;
		}
	}

	return 0;
#endif
}
EXPORT_SYMBOL(shm_get_msi_base);

void __iomem *shm_get_vss_region(void)
{
	return cp_shmem_get_region(0, SHMEM_VSS);
}
EXPORT_SYMBOL(shm_get_vss_region);

unsigned long shm_get_vss_base(void)
{
	return cp_shmem_get_base(0, SHMEM_VSS);
}
EXPORT_SYMBOL(shm_get_vss_base);

u32 shm_get_vss_size(void)
{
	return cp_shmem_get_size(0, SHMEM_VSS);
}
EXPORT_SYMBOL(shm_get_vss_size);

void __iomem *shm_get_vparam_region(void)
{
	return cp_shmem_get_region(0, SHMEM_VPA);
}
EXPORT_SYMBOL(shm_get_vparam_region);

unsigned long shm_get_vparam_base(void)
{
	return cp_shmem_get_base(0, SHMEM_VPA);
}
EXPORT_SYMBOL(shm_get_vparam_base);

u32 shm_get_vparam_size(void)
{
	return cp_shmem_get_size(0, SHMEM_VPA);
}
EXPORT_SYMBOL(shm_get_vparam_size);

/*
 * Export functions
 */
int cp_shmem_get_mem_map_on_cp_flag(u32 cp_num)
{
	mif_info("cp:%d flag:%d\n", cp_num, _mem_map_on_cp[cp_num]);

	return _mem_map_on_cp[cp_num];
}
EXPORT_SYMBOL(cp_shmem_get_mem_map_on_cp_flag);

static struct page *nc_region_pages[SZ_64K];
void __iomem *cp_shmem_get_nc_region(unsigned long base, u32 size)
{
	int i;
	unsigned int num_pages = (size >> PAGE_SHIFT);
	pgprot_t prot = pgprot_writecombine(PAGE_KERNEL);
	void *v_addr;

	if (!base)
		return NULL;

	if (size > (num_pages << PAGE_SHIFT))
		num_pages++;

	for (i = 0; i < num_pages; i++) {
		nc_region_pages[i] = phys_to_page(base);
		base += PAGE_SIZE;
	}

	v_addr = vmap(nc_region_pages, num_pages, VM_MAP, prot);
	if (v_addr == NULL)
		mif_err("%s: Failed to vmap pages\n", __func__);

	return (void __iomem *)v_addr;
}
EXPORT_SYMBOL(cp_shmem_get_nc_region);

void __iomem *cp_shmem_get_region(u32 cp, u32 idx)
{
	if (_cp_shmem[cp][idx].v_base)
		return _cp_shmem[cp][idx].v_base;

	if (_cp_shmem[cp][idx].cached)
		_cp_shmem[cp][idx].v_base = phys_to_virt(_cp_shmem[cp][idx].p_base);
	else
		_cp_shmem[cp][idx].v_base = cp_shmem_get_nc_region(_cp_shmem[cp][idx].p_base, _cp_shmem[cp][idx].size);

	return _cp_shmem[cp][idx].v_base;
}
EXPORT_SYMBOL(cp_shmem_get_region);

void cp_shmem_release_region(u32 cp, u32 idx)
{
	if (_cp_shmem[cp][idx].v_base)
		vunmap(_cp_shmem[cp][idx].v_base);
}
EXPORT_SYMBOL(cp_shmem_release_region);

void cp_shmem_release_rmem(u32 cp, u32 idx)
{
	int i;
	unsigned long base;
	u32 size;
	struct page *page;

	base = cp_shmem_get_base(cp, idx);
	size = cp_shmem_get_size(cp, idx);
	mif_info("Release rmem 0x%08lx 0x%08x\n", base, size);

	for (i = 0; i < (size >> PAGE_SHIFT); i++) {
		page = phys_to_page(base);
		base += PAGE_SIZE;
		free_reserved_page(page);
	}
}
EXPORT_SYMBOL(cp_shmem_release_rmem);

unsigned long cp_shmem_get_base(u32 cp, u32 idx)
{
	return _cp_shmem[cp][idx].p_base;
}
EXPORT_SYMBOL(cp_shmem_get_base);

u32 cp_shmem_get_size(u32 cp, u32 idx)
{
	return _cp_shmem[cp][idx].size;
}
EXPORT_SYMBOL(cp_shmem_get_size);

/*
 * Platform driver
 */
static int cp_shmem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;
	u32 use_map_on_cp = 0;
	int i, j;
	bool log_cpmem = true;

	mif_info("+++\n");

	ret = cp_shmem_setup(dev);
	if (ret) {
		mif_err("cp_shmem_setup() error:%d\n", ret);
		return ret;
	}

	mif_dt_read_u32(dev->of_node, "use_mem_map_on_cp", use_map_on_cp);
	if (use_map_on_cp) {
		ret = cp_shmem_check_mem_map_on_cp(dev);
		if (ret) {
			mif_err("cp_shmem_check_mem_map_on_cp() error:%d\n", ret);
			return ret;
		}
	} else {
		mif_info("use_mem_map_on_cp is disabled. Use dt information\n");
	}

	for (i = 0; i < MAX_CP_NUM; i++) {
		for (j = 0; j < MAX_CP_SHMEM; j++) {
			if (!_cp_shmem[i][j].name)
				continue;

			mif_info("%d %d %d %s 0x%08lx 0x%08x %d\n",
				_cp_shmem[i][j].cp_num, _cp_shmem[i][j].rmem, _cp_shmem[i][j].index, _cp_shmem[i][j].name,
				_cp_shmem[i][j].p_base, _cp_shmem[i][j].size, _cp_shmem[i][j].cached);
		}
	}

	/* Set ramdump for rmem index 0 */
#if defined(CONFIG_CPIF_CHECK_SJTAG_STATUS)
	if (dbg_snapshot_get_sjtag_status() && !dbg_snapshot_get_dpm_status())
		log_cpmem = false;
#endif
	mif_info("cpmem dump on fastboot is %s\n", log_cpmem ? "enabled" : "disabled");
	if (log_cpmem)
		dbg_snapshot_add_bl_item_info("log_cpmem", (u32)_cp_rmem[0].p_base, _cp_rmem[0].size);

	mif_info("---\n");

	return 0;
}

static int cp_shmem_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id cp_shmem_dt_match[] = {
		{ .compatible = "sam,abox-cp-shmem-reserve", },
		{},
};
MODULE_DEVICE_TABLE(of, cp_shmem_dt_match);

static struct platform_driver cp_shmem_driver = {
	.probe = cp_shmem_probe,
	.remove = cp_shmem_remove,
	.driver = {
		.name = "sam-abox-cp-shmem-reserve",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(cp_shmem_dt_match),
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(cp_shmem_driver);

MODULE_DESCRIPTION("Exynos CP shared memory driver");
MODULE_LICENSE("GPL");
