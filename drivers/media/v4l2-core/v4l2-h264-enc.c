// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * V4L2 H.264 Encode Core
 *
 * Copyright (C) 2025 Paul Kocialkowski <paulk@sys-base.io>
 */

#include <linux/module.h>
#include <linux/v4l2-controls.h>
#include <media/v4l2-h264.h>
#include <media/v4l2-h264-enc.h>
#include <media/v4l2-h264-enc-rbsp.h>
#include <media/videobuf2-v4l2.h>

int v4l2_h264_enc_init(struct v4l2_h264_enc *enc)
{
	struct v4l2_h264_enc_rbsp *rbsp = &enc->rbsp;
	int ret;

	if ((!enc->format && !enc->format_mplane) || !enc->timeperframe ||
	    !enc->ctrl_handler)
		return -EINVAL;

	memset(&enc->state_active, 0, sizeof(enc->state_active));
	memset(&enc->state_next, 0, sizeof(enc->state_next));

	rbsp->ops = enc->rbsp_ops;
	rbsp->private_data = enc->private_data;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_h264_enc_init);

void v4l2_h264_enc_exit(struct v4l2_h264_enc *enc)
{
}
EXPORT_SYMBOL_GPL(v4l2_h264_enc_exit);

static int state_prepare_params(struct v4l2_h264_enc *enc)
{
	struct v4l2_h264_enc_state *state = &enc->state_next;
	struct v4l2_ctrl_h264_sps *sps = &state->sps;
	struct v4l2_h264_sps_video *sps_video = &state->sps_video;
	struct v4l2_ctrl_h264_pps *pps = &state->pps;
	struct v4l2_ctrl_h264_encode_params *encode = &state->encode;
	struct v4l2_ctrl_handler *handler = enc->ctrl_handler;
	struct v4l2_fract *timeperframe = enc->timeperframe;
	unsigned int width, height;
	struct v4l2_ctrl *ctrl;

	/* Time per frame */

	state->timeperframe = *timeperframe;

	/* Format */

	if (enc->format_mplane)
		width = enc->format_mplane->width;
	else
		width = enc->format->width;

	state->width_mbs = DIV_ROUND_UP(width, V4L2_H264_ENC_MB_UNIT);
	state->width_aligned = ALIGN(width, V4L2_H264_ENC_MB_UNIT);

	if (enc->format_mplane)
		height = enc->format_mplane->height;
	else
		height = enc->format->height;

	state->height_mbs = DIV_ROUND_UP(height, V4L2_H264_ENC_MB_UNIT);
	state->height_aligned = ALIGN(height, V4L2_H264_ENC_MB_UNIT);

	/* SPS */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_STATELESS_H264_SPS);
	if (!ctrl)
		return -EINVAL;

	memcpy(sps, ctrl->p_cur.p_h264_sps, sizeof(*sps));

	sps->pic_width_in_mbs_minus1 = state->width_mbs - 1;
	sps->pic_height_in_map_units_minus1 = state->height_mbs - 1;

	/*
	 * Only pic_order_cnt_type = 0 is currently supported.
	 * Error out since no pic_order_cnt_lsb was provided.
	 */
	if (sps->pic_order_cnt_type)
		return -EINVAL;

	/* SPS Video */

	if (width < state->width_aligned || height < state->height_aligned) {
		sps_video->flags |= V4L2_H264_SPS_VIDEO_FLAG_FRAME_CROPPING;

		sps_video->frame_crop_left_offset = 0;
		sps_video->frame_crop_right_offset = (state->width_aligned -
						      width) / 2;
		sps_video->frame_crop_top_offset = 0;
		sps_video->frame_crop_bottom_offset = (state->height_aligned -
						       height) / 2;
	}

	if (timeperframe->numerator && timeperframe->denominator) {
		sps_video->flags |=
			V4L2_H264_SPS_VIDEO_FLAG_VUI_PARAMETERS_PRESENT |
			V4L2_H264_SPS_VIDEO_FLAG_VUI_TIMING_INFO_PRESENT |
			V4L2_H264_SPS_VIDEO_FLAG_VUI_FIXED_FRAME_RATE;

		/* Timing info is always provided as a field rate. */
		sps_video->num_units_in_tick = timeperframe->numerator;
		sps_video->time_scale = timeperframe->denominator * 2;
	}

	/* PPS */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_STATELESS_H264_PPS);
	if (!ctrl)
		return -EINVAL;

	memcpy(pps, ctrl->p_cur.p_h264_pps, sizeof(*pps));

	/* Only single instances of PPS and SPS are supported. */
	if (pps->seq_parameter_set_id != sps->seq_parameter_set_id)
		pps->seq_parameter_set_id = sps->seq_parameter_set_id;

	pps->flags &= ~V4L2_H264_PPS_FLAG_BOTTOM_FIELD_PIC_ORDER_IN_FRAME_PRESENT;

	/* Slice groups are not supported. */
	pps->num_slice_groups_minus1 = 0;

	if (pps->num_ref_idx_l0_default_active_minus1 >
	    (sps->max_num_ref_frames - 1))
		pps->num_ref_idx_l0_default_active_minus1 =
			sps->max_num_ref_frames - 1;

	if (pps->num_ref_idx_l1_default_active_minus1 >
	    (sps->max_num_ref_frames - 1))
		pps->num_ref_idx_l1_default_active_minus1 =
			sps->max_num_ref_frames - 1;

	pps->flags &= ~V4L2_H264_PPS_FLAG_WEIGHTED_PRED;
	pps->weighted_bipred_idc = 0;

	/* Switching slices are not supported. */
	pps->pic_init_qs_minus26 = 0;

	/* Redundant pictures are not supported. */
	pps->flags &= ~V4L2_H264_PPS_FLAG_REDUNDANT_PIC_CNT_PRESENT;

	/* Scaling matrix is not supported. */
	pps->flags &= ~V4L2_H264_PPS_FLAG_SCALING_MATRIX_PRESENT;

	/*
	 * RBSP always writes the optional second_chroma_qp_index_offset,
	 * which has to take the chroma_qp_index_offset value if unsupported.
	 */
	if (!(enc->flags & V4L2_H264_ENC_FLAG_CHROMA_QP_CR_OFFSET))
		pps->second_chroma_qp_index_offset =
			pps->chroma_qp_index_offset;

	/* Encode */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_STATELESS_H264_ENCODE_PARAMS);
	if (!ctrl)
		return -EINVAL;

	memcpy(encode, ctrl->p_cur.p_h264_encode_params, sizeof(*encode));

	if (encode->slice_type == V4L2_H264_SLICE_TYPE_SI)
		encode->slice_type = V4L2_H264_SLICE_TYPE_I;
	else if (encode->slice_type == V4L2_H264_SLICE_TYPE_SP)
		encode->slice_type = V4L2_H264_SLICE_TYPE_P;

	/*
	 * FIXME: We cannot rely on the poc provided for a B frame to work
	 * for a P frame. Maybe this should be reported before and errored out.
	 */
	if (encode->slice_type == V4L2_H264_SLICE_TYPE_B &&
	    !(enc->flags & V4L2_H264_ENC_FLAG_INTER_BIPRED))
		encode->slice_type = V4L2_H264_SLICE_TYPE_P;

	if (encode->slice_type == V4L2_H264_SLICE_TYPE_P &&
	    !(enc->flags & V4L2_H264_ENC_FLAG_INTER_PRED))
		encode->slice_type = V4L2_H264_SLICE_TYPE_I;

	if (encode->slice_type != V4L2_H264_SLICE_TYPE_I &&
	    !sps->max_num_ref_frames)
		return -EINVAL;

	/* Only single instances of PPS and SPS are supported. */
	if (encode->pic_parameter_set_id != pps->pic_parameter_set_id)
		encode->pic_parameter_set_id = pps->pic_parameter_set_id;

	if (encode->flags & V4L2_H264_ENCODE_FLAG_NUM_REF_IDX_ACTIVE_OVERRIDE) {
		if (encode->num_ref_idx_l0_active_minus1 >
		    (sps->max_num_ref_frames - 1))
			encode->num_ref_idx_l0_active_minus1 =
				sps->max_num_ref_frames - 1;

		if (encode->num_ref_idx_l1_active_minus1 >
		    (sps->max_num_ref_frames - 1))
			encode->num_ref_idx_l1_active_minus1 =
				sps->max_num_ref_frames - 1;
	}

	return 0;
}

