/*
 * Samsung Exynos SoC series Pablo driver
 *
 * Copyright (c) 2019 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "is-hw-mcscaler-v3.h"
#include "api/is-hw-api-mcscaler-v3.h"
#include "../interface/is-interface-ischain.h"
#include "is-param.h"
#include "is-err.h"

static const struct djag_scaling_ratio_depended_config init_djag_cfgs = {
	.xfilter_dejagging_coeff_cfg = {
		.xfilter_dejagging_weight0 = 3,
		.xfilter_dejagging_weight1 = 4,
		.xfilter_hf_boost_weight = 4,
		.center_hf_boost_weight = 2,
		.diagonal_hf_boost_weight = 3,
		.center_weighted_mean_weight = 3,
	},
	.thres_1x5_matching_cfg = {
		.thres_1x5_matching_sad = 128,
		.thres_1x5_abshf = 512,
	},
	.thres_shooting_detect_cfg = {
		.thres_shooting_llcrr = 255,
		.thres_shooting_lcr = 255,
		.thres_shooting_neighbor = 255,
		.thres_shooting_uucdd = 80,
		.thres_shooting_ucd = 80,
		.min_max_weight = 2,
	},
	.lfsr_seed_cfg = {
		.lfsr_seed_0 = 44257,
		.lfsr_seed_1 = 4671,
		.lfsr_seed_2 = 47792,
	},
	.dither_cfg = {
		.dither_value = {0, 0, 1, 2, 3, 4, 6, 7, 8},
		.sat_ctrl = 5,
		.dither_sat_thres = 1000,
		.dither_thres = 5,
	},
	.cp_cfg = {
		.cp_hf_thres = 40,
		.cp_arbi_max_cov_offset = 0,
		.cp_arbi_max_cov_shift = 6,
		.cp_arbi_denom = 7,
		.cp_arbi_mode = 1,
	},
};

void is_hw_mcsc_adjust_size_with_djag(struct is_hw_ip *hw_ip, struct param_mcs_input *input,
	struct is_hw_mcsc_cap *cap, u32 *x, u32 *y, u32 *width, u32 *height)
{
	u32 src_pos_x = *x, src_pos_y = *y, src_width = *width, src_height = *height;

#ifdef ENABLE_DJAG_IN_MCSC
	if (cap->djag == MCSC_CAP_SUPPORT && input->djag_out_width) {
		/* re-sizing crop size for DJAG output image to poly-phase scaler */
		src_pos_x = ALIGN(CONVRES(src_pos_x, input->width, input->djag_out_width), MCSC_WIDTH_ALIGN);
		src_pos_y = ALIGN(CONVRES(src_pos_y, input->height, input->djag_out_height), MCSC_HEIGHT_ALIGN);
		src_width = ALIGN(CONVRES(src_width, input->width, input->djag_out_width), MCSC_WIDTH_ALIGN);
		src_height = ALIGN(CONVRES(src_height, input->height, input->djag_out_height), MCSC_HEIGHT_ALIGN);

		if (src_pos_x + src_width > input->djag_out_width) {
			warn_hw("%s: Out of input_crop width (djag_out_w: %d < (%d + %d))",
				__func__, input->djag_out_width, src_pos_x, src_width);
			src_pos_x = 0;
			src_width = input->djag_out_width;
		}

		if (src_pos_y + src_height > input->djag_out_height) {
			warn_hw("%s: Out of input_crop height (djag_out_h: %d < (%d + %d))",
				__func__, input->djag_out_height, src_pos_y, src_height);
			src_pos_y = 0;
			src_height = input->djag_out_height;
		}

		sdbg_hw(2, "crop size changed (%d, %d, %d, %d) -> (%d, %d, %d, %d) by DJAG\n", hw_ip,
			*x, *y, *width, *height,
			src_pos_x, src_pos_y, src_width, src_height);
	}
#endif
	*x = src_pos_x;
	*y = src_pos_y;
	*width = src_width;
	*height = src_height;
}

int is_hw_mcsc_update_djag_register(struct is_hw_ip *hw_ip,
		struct mcs_param *param,
		u32 instance)
{
	int ret = 0;
	struct is_device_ischain *ischain;
	struct is_hw_mcsc *hw_mcsc = NULL;
	struct is_hw_mcsc_cap *cap = GET_MCSC_HW_CAP(hw_ip);
	u32 in_width, in_height;
	u32 out_width = 0, out_height = 0;
	const struct djag_scaling_ratio_depended_config *djag_tuneset;
	struct djag_wb_thres_cfg *djag_wb = NULL;
	u32 hratio, vratio, min_ratio;
	u32 scale_index = MCSC_DJAG_PRESCALE_INDEX_1;
	enum exynos_sensor_position sensor_position;
	int output_id = 0;
#if defined(USE_YUV_RANGE_BY_ISP)
	u32 yuv = 0;
#endif
	u32 white = 1023, black = 0;
	bool djag_en = true;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->priv_info);
	FIMC_BUG(!param);

	if (cap->djag != MCSC_CAP_SUPPORT)
		return ret;

	ischain = hw_ip->group[instance]->device;
	hw_mcsc = (struct is_hw_mcsc *)hw_ip->priv_info;

