// SPDX-License-Identifier: GPL-2.0
/*
 * sun6i_dphy.c
 * Copyright Kévin L'hôpital (C) 2020
 */

#include "sun6i_dphy.h"
#define DPHY_OFFSET 0x1000

void sun6i_reg_default(struct sun6i_csi_dev *sdev)
{
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x004, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x004, 0xb8c39bec);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x008, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x008, 0xb8d257f8);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0xb8df698e);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x018, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x018, 0xb8c8a30c);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x01c, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x01c, 0xb8df8ad7);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x028, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x02c, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x030, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x008, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x104, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x10c, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x110, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x100, 0);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x100, 0xb8c64f24);
}

void sun6i_dphy_init(struct sun6i_csi_dev *sdev)
{
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x00000000);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x00000000);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x00000100);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x00000100);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x00000100);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x00000100);

	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x80000100);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x80008100);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x010, 0x80008000);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x030, 0xa0200000);
}

void sun6i_ctl_init(struct sun6i_csi_dev *sdev)
{
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x004, 0x80000000);
	regmap_write(sdev->regmap, DPHY_OFFSET + 0x100, 0x12200000);
}

void sun6i_mipi_csi_dphy_init(struct sun6i_csi_dev *sdev)
{
	sun6i_reg_default(sdev);
	sun6i_dphy_init(sdev);
	sun6i_ctl_init(sdev);
}
