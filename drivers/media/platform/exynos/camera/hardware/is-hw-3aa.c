/*
 * Samsung EXYNOS FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2014 Samsung Electronics Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "is-hw-3aa.h"
#include "is-err.h"

extern struct is_lib_support gPtr_lib_support;

static int __nocfi is_hw_3aa_open(struct is_hw_ip *hw_ip, u32 instance,
	struct is_group *group)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa = NULL;
#ifdef TAA_SRAM_STATIC_CONFIG
	struct is_hardware *hw = NULL;
#endif
	FIMC_BUG(!hw_ip);

	if (test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	frame_manager_probe(hw_ip->framemgr, BIT(hw_ip->id), "HW3AA");
	frame_manager_probe(hw_ip->framemgr_late, BIT(hw_ip->id) | 0xF000, "HW3AA LATE");
	frame_manager_open(hw_ip->framemgr, IS_MAX_HW_FRAME);
	frame_manager_open(hw_ip->framemgr_late, IS_MAX_HW_FRAME_LATE);

	hw_ip->priv_info = vzalloc(sizeof(struct is_hw_3aa));
	if(!hw_ip->priv_info) {
		mserr_hw("hw_ip->priv_info(null)", instance, hw_ip);
		ret = -ENOMEM;
		goto err_alloc;
	}

	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
#ifdef ENABLE_FPSIMD_FOR_USER
	fpsimd_get();
	ret = get_lib_func(LIB_FUNC_3AA, (void **)&hw_3aa->lib_func);
	fpsimd_put();
#else
	ret = get_lib_func(LIB_FUNC_3AA, (void **)&hw_3aa->lib_func);
#endif

	if (hw_3aa->lib_func == NULL) {
		mserr_hw("hw_3aa->lib_func(null)", instance, hw_ip);
		is_load_clear();
		ret = -EINVAL;
		goto err_lib_func;
	}
	msinfo_hw("get_lib_func is set\n", instance, hw_ip);

	hw_3aa->lib_support = &gPtr_lib_support;
	hw_3aa->lib[instance].func = hw_3aa->lib_func;

	ret = is_lib_isp_chain_create(hw_ip, &hw_3aa->lib[instance], instance);
	if (ret) {
		mserr_hw("chain create fail", instance, hw_ip);
		ret = -EINVAL;
		goto err_chain_create;
	}

#ifdef TAA_SRAM_STATIC_CONFIG
	if (IS_ENABLED(ENABLE_3AA_LIC_OFFSET) && (hw_ip->id == DEV_HW_3AA0)) {
		/* set default lic offset */
		hw = hw_ip->hardware;
		if (hw->lic_offset[0][0] == 0) {
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_0] =
			DEFAULT_LIC_CTX0_OFFSET0;
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_1] =
			DEFAULT_LIC_CTX0_OFFSET1;
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE] =
			LIC_NO_TRIGGER;
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_0] =
			DEFAULT_LIC_CTX1_OFFSET0;
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_1] =
			DEFAULT_LIC_CTX1_OFFSET1;
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE] =
			LIC_NO_TRIGGER;
			msinfo_hw("[%s] 3AA SRAM offset(default): setA(%d, %d, %d), setB(%d, %d, %d)",
			instance, hw_ip, group_id_name[group->id],
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_0],
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_1],
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE],
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_0],
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_1],
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE]);
		} else {
			msinfo_hw("[%s] 3AA SRAM offset: setA(%d, %d, %d), setB(%d, %d, %d)",
			instance, hw_ip, group_id_name[group->id],
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_0],
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_1],
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE],
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_0],
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_1],
			hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE]);
		}

		ret = is_lib_set_sram_offset(hw_ip, &hw_3aa->lib[instance], instance);
			if (ret) {
			mserr_hw("lib_set_sram_offset fail", instance, hw_ip);
			ret = -EINVAL;
			goto err_chain_create;
		}
	}
#endif
	set_bit(HW_OPEN, &hw_ip->state);
	msdbg_hw(2, "open: [G:0x%x], framemgr[%s]", instance, hw_ip,
		GROUP_ID(group->id), hw_ip->framemgr->name);

	return 0;

err_chain_create:
err_lib_func:
	vfree(hw_ip->priv_info);
	hw_ip->priv_info = NULL;
err_alloc:
	frame_manager_close(hw_ip->framemgr);
	frame_manager_close(hw_ip->framemgr_late);
	return ret;
}

static int is_hw_3aa_init(struct is_hw_ip *hw_ip, u32 instance,
	struct is_group *group, bool flag, u32 module_id)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa = NULL;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->priv_info);
	FIMC_BUG(!group);

	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;

	hw_3aa->lib[instance].object = NULL;
	hw_3aa->lib[instance].func   = hw_3aa->lib_func;
	hw_3aa->param_set[instance].reprocessing = flag;

	if (hw_3aa->lib[instance].object != NULL) {
		msdbg_hw(2, "object is already created\n", instance, hw_ip);
	} else {
		ret = is_lib_isp_object_create(hw_ip, &hw_3aa->lib[instance],
				instance, (u32)flag, module_id);
		if (ret) {
			mserr_hw("object create fail", instance, hw_ip);
			return -EINVAL;
		}
	}

	group->hw_ip = hw_ip;
	msinfo_hw("[%s] Binding\n", instance, hw_ip, group_id_name[group->id]);

	set_bit(HW_INIT, &hw_ip->state);
	return ret;
}

static int is_hw_3aa_deinit(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->priv_info);

	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;

	is_lib_isp_object_destroy(hw_ip, &hw_3aa->lib[instance], instance);
	hw_3aa->lib[instance].object = NULL;

	return ret;
}

