// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2025 Paul Kocialkowski <paulk@sys-base.io>
 */

#include "hantro.h"
#include "hantro_vc8000e_regs.h"

irqreturn_t hantro_vc8000e_irq(int irq, void *dev_id)
{
	struct hantro_dev *vpu = dev_id;
	u32 regs_buffer[HANTRO_VC8000E_SWREG_OFFSET(swreg2) / 4];
	struct hantro_vc8000e_regs *regs =
		(struct hantro_vc8000e_regs *)regs_buffer;
	enum vb2_buffer_state state;

	hantro_vc8000e_swreg_read(vpu, regs, swreg1);

	pr_debug("+ hantro-vc8000e-irq: %#x\n",
		 regs_buffer[HANTRO_VC8000E_SWREG_OFFSET(swreg1) / 4]);

	if (regs->swreg1.irq)
		pr_debug("  - irq\n");
	if (regs->swreg1.frame_rdy_status)
		pr_debug("  - frame ready\n");
	if (regs->swreg1.bus_error_status)
		pr_debug("  - bus error\n");
	if (regs->swreg1.sw_reset)
		pr_debug("  - sw reset\n");
	if (regs->swreg1.buffer_full)
		pr_debug("  - buffer full\n");
	if (regs->swreg1.timeout)
		pr_debug("  - timeout\n");
	if (regs->swreg1.slice_rdy_status)
		pr_debug("  - slice ready\n");
	if (regs->swreg1.irq_fuse_error)
		pr_debug("  - fuse error\n");
	if (regs->swreg1.strm_segment_rdy_int)
		pr_debug("  - segment ready\n");

	state = regs->swreg1.frame_rdy_status ? VB2_BUF_STATE_DONE :
						VB2_BUF_STATE_ERROR;

	regs->swreg1.irq_dis = 1;
	regs->swreg1.timeout_int = 0;

	hantro_vc8000e_swreg_write(vpu, regs, swreg1);

	hantro_irq_done(vpu, state);

	return IRQ_HANDLED;
}
