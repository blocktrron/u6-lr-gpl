/*
 * Common part for MediaTek MT753x gigabit switch
 *
 * Copyright (C) 2018 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/phy.h>
#include <linux/rtnetlink.h>

#include "mt753x.h"
#include "mt753x_regs.h"

void mt753x_irq_enable(struct gsw_mt753x *gsw)
{
	u32 val;
	int i;

	/* Record initial PHY link status */
	for (i = 0; i < MT753X_NUM_PHYS; i++) {
		val = gsw->mii_read(gsw, i, MII_BMSR);
		if (val & BMSR_LSTATUS)
			gsw->phy_link_sts |= BIT(i);
	}

	val = BIT(MT753X_NUM_PHYS) - 1;

	mt753x_reg_write(gsw, SYS_INT_EN, val);
}

static void mt753x_send_phy_event(struct net_device *ndev, int port_idx, int link)
{
	struct phymsg msg;
	struct net *net = dev_net(ndev);

	msg.port_idx = port_idx;
	msg.status = link;
	ubnt_net_notify((void *)net, RTNLGRP_LINK, RTM_PHY, (void *)&msg, sizeof(struct phymsg));
}

static void display_port_link_status(struct gsw_mt753x *gsw, u32 port)
{
	u32 pmsr = 0, speed_bits = 0, link = 0, duplex = 0;
	const char *speed;
	struct net_device *netdev = NULL;

	netdev = dev_get_by_name(&init_net, MTK_DEV_NAME);
	if (!netdev)
		return;

	pmsr = mt753x_reg_read(gsw, PMSR(port));
	link = pmsr & MAC_LNK_STS;
	duplex = pmsr & MAC_DPX_STS;
	speed_bits = (pmsr & MAC_SPD_STS_M) >> MAC_SPD_STS_S;

	switch (speed_bits) {
	case MAC_SPD_10:
		speed = "10Mbps";
		break;
	case MAC_SPD_100:
		speed = "100Mbps";
		break;
	case MAC_SPD_1000:
		speed = "1Gbps";
		break;
	case MAC_SPD_2500:
		speed = "2.5Gbps";
		break;
	}

	if (link) {
		/* cache arl on phy up (not too quickly or read misses clients) */
		cancel_delayed_work_sync(&gsw->arl_update_work);
		schedule_delayed_work(&gsw->arl_update_work, msecs_to_jiffies(100));
		dev_info(gsw->dev, "Port %d Link is Up - %s/%s\n",
				port, speed, (duplex) ? "Full" : "Half");
	} else {
		dev_info(gsw->dev, "Port %d Link is Down\n", port);
		mt753x_flush_port_arl_table(gsw, port);
	}
	mt753x_send_phy_event(netdev, port, link);
	dev_put(netdev);
}

void mt753x_irq_worker(struct work_struct *work)
{
	struct gsw_mt753x *gsw = container_of(work, struct gsw_mt753x, irq_worker.work);
	u32 sts, physts, laststs;
	int i;

	sts = mt753x_reg_read(gsw, SYS_INT_STS);

	/* Check for changed PHY link status */
	for (i = 0; i < MT753X_NUM_PHYS; i++) {
		if (!(sts & PHY_LC_INT(i)))
			continue;

		laststs = gsw->phy_link_sts & BIT(i);
		physts = !!(gsw->mii_read(gsw, i, MII_BMSR) & BMSR_LSTATUS);
		physts <<= i;

		if (physts ^ laststs) {
			gsw->phy_link_sts ^= BIT(i);
			display_port_link_status(gsw, i);
		}
	}

	mt753x_reg_write(gsw, SYS_INT_STS, sts);
	schedule_delayed_work(&gsw->irq_worker, msecs_to_jiffies(MT753X_LINK_WORK_DELAY));
}