static int is_hw_3aa_close(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa;

	FIMC_BUG(!hw_ip);

	if (!test_bit(HW_OPEN, &hw_ip->state))
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
	FIMC_BUG(!hw_3aa->lib_support);

	is_lib_isp_chain_destroy(hw_ip, &hw_3aa->lib[instance], instance);
	vfree(hw_ip->priv_info);
	hw_ip->priv_info = NULL;
	frame_manager_close(hw_ip->framemgr);
	frame_manager_close(hw_ip->framemgr_late);

	clear_bit(HW_OPEN, &hw_ip->state);

	return ret;
}

static int is_hw_3aa_sensor_start(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa;
	struct is_frame *frame = NULL;
	struct is_framemgr *framemgr;
	struct camera2_shot *shot = NULL;
#ifdef ENABLE_MODECHANGE_CAPTURE
	struct is_device_sensor *sensor;
#endif

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	msinfo_hw("sensor_start: mode_change for sensor\n", instance, hw_ip);

	FIMC_BUG(!hw_ip->group[instance]);

	if (test_bit(IS_GROUP_OTF_INPUT, &hw_ip->group[instance]->state)) {
		/* For sensor info initialize for mode change */
		framemgr = hw_ip->framemgr;
		FIMC_BUG(!framemgr);

		framemgr_e_barrier(framemgr, 0);
		frame = peek_frame(framemgr, FS_HW_CONFIGURE);
		framemgr_x_barrier(framemgr, 0);
		if (frame) {
			shot = frame->shot;
		} else {
			mswarn_hw("enable (frame:NULL)(%d)", instance, hw_ip,
				framemgr->queued_count[FS_HW_CONFIGURE]);
#ifdef ENABLE_MODECHANGE_CAPTURE
			sensor = hw_ip->group[instance]->device->sensor;
			if (sensor && sensor->mode_chg_frame) {
				frame = sensor->mode_chg_frame;
				shot = frame->shot;
				msinfo_hw("[F:%d]mode_chg_frame used for REMOSAIC\n",
					instance, hw_ip, frame->fcount);
			}
#endif
		}

		FIMC_BUG(!hw_ip->priv_info);
		hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
		if (shot) {
			ret = is_lib_isp_sensor_info_mode_chg(&hw_3aa->lib[instance],
					instance, shot);
			if (ret < 0) {
				mserr_hw("is_lib_isp_sensor_info_mode_chg fail)",
					instance, hw_ip);
			}
		}
	}

	return ret;
}

static int is_hw_3aa_enable(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	atomic_inc(&hw_ip->run_rsccount);
	set_bit(HW_RUN, &hw_ip->state);

	return ret;
}

static int is_hw_3aa_disable(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	int ret = 0;
	long timetowait;
	struct is_hw_3aa *hw_3aa;
	struct taa_param_set *param_set;
	u32 i;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	msinfo_hw("disable: Vvalid(%d)\n", instance, hw_ip,
		atomic_read(&hw_ip->status.Vvalid));

	FIMC_BUG(!hw_ip->priv_info);
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
	param_set = &hw_3aa->param_set[instance];

	timetowait = wait_event_timeout(hw_ip->status.wait_queue,
		!atomic_read(&hw_ip->status.Vvalid),
		IS_HW_STOP_TIMEOUT);

	if (!timetowait) {
		mserr_hw("wait FRAME_END timeout (%ld)", instance,
			hw_ip, timetowait);
		ret = -ETIME;
	}

	param_set->fcount = 0;

	/* TODO: need to divide each task index */
	for (i = 0; i < TASK_INDEX_MAX; i++) {
		FIMC_BUG(!hw_3aa->lib_support);
		if (hw_3aa->lib_support->task_taaisp[i].task == NULL)
			serr_hw("task is null", hw_ip);
		else
			kthread_flush_worker(&hw_3aa->lib_support->task_taaisp[i].worker);
	}

	/*
	 * It doesn't matter, if object_stop command is always called.
	 * That is becuase DDK can prevent object_stop that is over called.
	 */
	is_lib_isp_stop(hw_ip, &hw_3aa->lib[instance], instance);

	if (atomic_read(&hw_ip->run_rsccount) == 0) {
		mswarn_hw("run_rsccount is not paired.\n", instance, hw_ip);
		return 0;
	}

	if (atomic_dec_return(&hw_ip->run_rsccount) > 0)
		return 0;

	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);

	return ret;
}

#if IS_ENABLED(ENABLE_3AA_LIC_OFFSET)
static int __is_hw_3aa_get_change_state(struct is_hw_ip *hw_ip, int instance, int hint)
{
	struct is_hardware *hw = NULL;
	struct is_group *head;
	int i, ref_cnt = 0, ret = 0;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->hardware);

	hw = hw_ip->hardware;
	head = GET_HEAD_GROUP_IN_DEVICE(IS_DEVICE_ISCHAIN, hw_ip->group[instance]);

	if (test_bit(IS_GROUP_OTF_INPUT, &head->state)) {
		/* OTF or vOTF case */
		for (i = 0; i < IS_STREAM_COUNT; i++)
			ref_cnt += atomic_read(&hw->streaming[hw->sensor_position[i]]);

		if (ref_cnt)
			ret = SRAM_CFG_BLOCK;
		else
			ret = SRAM_CFG_N;
	} else {
		/* Reprocessing case */
		for (i = 0; i < IS_STREAM_COUNT; i++)
			ref_cnt += atomic_read(&hw->streaming[hw->sensor_position[i]]);

		if (ref_cnt)
			ret = SRAM_CFG_BLOCK;
		else
			ret = SRAM_CFG_R;

		/* if this is mode change capture case, ignoring sensor streaming */
		if (hint)
			ret = SRAM_CFG_MODE_CHANGE_R;
	}

	return ret;
}

