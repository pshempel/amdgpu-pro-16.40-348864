/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 */

#include "dm_services.h"

#include "dc.h"

#include "core_status.h"
#include "core_types.h"
#include "hw_sequencer.h"

#include "resource.h"

#include "adapter_service_interface.h"
#include "clock_source.h"
#include "dc_bios_types.h"

#include "bandwidth_calcs.h"
#include "include/irq_service_interface.h"
#include "transform.h"
#include "timing_generator.h"
#include "virtual/virtual_link_encoder.h"

#include "link_hwss.h"
#include "link_encoder.h"

#include "dc_link_ddc.h"
#include "dm_helpers.h"

/*******************************************************************************
 * Private structures
 ******************************************************************************/

struct dc_target_sync_report {
	uint32_t h_count;
	uint32_t v_count;
};

/*******************************************************************************
 * Private functions
 ******************************************************************************/
static void destroy_links(struct core_dc *dc)
{
	uint32_t i;

	for (i = 0; i < dc->link_count; i++) {
		if (NULL != dc->links[i])
			link_destroy(&dc->links[i]);
	}
}

static bool create_links(
		struct core_dc *dc,
		struct adapter_service *as,
		uint32_t num_virtual_links)
{
	int i;
	int connectors_num;
	struct dc_bios *dcb;

	dc->link_count = 0;

	dcb = dc->ctx->dc_bios;

	connectors_num = dcb->funcs->get_connectors_number(dcb);

	if (connectors_num > ENUM_ID_COUNT) {
		dm_error(
			"DC: Number of connectors %d exceeds maximum of %d!\n",
			connectors_num,
			ENUM_ID_COUNT);
		return false;
	}

	if (connectors_num == 0 && num_virtual_links == 0) {
		dm_error("DC: Number of connectors is zero!\n");
	}

	dm_output_to_console(
		"DC: %s: connectors_num: physical:%d, virtual:%d\n",
		__func__,
		connectors_num,
		num_virtual_links);

	for (i = 0; i < connectors_num; i++) {
		struct link_init_data link_init_params = {0};
		struct core_link *link;

		link_init_params.ctx = dc->ctx;
		link_init_params.adapter_srv = as;
		link_init_params.connector_index = i;
		link_init_params.link_index = dc->link_count;
		link_init_params.dc = dc;
		link = link_create(&link_init_params);

		if (link) {
			dc->links[dc->link_count] = link;
			link->dc = dc;
			++dc->link_count;
		} else {
			dm_error("DC: failed to create link!\n");
		}
	}

	for (i = 0; i < num_virtual_links; i++) {
		struct core_link *link = dm_alloc(sizeof(*link));
		struct encoder_init_data enc_init = {0};

		if (link == NULL) {
			BREAK_TO_DEBUGGER();
			goto failed_alloc;
		}

		link->adapter_srv = as;
		link->ctx = dc->ctx;
		link->dc = dc;
		link->public.connector_signal = SIGNAL_TYPE_VIRTUAL;
		link->link_id.type = OBJECT_TYPE_CONNECTOR;
		link->link_id.id = CONNECTOR_ID_VIRTUAL;
		link->link_id.enum_id = ENUM_ID_1;
		link->link_enc = dm_alloc(sizeof(*link->link_enc));

		enc_init.adapter_service = as;
		enc_init.ctx = dc->ctx;
		enc_init.channel = CHANNEL_ID_UNKNOWN;
		enc_init.hpd_source = HPD_SOURCEID_UNKNOWN;
		enc_init.transmitter = TRANSMITTER_UNKNOWN;
		enc_init.connector = link->link_id;
		enc_init.encoder.type = OBJECT_TYPE_ENCODER;
		enc_init.encoder.id = ENCODER_ID_INTERNAL_VIRTUAL;
		enc_init.encoder.enum_id = ENUM_ID_1;
		virtual_link_encoder_construct(link->link_enc, &enc_init);

		link->public.link_index = dc->link_count;
		dc->links[dc->link_count] = link;
		dc->link_count++;
	}

	return true;

failed_alloc:
	return false;
}



static struct adapter_service *create_as(
		const struct dc_init_data *init,
		struct dc_context *dc_ctx)
{
	struct adapter_service *as = NULL;
	struct as_init_data init_data;

	memset(&init_data, 0, sizeof(init_data));

	init_data.ctx = dc_ctx;

	/* BIOS parser init data */
	init_data.bp_init_data.ctx = dc_ctx;
	init_data.bp_init_data.bios = init->asic_id.atombios_base_address;

	/* HW init data */
	init_data.hw_init_data.chip_id = init->asic_id.chip_id;
	init_data.hw_init_data.chip_family = init->asic_id.chip_family;
	init_data.hw_init_data.pci_revision_id = init->asic_id.pci_revision_id;
	init_data.hw_init_data.fake_paths_num = init->asic_id.fake_paths_num;
	init_data.hw_init_data.feature_flags = init->asic_id.feature_flags;
	init_data.hw_init_data.hw_internal_rev = init->asic_id.hw_internal_rev;
	init_data.hw_init_data.runtime_flags = init->asic_id.runtime_flags;
	init_data.hw_init_data.vram_width = init->asic_id.vram_width;
	init_data.hw_init_data.vram_type = init->asic_id.vram_type;

