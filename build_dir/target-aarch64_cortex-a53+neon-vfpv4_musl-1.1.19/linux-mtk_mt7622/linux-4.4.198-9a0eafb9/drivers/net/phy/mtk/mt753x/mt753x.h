/*
 * Driver for MediaTek MT753x gigabit switch
 *
 * Copyright (C) 2018 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#ifndef _MT753X_H_
#define _MT753X_H_

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of_mdio.h>
#include <linux/workqueue.h>
#include <linux/gpio/consumer.h>
#include <linux/netdevice.h>
#include <linux/hashtable.h>

#ifdef CONFIG_SWCONFIG
#include <linux/switch.h>
#endif

#define MT753X_DFL_CPU_PORT	6
#define MT753X_NUM_PORTS	7
#define MT753X_NUM_PHYS		5
#define MT753X_NUM_VLANS	4095

#define MT753X_MAX_VID		4095
#define MT753X_MIN_VID		0

#define MTK_DEV_NAME    "eth0"

#define MT753X_LINK_WORK_DELAY 1000
#define MT753X_ARL_READ_WORK_DELAY 6000
#define MT753X_MAX_MIRROR_SESSIONS 1

#define MT753X_DFL_SMI_ADDR	0x1f
#define MT753X_SMI_ADDR_MASK	0x1f

#define MT753X_ARL_LIST_HASH_ORDER  8
#define MT753X_ARL_LIST_HASH_SIZE   (256) /* 2^MT753X_ARL_LIST_HASH_ORDER */
#define MT753X_ARL_LIST_HASH(_e)    ((_e)->mac[5] % MT753X_ARL_LIST_HASH_SIZE)

struct gsw_mt753x;

enum mt753x_model {
	MT7530 = 0x7530,
	MT7531 = 0x7531
};

struct mt753x_port_entry {
	u16	pvid;
	bool disable;
	bool ingress_mode_override;
	u8   ingress_mode;
	bool egress_mode_override;
	u8   egress_mode;
	u8   security;
	bool protected;    /* port isolated */
	struct switch_port_link link;
};

struct mt753x_vlan_entry {
	u16	vid;
	u8	member;
	u8	etags;
};

struct mt753x_port_cfg {
	struct device_node *np;
	int phy_mode;
	u32 enabled: 1;
	u32 force_link: 1;
	u32 speed: 2;
	u32 duplex: 1;
};

struct mt753x_phy {
	struct gsw_mt753x *gsw;
	struct net_device netdev;
	struct phy_device *phydev;
};

struct mt753x_arl_entry {
	struct hlist_node list;
	u8 port;
	u8 mac[ETH_ALEN];
	u16 vid;
	u32 last_seen;
	unsigned long first_add;
	bool is_update;
	u8 reserved[2];
};

struct mt753x_reg_entry {
	char *name;
	u32 regnum;
	u32 val;
};

struct gsw_mt753x {
	u32 id;

	struct device *dev;
	struct mii_bus *host_bus;
	struct mii_bus *gphy_bus;
	struct mutex mii_lock;	/* MII access lock */
	u32 smi_addr;
	u32 phy_base;
	int direct_phy_access;

	enum mt753x_model model;
	const char *name;

	struct mt753x_port_cfg port5_cfg;
	struct mt753x_port_cfg port6_cfg;

	bool phy_status_poll;
	struct mt753x_phy phys[MT753X_NUM_PHYS];
	int phy_irqs[PHY_MAX_ADDR];

	int phy_link_sts;

	int reset_pin;
	struct delayed_work irq_worker;

#ifdef CONFIG_SWCONFIG
	struct switch_dev swdev;

	struct mt753x_vlan_entry vlan_entries[MT753X_NUM_VLANS];
	struct mt753x_port_entry port_entries[MT753X_NUM_PORTS];

	int global_vlan_enable;
	u32 cpu_port;
	struct mutex arl_mutex;
	u32 arl_count;
	u32 arl_age_time;
	DECLARE_HASHTABLE(arl_entries, MT753X_ARL_LIST_HASH_ORDER);
	struct delayed_work arl_update_work;
	struct mt753x_reg_entry reg_entry;

	unsigned char port_disable;
	u8 protected_ports;

	/* mirroring */
	bool mirror_rx;
	bool mirror_tx;
	int mirror_src_port;
	int mirror_dst_port;

	/* jumbo frame */
	bool ingress_jumbo_frame_enable;
	size_t max_ingress_jumbo_frame_kbytes;
#endif

	int (*mii_read)(struct gsw_mt753x *gsw, int phy, int reg);
	void (*mii_write)(struct gsw_mt753x *gsw, int phy, int reg, u16 val);

	int (*mmd_read)(struct gsw_mt753x *gsw, int addr, int devad, u16 reg);
	void (*mmd_write)(struct gsw_mt753x *gsw, int addr, int devad, u16 reg,
			  u16 val);

	struct list_head list;
};

struct chip_rev {
	const char *name;
	u32 rev;
};

struct mt753x_sw_id {
	enum mt753x_model model;
	int (*detect)(struct gsw_mt753x *gsw, struct chip_rev *crev);
	int (*init)(struct gsw_mt753x *gsw);
	int (*post_init)(struct gsw_mt753x *gsw);
};

