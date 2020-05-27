// SPDX-License-Identifier: GPL-2.0
/*
 * sun6i_dphy.h
 * Copyright Kévin L'hôpital (C) 2020
 */

#ifndef __SUN6I_DPHY_H__
#define __SUN6I_DPHY_H__

#include <linux/regmap.h>
#include "sun6i_csi.h"

void sun6i_mipi_csi_dphy_init(struct sun6i_csi_dev *sdev);

#endif /* __SUN6I_DPHY_H__ */