	init_data.display_param = &init->display_param;
	init_data.vbios_override = init->vbios_override;
	init_data.dce_environment = init->dce_environment;

	as = dal_adapter_service_create(&init_data);

	return as;
}

static bool stream_adjust_vmin_vmax(struct dc *dc,
		const struct dc_stream **stream, int num_streams,
		int vmin, int vmax)
{
	/* TODO: Support multiple streams */
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct core_stream *core_stream = DC_STREAM_TO_CORE(stream[0]);
	int i = 0;
	bool ret = false;
	struct pipe_ctx *pipes;

	for (i = 0; i < MAX_PIPES; i++) {
		if (core_dc->current_context->res_ctx.pipe_ctx[i].stream
				== core_stream) {

			pipes = &core_dc->current_context->res_ctx.pipe_ctx[i];
			core_dc->hwss.set_drr(&pipes, 1, vmin, vmax);

			/* build and update the info frame */
			resource_build_info_frame(pipes);
			core_dc->hwss.update_info_frame(pipes);

			ret = true;
		}
	}

	return ret;
}

static bool set_gamut_remap(struct dc *dc,
			const struct dc_stream **stream, int num_streams)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct core_stream *core_stream = DC_STREAM_TO_CORE(stream[0]);
	int i = 0;
	bool ret = false;
	struct pipe_ctx *pipes;

	for (i = 0; i < MAX_PIPES; i++) {
		if (core_dc->current_context->res_ctx.pipe_ctx[i].stream
				== core_stream) {

			pipes = &core_dc->current_context->res_ctx.pipe_ctx[i];
			core_dc->hwss.set_plane_config(core_dc, pipes,
					&core_dc->current_context->res_ctx);
			ret = true;
		}
	}

	return ret;
}

static void stream_send_null_packet(const struct dc_stream *dc_stream,
		bool enable)
{
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);
	struct core_dc *core_dc = DC_TO_CORE(stream->ctx->dc);

	int i;

	struct resource_context *res_ctx = &core_dc->current_context->res_ctx;

	for (i = 0; i < res_ctx->pool->pipe_count; i++) {
		if (res_ctx->pipe_ctx[i].stream != stream)
			continue;

		res_ctx->pipe_ctx[i].stream_enc->funcs->
		send_null_packet(res_ctx->pipe_ctx[i].stream_enc, enable);
	}
}

static void allocate_dc_stream_funcs(struct core_dc *core_dc)
{
	if (core_dc->hwss.set_drr != NULL) {
		core_dc->public.stream_funcs.adjust_vmin_vmax =
				stream_adjust_vmin_vmax;
		core_dc->public.stream_funcs.send_null_packet =
				stream_send_null_packet;
	}

	core_dc->public.stream_funcs.set_gamut_remap =
			set_gamut_remap;
}

static bool construct(struct core_dc *dc,
		const struct dc_init_data *init_params)
{
	struct dal_logger *logger;
	struct adapter_service *as;
	struct dc_context *dc_ctx = dm_alloc(sizeof(*dc_ctx));
	enum dce_version dc_version = DCE_VERSION_UNKNOWN;

	if (!dc_ctx) {
		dm_error("%s: failed to create ctx\n", __func__);
		goto ctx_fail;
	}

	dc->current_context = dm_alloc(sizeof(*dc->current_context));

	if (!dc->current_context) {
		dm_error("%s: failed to create validate ctx\n", __func__);
		goto val_ctx_fail;
	}

	dc_ctx->cgs_device = init_params->cgs_device;
	dc_ctx->driver_context = init_params->driver;
	dc_ctx->dc = &dc->public;

	/* Create logger */
	logger = dal_logger_create(dc_ctx);

	if (!logger) {
		/* can *not* call logger. call base driver 'print error' */
		dm_error("%s: failed to create Logger!\n", __func__);
		goto logger_fail;
	}
	dc_ctx->logger = logger;
	dc->ctx = dc_ctx;
	dc->ctx->dce_environment = init_params->dce_environment;


	dc_version = resource_parse_asic_id(init_params->asic_id);


	/* TODO: Refactor DCE code to remove AS and asic caps */
	if (dc_version < DCE_VERSION_MAX) {
		/* Create adapter service */
		as = create_as(init_params, dc_ctx);

		if (!as) {
			dm_error("%s: create_as() failed!\n", __func__);
			goto as_fail;
		}

		/* Initialize HW controlled by Adapter Service */
		if (false == dal_adapter_service_initialize_hw_data(
				as)) {
			dm_error("%s: dal_adapter_service_initialize_hw_data()"\
					"  failed!\n", __func__);
			/* Note that AS exist, so have to destroy it.*/
			goto as_fail;
		}

		dc_ctx->dc_bios = dal_adapter_service_get_bios_parser(as);

		dc->res_pool = dc_create_resource_pool(as, dc,
				init_params->num_virtual_links, dc_version);
		if (!dc->res_pool)
			goto create_resource_fail;

		if (!create_links(dc, as, init_params->num_virtual_links))
			goto create_links_fail;
	} else {

		/* Resource should construct all asic specific resources.
		 * This should be the only place where we need to parse the asic id
		 */

		dc_ctx->dc_bios = init_params->vbios_override;
		dc->res_pool = dc_create_resource_pool(NULL, dc,
				init_params->num_virtual_links, dc_version);
		if (!dc->res_pool)
			goto create_resource_fail;

		if (!create_links(dc, NULL, init_params->num_virtual_links))
			goto create_links_fail;
	}

	allocate_dc_stream_funcs(dc);

	return true;

	/**** error handling here ****/
create_links_fail:
	dc->res_pool->funcs->destroy(&dc->res_pool);
create_resource_fail:
	if (as)
		dal_adapter_service_destroy(&as);
as_fail:
	dal_logger_destroy(&dc_ctx->logger);
logger_fail:
	dm_free(dc->current_context);
val_ctx_fail:
	dm_free(dc_ctx);
ctx_fail:
	return false;
}

