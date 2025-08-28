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

	state = regs->swreg1.frame_rdy_status ? VB2_BUF_STATE_DONE :
						VB2_BUF_STATE_ERROR;

	regs->swreg1.irq_dis = 1;
	regs->swreg1.timeout_int = 0;

	hantro_vc8000e_swreg_write(vpu, regs, swreg1);

	hantro_irq_done(vpu, state);

	return IRQ_HANDLED;
}