#ifdef ENABLE_DJAG_IN_MCSC
	param->input.djag_out_width = 0;
	param->input.djag_out_height = 0;
#endif

	sensor_position = hw_ip->hardware->sensor_position[instance];
	djag_tuneset = &init_djag_cfgs;

	in_width = param->input.width;
	in_height = param->input.height;

	/* select compare output_port : select max output size */
	for (output_id = MCSC_OUTPUT0; output_id < cap->max_output; output_id++) {
		if (test_bit(output_id, &hw_mcsc->out_en)
				&& (param->output[output_id].dma_cmd == DMA_OUTPUT_COMMAND_ENABLE)) {
			if (out_width <= param->output[output_id].width) {
				out_width = param->output[output_id].width;
				out_height = param->output[output_id].height;
			}
#if defined(USE_YUV_RANGE_BY_ISP)
			if (yuv != param->output[output_id].yuv_range)
				sdbg_hw(2, "[OUT:%d] yuv: %s -> %s\n", hw_ip, output_id,
					yuv ? "N" : "W", param->output[output_id].yuv_range ? "N" : "W");
			yuv = param->output[output_id].yuv_range;
#endif
		}
	}

	if (out_width > MCSC_LINE_BUF_SIZE) {
		/* Poly input size should be equal and less than line buffer size */
		warn_hw("%s: DJAG output size exceeds line buffer size(%d > %d), change (%d) -> (%d)",
			__func__,
			out_width, MCSC_LINE_BUF_SIZE,
			out_width, MCSC_LINE_BUF_SIZE);
		out_width = MCSC_LINE_BUF_SIZE;
	}
	is_hw_djag_adjust_out_size(ischain, in_width, in_height, &out_width, &out_height);

	if (param->input.width > out_width || param->input.height > out_height) {
		sdbg_hw(2, "DJAG is not applied still.(input : %dx%d > output : %dx%d)\n", hw_ip,
				param->input.width, param->input.height,
				out_width, out_height);
		is_scaler_set_djag_enable(hw_ip->regs[REG_SETA], 0);
		return ret;
	}

	/* check scale ratio if over 2.5 times */
	if (out_width * 10 > in_width * 25)
		out_width = round_down(in_width * 25 / 10, MCSC_WIDTH_ALIGN);

	if (out_height * 10 > in_height * 25)
		out_height = round_down(in_height * 25 / 10, MCSC_HEIGHT_ALIGN);

	hratio = GET_DJAG_ZOOM_RATIO(in_width, out_width);
	vratio = GET_DJAG_ZOOM_RATIO(in_height, out_height);
	min_ratio = min(hratio, vratio);
	if (min_ratio >= GET_DJAG_ZOOM_RATIO(10, 10)) /* Zoom Ratio = 1.0 */
		scale_index = MCSC_DJAG_PRESCALE_INDEX_1;
	else if (min_ratio >= GET_DJAG_ZOOM_RATIO(10, 14)) /* Zoom Ratio = 1.4 */
		scale_index = MCSC_DJAG_PRESCALE_INDEX_2;
	else if (min_ratio >= GET_DJAG_ZOOM_RATIO(10, 20)) /* Zoom Ratio = 2.0 */
		scale_index = MCSC_DJAG_PRESCALE_INDEX_3;
	else
		scale_index = MCSC_DJAG_PRESCALE_INDEX_4;

#ifdef ENABLE_DJAG_IN_MCSC
	param->input.djag_out_width = out_width;
	param->input.djag_out_height = out_height;
#endif

	sdbg_hw(2, "djag scale up (%dx%d) -> (%dx%d)\n", hw_ip,
		in_width, in_height, out_width, out_height);

	/* djag image size setting */
	is_scaler_set_djag_src_size(hw_ip->regs[REG_SETA], in_width, in_height);
	is_scaler_set_djag_dst_size(hw_ip->regs[REG_SETA], out_width, out_height);
	is_scaler_set_djag_scaling_ratio(hw_ip->regs[REG_SETA], hratio, vratio);
	is_scaler_set_djag_init_phase_offset(hw_ip->regs[REG_SETA], 0, 0);
	is_scaler_set_djag_round_mode(hw_ip->regs[REG_SETA], 1);

#ifdef MCSC_USE_DEJAG_TUNING_PARAM
	djag_en = hw_mcsc->cur_setfile[sensor_position][instance]->djag.djag_en;
	djag_tuneset = &hw_mcsc->cur_setfile[sensor_position][instance]->djag.scaling_ratio_depended_cfg[scale_index];
	djag_wb = &hw_mcsc->cur_setfile[sensor_position][instance]->djag_wb[scale_index];
#endif
	is_scaler_set_djag_tunning_param(hw_ip->regs[REG_SETA], djag_tuneset);
#if defined(USE_YUV_RANGE_BY_ISP)
	if (yuv == SCALER_OUTPUT_YUV_RANGE_FULL) {
		white = 1023;
		black = 0;
	} else {
		white = 940;
		black = 64;
	}
#endif
	is_scaler_set_djag_dither_wb(hw_ip->regs[REG_SETA], djag_wb, white, black);

	is_scaler_set_djag_enable(hw_ip->regs[REG_SETA], djag_en);

	return ret;
}