static void destruct(struct core_dc *dc)
{
	resource_validate_ctx_destruct(dc->current_context);
	dm_free(dc->current_context);
	dc->current_context = NULL;
	destroy_links(dc);
	dc->res_pool->funcs->destroy(&dc->res_pool);
	dal_logger_destroy(&dc->ctx->logger);
	dm_free(dc->ctx);
	dc->ctx = NULL;
}

/*
void ProgramPixelDurationV(unsigned int pixelClockInKHz )
{
	fixed31_32 pixel_duration = Fixed31_32(100000000, pixelClockInKHz) * 10;
	unsigned int pixDurationInPico = round(pixel_duration);

	DPG_PIPE_ARBITRATION_CONTROL1 arb_control;

	arb_control.u32All = ReadReg (mmDPGV0_PIPE_ARBITRATION_CONTROL1);
	arb_control.bits.PIXEL_DURATION = pixDurationInPico;
	WriteReg (mmDPGV0_PIPE_ARBITRATION_CONTROL1, arb_control.u32All);

	arb_control.u32All = ReadReg (mmDPGV1_PIPE_ARBITRATION_CONTROL1);
	arb_control.bits.PIXEL_DURATION = pixDurationInPico;
	WriteReg (mmDPGV1_PIPE_ARBITRATION_CONTROL1, arb_control.u32All);

	WriteReg (mmDPGV0_PIPE_ARBITRATION_CONTROL2, 0x4000800);
	WriteReg (mmDPGV0_REPEATER_PROGRAM, 0x11);

	WriteReg (mmDPGV1_PIPE_ARBITRATION_CONTROL2, 0x4000800);
	WriteReg (mmDPGV1_REPEATER_PROGRAM, 0x11);
}
*/

/*******************************************************************************
 * Public functions
 ******************************************************************************/

struct dc *dc_create(const struct dc_init_data *init_params)
 {
	struct dc_context ctx = {
		.driver_context = init_params->driver,
		.cgs_device = init_params->cgs_device
	};
	struct core_dc *core_dc = dm_alloc(sizeof(*core_dc));

	if (NULL == core_dc)
		goto alloc_fail;

	ctx.dc = &core_dc->public;
	if (false == construct(core_dc, init_params))
		goto construct_fail;

	/*TODO: separate HW and SW initialization*/
	core_dc->hwss.init_hw(core_dc);

	core_dc->public.caps.max_targets = dm_min(
			core_dc->res_pool->pipe_count,
			core_dc->res_pool->stream_enc_count);
	core_dc->public.caps.max_links = core_dc->link_count;
	core_dc->public.caps.max_audios = core_dc->res_pool->audio_count;

	core_dc->public.config.gpu_vm_support = init_params->flags.gpu_vm_support;

	dal_logger_write(core_dc->ctx->logger,
			LOG_MAJOR_INTERFACE_TRACE,
			LOG_MINOR_COMPONENT_DC,
			"Display Core initialized\n");

	return &core_dc->public;

construct_fail:
	dm_free(core_dc);

alloc_fail:
	return NULL;
}

void dc_destroy(struct dc **dc)
{
	struct core_dc *core_dc = DC_TO_CORE(*dc);
	destruct(core_dc);
	dm_free(core_dc);
	*dc = NULL;
}

bool dc_validate_resources(
		const struct dc *dc,
		const struct dc_validation_set set[],
		uint8_t set_count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;

	context = dm_alloc(sizeof(struct validate_context));
	if(context == NULL)
		goto context_alloc_fail;

	result = core_dc->res_pool->funcs->validate_with_context(
						core_dc, set, set_count, context);

	resource_validate_ctx_destruct(context);
	dm_free(context);

context_alloc_fail:
	if (result != DC_OK) {
		dal_logger_write(core_dc->ctx->logger,
				LOG_MAJOR_WARNING,
				LOG_MINOR_COMPONENT_TOPOLOGY_MANAGER,
				"%s:resource validation failed, dc_status:%d\n",
				__func__,
				result);
	}

	return (result == DC_OK);

}