static int state_prepare(struct v4l2_h264_enc *enc)
{
	struct v4l2_h264_enc_state *state = &enc->state_next;
	int ret;

	memset(state, 0, sizeof(*state));
	state->valid = true;

	ret = state_prepare_params(enc);
	if (ret)
		return ret;

	ret = v4l2_h264_enc_op(enc, state_constrain, state);
	if (ret && ret != -EOPNOTSUPP)
		return ret;

	return 0;
}

static int state_commit(struct v4l2_h264_enc *enc)
{
	struct v4l2_ctrl_handler *handler = enc->ctrl_handler;
	struct v4l2_ctrl *ctrl;
	int ret;

	/* The presence of required controls was checked already. */
	/* TODO: Attach to media request. */

	/* SPS */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_STATELESS_H264_SPS);
	ret = v4l2_ctrl_s_ctrl_compound(ctrl, V4L2_CTRL_TYPE_H264_SPS,
					&enc->state_next.sps);
	if (ret)
		return ret;

	/* TODO: Return sps_video too. */

	/* PPS */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_STATELESS_H264_PPS);
	ret = v4l2_ctrl_s_ctrl_compound(ctrl, V4L2_CTRL_TYPE_H264_PPS,
					&enc->state_next.pps);
	if (ret)
		return ret;

	/* Encode */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_STATELESS_H264_ENCODE_PARAMS);
	ret = v4l2_ctrl_s_ctrl_compound(ctrl, V4L2_CTRL_TYPE_H264_ENCODE_PARAMS,
					&enc->state_next.encode);
	if (ret)
		return ret;

	/* State */

	memcpy(&enc->state_active, &enc->state_next, sizeof(enc->state_active));

	return 0;
}