extern struct list_head mt753x_devs;

struct gsw_mt753x *mt753x_get_gsw(u32 id);
struct gsw_mt753x *mt753x_get_first_gsw(void);
void mt753x_put_gsw(void);
void mt753x_lock_gsw(void);

void mt753x_flush_port_arl_table(struct gsw_mt753x *gsw, int port);

u32 mt753x_reg_read(struct gsw_mt753x *gsw, u32 reg);
void mt753x_reg_write(struct gsw_mt753x *gsw, u32 reg, u32 val);

int mt753x_mii_read(struct gsw_mt753x *gsw, int phy, int reg);
void mt753x_mii_write(struct gsw_mt753x *gsw, int phy, int reg, u16 val);

int mt753x_mmd_read(struct gsw_mt753x *gsw, int addr, int devad, u16 reg);
void mt753x_mmd_write(struct gsw_mt753x *gsw, int addr, int devad, u16 reg,
		      u16 val);

int mt753x_mmd_ind_read(struct gsw_mt753x *gsw, int addr, int devad, u16 reg);
void mt753x_mmd_ind_write(struct gsw_mt753x *gsw, int addr, int devad, u16 reg,
			  u16 val);

void mt753x_irq_worker(struct work_struct *work);
void mt753x_irq_enable(struct gsw_mt753x *gsw);

int mt753x_phy_calibration(struct gsw_mt753x *gsw, u8 phyaddr);
int extphy_init(struct gsw_mt753x *gsw, int addr);

/* MDIO Indirect Access Registers */
#define MII_MMD_ACC_CTL_REG		0x0d
#define MMD_CMD_S			14
#define MMD_CMD_M			0xc000
#define MMD_DEVAD_S			0
#define MMD_DEVAD_M			0x1f

/* MMD_CMD: MMD commands */
#define MMD_ADDR			0
#define MMD_DATA			1

#define MII_MMD_ADDR_DATA_REG		0x0e

/* Procedure of MT753x Internal Register Access
 *
 * 1. Internal Register Address
 *
 *    The MT753x has a 16-bit register address and each register is 32-bit.
 *    This means the lowest two bits are not used as the register address is
 *    4-byte aligned.
 *
 *    Rest of the valid bits are divided into two parts:
 *      Bit 15..6 is the Page address
 *      Bit 5..2 is the low address
 *
 *    -------------------------------------------------------------------
 *    | 15  14  13  12  11  10   9   8   7   6 | 5   4   3   2 | 1   0  |
 *    |----------------------------------------|---------------|--------|
 *    |              Page Address              |    Address    | Unused |
 *    -------------------------------------------------------------------
 *
 * 2. MDIO access timing
 *
 *    The MT753x uses the following MDIO timing for a single register read
 *
 *      Phase 1: Write Page Address
 *    -------------------------------------------------------------------
 *    | ST | OP | PHY_ADDR | TYPE | RSVD | TA |  RSVD |    PAGE_ADDR    |
 *    -------------------------------------------------------------------
 *    | 01 | 01 |   11111  |   1  | 1111 | xx | 00000 | REG_ADDR[15..6] |
 *    -------------------------------------------------------------------
 *
 *      Phase 2: Write low Address & Read low word
 *    -------------------------------------------------------------------
 *    | ST | OP | PHY_ADDR | TYPE |    LOW_ADDR    | TA |      DATA     |
 *    -------------------------------------------------------------------
 *    | 01 | 10 |   11111  |   0  | REG_ADDR[5..2] | xx |  DATA[15..0]  |
 *    -------------------------------------------------------------------
 *
 *      Phase 3: Read high word
 *    -------------------------------------------------------------------
 *    | ST | OP | PHY_ADDR | TYPE | RSVD | TA |           DATA          |
 *    -------------------------------------------------------------------
 *    | 01 | 10 |   11111  |   1  | 0000 | xx |       DATA[31..16]      |
 *    -------------------------------------------------------------------
 *
 *    The MT753x uses the following MDIO timing for a single register write
 *
 *      Phase 1: Write Page Address (The same as read)
 *
 *      Phase 2: Write low Address and low word
 *    -------------------------------------------------------------------
 *    | ST | OP | PHY_ADDR | TYPE |    LOW_ADDR    | TA |      DATA     |
 *    -------------------------------------------------------------------
 *    | 01 | 01 |   11111  |   0  | REG_ADDR[5..2] | xx |  DATA[15..0]  |
 *    -------------------------------------------------------------------
 *
 *      Phase 3: write high word
 *    -------------------------------------------------------------------
 *    | ST | OP | PHY_ADDR | TYPE | RSVD | TA |           DATA          |
 *    -------------------------------------------------------------------
 *    | 01 | 01 |   11111  |   1  | 0000 | xx |       DATA[31..16]      |
 *    -------------------------------------------------------------------
 *
 */

/* Internal Register Address fields */
#define MT753X_REG_PAGE_ADDR_S		6
#define MT753X_REG_PAGE_ADDR_M		0xffc0
#define MT753X_REG_ADDR_S		2
#define MT753X_REG_ADDR_M		0x3c

#endif /* _MT753X_H_ */