bool dc_validate_guaranteed(
		const struct dc *dc,
		const struct dc_target *dc_target)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;

	context = dm_alloc(sizeof(struct validate_context));
	if (context == NULL)
		goto context_alloc_fail;

	result = core_dc->res_pool->funcs->validate_guaranteed(
					core_dc, dc_target, context);

	resource_validate_ctx_destruct(context);
	dm_free(context);

context_alloc_fail:
	if (result != DC_OK) {
		dal_logger_write(core_dc->ctx->logger,
			LOG_MAJOR_WARNING,
			LOG_MINOR_COMPONENT_TOPOLOGY_MANAGER,
			"%s:guaranteed validation failed, dc_status:%d\n",
			__func__,
			result);
		}

	return (result == DC_OK);
}

static void program_timing_sync(
		struct core_dc *core_dc,
		struct validate_context *ctx)
{
	int i, j;
	int group_index = 0;
	int pipe_count = ctx->res_ctx.pool->pipe_count;
	struct pipe_ctx *unsynced_pipes[MAX_PIPES] = { NULL };

	for (i = 0; i < pipe_count; i++) {
		if (!ctx->res_ctx.pipe_ctx[i].stream)
			continue;

		unsynced_pipes[i] = &ctx->res_ctx.pipe_ctx[i];
	}

	for (i = 0; i < pipe_count; i++) {
		int group_size = 1;
		struct pipe_ctx *pipe_set[MAX_PIPES];

		if (!unsynced_pipes[i])
			continue;

		pipe_set[0] = unsynced_pipes[i];
		unsynced_pipes[i] = NULL;

		/* Add tg to the set, search rest of the tg's for ones with
		 * same timing, add all tgs with same timing to the group
		 */
		for (j = i + 1; j < pipe_count; j++) {
			if (!unsynced_pipes[j])
				continue;

			if (resource_are_streams_timing_synchronizable(
					unsynced_pipes[j]->stream,
					pipe_set[0]->stream)) {
				pipe_set[group_size] = unsynced_pipes[j];
				unsynced_pipes[j] = NULL;
				group_size++;
			}
		}

		if (group_size > 1) {
			core_dc->hwss.enable_timing_synchronization(
				core_dc, group_index, group_size, pipe_set);
			group_index++;
		}
	}
}

static bool targets_changed(
		struct core_dc *dc,
		struct dc_target *targets[],
		uint8_t target_count)
{
	uint8_t i;

	if (target_count != dc->current_context->target_count)
		return true;

	for (i = 0; i < dc->current_context->target_count; i++) {
		if (&dc->current_context->targets[i]->public != targets[i])
			return true;
	}

	return false;
}

static void target_enable_memory_requests(struct dc_target *dc_target,
		struct resource_context *res_ctx)
{
	uint8_t i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < MAX_PIPES; j++) {
			struct timing_generator *tg = res_ctx->pipe_ctx[j].tg;

			if (res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			if (!tg->funcs->set_blank(tg, false)) {
				dm_error("DC: failed to unblank crtc!\n");
				BREAK_TO_DEBUGGER();
			}
		}
	}
}

static void target_disable_memory_requests(struct dc_target *dc_target,
		struct resource_context *res_ctx)
{
	int i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);

	for (i = 0; i < target->public.stream_count; i++) {
		for (j = 0; j < res_ctx->pool->pipe_count; j++) {
			struct timing_generator *tg = res_ctx->pipe_ctx[j].tg;

			if (res_ctx->pipe_ctx[j].top_pipe != NULL ||
				res_ctx->pipe_ctx[j].stream !=
				DC_STREAM_TO_CORE(target->public.streams[i]))
				continue;

			if (!tg->funcs->set_blank(tg, true)) {
				dm_error("DC: failed to blank crtc!\n");
				BREAK_TO_DEBUGGER();
			}
		}
	}
}

void pplib_apply_safe_state(
	const struct core_dc *dc)
{
	dm_pp_apply_safe_state(dc->ctx);
}

static void fill_display_configs(
	const struct validate_context *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{
	uint8_t i, j, k;
	uint8_t num_cfgs = 0;

	for (i = 0; i < context->target_count; i++) {
		const struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			const struct core_stream *stream =
				DC_STREAM_TO_CORE(target->public.streams[j]);
			struct dm_pp_single_disp_config *cfg =
					&pp_display_cfg->disp_configs[num_cfgs];
			const struct pipe_ctx *pipe_ctx = NULL;

			for (k = 0; k < MAX_PIPES; k++)
				if (stream ==
					context->res_ctx.pipe_ctx[k].stream) {
					pipe_ctx = &context->res_ctx.pipe_ctx[k];
					break;
				}

			ASSERT(pipe_ctx != NULL);

			num_cfgs++;
			cfg->signal = pipe_ctx->stream->signal;
			cfg->pipe_idx = pipe_ctx->pipe_idx;
			cfg->src_height = stream->public.src.height;
			cfg->src_width = stream->public.src.width;
			cfg->ddi_channel_mapping =
				stream->sink->link->ddi_channel_mapping.raw;
			cfg->transmitter =
				stream->sink->link->link_enc->transmitter;
			cfg->link_settings.lane_count = stream->sink->link->public.cur_link_settings.lane_count;
			cfg->link_settings.link_rate = stream->sink->link->public.cur_link_settings.link_rate;
			cfg->link_settings.link_spread = stream->sink->link->public.cur_link_settings.link_spread;
			cfg->sym_clock = stream->phy_pix_clk;
			/* Round v_refresh*/
			cfg->v_refresh = stream->public.timing.pix_clk_khz * 1000;
			cfg->v_refresh /= stream->public.timing.h_total;
			cfg->v_refresh = (cfg->v_refresh + stream->public.timing.v_total / 2)
						/ stream->public.timing.v_total;
		}
	}
	pp_display_cfg->display_count = num_cfgs;
}

