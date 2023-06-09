/* sound/soc/samsung/abox_v2/abox_soc.h
 *
 * ALSA SoC - Samsung Abox SoC layer
 *
 * Copyright (c) 2018 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SND_SOC_ABOX_SOC_H
#define __SND_SOC_ABOX_SOC_H

#include "abox.h"

#define GENMASK32(h, l) \
	(((~0U) - (1U << (l)) + 1) & (~0U >> (32 - 1 - (h))))
#define ABOX_SOC_VERSION(m, n, r) (((m) << 16) | ((n) << 8) | (r))
#define ABOX_FLD(name) (GENMASK32(ABOX_##name##_H, ABOX_##name##_L))
#define ABOX_FLD_X(name, x) (GENMASK32(ABOX_##name##_H(x), ABOX_##name##_L(x)))
#define ABOX_SFR(name, o, x) \
	(ABOX_##name##_BASE + ((x) * ABOX_##name##_ITV) + (o))
#define ABOX_L(name, o, x) (ABOX_SFR(name, o, x) % 32)
#define ABOX_H(name, o, x) (ABOX_SFR(name, o, x) % 32)

#if (ABOX_SOC_VERSION(3, 0, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_3.h"
#elif (ABOX_SOC_VERSION(2, 3, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_23.h"
#elif (ABOX_SOC_VERSION(2, 1, 0) <= CONFIG_SND_SOC_SAMSUNG_ABOX_VERSION)
#include "abox_soc_21.h"
#else
#include "abox_soc_2.h"
#endif

#define set_mask_value(id, mask, value) \
		{id = (typeof(id))((id & ~mask) | (value & mask)); }

#define set_value_by_name(id, name, value) \
		set_mask_value(id, name##_MASK, value << name##_L)

/**
 * get version of ABOX IP
 * @param[in]	adev		pointer to abox device
 * @return	ABOX IP version. Format: major << 16 | minor << 8 | revision
 */
extern int abox_soc_ver(struct device *adev);

/**
 * compare version of ABOX IP
 * @param[in]	adev		pointer to abox device
 * @param[in]	m		major version
 * @param[in]	n		minor version
 * @param[in]	r		revision
 * @return	> 0: given version is bigger than SoC version
 *		= 0: given version is same with SoC version
 *		< 0: given version is smaller with SoC version
 */
extern int abox_soc_vercmp(struct device *adev, int m, int n, int r);

/**
 * register sfr regmap
 * @param[in]	adev		pointer to abox device
 * @return	regmap for sfr
 */
extern struct regmap *abox_soc_get_regmap(struct device *adev);
#endif /* __SND_SOC_ABOX_SOC_H */