static int __is_hw_3aa_change_sram_offset(struct is_hw_ip *hw_ip, int instance, int mode)
{
	struct is_hw_3aa *hw_3aa = NULL;
	struct is_hardware *hw = NULL;
	struct taa_param_set *param_set = NULL;
	u32 offset0, offset1, target_w;
	int ret = 0;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!hw_ip->hardware);
	FIMC_BUG(!hw_ip->priv_info);

	hw = hw_ip->hardware;
	offset0 = hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_0];
	offset1 = hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_1] -
			hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_0];
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
	param_set = &hw_3aa->param_set[instance];

	/* check condition */
	switch (mode) {
	case SRAM_CFG_BLOCK:
		return ret;
	case SRAM_CFG_N:
		offset0 = IS_MAX_HW_3AA_SRAM / 2;
		offset1 = IS_MAX_HW_3AA_SRAM;
		target_w = param_set->otf_input.width;
		/* 3AA2 is not considered */
		if (hw_ip->id == DEV_HW_3AA0) {
			if (target_w > offset0)
				offset0 = target_w;
		} else if (hw_ip->id == DEV_HW_3AA1) {
			if (target_w > (IS_MAX_HW_3AA_SRAM - offset0))
				offset0 = IS_MAX_HW_3AA_SRAM - target_w;
		}
		break;
	case SRAM_CFG_R:
	case SRAM_CFG_MODE_CHANGE_R:
		target_w = param_set->otf_input.width;
		target_w = MAX(param_set->dma_input.width, target_w);
		offset0 = IS_MAX_HW_3AA_SRAM - target_w;
		offset1 = IS_MAX_HW_3AA_SRAM;
		break;
	default:
		mserr_hw("invalid mode (%d)", instance, hw_ip, mode);
		return ret;
	}

	hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_0] = offset0;
	hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_1] = offset1;
	hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_0] = offset0;
	hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_1] = offset1;
	ret = is_lib_set_sram_offset(hw_ip, &hw_3aa->lib[instance], instance);
	if (ret) {
		mserr_hw("lib_set_sram_offset fail", instance, hw_ip);
		ret = -EINVAL;
	}

	msinfo_hw("=> (%d) update 3AA SRAM offset: setA(%d, %d, %d), setB(%d, %d, %d)",
		instance, hw_ip,
		mode,
		hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_0],
		hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_OFFSET_1],
		hw->lic_offset[0][(COREX_SETA * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE],
		hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_0],
		hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_OFFSET_1],
		hw->lic_offset[0][(COREX_SETB * LIC_OFFSET_MAX) + LIC_TRIGGER_MODE]);

	return ret;
}
#endif