static uint32_t get_min_vblank_time_us(const struct validate_context *context)
{
	uint8_t i, j;
	uint32_t min_vertical_blank_time = -1;

	for (i = 0; i < context->target_count; i++) {
		const struct core_target *target = context->targets[i];

		for (j = 0; j < target->public.stream_count; j++) {
			const struct dc_stream *stream =
						target->public.streams[j];
			uint32_t vertical_blank_in_pixels = 0;
			uint32_t vertical_blank_time = 0;

			vertical_blank_in_pixels = stream->timing.h_total *
				(stream->timing.v_total
					- stream->timing.v_addressable);
			vertical_blank_time = vertical_blank_in_pixels
				* 1000 / stream->timing.pix_clk_khz;
			if (min_vertical_blank_time > vertical_blank_time)
				min_vertical_blank_time = vertical_blank_time;
		}
	}
	return min_vertical_blank_time;
}

void pplib_apply_display_requirements(
	const struct core_dc *dc,
	const struct validate_context *context,
	struct dm_pp_display_configuration *pp_display_cfg)
{
	pp_display_cfg->all_displays_in_sync =
		context->bw_results.all_displays_in_sync;
	pp_display_cfg->nb_pstate_switch_disable =
			context->bw_results.nbp_state_change_enable == false;
	pp_display_cfg->cpu_cc6_disable =
			context->bw_results.cpuc_state_change_enable == false;
	pp_display_cfg->cpu_pstate_disable =
			context->bw_results.cpup_state_change_enable == false;
	pp_display_cfg->cpu_pstate_separation_time =
			context->bw_results.required_blackout_duration_us;

	pp_display_cfg->min_memory_clock_khz = context->bw_results.required_yclk
		/ MEMORY_TYPE_MULTIPLIER;
	pp_display_cfg->min_engine_clock_khz = context->bw_results.required_sclk;
	pp_display_cfg->min_engine_clock_deep_sleep_khz
			= context->bw_results.required_sclk_deep_sleep;

	pp_display_cfg->avail_mclk_switch_time_us =
						get_min_vblank_time_us(context);
	/* TODO: dce11.2*/
	pp_display_cfg->avail_mclk_switch_time_in_disp_active_us = 0;

	pp_display_cfg->disp_clk_khz = context->bw_results.dispclk_khz;

	fill_display_configs(context, pp_display_cfg);

	/* TODO: is this still applicable?*/
	if (pp_display_cfg->display_count == 1) {
		const struct dc_crtc_timing *timing =
			&context->targets[0]->public.streams[0]->timing;

		pp_display_cfg->crtc_index =
			pp_display_cfg->disp_configs[0].pipe_idx;
		pp_display_cfg->line_time_in_us = timing->h_total * 1000
							/ timing->pix_clk_khz;
	}

	dm_pp_apply_display_requirements(dc->ctx, pp_display_cfg);
}

bool dc_commit_targets(
	struct dc *dc,
	struct dc_target *targets[],
	uint8_t target_count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dc_bios *dcb = core_dc->ctx->dc_bios;
	enum dc_status result = DC_ERROR_UNEXPECTED;
	struct validate_context *context;
	struct dc_validation_set set[MAX_TARGETS];
	uint8_t i;

	if (false == targets_changed(core_dc, targets, target_count))
		return DC_OK;

	dal_logger_write(core_dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC,
				"%s: %d targets\n",
				__func__,
				target_count);

	for (i = 0; i < target_count; i++) {
		struct dc_target *target = targets[i];

		dc_target_log(target,
				core_dc->ctx->logger,
				LOG_MAJOR_INTERFACE_TRACE,
				LOG_MINOR_COMPONENT_DC);

		set[i].target = targets[i];
		set[i].surface_count = 0;

	}

	context = dm_alloc(sizeof(struct validate_context));
	if (context == NULL)
		goto context_alloc_fail;

	result = core_dc->res_pool->funcs->validate_with_context(core_dc, set, target_count, context);
	if (result != DC_OK){
		dal_logger_write(core_dc->ctx->logger,
					LOG_MAJOR_ERROR,
					LOG_MINOR_COMPONENT_DC,
					"%s: Context validation failed! dc_status:%d\n",
					__func__,
					result);
		BREAK_TO_DEBUGGER();
		resource_validate_ctx_destruct(context);
		goto fail;
	}

	pplib_apply_safe_state(core_dc);

	if (!dcb->funcs->is_accelerated_mode(dcb)) {
		core_dc->hwss.enable_accelerated_mode(core_dc);
	}

	if (result == DC_OK) {
		result = core_dc->hwss.apply_ctx_to_hw(core_dc, context);
	}

	program_timing_sync(core_dc, context);

	for (i = 0; i < context->target_count; i++) {
		struct dc_target *dc_target = &context->targets[i]->public;
		struct core_sink *sink = DC_SINK_TO_CORE(dc_target->streams[0]->sink);

		if (context->target_status[i].surface_count > 0)
			target_enable_memory_requests(dc_target,
					&core_dc->current_context->res_ctx);

		CONN_MSG_MODE(sink->link, "{%dx%d, %dx%d@%dKhz}",
				dc_target->streams[0]->timing.h_addressable,
				dc_target->streams[0]->timing.v_addressable,
				dc_target->streams[0]->timing.h_total,
				dc_target->streams[0]->timing.v_total,
				dc_target->streams[0]->timing.pix_clk_khz);
	}

	pplib_apply_display_requirements(core_dc,
			context, &context->pp_display_cfg);

	resource_validate_ctx_destruct(core_dc->current_context);

	dm_free(core_dc->current_context);
	core_dc->current_context = context;

	return (result == DC_OK);

fail:
	dm_free(context);

context_alloc_fail:
	return (result == DC_OK);
}

