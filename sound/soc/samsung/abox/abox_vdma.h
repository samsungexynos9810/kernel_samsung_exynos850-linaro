/* sound/soc/samsung/abox_v2/abox_vdma.h
 *
 * ALSA SoC Audio Layer - Samsung Abox Virtual DMA driver
 *
 * Copyright (c) 2017 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SND_SOC_ABOX_VDMA_H
#define __SND_SOC_ABOX_VDMA_H

/**
 * Register abox virtual pcm
 * @param[in]	dev		pointer to abox device
 * @param[in]	id		unique buffer id
 * @param[in]	stream		SNDRV_PCM_STREAM_*
 * @param[in]	area		virtual address of the buffer
 * @param[in]	addr		pysical address of the buffer
 * @param[in]	pcm_hardware	hardware information of virtual DMA
 * @return	error code if any
 */
extern int abox_vdma_register(struct device *dev, int id, int stream,
		void *area, phys_addr_t addr,
		const struct PCMTASK_HARDWARE *pcm_hardware);

/**
 * Register abox virtual component
 * @param[in]	dev		pointer to abox device
 * @param[in]	id		unique buffer id
 * @param[in]	name		name of the component
 * @param[in]	playback	playback capability
 * @param[in]	capture		capture capability
 * @return	device containing asoc component
 */
extern struct device *abox_vdma_register_component(struct device *dev,
		int id, const char *name,
		struct snd_pcm_hardware *playback,
		struct snd_pcm_hardware *capture);

/**
 * register sound card for abox vdma
 * @param[in]	dev_abox	pointer to abox device
 */
extern void abox_vdma_register_card(struct device *dev_abox);

/**
 * Initialize abox vdma
 * @param[in]	dev_abox	pointer to abox device
 */
extern void abox_vdma_init(struct device *dev_abox);

#endif /* __SND_SOC_ABOX_VDMA_H */