static int is_hw_3aa_shot(struct is_hw_ip *hw_ip, struct is_frame *frame,
	ulong hw_map)
{
	int ret = 0;
	int i, cur_idx;
	struct is_hw_3aa *hw_3aa;
	struct taa_param_set *param_set;
	struct is_region *region;
	struct taa_param *param;
	u32 lindex, hindex, instance;
	u32 input_w, input_h, crop_x, crop_y, output_w = 0, output_h = 0;
	bool frame_done = false;
	struct is_param_region *param_region;
#if IS_ENABLED(ENABLE_3AA_LIC_OFFSET)
	int mode;
#endif

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	instance = frame->instance;
	msdbgs_hw(2, "[F:%d]shot\n", instance, hw_ip, frame->fcount);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	is_hw_g_ctrl(hw_ip, hw_ip->id, HW_G_CTRL_FRM_DONE_WITH_DMA, (void *)&frame_done);
	if ((!frame_done)
		|| (!test_bit(ENTRY_3AC, &frame->out_flag) && !test_bit(ENTRY_3AP, &frame->out_flag)
			&& !test_bit(ENTRY_3AF, &frame->out_flag) && !test_bit(ENTRY_3AG, &frame->out_flag)
			&& !test_bit(ENTRY_MEXC, &frame->out_flag)
			&& !test_bit(ENTRY_ORBXC, &frame->out_flag)))
		set_bit(hw_ip->id, &frame->core_flag);

	FIMC_BUG(!hw_ip->priv_info);
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
	param_set = &hw_3aa->param_set[instance];
	region = hw_ip->region[instance];
	FIMC_BUG(!region);

	param = &region->parameter.taa;

	param_region = (struct is_param_region *)frame->shot->ctl.vendor_entry.parameter;
	if (frame->type == SHOT_TYPE_INTERNAL) {
		/* OTF INPUT case */
		param_set->dma_output_before_bds.cmd  = DMA_OUTPUT_COMMAND_DISABLE;
		param_set->output_dva_before_bds[0] = 0x0;
		param_set->dma_output_after_bds.cmd  = DMA_OUTPUT_COMMAND_DISABLE;
		param_set->output_dva_after_bds[0] = 0x0;
		param_set->dma_output_efd.cmd  = DMA_OUTPUT_COMMAND_DISABLE;
		param_set->output_dva_efd[0] = 0x0;
		param_set->dma_output_mrg.cmd  = DMA_OUTPUT_COMMAND_DISABLE;
		param_set->output_dva_mrg[0] = 0x0;
		param_set->output_kva_me[0] = 0;
#if defined(SOC_ORBMCH)
		param_set->output_kva_orb[0] = 0;
#endif
		hw_ip->internal_fcount[instance] = frame->fcount;
		goto config;
	} else {
		FIMC_BUG(!frame->shot);
		/* per-frame control
		 * check & update size from region */
		lindex = frame->shot->ctl.vendor_entry.lowIndexParam;
		hindex = frame->shot->ctl.vendor_entry.highIndexParam;

		if (hw_ip->internal_fcount[instance] != 0) {
			hw_ip->internal_fcount[instance] = 0;
			lindex |= LOWBIT_OF(PARAM_3AA_OTF_INPUT);
			lindex |= LOWBIT_OF(PARAM_3AA_VDMA1_INPUT);
			lindex |= LOWBIT_OF(PARAM_3AA_OTF_OUTPUT);
			lindex |= LOWBIT_OF(PARAM_3AA_VDMA4_OUTPUT);
			lindex |= LOWBIT_OF(PARAM_3AA_VDMA2_OUTPUT);
			lindex |= LOWBIT_OF(PARAM_3AA_FDDMA_OUTPUT);
			lindex |= LOWBIT_OF(PARAM_3AA_MRGDMA_OUTPUT);

			hindex |= HIGHBIT_OF(PARAM_3AA_OTF_INPUT);
			hindex |= HIGHBIT_OF(PARAM_3AA_VDMA1_INPUT);
			hindex |= HIGHBIT_OF(PARAM_3AA_OTF_OUTPUT);
			hindex |= HIGHBIT_OF(PARAM_3AA_VDMA4_OUTPUT);
			hindex |= HIGHBIT_OF(PARAM_3AA_VDMA2_OUTPUT);
			hindex |= HIGHBIT_OF(PARAM_3AA_FDDMA_OUTPUT);
			hindex |= HIGHBIT_OF(PARAM_3AA_MRGDMA_OUTPUT);
			param_region = &region->parameter;
		}
	}

	is_hw_3aa_update_param(hw_ip,
		param_region, param_set,
		lindex, hindex, instance);

	/* DMA settings */
	cur_idx = frame->cur_buf_index;

	input_w = param_set->otf_input.bayer_crop_width;
	input_h = param_set->otf_input.bayer_crop_height;
	crop_x = param_set->otf_input.bayer_crop_offset_x;
	crop_y = param_set->otf_input.bayer_crop_offset_y;
	if (param_set->dma_input.cmd != DMA_INPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->planes; i++) {
			param_set->input_dva[i] =
				(typeof(*param_set->input_dva))frame->dvaddr_buffer[i + cur_idx];
			if (param_set->input_dva[i] == 0) {
				msinfo_hw("[F:%d]dvaddr_buffer[%d] is zero\n",
					instance, hw_ip, frame->fcount, i);
				FIMC_BUG(1);
			}
		}
		input_w = param_set->dma_input.bayer_crop_width;
		input_h = param_set->dma_input.bayer_crop_height;
		crop_x = param_set->dma_input.bayer_crop_offset_x;
		crop_y = param_set->dma_input.bayer_crop_offset_y;
	}

	if (param_set->dma_output_before_bds.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->planes; i++) {
			param_set->output_dva_before_bds[i] = frame->txcTargetAddress[i + cur_idx];
			if (param_set->output_dva_before_bds[i] == 0) {
				msinfo_hw("[F:%d]txcTargetAddress[%d] is zero\n",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_before_bds.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}

	if (param_set->dma_output_after_bds.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->planes; i++) {
			param_set->output_dva_after_bds[i] = frame->txpTargetAddress[i + cur_idx];
			if (param_set->output_dva_after_bds[i] == 0) {
				msinfo_hw("[F:%d]txpTargetAddress[%d] is zero\n",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_after_bds.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
		output_w = param_set->dma_output_after_bds.width;
		output_h = param_set->dma_output_after_bds.height;
	}

	if (param_set->dma_output_efd.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < param->efd_output.plane; i++) {
			param_set->output_dva_efd[i] = frame->efdTargetAddress[i + cur_idx];
			if (param_set->output_dva_efd[i] == 0) {
				msinfo_hw("[F:%d]efdTargetAddress[%d] is zero\n",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_efd.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}

	if (param_set->dma_output_mrg.cmd != DMA_OUTPUT_COMMAND_DISABLE) {
		for (i = 0; i < frame->planes; i++) {
			param_set->output_dva_mrg[i] = frame->mrgTargetAddress[i + cur_idx];
			if (param_set->output_dva_mrg[i] == 0) {
				msinfo_hw("[F:%d]mrgTargetAddress[%d] is zero\n",
					instance, hw_ip, frame->fcount, i);
				param_set->dma_output_mrg.cmd = DMA_OUTPUT_COMMAND_DISABLE;
			}
		}
	}

	for (i = 0; i < frame->planes; i++) {
		param_set->output_kva_me[i] = frame->mexcTargetAddress[i + cur_idx];
		if (param_set->output_kva_me[i] == 0) {
			msdbg_hw(2, "[F:%d]mexcTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
		}
#if defined(SOC_ORBMCH)
		param_set->output_kva_orb[i] = frame->orbxcTargetAddress[i + cur_idx];
		if (param_set->output_kva_orb[i] == 0) {
			msdbg_hw(2, "[F:%d]orbxcTargetAddress[%d] is zero",
					instance, hw_ip, frame->fcount, i);
		}
#endif
	}

	if (frame->shot_ext) {
		frame->shot_ext->binning_ratio_x = (u16)param_set->sensor_config.sensor_binning_ratio_x;
		frame->shot_ext->binning_ratio_y = (u16)param_set->sensor_config.sensor_binning_ratio_y;
		frame->shot_ext->crop_taa_x = crop_x;
		frame->shot_ext->crop_taa_y = crop_y;
		if (output_w && output_h) {
			frame->shot_ext->bds_ratio_x = (input_w / output_w);
			frame->shot_ext->bds_ratio_y = (input_h / output_h);
		} else {
			frame->shot_ext->bds_ratio_x = 1;
			frame->shot_ext->bds_ratio_y = 1;
		}
	}

config:
	param_set->instance_id = instance;
	param_set->fcount = frame->fcount;

	/* multi-buffer */
	if (frame->num_buffers)
		hw_ip->num_buffers = frame->num_buffers;

	if (frame->type == SHOT_TYPE_INTERNAL) {
		is_log_write("[@][DRV][%d]3aa_shot [T:%d][R:%d][F:%d][IN:0x%x] [%d][C:0x%x]" \
			" [%d][P:0x%x] [%d][F:0x%x] [%d][G:0x%x]\n",
			param_set->instance_id, frame->type, param_set->reprocessing,
			param_set->fcount, param_set->input_dva[0],
			param_set->dma_output_before_bds.cmd, param_set->output_dva_before_bds[0],
			param_set->dma_output_after_bds.cmd,  param_set->output_dva_after_bds[0],
			param_set->dma_output_efd.cmd,  param_set->output_dva_efd[0],
			param_set->dma_output_mrg.cmd,  param_set->output_dva_mrg[0]);
	}

	if (frame->shot) {
		param_set->sensor_config.min_target_fps = frame->shot->ctl.aa.aeTargetFpsRange[0];
		param_set->sensor_config.max_target_fps = frame->shot->ctl.aa.aeTargetFpsRange[1];
		param_set->sensor_config.frametime = 1000000 / param_set->sensor_config.min_target_fps;
		dbg_hw(2, "3aa_shot: min_fps(%d), max_fps(%d), frametime(%d)\n",
			param_set->sensor_config.min_target_fps,
			param_set->sensor_config.max_target_fps,
			param_set->sensor_config.frametime);

		ret = is_lib_isp_convert_face_map(hw_ip->hardware, param_set, frame);
		if (ret)
			mserr_hw("Convert face size is fail : %d", instance, hw_ip, ret);

		ret = is_lib_isp_set_ctrl(hw_ip, &hw_3aa->lib[instance], frame);
		if (ret)
			mserr_hw("set_ctrl fail", instance, hw_ip);
	}
	if (is_lib_isp_sensor_update_control(&hw_3aa->lib[instance],
			instance, frame->fcount, frame->shot) < 0) {
		mserr_hw("is_lib_isp_sensor_update_control fail",
			instance, hw_ip);
	}

#if IS_ENABLED(ENABLE_3AA_LIC_OFFSET)
	if (frame->type != SHOT_TYPE_INTERNAL) {
		mode = __is_hw_3aa_get_change_state(hw_ip, instance,
			CHK_MODECHANGE_SCN(frame->shot->ctl.aa.captureIntent));
		ret = __is_hw_3aa_change_sram_offset(hw_ip, instance, mode);
	}
#endif
	ret = is_lib_isp_shot(hw_ip, &hw_3aa->lib[instance], param_set, frame->shot);

	set_bit(HW_CONFIG, &hw_ip->state);

	return ret;
}

static int is_hw_3aa_set_param(struct is_hw_ip *hw_ip, struct is_region *region,
	u32 lindex, u32 hindex, u32 instance, ulong hw_map)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa;
	struct taa_param_set *param_set;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -EINVAL;
	}

	FIMC_BUG(!hw_ip->priv_info);

	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
	param_set = &hw_3aa->param_set[instance];

	hw_ip->region[instance] = region;
	hw_ip->lindex[instance] = lindex;
	hw_ip->hindex[instance] = hindex;

	is_hw_3aa_update_param(hw_ip, &region->parameter, param_set, lindex, hindex, instance);

	ret = is_lib_isp_set_param(hw_ip, &hw_3aa->lib[instance], param_set);
	if (ret)
		mserr_hw("set_param fail", instance, hw_ip);

	return ret;
}

void is_hw_3aa_update_param(struct is_hw_ip *hw_ip, struct is_param_region *param_region,
	struct taa_param_set *param_set, u32 lindex, u32 hindex, u32 instance)
{
	struct taa_param *param;

	FIMC_BUG_VOID(!param_region);
	FIMC_BUG_VOID(!param_set);

	param = &param_region->taa;
	param_set->instance_id = instance;

	if (lindex & LOWBIT_OF(PARAM_SENSOR_CONFIG)) {
		memcpy(&param_set->sensor_config, &param_region->sensor.config,
			sizeof(struct param_sensor_config));
	}

	/* check input */
	if (lindex & LOWBIT_OF(PARAM_3AA_OTF_INPUT)) {
		memcpy(&param_set->otf_input, &param->otf_input,
			sizeof(struct param_otf_input));
	}

	if (lindex & LOWBIT_OF(PARAM_3AA_VDMA1_INPUT)) {
		memcpy(&param_set->dma_input, &param->vdma1_input,
			sizeof(struct param_dma_input));
	}

	/* check output*/
	if (lindex & LOWBIT_OF(PARAM_3AA_OTF_OUTPUT)) {
		memcpy(&param_set->otf_output, &param->otf_output,
			sizeof(struct param_otf_output));
	}

	if (lindex & LOWBIT_OF(PARAM_3AA_VDMA4_OUTPUT)) {
		memcpy(&param_set->dma_output_before_bds, &param->vdma4_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_3AA_VDMA2_OUTPUT)) {
		memcpy(&param_set->dma_output_after_bds, &param->vdma2_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_3AA_FDDMA_OUTPUT)) {
		memcpy(&param_set->dma_output_efd, &param->efd_output,
			sizeof(struct param_dma_output));
	}

	if (lindex & LOWBIT_OF(PARAM_3AA_MRGDMA_OUTPUT)) {
		memcpy(&param_set->dma_output_mrg, &param->mrg_output,
			sizeof(struct param_dma_output));
	}

#ifdef CHAIN_USE_STRIPE_PROCESSING
	if (lindex & LOWBIT_OF(PARAM_3AA_STRIPE_INPUT)) {
		memcpy(&param_set->stripe_input, &param->stripe_input,
			sizeof(struct param_stripe_input));
	}
#endif
}

static int is_hw_3aa_get_meta(struct is_hw_ip *hw_ip, struct is_frame *frame,
	ulong hw_map)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	if (!test_bit_variables(hw_ip->id, &hw_map))
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;

	ret = is_lib_isp_get_meta(hw_ip, &hw_3aa->lib[frame->instance], frame);
	if (ret)
		mserr_hw("get_meta fail", frame->instance, hw_ip);

	if (frame->shot && frame->shot_ext) {
		msdbg_hw(2, "get_meta[F:%d]: ni(%d,%d,%d) binning_r(%d,%d), crop_taa(%d,%d), bds(%d,%d)\n",
			frame->instance, hw_ip, frame->fcount,
			frame->shot->udm.ni.currentFrameNoiseIndex,
			frame->shot->udm.ni.nextFrameNoiseIndex,
			frame->shot->udm.ni.nextNextFrameNoiseIndex,
			frame->shot_ext->binning_ratio_x, frame->shot_ext->binning_ratio_y,
			frame->shot_ext->crop_taa_x, frame->shot_ext->crop_taa_y,
			frame->shot_ext->bds_ratio_x, frame->shot_ext->bds_ratio_y);
	}

	return ret;
}

static int is_hw_3aa_frame_ndone(struct is_hw_ip *hw_ip, struct is_frame *frame,
	u32 instance, enum ShotErrorType done_type)
{
	int wq_id_3xc, wq_id_3xp, wq_id_3xf, wq_id_3xg, wq_id_mexc, wq_id_orbxc;
	int output_id;
	int ret = 0;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!frame);

	switch (hw_ip->id) {
	case DEV_HW_3AA0:
		wq_id_3xc = WORK_30C_FDONE;
		wq_id_3xp = WORK_30P_FDONE;
		wq_id_3xf = WORK_30F_FDONE;
		wq_id_3xg = WORK_30G_FDONE;
		wq_id_mexc = WORK_ME0C_FDONE;
		wq_id_orbxc = WORK_ORB0C_FDONE;
		break;
	case DEV_HW_3AA1:
		wq_id_3xc = WORK_31C_FDONE;
		wq_id_3xp = WORK_31P_FDONE;
		wq_id_3xf = WORK_31F_FDONE;
		wq_id_3xg = WORK_31G_FDONE;
		wq_id_mexc = WORK_ME1C_FDONE;
		wq_id_orbxc = WORK_ORB1C_FDONE;
		break;
	case DEV_HW_3AA2:
		wq_id_3xc = WORK_32C_FDONE;
		wq_id_3xp = WORK_32P_FDONE;
		wq_id_3xf = WORK_32F_FDONE;
		wq_id_3xg = WORK_32G_FDONE;
		wq_id_mexc = WORK_MAX_MAP;
		wq_id_orbxc = WORK_MAX_MAP;
		break;
	default:
		mserr_hw("[F:%d]invalid hw(%d)", instance, hw_ip, frame->fcount, hw_ip->id);
		return -1;
		break;
	}

	output_id = ENTRY_3AC;
	if (test_bit(output_id, &frame->out_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, wq_id_3xc,
				output_id, done_type, false);
	}

	output_id = ENTRY_3AP;
	if (test_bit(output_id, &frame->out_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, wq_id_3xp,
				output_id, done_type, false);
	}

	output_id = ENTRY_3AF;
	if (test_bit(output_id, &frame->out_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, wq_id_3xf,
				output_id, done_type, false);
	}

	output_id = ENTRY_3AG;
	if (test_bit(output_id, &frame->out_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, wq_id_3xg,
				output_id, done_type, false);
	}

	output_id = ENTRY_ORBXC;
	if (test_bit(output_id, &frame->out_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, wq_id_orbxc,
				output_id, done_type, false);
	}

	output_id = ENTRY_MEXC;
	if (test_bit(output_id, &frame->out_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, wq_id_mexc,
				output_id, done_type, false);
	}

	output_id = IS_HW_CORE_END;
	if (test_bit(hw_ip->id, &frame->core_flag)) {
		ret = is_hardware_frame_done(hw_ip, frame, -1,
				output_id, done_type, false);
	}

	return ret;
}

static int is_hw_3aa_load_setfile(struct is_hw_ip *hw_ip, u32 instance, ulong hw_map)
{
	struct is_hw_3aa *hw_3aa = NULL;
	ulong addr;
	u32 size, index;
	int flag = 0, ret = 0;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -ESRCH;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	switch (setfile->version) {
	case SETFILE_V2:
		flag = false;
		break;
	case SETFILE_V3:
		flag = true;
		break;
	default:
		mserr_hw("invalid version (%d)", instance, hw_ip,
			setfile->version);
		return -EINVAL;
		break;
	}

	FIMC_BUG(!hw_ip->priv_info);
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;

	for (index = 0; index < setfile->using_count; index++) {
		addr = setfile->table[index].addr;
		size = setfile->table[index].size;
		ret = is_lib_isp_create_tune_set(&hw_3aa->lib[instance],
			addr, size, index, flag, instance);

		set_bit(index, &hw_3aa->lib[instance].tune_count);
	}

	set_bit(HW_TUNESET, &hw_ip->state);

	return ret;
}

static int is_hw_3aa_apply_setfile(struct is_hw_ip *hw_ip, u32 scenario,
	u32 instance, ulong hw_map)
{
	struct is_hw_3aa *hw_3aa = NULL;
	ulong cal_addr = 0;
	u32 setfile_index = 0;
	int ret = 0;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	if (!test_bit(HW_INIT, &hw_ip->state)) {
		mserr_hw("not initialized!!", instance, hw_ip);
		return -ESRCH;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	if (setfile->using_count == 0) {
		mswarn_hw(" setfile using_count is 0", instance, hw_ip);
		return 0;
	}

	setfile_index = setfile->index[scenario];
	if (setfile_index >= setfile->using_count) {
		mserr_hw("setfile index is out-of-range, [%d:%d]",
				instance, hw_ip, scenario, setfile_index);
		return -EINVAL;
	}

	msinfo_hw("setfile (%d) scenario (%d)\n", instance, hw_ip,
		setfile_index, scenario);

	FIMC_BUG(!hw_ip->priv_info);
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;

	ret = is_lib_isp_apply_tune_set(&hw_3aa->lib[instance], setfile_index, instance);
	if (ret)
		return ret;

	if (sensor_position < SENSOR_POSITION_MAX) {
		cal_addr = hw_3aa->lib_support->minfo->kvaddr_cal[sensor_position];

		msinfo_hw("load cal data, position: %d, addr: 0x%lx\n", instance, hw_ip,
				sensor_position, cal_addr);
		ret = is_lib_isp_load_cal_data(&hw_3aa->lib[instance], instance, cal_addr);
		ret = is_lib_isp_get_cal_data(&hw_3aa->lib[instance], instance,
				&hw_ip->hardware->cal_info[sensor_position], CAL_TYPE_LSC_UVSP);
	} else {
		return 0;
	}

	return ret;
}

static int is_hw_3aa_delete_setfile(struct is_hw_ip *hw_ip, u32 instance,
	ulong hw_map)
{
	struct is_hw_3aa *hw_3aa = NULL;
	int i, ret = 0;
	struct is_hw_ip_setfile *setfile;
	enum exynos_sensor_position sensor_position;

	FIMC_BUG(!hw_ip);

	if (!test_bit_variables(hw_ip->id, &hw_map)) {
		msdbg_hw(2, "%s: hw_map(0x%lx)\n", instance, hw_ip, __func__, hw_map);
		return 0;
	}

	sensor_position = hw_ip->hardware->sensor_position[instance];
	setfile = &hw_ip->setfile[sensor_position];

	if (setfile->using_count == 0)
		return 0;

	FIMC_BUG(!hw_ip->priv_info);
	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;

	for (i = 0; i < setfile->using_count; i++) {
		if (test_bit(i, &hw_3aa->lib[instance].tune_count)) {
			ret = is_lib_isp_delete_tune_set(&hw_3aa->lib[instance],
				(u32)i, instance);
			clear_bit(i, &hw_3aa->lib[instance].tune_count);
		}
	}

	clear_bit(HW_TUNESET, &hw_ip->state);

	return ret;
}

static void is_hw_3aa_size_dump(struct is_hw_ip *hw_ip)
{
#if 0 /* TODO: carrotsm */
	u32 bcrop_w = 0, bcrop_h = 0;

	is_isp_get_bcrop1_size(hw_ip->regs[REG_SETA], &bcrop_w, &bcrop_h);

	sdbg_hw(2, "=SIZE=====================================\n"
		"[BCROP1]w(%d), h(%d)\n"
		"[3AA]==========================================\n",
		 hw_ip, bcrop_w, bcrop_h);
#endif
}

void is_hw_3aa_dump(void)
{
	int ret = 0;

	ret = is_lib_logdump();
}

int is_hw_3aa_restore(struct is_hw_ip *hw_ip, u32 instance)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa = NULL;

	BUG_ON(!hw_ip);
	BUG_ON(!hw_ip->priv_info);

	if (!test_bit(HW_OPEN, &hw_ip->state))
		return -EINVAL;

	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;

	ret = is_lib_isp_reset_recovery(hw_ip, &hw_3aa->lib[instance], instance);
	if (ret) {
		mserr_hw("is_lib_isp_reset_recovery fail ret(%d)",
				instance, hw_ip, ret);
	}

	return ret;
}

static int is_hw_3aa_change_chain(struct is_hw_ip *hw_ip, u32 instance,
	u32 next_id, struct is_hardware *hardware)
{
	int ret = 0;
	struct is_hw_3aa *hw_3aa;
	u32 curr_id;
	u32 next_hw_id;
	int hw_slot;
	struct is_hw_ip *next_hw_ip;
	struct is_group *group;
	enum exynos_sensor_position sensor_position;

	curr_id = hw_ip->id - DEV_HW_3AA0;
	if (curr_id == next_id) {
		mswarn_hw("Same chain (curr:%d, next:%d)", instance, hw_ip,
			curr_id, next_id);
		goto p_err;
	}

	hw_3aa = (struct is_hw_3aa *)hw_ip->priv_info;
	if (!hw_3aa) {
		mserr_hw("failed to get HW 3AA", instance, hw_ip);
		return -ENODEV;
	}

	/* Call DDK */
	ret = is_lib_isp_change_chain(hw_ip, &hw_3aa->lib[instance], instance, next_id);
	if (ret) {
		mserr_hw("is_lib_isp_change_chain", instance, hw_ip);
		return ret;
	}

	next_hw_id = DEV_HW_3AA0 + next_id;
	hw_slot = is_hw_slot_id(next_hw_id);
	if (!valid_hw_slot_id(hw_slot)) {
		merr_hw("[ID:%d]invalid next hw_slot_id [SLOT:%d]", instance,
			next_hw_id, hw_slot);
		return -EINVAL;
	}

	next_hw_ip = &hardware->hw_ip[hw_slot];

	/* This is for decreasing run_rsccount */
	ret = is_hw_3aa_disable(hw_ip, instance, hardware->hw_map[instance]);
	if (ret) {
		msinfo_hw("is_hw_3aa_disable is fail", instance, hw_ip);
		return -EINVAL;
	}

	/*
	 * Copy instance infromation.
	 * But do not clear current hw_ip,
	 * because logical(initial) HW must be refered at close time.
	 */
	((struct is_hw_3aa *)(next_hw_ip->priv_info))->lib[instance]
		= hw_3aa->lib[instance];
	((struct is_hw_3aa *)(next_hw_ip->priv_info))->param_set[instance]
		= hw_3aa->param_set[instance];

	next_hw_ip->group[instance] = hw_ip->group[instance];
	next_hw_ip->region[instance] = hw_ip->region[instance];
	next_hw_ip->hindex[instance] = hw_ip->hindex[instance];
	next_hw_ip->lindex[instance] = hw_ip->lindex[instance];

	sensor_position = hardware->sensor_position[instance];
	next_hw_ip->setfile[sensor_position] = hw_ip->setfile[sensor_position];

	/* set & clear physical HW */
	set_bit(next_hw_id, &hardware->hw_map[instance]);
	clear_bit(hw_ip->id, &hardware->hw_map[instance]);

	/* This is for increasing run_rsccount */
	ret = is_hw_3aa_enable(next_hw_ip, instance, hardware->hw_map[instance]);
	if (ret) {
		msinfo_hw("is_hw_3aa_enable is fail", instance, next_hw_ip);
		return -EINVAL;
	}

	/*
	 * Group in same stream is not change because that is logical,
	 * but hw_ip is not fixed at logical group that is physical.
	 * So, hw_ip have to be saved in group sttucture.
	 */
	group = hw_ip->group[instance];
	group->hw_ip = next_hw_ip;

	msinfo_hw("change_chain done (state: curr(0x%lx) next(0x%lx))", instance, hw_ip,
		hw_ip->state, next_hw_ip->state);
p_err:
	return ret;
}

const struct is_hw_ip_ops is_hw_3aa_ops = {
	.open			= is_hw_3aa_open,
	.init			= is_hw_3aa_init,
	.deinit			= is_hw_3aa_deinit,
	.close			= is_hw_3aa_close,
	.enable			= is_hw_3aa_enable,
	.disable		= is_hw_3aa_disable,
	.shot			= is_hw_3aa_shot,
	.set_param		= is_hw_3aa_set_param,
	.get_meta		= is_hw_3aa_get_meta,
	.frame_ndone		= is_hw_3aa_frame_ndone,
	.load_setfile		= is_hw_3aa_load_setfile,
	.apply_setfile		= is_hw_3aa_apply_setfile,
	.delete_setfile		= is_hw_3aa_delete_setfile,
	.size_dump		= is_hw_3aa_size_dump,
	.restore		= is_hw_3aa_restore,
	.sensor_start		= is_hw_3aa_sensor_start,
	.change_chain		= is_hw_3aa_change_chain,
};

int is_hw_3aa_probe(struct is_hw_ip *hw_ip, struct is_interface *itf,
	struct is_interface_ischain *itfc, int id, const char *name)
{
	int ret = 0;

	FIMC_BUG(!hw_ip);
	FIMC_BUG(!itf);
	FIMC_BUG(!itfc);

	/* initialize device hardware */
	hw_ip->id   = id;
	snprintf(hw_ip->name, sizeof(hw_ip->name), "%s", name);
	hw_ip->ops  = &is_hw_3aa_ops;
	hw_ip->itf  = itf;
	hw_ip->itfc = itfc;
	atomic_set(&hw_ip->fcount, 0);
	hw_ip->is_leader = true;
	atomic_set(&hw_ip->status.Vvalid, V_BLANK);
	atomic_set(&hw_ip->status.otf_start, 0);
	atomic_set(&hw_ip->rsccount, 0);
	atomic_set(&hw_ip->run_rsccount, 0);
	init_waitqueue_head(&hw_ip->status.wait_queue);

	clear_bit(HW_OPEN, &hw_ip->state);
	clear_bit(HW_INIT, &hw_ip->state);
	clear_bit(HW_CONFIG, &hw_ip->state);
	clear_bit(HW_RUN, &hw_ip->state);
	clear_bit(HW_TUNESET, &hw_ip->state);

	sinfo_hw("probe done\n", hw_ip);

	return ret;
}