bool dc_commit_surfaces_to_target(
		struct dc *dc,
		struct dc_surface *new_surfaces[],
		uint8_t new_surface_count,
		struct dc_target *dc_target)

{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dc_bios *dcb = core_dc->ctx->dc_bios;

	int i, j;
	uint32_t prev_disp_clk = core_dc->current_context->bw_results.dispclk_khz;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct dc_target_status *target_status = NULL;
	struct validate_context *context;
	struct validate_context *temp_context;

	int current_enabled_surface_count = 0;
	int new_enabled_surface_count = 0;

	if (core_dc->current_context->target_count == 0)
		return false;


	context = dm_alloc(sizeof(struct validate_context));

	if (!context) {
		dm_error("%s: failed to create validate ctx\n", __func__);
		goto val_ctx_fail;
	}

	resource_validate_ctx_copy_construct(core_dc->current_context, context);

	/* Cannot commit surface to a target that is not commited */
	for (i = 0; i < context->target_count; i++)
		if (target == context->targets[i])
			break;

	target_status = &context->target_status[i];

	if (!dcb->funcs->is_accelerated_mode(dcb)
			|| i == context->target_count) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	for (i = 0; i < target_status->surface_count; i++)
		if (target_status->surfaces[i]->visible)
			current_enabled_surface_count++;

	for (i = 0; i < new_surface_count; i++)
		if (new_surfaces[i]->visible)
			new_enabled_surface_count++;

	/*
	 * Do not print if we have 2 surfaces previously and we are currently
	 * comiting 2 surfaces. Since the multi-plane case generate a lot of
	 * spam
	 */
	if (!((new_surface_count > 1) && target_status->surface_count > 1))
		dal_logger_write(core_dc->ctx->logger,
					LOG_MAJOR_INTERFACE_TRACE,
					LOG_MINOR_COMPONENT_DC,
					"%s: commit %d surfaces to target 0x%x\n",
					__func__,
					new_surface_count,
					dc_target);

	if (!resource_attach_surfaces_to_context(
			new_surfaces, new_surface_count, dc_target, context)) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	for (i = 0; i < new_surface_count; i++)
		for (j = 0; j < MAX_PIPES; j++) {
			if (context->res_ctx.pipe_ctx[j].surface !=
					DC_SURFACE_TO_CORE(new_surfaces[i]))
				continue;

			resource_build_scaling_params(
				new_surfaces[i], &context->res_ctx.pipe_ctx[j]);

			if (dc->debug.surface_visual_confirm) {
				context->res_ctx.pipe_ctx[j].scl_data.recout.height -= 2;
				context->res_ctx.pipe_ctx[j].scl_data.recout.width -= 2;
			}
		}

	if (core_dc->res_pool->funcs->validate_bandwidth(core_dc, context) != DC_OK) {
		BREAK_TO_DEBUGGER();
		goto unexpected_fail;
	}

	if (core_dc->res_pool->funcs->apply_clk_constraints) {
		temp_context = core_dc->res_pool->funcs->apply_clk_constraints(
				core_dc,
				context);
		if (!temp_context) {
			dm_error("%s:failed apply clk constraints\n", __func__);
			BREAK_TO_DEBUGGER();
			goto unexpected_fail;
		}
		resource_validate_ctx_destruct(context);
		dm_free(context);
		context = temp_context;
	}

	if (prev_disp_clk < context->bw_results.dispclk_khz) {
		pplib_apply_display_requirements(core_dc, context,
								&context->pp_display_cfg);
		core_dc->hwss.set_display_clock(context);
	}


	if (current_enabled_surface_count > 0 && new_enabled_surface_count == 0)
		target_disable_memory_requests(dc_target,
				&core_dc->current_context->res_ctx);

	core_dc->hwss.apply_ctx_to_surface(core_dc, context);