static int state_complete(struct v4l2_h264_enc *enc,
			  struct vb2_v4l2_buffer *buffer)
{

	struct v4l2_h264_enc_state *state = &enc->state_active;
	struct v4l2_ctrl_h264_encode_params *encode = &state->encode;

	if (encode->slice_type == V4L2_H264_SLICE_TYPE_I)
		buffer->flags |= V4L2_BUF_FLAG_KEYFRAME;
	else if (encode->slice_type == V4L2_H264_SLICE_TYPE_P)
		buffer->flags |= V4L2_BUF_FLAG_PFRAME;
	else if (encode->slice_type == V4L2_H264_SLICE_TYPE_B)
		buffer->flags |= V4L2_BUF_FLAG_BFRAME;

	return 0;
}

static int rbsp_update(struct v4l2_h264_enc *enc)
{
	struct v4l2_h264_enc_state *state_next = &enc->state_next;
	struct v4l2_h264_enc_state *state_active = &enc->state_active;
	struct v4l2_ctrl_h264_encode_params *encode = &state_next->encode;
	struct v4l2_ctrl_handler *handler = enc->ctrl_handler;
	struct v4l2_ctrl *ctrl;

	enc->rbsp_update = 0;

	/* Start Code */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_STATELESS_H264_START_CODE);
	if ((ctrl && ctrl->cur.val == V4L2_STATELESS_H264_START_CODE_ANNEX_B) ||
	    !ctrl)
		enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_START_CODE;

	/* AUD */

	ctrl = v4l2_ctrl_find(handler, V4L2_CID_MPEG_VIDEO_AU_DELIMITER);
	if (ctrl && ctrl->cur.val)
		enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_AUD;

	/* SPS */

	if (state_active->valid) {
		if (memcmp(&state_active->sps, &state_next->sps,
			   sizeof(state_active->sps)) ||
		    memcmp(&state_active->sps_video, &state_next->sps_video,
			   sizeof(state_active->sps_video)))
			enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_SPS;
	} else {
		enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_SPS;
	}

	/* PPS */

	if (state_active->valid) {
		if (memcmp(&state_active->pps, &state_next->pps,
			   sizeof(state_active->pps)))
			enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_PPS;
	} else {
		enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_PPS;
	}

	/* IDR Prepend */

	ctrl = v4l2_ctrl_find(handler,
			      V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR);
	if (ctrl && ctrl->cur.val &&
	    encode->flags & V4L2_H264_ENCODE_FLAG_IDR_PIC)
		enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_SPS |
				    V4L2_H264_ENC_RBSP_UPDATE_PPS;

	/* Slice */

	enc->rbsp_update |= V4L2_H264_ENC_RBSP_UPDATE_SLICE_HEADER;

	return 0;
}