	/* TODO: decouple wm programming and display clock and unhack this condition*/
	/* Lower display clock if necessary */
	if (prev_disp_clk > context->bw_results.dispclk_khz) {
		core_dc->hwss.set_display_clock(context);
		pplib_apply_display_requirements(core_dc, context,
						&context->pp_display_cfg);
	}

	resource_validate_ctx_destruct(core_dc->current_context);
	dm_free(core_dc->current_context);
	core_dc->current_context = context;

	return true;

unexpected_fail:
	resource_validate_ctx_destruct(context);
	dm_free(context);
val_ctx_fail:

	return false;
}

bool dc_update_surfaces_for_target(
		struct dc *dc,
		struct dc_surface *new_surfaces[],
		uint8_t new_surface_count,
		struct dc_target *dc_target)

{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	struct dc_bios *dcb = core_dc->ctx->dc_bios;

	int i, j;
	struct core_target *target = DC_TARGET_TO_CORE(dc_target);
	struct dc_target_status *target_status = NULL;
	struct validate_context *context;

	if (core_dc->current_context->target_count == 0)
		return false;

	context = core_dc->current_context;

	/* Cannot commit surface to a target that is not committed */
	for (i = 0; i < context->target_count; i++)
		if (target == context->targets[i])
			break;

	target_status = &context->target_status[i];

	if (!dcb->funcs->is_accelerated_mode(dcb)
			|| i == context->target_count) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	if (!resource_attach_surfaces_to_context(
			new_surfaces, new_surface_count, dc_target, context)) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	for (i = 0; i < new_surface_count; i++)
		for (j = 0; j < MAX_PIPES; j++) {
			if (context->res_ctx.pipe_ctx[j].surface !=
					DC_SURFACE_TO_CORE(new_surfaces[i]))
				continue;

			resource_build_scaling_params(
				new_surfaces[i], &context->res_ctx.pipe_ctx[j]);
		}

	core_dc->hwss.update_plane_surface(core_dc, context, new_surfaces,
			new_surface_count);
	return true;
}

uint8_t dc_get_current_target_count(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->current_context->target_count;
}

struct dc_target *dc_get_target_at_index(const struct dc *dc, uint8_t i)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	if (i < core_dc->current_context->target_count)
		return &(core_dc->current_context->targets[i]->public);
	return NULL;
}

const struct dc_link *dc_get_link_at_index(const struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return &core_dc->links[link_index]->public;
}

const struct graphics_object_id dc_get_link_id_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->link_id;
}

const struct ddc_service *dc_get_ddc_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->ddc;
}

enum dc_irq_source dc_get_hpd_irq_source_at_index(
	struct dc *dc, uint32_t link_index)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return core_dc->links[link_index]->public.irq_source_hpd;
}

const struct audio **dc_get_audios(struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return (const struct audio **)core_dc->res_pool->audios;
}

void dc_flip_surface_addrs(
		struct dc *dc,
		const struct dc_surface *const surfaces[],
		struct dc_flip_addrs flip_addrs[],
		uint32_t count)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	int i, j;
	int pipe_count = core_dc->res_pool->pipe_count;

	for (i = 0; i < count; i++)
		for (j = 0; j < pipe_count; j++) {
			struct pipe_ctx *pipe_ctx =
				&core_dc->current_context->res_ctx.pipe_ctx[j];
			struct core_surface *ctx_surface = pipe_ctx->surface;

			if (DC_SURFACE_TO_CORE(surfaces[i]) != ctx_surface)
				continue;

			ctx_surface->public.address = flip_addrs[i].address;
			ctx_surface->public.flip_immediate = flip_addrs[i].flip_immediate;

			if (!ctx_surface->public.flip_immediate)
				core_dc->hwss.pipe_control_lock(
						core_dc->ctx,
						pipe_ctx->pipe_idx,
						PIPE_LOCK_CONTROL_SURFACE |
						PIPE_LOCK_CONTROL_MODE,
						true);

			core_dc->hwss.update_plane_addr(core_dc, pipe_ctx);
		}

	for (j = pipe_count - 1; j >= 0; j--)
		for (i = count - 1; i >= 0; i--) {
			struct pipe_ctx *pipe_ctx =
				&core_dc->current_context->res_ctx.pipe_ctx[j];
			struct core_surface *ctx_surface = pipe_ctx->surface;

			if (DC_SURFACE_TO_CORE(surfaces[i]) != ctx_surface)
				continue;

			if (!ctx_surface->public.flip_immediate)
				core_dc->hwss.pipe_control_lock(
						core_dc->ctx,
						pipe_ctx->pipe_idx,
						PIPE_LOCK_CONTROL_SURFACE,
						false);
		}
}

enum dc_irq_source dc_interrupt_to_irq_source(
		struct dc *dc,
		uint32_t src_id,
		uint32_t ext_id)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	return dal_irq_service_to_irq_source(core_dc->res_pool->irqs, src_id, ext_id);
}

void dc_interrupt_set(const struct dc *dc, enum dc_irq_source src, bool enable)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	dal_irq_service_set(core_dc->res_pool->irqs, src, enable);
}

void dc_interrupt_ack(struct dc *dc, enum dc_irq_source src)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);
	dal_irq_service_ack(core_dc->res_pool->irqs, src);
}

void dc_set_power_state(
	struct dc *dc,
	enum dc_acpi_cm_power_state power_state,
	enum dc_video_power_state video_power_state)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	core_dc->previous_power_state = core_dc->current_power_state;
	core_dc->current_power_state = video_power_state;

	switch (power_state) {
	case DC_ACPI_CM_POWER_STATE_D0:
		core_dc->hwss.init_hw(core_dc);
		break;
	default:
		/* NULL means "reset/release all DC targets" */
		dc_commit_targets(dc, NULL, 0);

		core_dc->hwss.power_down(core_dc);

		/* Zero out the current context so that on resume we start with
		 * clean state, and dc hw programming optimizations will not
		 * cause any trouble.
		 */
		memset(core_dc->current_context, 0,
				sizeof(*core_dc->current_context));
		break;
	}

}

void dc_resume(const struct dc *dc)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	uint32_t i;

	for (i = 0; i < core_dc->link_count; i++)
		core_link_resume(core_dc->links[i]);
}

bool dc_read_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];
	enum ddc_result r = dal_ddc_service_read_dpcd_data(
			link->ddc,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
}

bool dc_write_dpcd(
		struct dc *dc,
		uint32_t link_index,
		uint32_t address,
		const uint8_t *data,
		uint32_t size)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];

	enum ddc_result r = dal_ddc_service_write_dpcd_data(
			link->ddc,
			address,
			data,
			size);
	return r == DDC_RESULT_SUCESSFULL;
}

bool dc_submit_i2c(
		struct dc *dc,
		uint32_t link_index,
		struct i2c_command *cmd)
{
	struct core_dc *core_dc = DC_TO_CORE(dc);

	struct core_link *link = core_dc->links[link_index];
	struct ddc_service *ddc = link->ddc;

	return dal_i2caux_submit_i2c_command(
		dal_adapter_service_get_i2caux(ddc->as),
		ddc->ddc_pin,
		cmd);
}

static bool link_add_remote_sink_helper(struct core_link *core_link, struct dc_sink *sink)
{
	struct dc_link *dc_link = &core_link->public;

	if (dc_link->sink_count >= MAX_SINKS_PER_LINK) {
		BREAK_TO_DEBUGGER();
		return false;
	}

	dc_sink_retain(sink);

	dc_link->remote_sinks[dc_link->sink_count] = sink;
	dc_link->sink_count++;

	return true;
}

struct dc_sink *dc_link_add_remote_sink(
		const struct dc_link *link,
		const uint8_t *edid,
		int len,
		struct dc_sink_init_data *init_data)
{
	struct dc_sink *dc_sink;
	enum dc_edid_status edid_status;
	struct core_link *core_link = DC_LINK_TO_LINK(link);

	if (len > MAX_EDID_BUFFER_SIZE) {
		dm_error("Max EDID buffer size breached!\n");
		return NULL;
	}

	if (!init_data) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	if (!init_data->link) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dc_sink = dc_sink_create(init_data);

	if (!dc_sink)
		return NULL;

	memmove(dc_sink->dc_edid.raw_edid, edid, len);
	dc_sink->dc_edid.length = len;

	if (!link_add_remote_sink_helper(
			core_link,
			dc_sink))
		goto fail_add_sink;

	edid_status = dm_helpers_parse_edid_caps(
			core_link->ctx,
			&dc_sink->dc_edid,
			&dc_sink->edid_caps);

	if (edid_status != EDID_OK)
		goto fail;

	return dc_sink;
fail:
	dc_link_remove_remote_sink(link, dc_sink);
fail_add_sink:
	dc_sink_release(dc_sink);
	return NULL;
}

void dc_link_set_sink(const struct dc_link *link, struct dc_sink *sink)
{
	struct core_link *core_link = DC_LINK_TO_LINK(link);
	struct dc_link *dc_link = &core_link->public;

	dc_link->local_sink = sink;

	if (sink == NULL) {
		dc_link->type = dc_connection_none;
	} else {
		dc_link->type = dc_connection_single;
	}
}

void dc_link_remove_remote_sink(const struct dc_link *link, const struct dc_sink *sink)
{
	int i;
	struct core_link *core_link = DC_LINK_TO_LINK(link);
	struct dc_link *dc_link = &core_link->public;

	if (!link->sink_count) {
		BREAK_TO_DEBUGGER();
		return;
	}

	for (i = 0; i < dc_link->sink_count; i++) {
		if (dc_link->remote_sinks[i] == sink) {
			dc_sink_release(sink);
			dc_link->remote_sinks[i] = NULL;

			/* shrink array to remove empty place */
			while (i < dc_link->sink_count - 1) {
				dc_link->remote_sinks[i] = dc_link->remote_sinks[i+1];
				i++;
			}

			dc_link->sink_count--;
			return;
		}
	}
}

const struct dc_stream_status *dc_stream_get_status(
	const struct dc_stream *dc_stream)
{
	struct core_stream *stream = DC_STREAM_TO_CORE(dc_stream);

	return &stream->status;
}