static int rbsp_step_unit(struct v4l2_h264_enc *enc,
			      unsigned int rbsp_update, unsigned int hw_flag)
{
	struct v4l2_h264_enc_state *state = &enc->state_active;
	struct v4l2_ctrl_h264_sps *sps = &state->sps;
	struct v4l2_h264_sps_video *sps_video = &state->sps_video;
	struct v4l2_ctrl_h264_pps *pps = &state->pps;
	struct v4l2_ctrl_h264_encode_params *encode = &state->encode;
	struct v4l2_h264_enc_rbsp *rbsp = &enc->rbsp;
	u8 primary_pic_type;
	int ret;

	/* Return if no update is needed or if hardware generates the unit. */
	if (!(enc->rbsp_update & rbsp_update) || enc->flags & hw_flag)
		return 0;

	if (enc->rbsp_update & V4L2_H264_ENC_RBSP_UPDATE_START_CODE) {
		ret = v4l2_h264_enc_rbsp_start_code(rbsp);
		if (ret)
			return ret;
	}

	if (rbsp_update == V4L2_H264_ENC_RBSP_UPDATE_AUD) {
		if (enc->flags & V4L2_H264_ENC_FLAG_INTER_BIPRED &&
		    enc->flags & V4L2_H264_ENC_FLAG_INTER_PRED)
			primary_pic_type = V4L2_H264_PRIMARY_PIC_TYPE_IPB;
		else if (enc->flags & V4L2_H264_ENC_FLAG_INTER_PRED)
			primary_pic_type = V4L2_H264_PRIMARY_PIC_TYPE_IP;
		else
			primary_pic_type = V4L2_H264_PRIMARY_PIC_TYPE_I;
	}

	if (rbsp_update == V4L2_H264_ENC_RBSP_UPDATE_AUD)
		return v4l2_h264_enc_rbsp_aud(rbsp, primary_pic_type);
	else if (rbsp_update == V4L2_H264_ENC_RBSP_UPDATE_SPS)
		return v4l2_h264_enc_rbsp_sps(rbsp, sps, sps_video);
	else if (rbsp_update == V4L2_H264_ENC_RBSP_UPDATE_PPS)
		return v4l2_h264_enc_rbsp_pps(rbsp, pps);
	else if (rbsp_update == V4L2_H264_ENC_RBSP_UPDATE_SLICE_HEADER)
		return v4l2_h264_enc_rbsp_slice_header(rbsp, sps, pps, encode);

	return -EINVAL;
}

static int rbsp_step(struct v4l2_h264_enc *enc,
			 struct vb2_v4l2_buffer *buffer)
{
	struct v4l2_h264_enc_rbsp *rbsp = &enc->rbsp;
	void *pointer = vb2_plane_vaddr(&buffer->vb2_buf, 0);
	unsigned int size = vb2_plane_size(&buffer->vb2_buf, 0);
	int ret;

	ret = v4l2_h264_enc_rbsp_init(rbsp, pointer, size);
	if (ret)
		return ret;

	ret = rbsp_step_unit(enc, V4L2_H264_ENC_RBSP_UPDATE_AUD,
			     V4L2_H264_ENC_FLAG_HW_AUD);
	if (ret)
		return ret;

	ret = rbsp_step_unit(enc, V4L2_H264_ENC_RBSP_UPDATE_SPS,
			     V4L2_H264_ENC_FLAG_HW_SPS);
	if (ret)
		return ret;

	ret = rbsp_step_unit(enc, V4L2_H264_ENC_RBSP_UPDATE_PPS,
			     V4L2_H264_ENC_FLAG_HW_PPS);
	if (ret)
		return ret;

	ret = rbsp_step_unit(enc, V4L2_H264_ENC_RBSP_UPDATE_SLICE_HEADER,
			     V4L2_H264_ENC_FLAG_HW_SLICE_HEADER);
	if (ret)
		return ret;

	return 0;
}

int v4l2_h264_enc_step(struct v4l2_h264_enc *enc,
		       struct vb2_v4l2_buffer *buffer)
{
	int ret;

	ret = state_prepare(enc);
	if (ret)
		return ret;

	ret = rbsp_update(enc);
	if (ret)
		return ret;

	ret = state_commit(enc);
	if (ret)
		return ret;

	ret = rbsp_step(enc, buffer);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_h264_enc_step);

int v4l2_h264_enc_complete(struct v4l2_h264_enc *enc,
			   struct vb2_v4l2_buffer *buffer)
{
	int ret;

	ret = state_complete(enc, buffer);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_h264_enc_complete);

MODULE_DESCRIPTION("V4L2 H.264 Encode Core");
MODULE_AUTHOR("Paul Kocialkowski <paulk@sys-base.io>");
MODULE_LICENSE("GPL");
