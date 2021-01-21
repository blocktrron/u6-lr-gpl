/*
 * OpenWrt swconfig support for MediaTek MT753x Gigabit switch
 *
 * Copyright (C) 2018 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <linux/if.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <linux/bitops.h>
#include <net/genetlink.h>
#include <linux/switch.h>
#include <linux/delay.h>
#include <linux/phy.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/lockdep.h>
#include <linux/workqueue.h>
#include <linux/of_device.h>
#include <linux/hashtable.h>
#include <linux/wireless.h>
#include <linux/if_arp.h>
#include <net/iw_handler.h>
#include <linux/parser.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif /* CONFIG_PROC_FS */

#include "mt753x.h"
#include "mt753x_swconfig.h"
#include "mt753x_regs.h"
#include "mt753x_phy.h"

/* MAC Forward Control */
#define REG_ESW_WT_MAC_MFC                  0x10
#define REG_ESW_ISC                         0x18
#define REG_ESW_BPC                         0x24
#define REG_ESW_WT_MAC_ATA1                 0x74
#define REG_ESW_WT_MAC_ATA2                 0x78
#define REG_ESW_WT_MAC_ATWD                 0x7C
#define REG_ESW_WT_MAC_ATC                  0x80
#define REG_ESW_WT_MAC_MFC_MIR_EN           BIT(3)
#define REG_ESW_WT_MAC_MFC_MIR_PORT_MASK    GENMASK(2, 0)

/* registers */
#define REG_ESW_VLAN_VTCR       0x90
#define REG_ESW_VLAN_VAWD1      0x94
#define REG_ESW_VLAN_VAWD2      0x98
#define REG_ESW_VLAN_VTIM(x)    (0x100 + 4 * ((x) / 2))

/* arl read data */
#define REG_ESW_ARL_TSRA1           0x84
#define REG_ESW_ARL_TSRA1_MAC0_S    24
#define REG_ESW_ARL_TSRA1_MAC1_S    16
#define REG_ESW_ARL_TSRA1_MAC2_S    8
#define REG_ESW_ARL_TSRA1_MAC3_S    0

#define REG_ESW_ARL_TSRA2           0x88
#define REG_ESW_ARL_TSRA2_MAC4_S    24
#define REG_ESW_ARL_TSRA2_MAC5_S    16
#define REG_ESW_ARL_TSRA_MAC_MASK   0xff
#define REG_ESW_ARL_TSRA2_IVL       BIT(15)
#define REG_ESW_ARL_TSRA2_CVID_S    0
#define REG_ESW_ARL_TSRA2_CVID_MASK 0xfff

#define REG_ESW_ARL_ATRD                0x8c
#define REG_ESW_ARL_ATRD_AGE_CNT_S      24
#define REG_ESW_ARL_ATRD_AGE_CNT_MASK   0xff
#define REG_ESW_ARL_ATRD_PORT_MAP_S     4
#define REG_ESW_ARL_ATRD_PORT_MAP_MASK  0xff
#define REG_ESW_ARL_ATRD_STATUS_S       2
#define REG_ESW_ARL_ATRD_STATUS_MASK    0x3

/* vlan and acl write data */
#define REG_ESW_VLAN_VAWD1_IVL_MAC  BIT(30)
#define REG_ESW_VLAN_VAWD1_VTAG_EN  BIT(28)
#define REG_ESW_VLAN_VAWD1_VALID    BIT(0)

/* arl age time control */
#define REG_ESW_ARL_AAC                 0xa0
#define REG_ESW_ARL_AAC_AGE_DIS         BIT(20)
#define REG_ESW_ARL_AAC_AGE_CNT_S       12
#define REG_ESW_ARL_AAC_AGE_CNT_MASK    0xff
#define REG_ESW_ARL_AAC_AGE_UNIT_S      0
#define REG_ESW_ARL_AAC_AGE_UNIT_MASK   0xfff

/* Port Control Register */
#define REG_ESW_PORT_PCR(x)         PCR(x)
#define REG_ESW_PORT_PCR_TX_MIR     BIT(9)
#define REG_ESW_PORT_PCR_RX_MIR     BIT(8)
/*
 * Port Matrix Member[23:16] The legacy port VLAN function.
 * Each bit indicates the permissible egress port.
 */
#define REG_ESW_PORT_PCR_BIT_PORT_MATRIX(_ports)    (((_ports) & 0xff) << 16)
#define REG_ESW_PORT_PCR_PORT_MATRIX(_pcr)          (((_pcr) >> 16) & 0xff)
/* Port-Based Egress VLAN Tag Attribution[29:28] */
#define REG_ESW_PORT_PCR_EG_TAG_S       28
#define REG_ESW_PORT_PCR_EG_TAG_MASK    0x3

/* Port VLAN Control */
#define REG_ESW_PORT_PVC(x)             PVC(x)
#define REG_ESW_PORT_PVC_EG_TAG_S       8
#define REG_ESW_PORT_PVC_EG_TAG_MASK    0x7

#define DEFAULT_VLAN    1

/* vlan egress mode */
enum {
    ETAG_CTRL_UNTAG = 0,
    ETAG_CTRL_TAG   = 2,
    ETAG_CTRL_SWAP  = 1,
    ETAG_CTRL_STACK = 3,
};

/* vlan port mode */
enum {
    VLAN_PORT_USER = 0,
    VLAN_PORT_STACK = 1,
    VLAN_PORT_TRANSLATION = 2,
    VLAN_PORT_TRANSPARENT = 3,
};

/* port vlan select mechanism */
enum {
    INGRESS_PORT_MATRIX = 0,
    INGRESS_FALLBACK = 1,
    INGRESS_CHECK = 2,
    INGRESS_SECURE = 3,
};

/* egress mode api options */
enum {
    EGRESS_MODE_UNMOD = 0,
    EGRESS_MODE_UNTAG = 1,
    EGRESS_MODE_TAG = 2,
    EGRESS_MODE_UNTOUCH = 3,
};

/* incoming port egress vlan tag */
enum {
    EGRESS_SYS_DEFAULT = 0,
    EGRESS_CONSISTENT = 1,
    EGRESS_UNTAG = 4,
    EGRESS_SWAP = 5,
    EGRESS_TAG = 6,
    EGRESS_STACK = 7,
};

/* Port forwarding */
enum {
    REG_ESW_PORT_FW_DISABLE = 0,
    REG_ESW_PORT_FW_CPU_PORT_EXCLUDE = 4,
    REG_ESW_PORT_FW_CPU_PORT_INCLUDE = 5,
    REG_ESW_PORT_FW_CPU_PORT_ONLY = 6,
    REG_ESW_PORT_FW_DROP = 7,
};

/* Egress VLAN tag attribution */
enum {
    REG_ESW_EG_TAG_SYS_DEFAULT = 0,
    REG_ESW_EG_TAG_CONSISTENT = 1,
    REG_ESW_EG_TAG_UNTAG = 4,
    REG_ESW_EG_TAG_SWAP = 5,
    REG_ESW_EG_TAG_TAG = 6,
    REG_ESW_EG_TAG_STACK = 7,
};

#define REG_ESW_PORT_PPBV1(x)   (0x2014 | ((x) << 8))

#define REG_HWTRAP  0x7804

#define MT753X_PORT_MIB_TXB_ID  18	/* TxByte */
#define MT753X_PORT_MIB_RXB_ID  37	/* RxByte */

#define MIB_DESC(_s, _o, _n)   \
	{                       \
		.size = (_s),   \
		.offset = (_o), \
		.name = (_n),   \
	}

struct mt753x_mib_desc {
    unsigned int size;
    unsigned int offset;
    const char *name;
};

static struct gsw_mt753x *mtk_gsw = NULL;

static const struct mt753x_mib_desc mt753x_mibs[] = {
	MIB_DESC(1, STATS_TDPC, "TxDrop"),
	MIB_DESC(1, STATS_TCRC, "TxCRC"),
	MIB_DESC(1, STATS_TUPC, "TxUni"),
	MIB_DESC(1, STATS_TMPC, "TxMulti"),
	MIB_DESC(1, STATS_TBPC, "TxBroad"),
	MIB_DESC(1, STATS_TCEC, "TxCollision"),
	MIB_DESC(1, STATS_TSCEC, "TxSingleCol"),
	MIB_DESC(1, STATS_TMCEC, "TxMultiCol"),
	MIB_DESC(1, STATS_TDEC, "TxDefer"),
	MIB_DESC(1, STATS_TLCEC, "TxLateCol"),
	MIB_DESC(1, STATS_TXCEC, "TxExcCol"),
	MIB_DESC(1, STATS_TPPC, "TxPause"),
	MIB_DESC(1, STATS_TL64PC, "Tx64Byte"),
	MIB_DESC(1, STATS_TL65PC, "Tx65Byte"),
	MIB_DESC(1, STATS_TL128PC, "Tx128Byte"),
	MIB_DESC(1, STATS_TL256PC, "Tx256Byte"),
	MIB_DESC(1, STATS_TL512PC, "Tx512Byte"),
	MIB_DESC(1, STATS_TL1024PC, "Tx1024Byte"),
	MIB_DESC(2, STATS_TOC, "TxByte"),
	MIB_DESC(1, STATS_RDPC, "RxDrop"),
	MIB_DESC(1, STATS_RFPC, "RxFiltered"),
	MIB_DESC(1, STATS_RUPC, "RxUni"),
	MIB_DESC(1, STATS_RMPC, "RxMulti"),
	MIB_DESC(1, STATS_RBPC, "RxBroad"),
	MIB_DESC(1, STATS_RAEPC, "RxAlignErr"),
	MIB_DESC(1, STATS_RCEPC, "RxCRC"),
	MIB_DESC(1, STATS_RUSPC, "RxUnderSize"),
	MIB_DESC(1, STATS_RFEPC, "RxFragment"),
	MIB_DESC(1, STATS_ROSPC, "RxOverSize"),
	MIB_DESC(1, STATS_RJEPC, "RxJabber"),
	MIB_DESC(1, STATS_RPPC, "RxPause"),
	MIB_DESC(1, STATS_RL64PC, "Rx64Byte"),
	MIB_DESC(1, STATS_RL65PC, "Rx65Byte"),
	MIB_DESC(1, STATS_RL128PC, "Rx128Byte"),
	MIB_DESC(1, STATS_RL256PC, "Rx256Byte"),
	MIB_DESC(1, STATS_RL512PC, "Rx512Byte"),
	MIB_DESC(1, STATS_RL1024PC, "Rx1024Byte"),
	MIB_DESC(2, STATS_ROC, "RxByte"),
	MIB_DESC(1, STATS_RDPC_CTRL, "RxCtrlDrop"),
	MIB_DESC(1, STATS_RDPC_ING, "RxIngDrop"),
	MIB_DESC(1, STATS_RDPC_ARL, "RxARLDrop")
};

enum {
	/* Global attributes. */
	MT753X_ATTR_ENABLE_VLAN,
};

struct mt753x_mapping {
    char *name;
    u16 pvids[MT753X_NUM_PORTS];
    u8 members[MT753X_NUM_VLANS];
    u8 etags[MT753X_NUM_VLANS];
    u16 vids[MT753X_NUM_VLANS];
} mt753x_defaults[] = {
    {
        .name = "lllll", /* default if none */
        .pvids = { 1, 1, 1, 1, 1, 1, 1 },
        .members = { 0, 0x7f },
        .etags = { 0, 0x40 },
        .vids = { 0, 1 },
    }, {
        .name = "llllw",
        .pvids = { 1, 1, 1, 1, 2, 1, 1 },
        .members = { 0, 0x6f, 0x50 },
        .etags = { 0, 0x40, 0x40 },
        .vids = { 0, 1, 2 },
    }, {
        .name = "wllll",
        .pvids = { 2, 1, 1, 1, 1, 1, 1 },
        .members = { 0, 0x7e, 0x41 },
        .etags = { 0, 0x40, 0x40 },
        .vids = { 0, 1, 2 },
    }, {
        .name = "lwlll",
        .pvids = { 1, 2, 1, 1, 1, 1, 1 },
        .members = { 0, 0x7d, 0x42 },
        .etags = { 0, 0x40, 0x40 },
        .vids = { 0, 1, 2 },
    },
};

#ifdef CONFIG_PROC_FS
#define MT753X_ARL_PROC_ENTRY "arl"
struct proc_dir_entry *proc_switch_dir;
struct proc_dir_entry *proc_arl_table;
#endif /* CONFIG_PROC_FS */

static inline void mt753x_get_arl_age_info(struct gsw_mt753x *gsw, u32 *age_cnt, u32 *age_unit, u32 *age_time);
static void mt753x_flush_arl_table(struct gsw_mt753x *gsw);

struct mt753x_mapping *mt753x_find_mapping(struct device_node *np)
{
    const char *map;
    int i;

    if (of_property_read_string(np, "mediatek,portmap", &map))
        map = "lllll";

    for (i = 0; i < ARRAY_SIZE(mt753x_defaults); i++)
        if (!strcmp(map, mt753x_defaults[i].name))
            return &mt753x_defaults[i];

    return NULL;
}

static void mt753x_apply_mapping(struct gsw_mt753x *gsw,
                struct mt753x_mapping *map)
{
    int i = 0;

    for (i = 0; i < MT753X_NUM_PORTS; i++)
        gsw->port_entries[i].pvid = map->pvids[i];

    for (i = 0; i < MT753X_NUM_VLANS; i++) {
        gsw->vlan_entries[i].member = map->members[i];
        gsw->vlan_entries[i].etags = map->etags[i];
        gsw->vlan_entries[i].vid = map->vids[i];
    }
}

static int mt753x_get_vlan_enable(struct switch_dev *dev,
                const struct switch_attr *attr,
                struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    val->value.i = gsw->global_vlan_enable;
    return 0;
}

static int mt753x_set_vlan_enable(struct switch_dev *dev,
                const struct switch_attr *attr,
                struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    gsw->global_vlan_enable = val->value.i != 0;
    return 0;
}

#define MII_REG_ENTRY(_x) { .name = #_x, .regnum = (_x), .val = 0 }

static int mt753x_sw_get_port_phyreg(struct switch_dev *dev,
                const struct switch_attr *attr,
                struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int port = val->port_vlan;
    static char buf[4096] = { 0 };
    int len = 0;
    struct mt753x_reg_entry *reg;

    /* MII Registers to read */
    struct mt753x_reg_entry mii_regs[] = {
        MII_REG_ENTRY(MII_BMCR),
        MII_REG_ENTRY(MII_BMSR),
        MII_REG_ENTRY(MII_PHYSID1),
        MII_REG_ENTRY(MII_PHYSID2),
        MII_REG_ENTRY(MII_ADVERTISE),
        MII_REG_ENTRY(MII_LPA),
        MII_REG_ENTRY(MII_CTRL1000),
        MII_REG_ENTRY(MII_STAT1000),
        MII_REG_ENTRY(MII_ESTATUS),
    };

    if (port < 0 || port >= MT753X_NUM_PHYS)
        return -EINVAL;

    for (reg = mii_regs; reg < (mii_regs + ARRAY_SIZE(mii_regs)); reg++) {
        reg->val = gsw->mii_read(gsw, port, reg->regnum);
        len += snprintf(buf + len, sizeof(buf) - len, "\n%-15s[%02d] 0x%04x",
                        reg->name, reg->regnum, reg->val);
        if (len >= sizeof(buf)) {
            pr_err("%s: str_len(%d) >= buffer(%d)\n", __FUNCTION__, len, (int)sizeof(buf));
            return -EINVAL;
        }
    }

    val->value.s = buf;
    val->len = len;

    return 0;
}

static void mt753x_vlan_ctrl(struct gsw_mt753x *gsw, u32 cmd, u32 val)
{
    int i;

    mt753x_reg_write(gsw, VTCR,
        VTCR_BUSY | ((cmd << VTCR_FUNC_S) & VTCR_FUNC_M) |
        (val & VTCR_VID_M));

    for (i = 0; i < 300; i++) {
        u32 val = mt753x_reg_read(gsw, VTCR);

        if ((val & VTCR_BUSY) == 0)
            break;

        usleep_range(1000, 1100);
    }

    if (i == 300)
        dev_info(gsw->dev, "vtcr timeout\n");
}

static int mt753x_get_port_pvid(struct switch_dev *dev, int port, int *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (port >= MT753X_NUM_PORTS)
        return -EINVAL;

    *val = mt753x_reg_read(gsw, PPBV1(port));
    *val &= GRP_PORT_VID_M;

    return 0;
}

static int mt753x_set_port_pvid(struct switch_dev *dev, int port, int pvid)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (port >= MT753X_NUM_PORTS)
        return -EINVAL;

    if (pvid < MT753X_MIN_VID || pvid > MT753X_MAX_VID)
        return -EINVAL;

    gsw->port_entries[port].pvid = pvid;

    return 0;
}

static int mt753x_get_vlan_ports(struct switch_dev *dev, struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    u32 member;
    u32 etags;
    int i;

    val->len = 0;

    if (val->port_vlan < 0 || val->port_vlan >= MT753X_NUM_VLANS)
        return -EINVAL;

    mt753x_vlan_ctrl(gsw, VTCR_READ_VLAN_ENTRY, val->port_vlan);

    member = mt753x_reg_read(gsw, VAWD1);
    member &= PORT_MEM_M;
    member >>= PORT_MEM_S;

    etags = mt753x_reg_read(gsw, VAWD2);

    for (i = 0; i < MT753X_NUM_PORTS; i++) {
        struct switch_port *p;
        int etag;

        if (!(member & BIT(i)))
            continue;

        p = &val->value.ports[val->len++];
        p->id = i;

        etag = (etags >> PORT_ETAG_S(i)) & PORT_ETAG_M;

        if (etag == ETAG_CTRL_TAG)
            p->flags |= BIT(SWITCH_PORT_FLAG_TAGGED);
        else if (etag != ETAG_CTRL_UNTAG)
            dev_info(gsw->dev,
                "vlan egress tag control neither untag nor tag.\n");
    }

    return 0;
}

static int mt753x_set_vlan_ports(struct switch_dev *dev, struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    u8 member = 0;
    u8 etags = 0;
    int i;

    if (val->port_vlan < 0 || val->port_vlan >= MT753X_NUM_VLANS ||
        val->len > MT753X_NUM_PORTS)
        return -EINVAL;

    for (i = 0; i < val->len; i++) {
        struct switch_port *p = &val->value.ports[i];

        if (p->id >= MT753X_NUM_PORTS)
            return -EINVAL;

        member |= BIT(p->id);

        if (p->flags & BIT(SWITCH_PORT_FLAG_TAGGED))
            etags |= BIT(p->id);
    }

    gsw->vlan_entries[val->port_vlan].member = member;
    gsw->vlan_entries[val->port_vlan].etags = etags;

    return 0;
}

static int mt753x_set_vid(struct switch_dev *dev,
                const struct switch_attr *attr,
                struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int vlan;
    u16 vid;

    vlan = val->port_vlan;
    vid = (u16)val->value.i;

    if (vlan < 0 || vlan >= MT753X_NUM_VLANS)
        return -EINVAL;

    if (vid < MT753X_MIN_VID || vid > MT753X_MAX_VID)
        return -EINVAL;

    gsw->vlan_entries[vlan].vid = vid;
    return 0;
}

static int mt753x_get_vid(struct switch_dev *dev,
                const struct switch_attr *attr,
                struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    val->value.i = gsw->vlan_entries[val->port_vlan].vid;
    return 0;
}

static void mt753x_write_vlan_entry(struct gsw_mt753x *gsw, int vlan, u16 vid,
                u8 ports, u8 etags)
{
    int port;
    u32 val;

    /* vlan port membership */
    if (ports)
        mt753x_reg_write(gsw, VAWD1,
                IVL_MAC | VTAG_EN | VENTRY_VALID |
                ((ports << PORT_MEM_S) & PORT_MEM_M));
    else
        mt753x_reg_write(gsw, VAWD1, 0);

    /* egress mode */
    val = 0;
    for (port = 0; port < MT753X_NUM_PORTS; port++) {
        if (etags & BIT(port))
            val |= ETAG_CTRL_TAG << PORT_ETAG_S(port);
        else
            val |= ETAG_CTRL_UNTAG << PORT_ETAG_S(port);
    }
    mt753x_reg_write(gsw, VAWD2, val);

    /* write to vlan table */
    mt753x_vlan_ctrl(gsw, VTCR_WRITE_VLAN_ENTRY, vid);
}

static void mt753x_sw_apply_arl_age_time(struct gsw_mt753x *gsw)
{
    u32 old_age_cnt, age_cnt = gsw->arl_age_time;
    u32 old_age_unit, age_unit;
    u32 age_time;

    mt753x_get_arl_age_info(gsw, &old_age_cnt, &old_age_unit, &age_time);

    /* fit smallest unit possible in age unit */
    for (age_unit = 1; age_cnt > 0x100;)
        age_cnt = gsw->arl_age_time / ++age_unit;

    /* driver adds +1 to age_cnt and age_unit when computing age time */
    age_cnt -= 1;
    age_unit -= 1;

    if (old_age_cnt != age_cnt || old_age_unit != age_unit) {
        mt753x_reg_write(gsw, REG_ESW_ARL_AAC,
                   (((age_cnt) & REG_ESW_ARL_AAC_AGE_CNT_MASK) << REG_ESW_ARL_AAC_AGE_CNT_S) |
                   (((age_unit) & REG_ESW_ARL_AAC_AGE_UNIT_MASK) << REG_ESW_ARL_AAC_AGE_UNIT_S));

        /* changing age time causes duplicate arl entries to appear so flush table */
        mt753x_flush_arl_table(gsw);
    }
}

static void mt753x_sw_apply_port_mirror(struct gsw_mt753x *gsw)
{
    u32 regval, i;

    /* Reset mirror registers */
    for (i = 0; i < MT753X_NUM_PHYS; i++) {
        regval = mt753x_reg_read(gsw, REG_ESW_PORT_PCR(i));
        regval &= ~(REG_ESW_PORT_PCR_TX_MIR | REG_ESW_PORT_PCR_RX_MIR);
        mt753x_reg_write(gsw, REG_ESW_PORT_PCR(i), regval);
    }

    regval = mt753x_reg_read(gsw, REG_ESW_WT_MAC_MFC);
    regval &= ~(REG_ESW_WT_MAC_MFC_MIR_EN);
    regval &= ~(REG_ESW_WT_MAC_MFC_MIR_PORT_MASK);
    mt753x_reg_write(gsw, REG_ESW_WT_MAC_MFC, regval);

    if ((!gsw->mirror_tx && !gsw->mirror_rx) ||
        (gsw->mirror_src_port == gsw->mirror_dst_port))
        return;

    regval = mt753x_reg_read(gsw, REG_ESW_PORT_PCR(gsw->mirror_src_port));

    if (gsw->mirror_tx)
        regval |= REG_ESW_PORT_PCR_TX_MIR;

    if (gsw->mirror_rx)
        regval |= REG_ESW_PORT_PCR_RX_MIR;

    mt753x_reg_write(gsw, REG_ESW_PORT_PCR(gsw->mirror_src_port), regval);

    regval = mt753x_reg_read(gsw, REG_ESW_WT_MAC_MFC);
    regval |= REG_ESW_WT_MAC_MFC_MIR_EN;
    regval |= (gsw->mirror_dst_port & REG_ESW_WT_MAC_MFC_MIR_PORT_MASK);
    mt753x_reg_write(gsw, REG_ESW_WT_MAC_MFC, regval);
}

static void mt753x_sw_apply_ingress_jumbo_frame(struct gsw_mt753x *gsw)
{
    u32 regval, rx_pkt_len_type;

    if (gsw->max_ingress_jumbo_frame_kbytes < MT753X_MAC_MIN_RX_JUMBO_FRAME_KBYTES ||
        gsw->max_ingress_jumbo_frame_kbytes > MT753X_MAC_MAX_RX_JUMBO_FRAME_KBYTES)
        return;

    regval = mt753x_reg_read(gsw, REG_ESW_GMACCR);

    regval &= ~(REG_ESW_GMACCR_MAX_RX_JUMBO_MASK |
                REG_ESW_GMACCR_MAX_RX_PKT_LEN_MASK);

    regval |= REG_ESW_GMACCR_MAX_RX_JUMBO(gsw->max_ingress_jumbo_frame_kbytes);

    rx_pkt_len_type = (gsw->ingress_jumbo_frame_enable) ?
        (MT753X_MAC_MAX_RX_PKT_LEN_MAX_RX_JUMBO) : (MT753X_MAC_DEFAULT_RX_PKT_LEN_TYPE);
    regval |= REG_ESW_GMACCR_MAX_RX_PKT_LEN(rx_pkt_len_type);

    mt753x_reg_write(gsw, REG_ESW_GMACCR, regval);
}

struct wired_sta_event {
    u8 status;
    u8 mac[ETH_ALEN];
    u8 port;
};

static void mt753x_wired_send_event(struct gsw_mt753x *gsw, struct mt753x_arl_entry *entry, bool add) {
    struct wired_sta_event sta;
    union iwreq_data wreq;
    struct net_device *netdev = NULL;

    netdev = dev_get_by_name(&init_net, MTK_DEV_NAME);
    if (!netdev)
        return;

    memset(&wreq, 0, sizeof(wreq));
    memcpy(sta.mac, entry->mac, 6);
    sta.port = entry->port;
    sta.status = add ? 1 : 0;
    wreq.data.length = sizeof(struct wired_sta_event);

    wireless_send_event(netdev, IWEVCUSTOM, &wreq, (char *) &sta);
    dev_put(netdev);
}

void mt753x_flush_port_arl_table(struct gsw_mt753x *gsw, int port)
{
    u32 reg = REG_ESW_ARL_ATC_BUSY | (MT753X_ARL_CLEAN << REG_ESW_ARL_ATC_CMD_S);
    struct mt753x_arl_entry *entry;
    struct hlist_node *tmp;
    int i;

    mutex_lock(&gsw->arl_mutex);
    if (port >= 0) {
        mt753x_reg_write(gsw, REG_ESW_ARL_ATA1, ((1 << port) << REG_ESW_ARL_ATA1_PORT_S) & REG_ESW_ARL_ATA1_PORT_MASK);
        reg |= (MT753X_ARL_ALL_PORT << REG_ESW_ARL_ATC_MAT_S);
    } else {
        reg |= (MT753X_ARL_ALL << REG_ESW_ARL_ATC_MAT_S);
    }

    mt753x_reg_write(gsw, REG_ESW_ARL_ATC, reg);
    hash_for_each_safe(gsw->arl_entries, i, tmp, entry, list) {
        if (port < 0 || entry->port == port) {
            if (gsw->port_entries[entry->port].security == PORT_AUTH_AUTO ||
                    gsw->port_entries[entry->port].security == PORT_AUTH_MAC_BASED)
                mt753x_wired_send_event(gsw, entry, false);
            hash_del(&entry->list);
            kfree(entry);
        }
    }
    mutex_unlock(&gsw->arl_mutex);

    /* deauth port if needed */
    if (port >= 0 && (gsw->port_entries[port].security == PORT_AUTH_AUTO ||
                      gsw->port_entries[port].security == PORT_AUTH_MAC_BASED))
        mt753x_reg_write(gsw, REG_ESW_PORT_PSC(port),
                   0xfff00 | REG_ESW_PORT_PSC_TX_LOCK | REG_ESW_PORT_PSC_RX_LOCK);
}

static void mt753x_flush_arl_table(struct gsw_mt753x *gsw)
{
    mt753x_flush_port_arl_table(gsw, -1);
}

enum {
    mt753x_sw_reg_opt_reg,
    mt753x_sw_reg_opt_ops,
    mt753x_sw_reg_opt_val,
    mt753x_sw_reg_opt_err
};

static const match_table_t tokens = {
    {mt753x_sw_reg_opt_reg, "reg=%u"},
    {mt753x_sw_reg_opt_ops, "ops=%s"},
    {mt753x_sw_reg_opt_val, "val=%s"},
    {mt753x_sw_reg_opt_err, NULL},
};

static int mt753x_sw_set_reg_ops(struct switch_dev *dev, const struct switch_attr *attr,
                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    char buf[256] = { 0 }, ops[8] = { 0 }, *ptr = NULL, *p = NULL;
    u32 regnum = 0, regval = 0;

    strlcpy(buf, val->value.s, sizeof(buf));
    ptr = buf;

    while ((p = strsep(&ptr, " "))) {
        substring_t args[MAX_OPT_ARGS];
        int token;
        int option;

        if (!*p)
            continue;

        token = match_token(p, tokens, args);
        switch (token) {
            case mt753x_sw_reg_opt_reg:
                if (match_hex(&args[0], &option))
                    goto err_out;
                regnum = option;
                break;

            case mt753x_sw_reg_opt_ops:
                match_strlcpy(ops, &args[0], sizeof(ops));
                break;

            case mt753x_sw_reg_opt_val:
                if (match_hex(&args[0], &option))
                    goto err_out;
                regval = option;
                break;

            default:
                goto err_out;
        }
    }

    if (!strcmp(ops, "w")) {
        mt753x_reg_write(gsw, regnum, regval);
    } else if (!strcmp(ops, "r")) {
        /* TODO: Improve by using link list for reg_entry to read multiple registers */
        gsw->reg_entry.regnum = regnum;
    } else {
        pr_err("%s ops=%s\n", strlen(ops) ? "Unsupported" : "Must set", ops);
        goto err_out;
    }

    return 0;

err_out:
    pr_err("Invalid command [%s]\n", val->value.s);
    return -EINVAL;
}

static int
mt753x_sw_get_reg_ops(struct switch_dev *dev, const struct switch_attr *attr,
                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    static char buf[32] = { 0 };
    size_t len = 0;

    /* No specific what registers want to read. Call mt7530_sw_set_reg_ops() to set regnum */
    if (gsw->reg_entry.regnum == MT753X_UINT32_MAX)
        return -EINVAL;

    gsw->reg_entry.val = mt753x_reg_read(gsw, gsw->reg_entry.regnum);
    len = snprintf(buf, sizeof(buf), "\n[0x%08x] 0x%08x", gsw->reg_entry.regnum, gsw->reg_entry.val);

    val->value.s = buf;
    val->len = len;

    return 0;
}

static int
mt753x_sw_get_mirror_max_sessions(struct switch_dev *dev,
                                  const struct switch_attr *attr,
                                  struct switch_val *val)
{
    val->value.i = MT753X_MAX_MIRROR_SESSIONS;

    return 0;
}

static int
mt753x_sw_set_mirror_rx_enable(struct switch_dev *dev,
                               const struct switch_attr *attr,
                               struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    gsw->mirror_rx = !!val->value.i;

    return 0;
}

static int
mt753x_sw_get_mirror_rx_enable(struct switch_dev *dev,
                               const struct switch_attr *attr,
                               struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    val->value.i = gsw->mirror_rx;

    return 0;
}

static int
mt753x_sw_set_mirror_tx_enable(struct switch_dev *dev,
                               const struct switch_attr *attr,
                               struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    gsw->mirror_tx = !!val->value.i;

    return 0;
}

static int
mt753x_sw_get_mirror_tx_enable(struct switch_dev *dev,
                               const struct switch_attr *attr,
                               struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    val->value.i = gsw->mirror_tx;

    return 0;
}

static int
mt753x_sw_set_mirror_destination_port(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    gsw->mirror_dst_port = val->value.i;

    return 0;
}

static int
mt753x_sw_get_mirror_destination_port(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    val->value.i = gsw->mirror_dst_port;

    return 0;
}

static int
mt753x_sw_set_mirror_source_port(struct switch_dev *dev,
                                 const struct switch_attr *attr,
                                 struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    gsw->mirror_src_port = val->value.i;

    return 0;
}

static int
mt753x_sw_get_mirror_source_port(struct switch_dev *dev,
                                 const struct switch_attr *attr,
                                 struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    val->value.i = gsw->mirror_src_port;

    return 0;
}

static int
mt753x_sw_set_ingress_jumbo_frame_enable(struct switch_dev *dev,
                                         const struct switch_attr *attr,
                                         struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    gsw->ingress_jumbo_frame_enable = !!val->value.i;

    return 0;
}

static int
mt753x_sw_get_ingress_jumbo_frame_enable(struct switch_dev *dev,
                                         const struct switch_attr *attr,
                                         struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    val->value.i = gsw->ingress_jumbo_frame_enable;

    return 0;
}

static int
mt753x_sw_set_max_ingress_jumbo_frame(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (gsw->max_ingress_jumbo_frame_kbytes < MT753X_MAC_MIN_RX_JUMBO_FRAME_KBYTES ||
        gsw->max_ingress_jumbo_frame_kbytes > MT753X_MAC_MAX_RX_JUMBO_FRAME_KBYTES)
        return -EINVAL;

    gsw->max_ingress_jumbo_frame_kbytes = val->value.i;

    return 0;
}

static int
mt753x_sw_get_max_ingress_jumbo_frame(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    val->value.i = gsw->max_ingress_jumbo_frame_kbytes;

    return 0;
}

static void
mt753x_init_ports(struct gsw_mt753x *gsw)
{
    int i;

    for (i = 0; i < MT753X_NUM_PORTS; i++) {
        gsw->port_entries[i].disable = !!(gsw->port_disable & (1 << i));
        pr_info("port[%d] disable %d\n", i, gsw->port_entries[i].disable);
        gsw->port_entries[i].ingress_mode_override = false;
        gsw->port_entries[i].ingress_mode = INGRESS_SECURE;
        gsw->port_entries[i].egress_mode_override = false;
        gsw->port_entries[i].egress_mode = EGRESS_MODE_UNMOD;
        gsw->port_entries[i].security = PORT_AUTH_FORCE_AUTH;
        gsw->port_entries[i].protected = false;
        memset(&gsw->port_entries[i].link, 0, sizeof(struct switch_port_link));
    }
}

static int mt753x_reset_switch(struct switch_dev *dev)
{
	struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
	int i;

	memset(gsw->port_entries, 0, sizeof(gsw->port_entries));
	memset(gsw->vlan_entries, 0, sizeof(gsw->vlan_entries));

	/* set default vid of each vlan to the same number of vlan, so the vid
	 * won't need be set explicitly.
	 */
	for (i = 0; i < MT753X_NUM_VLANS; i++)
		gsw->vlan_entries[i].vid = i;

        /* reset port defaults */
    mt753x_init_ports(gsw);
    mt753x_flush_arl_table(gsw);

    gsw->protected_ports = 0x0;

    gsw->mirror_rx = gsw->mirror_tx = false;
    gsw->mirror_src_port = gsw->mirror_dst_port = 0;

    gsw->ingress_jumbo_frame_enable = false;
    gsw->max_ingress_jumbo_frame_kbytes = MT753X_MAC_DEFAULT_RX_JUMBO_FRAME_KBYTES;

    return 0;
}

static int mt753x_apply_config(struct switch_dev *dev)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int i, j;
    u8 tag_ports;
    u8 untag_ports;
    u32 regval;

    if (!gsw->global_vlan_enable) {
        for (i = 0; i < MT753X_NUM_PORTS; i++)
            mt753x_reg_write(gsw, REG_ESW_PORT_PCR(i), 0x00400000); /* any port can only egress to CPU port */

        mt753x_reg_write(gsw, REG_ESW_PORT_PCR(gsw->cpu_port), 0x00ff0000); /* CPU port can egress to any ports */

        for (i = 0; i < MT753X_NUM_PORTS; i++)
            mt753x_reg_write(gsw, REG_ESW_PORT_PVC(i), 0x810000c0);

        return 0;
	}

    /* Configure PCR(port control register) */
    for (i = 0; i < MT753X_NUM_PORTS; i++) {
        u32 pcr = 0x0;
        u8 egress_ports = 0xff;

        pcr = (ETAG_CTRL_TAG & REG_ESW_PORT_PCR_EG_TAG_MASK) << REG_ESW_PORT_PCR_EG_TAG_S;

        /* only forward frame to self and unprotected ports */
        if (gsw->port_entries[i].protected)
            egress_ports = ~gsw->protected_ports | BIT(i);

        pcr |= REG_ESW_PORT_PCR_BIT_PORT_MATRIX(egress_ports);

        pcr |= (gsw->port_entries[i].ingress_mode_override) ?
            gsw->port_entries[i].ingress_mode : INGRESS_SECURE;

        mt753x_reg_write(gsw, REG_ESW_PORT_PCR(i), pcr);

        pr_debug("write PCR[0x%02x] to 0x%04x\n", REG_ESW_PORT_PCR(i), pcr);
    }

	/* check if a port is used in tag/untag vlan egress mode */
	tag_ports = 0;
	untag_ports = 0;

	for (i = 0; i < MT753X_NUM_VLANS; i++) {
		u8 member = gsw->vlan_entries[i].member;
		u8 etags = gsw->vlan_entries[i].etags;

		if (!member)
			continue;

		for (j = 0; j < MT753X_NUM_PORTS; j++) {
			if (!(member & BIT(j)))
				continue;

			if (etags & BIT(j))
				tag_ports |= 1u << j;
			else
				untag_ports |= 1u << j;
		}
	}

	/* set all untag-only ports as transparent and the rest as user port */
    for (i = 0; i < MT753X_NUM_PORTS; i++) {
        u32 pvc_mode = 0x81000000;
        u32 pvc_egress_mode = EGRESS_SYS_DEFAULT;
        u32 psc = 0xfff00; /* default: PORT_AUTH_FORCE_AUTH */

		if (untag_ports & BIT(i) && !(tag_ports & BIT(i)))
            pvc_mode = 0x810000c0;

        if (gsw->port_entries[i].egress_mode_override) {
            switch (gsw->port_entries[i].egress_mode) {
                case EGRESS_MODE_UNTAG:
                    pvc_egress_mode = EGRESS_UNTAG;
                    break;
                case EGRESS_MODE_TAG:
                    pvc_egress_mode = EGRESS_TAG;
                    break;
                case EGRESS_MODE_UNTOUCH:
                    pvc_egress_mode = EGRESS_CONSISTENT;
                    break;
            }
        }

        pvc_egress_mode <<= REG_ESW_PORT_PVC_EG_TAG_S;
        mt753x_reg_write(gsw, REG_ESW_PORT_PVC(i), pvc_mode | pvc_egress_mode);

        switch(gsw->port_entries[i].security) {
            case PORT_AUTH_AUTO:
            case PORT_AUTH_FORCE_UNAUTH:
            case PORT_AUTH_MAC_BASED:
                psc |= REG_ESW_PORT_PSC_TX_LOCK | REG_ESW_PORT_PSC_RX_LOCK;
                break;
        }

        mt753x_reg_write(gsw, REG_ESW_PORT_PSC(i), psc);
	}

	/* first clear the swtich vlan table */
	for (i = 0; i < MT753X_NUM_VLANS; i++)
		mt753x_write_vlan_entry(gsw, i, i, 0, 0);

	/* now program only vlans with members to avoid
	 * clobbering remapped entries in later iterations
	 */
	for (i = 0; i < MT753X_NUM_VLANS; i++) {
		u16 vid = gsw->vlan_entries[i].vid;
		u8 member = gsw->vlan_entries[i].member;
		u8 etags = gsw->vlan_entries[i].etags;

		if (member)
			mt753x_write_vlan_entry(gsw, i, vid, member, etags);
	}

	/* Port Default PVID */
	for (i = 0; i < MT753X_NUM_PORTS; i++) {
		int vlan = gsw->port_entries[i].pvid;
		u16 pvid = 0;
		u32 val;

		if (vlan < MT753X_NUM_VLANS && gsw->vlan_entries[vlan].member)
			pvid = gsw->vlan_entries[vlan].vid;

		val = mt753x_reg_read(gsw, PPBV1(i));
		val &= ~GRP_PORT_VID_M;
		val |= pvid;
		mt753x_reg_write(gsw, PPBV1(i), val);
	}

    /* Port mirror */
    mt753x_sw_apply_port_mirror(gsw);

    /* jumbo frame */
    mt753x_sw_apply_ingress_jumbo_frame(gsw);

    /* Set link state per PHY */
    for (i = 0; i < MT753X_NUM_PHYS; i++) {
        /* set link speed */
        /*
         * FIXME:
         * When forced at speed 1G, switch_generic_set_link() unsets
         * BMCR_ANENABLE. According to 802.3ab, the auto-negotiation
         * is required at 1G speed. To adjust advertisement ability
         * register 9h(MII_CTRL1000) to force 1G FDX/HDX
         */
        switch_generic_set_link(dev, i, &gsw->port_entries[i].link);

        /* set link up/down state */
        regval = gsw->mii_read(gsw, i, MII_BMCR);
        if(gsw->port_entries[i].disable) {
            // power down PHY
            regval |= BMCR_PDOWN;
        } else {
            // power up PHY
            regval &= ~BMCR_PDOWN;
        }
        gsw->mii_write(gsw, i, MII_BMCR, regval);
    }

    mt753x_sw_apply_arl_age_time(gsw);

	return 0;
}

static int mt753x_get_port_link(struct switch_dev *dev, int port,
                struct switch_port_link *link)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    u32 speed, pmsr;

    if (port < 0 || port >= MT753X_NUM_PORTS)
        return -EINVAL;

    pmsr = mt753x_reg_read(gsw, PMSR(port));

    link->link = pmsr & MAC_LNK_STS;
    link->duplex = pmsr & MAC_DPX_STS;
    speed = (pmsr & MAC_SPD_STS_M) >> MAC_SPD_STS_S;

    switch (speed) {
    case MAC_SPD_10:
        link->speed = SWITCH_PORT_SPEED_10;
        break;
    case MAC_SPD_100:
        link->speed = SWITCH_PORT_SPEED_100;
        break;
    case MAC_SPD_1000:
        link->speed = SWITCH_PORT_SPEED_1000;
        break;
    case MAC_SPD_2500:
    default:
        /* TODO: swconfig has no support for 2500 now */
        link->speed = SWITCH_PORT_SPEED_UNKNOWN;
        break;
    }

    return 0;
}

static int mt753x_set_port_link(struct switch_dev *dev, int port,
                struct switch_port_link *link)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (port < 0 || port >= MT753X_NUM_PHYS) {
        printk(KERN_ERR "%s: Invalid port [%d]\n" , __func__, port);
        return -EINVAL;
    }

    if(gsw->port_entries[port].disable) {
        printk(KERN_WARNING "%s: port [%d] is disabled\n" , __func__, port);
        return -EINVAL;
    }

    if (link->speed == SWITCH_PORT_SPEED_1000 && !link->duplex) {
        printk(KERN_ERR "%s: Don't support half duplex at 1G speed\n" , __func__);
        return -EINVAL;
    }

    gsw->port_entries[port].link.speed = link->speed;
    gsw->port_entries[port].link.duplex = link->duplex;
    gsw->port_entries[port].link.aneg = link->aneg;

    return 0;
}

static int mt753x_phy_read16(struct switch_dev *dev, int addr, u8 reg, u16 *value)
{
	struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

	*value = gsw->mii_read(gsw, addr, reg);

	return 0;
}

static int mt753x_phy_write16(struct switch_dev *dev, int addr, u8 reg, u16 value)
{
	struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

	gsw->mii_write(gsw, addr, reg, value);

	return 0;
}

static u64 get_mib_counter(struct gsw_mt753x *gsw, int i, int port)
{
    unsigned int offset;
    u64 lo, hi, hi2;

    offset = mt753x_mibs[i].offset;

    if (mt753x_mibs[i].size == 1)
        return mt753x_reg_read(gsw, MIB_COUNTER_REG(port, offset));

    do {
        hi = mt753x_reg_read(gsw, MIB_COUNTER_REG(port, offset + 4));
        lo = mt753x_reg_read(gsw, MIB_COUNTER_REG(port, offset));
        hi2 = mt753x_reg_read(gsw, MIB_COUNTER_REG(port, offset + 4));
    } while (hi2 != hi);

    return (hi << 32) | lo;
}

static int mt753x_sw_get_port_mib(struct switch_dev *dev,
                    const struct switch_attr *attr,
                    struct switch_val *val)
{
    static char buf[4096];
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int i, len = 0;

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    len += snprintf(buf + len, sizeof(buf) - len,
            "Port %d MIB counters\n", val->port_vlan);

    for (i = 0; i < ARRAY_SIZE(mt753x_mibs); ++i) {
        u64 counter;

        len += snprintf(buf + len, sizeof(buf) - len,
                "%-11s: ", mt753x_mibs[i].name);
        counter = get_mib_counter(gsw, i, val->port_vlan);
        len += snprintf(buf + len, sizeof(buf) - len, "%llu\n",
                counter);
    }

    val->value.s = buf;
    val->len = len;
    return 0;
}

static int mt753x_sw_get_port_disable(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    // we only support disabling ports that are connected to PHYs (for now)
    if (val->port_vlan >= MT753X_NUM_PHYS)
        return -EINVAL;

    val->value.i = gsw->port_entries[val->port_vlan].disable;

    return 0;
}

static int mt753x_sw_set_port_disable(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    // we only support disabling ports that are connected to PHYs (for now)
    if (val->port_vlan >= MT753X_NUM_PHYS)
        return -EINVAL;

    gsw->port_entries[val->port_vlan].disable = val->value.i ? true : false;

    return 0;
}

static int mt753x_sw_get_port_ingress_mode_override(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    val->value.i = gsw->port_entries[val->port_vlan].ingress_mode_override;

    return 0;
}

static int mt753x_sw_set_port_ingress_mode_override(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    gsw->port_entries[val->port_vlan].ingress_mode_override = val->value.i ? true : false;

    return 0;
}

static int mt753x_sw_get_port_ingress_mode(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    val->value.i = mt753x_reg_read(gsw, REG_ESW_PORT_PCR(val->port_vlan)) & 0x3;

    return 0;
}

static int mt753x_sw_set_port_ingress_mode(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    gsw->port_entries[val->port_vlan].ingress_mode = val->value.i & 0x3;

    return 0;
}

static int mt753x_sw_get_port_egress_mode_override(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    val->value.i = gsw->port_entries[val->port_vlan].egress_mode_override;

    return 0;
}

static int mt753x_sw_set_port_egress_mode_override(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    gsw->port_entries[val->port_vlan].egress_mode_override = val->value.i ? true : false;

    return 0;
}

static int mt753x_sw_get_port_egress_mode(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    val->value.i = gsw->port_entries[val->port_vlan].egress_mode;

    return 0;
}

static int mt753x_sw_set_port_egress_mode(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    gsw->port_entries[val->port_vlan].egress_mode = val->value.i;

    return 0;
}

static int mt753x_sw_set_port_flush_arl_table(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    mt753x_flush_port_arl_table(gsw, val->port_vlan);

    return 0;
}

static inline void mt753x_get_arl_age_info(struct gsw_mt753x *gsw,
        u32 *age_cnt,
        u32 *age_unit,
        u32 *age_time) {
    u32 aac = mt753x_reg_read(gsw, REG_ESW_ARL_AAC);
    *age_cnt  = (aac >> REG_ESW_ARL_AAC_AGE_CNT_S) & REG_ESW_ARL_AAC_AGE_CNT_MASK;
    *age_unit = (aac >> REG_ESW_ARL_AAC_AGE_UNIT_S) & REG_ESW_ARL_AAC_AGE_UNIT_MASK;
    *age_time = MT753X_CALC_AGE_TIME(*age_cnt, *age_unit);
}

static int mt753x_sw_get_arl_age_time(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    u32 age_cnt;
    u32 age_unit;

    mt753x_get_arl_age_info(gsw, &age_cnt, &age_unit, &val->value.i);

    return 0;
}

static int mt753x_sw_set_arl_age_time(struct switch_dev *dev,
                                      const struct switch_attr *attr,
                                      struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->value.i < 1 || val->value.i > 0x100000)
        return -EINVAL;

    gsw->arl_age_time = val->value.i;

    return 0;
}

static int mt753x_wait_atc_ready(struct gsw_mt753x *gsw) {
    int timeout = 10;

    while (mt753x_reg_read(gsw, REG_ESW_ARL_ATC) & REG_ESW_ARL_ATC_BUSY && --timeout)
        udelay(10);

    if (!timeout) {
        printk("%s: timeout waiting for atc!\n", __func__);
        return -ETIMEDOUT;
    }

    return 0;
}

/*
 * Fetches either the first or next entry from the ARL table
 * Returns 0 on success, greater than 0 indicating more entries are available,
 * or less than 0 if caller should quit reading reading from the table
 */
static int mt753x_get_arl_entry(struct gsw_mt753x *gsw,
                                struct mt753x_arl_entry *a,
                                enum mt753x_arl_atc_cmd cmd) {
    u32 atc;

    if (cmd != MT753X_ARL_GET_FIRST && cmd != MT753X_ARL_GET_NEXT) {
        printk("%s: unknown cmd=%d\n", __func__, cmd);
        return -EINVAL;
    }

    mt753x_reg_write(gsw, REG_ESW_ARL_ATC, REG_ESW_ARL_ATC_BUSY |
               (MT753X_ARL_ALL_VALID << REG_ESW_ARL_ATC_MAT_S) |
               (cmd << REG_ESW_ARL_ATC_CMD_S));

    /* abort */
    if (mt753x_wait_atc_ready(gsw) < 0)
        return -ETIMEDOUT;

    atc = mt753x_reg_read(gsw, REG_ESW_ARL_ATC);

    /* end of table */
    if (atc & REG_ESW_ARL_ATC_SRCH_END)
        return -ENOSPC;

    /* found entry */
    if ((atc & REG_ESW_ARL_ATC_SRCH_HIT)) {
        u32 tsra1 = mt753x_reg_read(gsw, REG_ESW_ARL_TSRA1);
        u32 tsra2 = mt753x_reg_read(gsw, REG_ESW_ARL_TSRA2);
        u32 atrd  = mt753x_reg_read(gsw, REG_ESW_ARL_ATRD);
        u8 port_bitmap = (atrd >> REG_ESW_ARL_ATRD_PORT_MAP_S) & REG_ESW_ARL_ATRD_PORT_MAP_MASK;
        u8 p = 1, port = 0;

        /* get port number */
        while (!(port_bitmap & p) && ++port < MT753X_NUM_PORTS)
            p <<= 1;

        a->port   = port;
        a->mac[0] = (tsra1 >> REG_ESW_ARL_TSRA1_MAC0_S) & REG_ESW_ARL_TSRA_MAC_MASK;
        a->mac[1] = (tsra1 >> REG_ESW_ARL_TSRA1_MAC1_S) & REG_ESW_ARL_TSRA_MAC_MASK;
        a->mac[2] = (tsra1 >> REG_ESW_ARL_TSRA1_MAC2_S) & REG_ESW_ARL_TSRA_MAC_MASK;
        a->mac[3] = (tsra1 >> REG_ESW_ARL_TSRA1_MAC3_S) & REG_ESW_ARL_TSRA_MAC_MASK;
        a->mac[4] = (tsra2 >> REG_ESW_ARL_TSRA2_MAC4_S) & REG_ESW_ARL_TSRA_MAC_MASK;
        a->mac[5] = (tsra2 >> REG_ESW_ARL_TSRA2_MAC5_S) & REG_ESW_ARL_TSRA_MAC_MASK;
        a->vid    = (tsra2 >> REG_ESW_ARL_TSRA2_CVID_S) & REG_ESW_ARL_TSRA2_CVID_MASK;
        a->last_seen = (atrd >> REG_ESW_ARL_ATRD_AGE_CNT_S) & REG_ESW_ARL_ATRD_AGE_CNT_MASK; /* use count temporarily */

        return 0;
    }

    /* get next */
    return 1;
}

static void mt753x_update_arl_table(struct work_struct *work) {
    struct gsw_mt753x *gsw = container_of(work, struct gsw_mt753x, arl_update_work.work);
    struct mt753x_arl_entry a;
    struct mt753x_arl_entry *entry;
    struct hlist_node *tmp;
    int i;
    u32 age_cnt;
    u32 age_unit;
    u32 age_time;
    bool found = false;
    int ret;

    mt753x_get_arl_age_info(gsw, &age_cnt, &age_unit, &age_time);
    mutex_lock(&gsw->arl_mutex);

    /* get first entry */
    ret = mt753x_get_arl_entry(gsw, &a, MT753X_ARL_GET_FIRST);

    while (ret >= 0) {
        if (!ret) {
            found = false;
            hash_for_each_possible(gsw->arl_entries, entry, list, MT753X_ARL_LIST_HASH(&a)) {
                /* update */
                if (entry && !memcmp(entry->mac, a.mac, 6) && entry->vid == a.vid) {
                    entry->port = a.port;
                    entry->last_seen = age_time - MT753X_CALC_AGE_TIME(a.last_seen, age_unit);
                    entry->is_update = true;
                    found = true;
                }
            }

            if (!found) {
                /* add */
                entry = kzalloc(sizeof(struct mt753x_arl_entry), GFP_KERNEL);
                INIT_HLIST_NODE(&entry->list);
                entry->port = a.port;
                entry->vid = a.vid;
                memcpy(entry->mac, a.mac, sizeof(a.mac));
                entry->last_seen = age_time - MT753X_CALC_AGE_TIME(a.last_seen, age_unit);
                entry->first_add = jiffies;
                entry->is_update = true;
                hash_add(gsw->arl_entries, &entry->list, MT753X_ARL_LIST_HASH(&a));
                gsw->arl_count++;
                if (gsw->port_entries[a.port].security == PORT_AUTH_AUTO ||
                        gsw->port_entries[a.port].security == PORT_AUTH_MAC_BASED)
                    mt753x_wired_send_event(gsw, entry, true);
            }
        }
        /* get next entry */
        ret = mt753x_get_arl_entry(gsw, &a, MT753X_ARL_GET_NEXT);
    }

    hash_for_each_safe(gsw->arl_entries, i, tmp, entry, list) {
        /* If entry doesn't be updated by HW ARL table, increased last seen by interval of the work queue */
        if (!entry->is_update)
            entry->last_seen += (MT753X_ARL_READ_WORK_DELAY / 1000);

        entry->is_update = false;

        /* delete */
        if (entry->last_seen >= age_time) {
            if (gsw->port_entries[entry->port].security == PORT_AUTH_AUTO ||
                    gsw->port_entries[entry->port].security == PORT_AUTH_MAC_BASED)
                mt753x_wired_send_event(gsw, entry, false);
            hash_del(&entry->list);
            kfree(entry);
            gsw->arl_count--;
        }
    }
    mutex_unlock(&gsw->arl_mutex);

    schedule_delayed_work(&gsw->arl_update_work,
                          msecs_to_jiffies(MT753X_ARL_READ_WORK_DELAY));
}

static int mt753x_sw_get_arl_table(struct switch_dev *dev,
                                   const struct switch_attr *attr,
                                   struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    struct mt753x_arl_entry *entry;
    struct hlist_node *tmp;
    static char buf[4096];
    int i, len = 0, printed = 0;

    mutex_lock(&gsw->arl_mutex);
    len += snprintf(buf + len, sizeof(buf) - len, "address resolution table\n");
    len += snprintf(buf + len, sizeof(buf) - len, "Port\tMAC\t\t\tLast Seen\n");

    hash_for_each_safe(gsw->arl_entries, i, tmp, entry, list) {
        if (printed >= MT753X_MAX_MAC_ENTRIES_PRINTED) {
            len += snprintf(buf + len, sizeof(buf) - len, "WARNING: list truncated\n");
            break;
        }

        len += snprintf(buf + len, sizeof(buf) - len,
                        "%d\t%02x:%02x:%02x:%02x:%02x:%02x\t%d\n",
                        entry->port,
                        entry->mac[0], entry->mac[1], entry->mac[2], entry->mac[3], entry->mac[4], entry->mac[5],
                        entry->last_seen
                       );
        printed++;
    }
    mutex_unlock(&gsw->arl_mutex);

    val->value.s = buf;
    val->len = len;
    return 0;
}

#ifdef CONFIG_PROC_FS
struct arl_seq_iter {
    struct gsw_mt753x *gsw;
    struct mt753x_arl_entry *entry;
    int bkt;
};

/**
 * hlist_for_each_entry_safe_continue - iterate over a hlist continuing after current point,
 * 					safe against removal of list entry
 * @pos:	the type * to use as a loop cursor.
 * @n:	another &struct hlist_node to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the hlist_node within the struct.
 */
#define hlist_for_each_entry_safe_continue(pos, n, head, member)			 \
    for (pos = pos == NULL ? hlist_entry_safe((head)->first, typeof(*pos), member) : \
        hlist_entry_safe((pos)->member.next, typeof(*pos), member);		 \
        pos && ({ n = pos->member.next; 1; });					 \
        pos = hlist_entry_safe(n, typeof(*pos), member))

/**
 * hash_for_each_safe_continue - iterate over a hashtable continuing from bkt, safe against removal of
 * 			hash entry
 * @name: hashtable to iterate
 * @bkt: integer to use as bucket loop cursor
 * @tmp: a &struct used for temporary storage
 * @obj: the type * of current cursor (starts at hlist head if NULL)
 * @member: the name of the hlist_node within the struct
 */
#define hash_for_each_safe_continue(name, bkt, tmp, obj, member) \
    for (; (bkt) < HASH_SIZE(name); (bkt)++) \
        hlist_for_each_entry_safe_continue(obj, tmp, &name[bkt], member)

static struct mt753x_arl_entry *get_next_arl_entry(struct arl_seq_iter *iter)
{
    struct gsw_mt753x *gsw = iter->gsw;
    struct hlist_node *tmp;

    hash_for_each_safe_continue(gsw->arl_entries, iter->bkt, tmp, iter->entry, list) {
        return iter->entry;
    }

    return NULL;
}

static void *proc_arl_table_seq_start(struct seq_file *s, loff_t *pos)
{
    struct arl_seq_iter *iter = s->private;
    struct gsw_mt753x *gsw = iter->gsw;

    mutex_lock(&gsw->arl_mutex);

    if (!*pos)
        return SEQ_START_TOKEN;

    return get_next_arl_entry(iter);
}

static void *proc_arl_table_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
    ++*pos;
    return get_next_arl_entry(s->private);
}

static void proc_arl_table_seq_stop(struct seq_file *s, void *v)
{
    struct arl_seq_iter *iter = s->private;
    struct gsw_mt753x *gsw = iter->gsw;

    mutex_unlock(&gsw->arl_mutex);
    return;
}

static int proc_arl_table_seq_show(struct seq_file *s, void *v)
{
    struct mt753x_arl_entry *a = v;

    if (v != SEQ_START_TOKEN)
        seq_printf(s, "%d %d %02x:%02x:%02x:%02x:%02x:%02x %lu %u\n",
                    a->port,
                    a->vid,
                    a->mac[0], a->mac[1], a->mac[2],
                    a->mac[3], a->mac[4], a->mac[5],
                    (jiffies + (HZ / 2) - a->first_add) / HZ,
                    a->last_seen);

    return 0;
}

static struct seq_operations proc_arl_table_seq_ops =
{
    .start = proc_arl_table_seq_start,
    .next  = proc_arl_table_seq_next,
    .stop  = proc_arl_table_seq_stop,
    .show  = proc_arl_table_seq_show,
};

static int proc_arl_table_open(struct inode *inode, struct file *file)
{
    int ret = seq_open(file, &proc_arl_table_seq_ops);

    if (!ret) {
        struct arl_seq_iter *iter = kzalloc(sizeof(struct arl_seq_iter), GFP_KERNEL);
        if (!iter)
            return -ENOMEM;
        iter->gsw = mtk_gsw;
        ((struct seq_file *) file->private_data)->private = iter;
    }
    return ret;
}

static int proc_arl_table_release(struct inode *inode, struct file *file)
{
    struct seq_file *s = file->private_data;
    kfree(s->private);
    seq_release(inode, file);
    return 0;
}

static struct file_operations proc_arl_table_fops =
{
    .owner   = THIS_MODULE,
    .open    = proc_arl_table_open,
    .read    = seq_read,
    .llseek  = seq_lseek,
    .release = proc_arl_table_release,
};
#endif /* CONFIG_PROC_FS */

static int mt753x_sw_set_flush_arl_table(struct switch_dev *dev,
        const struct switch_attr *attr,
        struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    mt753x_flush_arl_table(gsw);

    return 0;
}

static int mt753x_get_port_stats(struct switch_dev *dev, int port,
				 struct switch_port_stats *stats)
{
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

	if (port < 0 || port >= MT753X_NUM_PORTS)
		return -EINVAL;

	stats->tx_bytes = get_mib_counter(gsw, MT753X_PORT_MIB_TXB_ID, port);
	stats->rx_bytes = get_mib_counter(gsw, MT753X_PORT_MIB_RXB_ID, port);

	return 0;
}

static int mt753x_sw_get_port_security(struct switch_dev *dev,
                                       const struct switch_attr *attr,
                                       struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS)
        return -EINVAL;

    val->value.i = gsw->port_entries[val->port_vlan].security;

    return 0;
}

static int mt753x_sw_set_port_security(struct switch_dev *dev,
                                       const struct switch_attr *attr,
                                       struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);

    if (val->port_vlan >= MT753X_NUM_PORTS || val->value.i >= PORT_AUTH_MAX)
        return -EINVAL;

    gsw->port_entries[val->port_vlan].security = val->value.i;

    return 0;
}

static int mt753x_sw_get_port_auth(struct switch_dev *dev,
                                   const struct switch_attr *attr,
                                   struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int port = val->port_vlan;
    u32 locked = REG_ESW_PORT_PSC_TX_LOCK | REG_ESW_PORT_PSC_RX_LOCK;

    if (port >= MT753X_NUM_PORTS ||
            (gsw->port_entries[port].security != PORT_AUTH_AUTO &&
             gsw->port_entries[port].security != PORT_AUTH_MAC_BASED))
        return -EINVAL;

    val->value.i = 1;
    if (locked & mt753x_reg_read(gsw, REG_ESW_PORT_PSC(port)))
        val->value.i = 0;

    return 0;
}

static int mt753x_sw_set_port_auth(struct switch_dev *dev,
                                   const struct switch_attr *attr,
                                   struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int port = val->port_vlan;
    u32 psc = 0xfff00;

    if (port >= MT753X_NUM_PORTS ||
            (gsw->port_entries[port].security != PORT_AUTH_AUTO &&
             gsw->port_entries[port].security != PORT_AUTH_MAC_BASED))
        return -EINVAL;

    if (!val->value.i)
        psc |= REG_ESW_PORT_PSC_TX_LOCK | REG_ESW_PORT_PSC_RX_LOCK;
    mt753x_reg_write(gsw, REG_ESW_PORT_PSC(port), psc);

    return 0;
}

static int mt753x_sw_get_port_protected(struct switch_dev *dev,
                                         const struct switch_attr *attr,
                                         struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int port = val->port_vlan, i;

    if (port >= MT753X_NUM_PORTS)
        return -EINVAL;

    if (!gsw->port_entries[port].protected) {
        val->value.i = false;
        return 0;
    }

    val->value.i = true;

    /*
     * check impermissible egress settings is applied to
     * other protected ports
     */
    for (i = 0; i < MT753X_NUM_PORTS; i++) {
        u32 pcr;
        u8 egress_ports;

        if (i == port || !gsw->port_entries[i].protected)
            continue;

        pcr = mt753x_reg_read(gsw, REG_ESW_PORT_PCR(i));
        egress_ports = REG_ESW_PORT_PCR_PORT_MATRIX(pcr);

        if (egress_ports & BIT(port)) {
            val->value.i = false;
        }
    }

    return 0;
}

static int mt753x_sw_set_port_protected(struct switch_dev *dev,
                                        const struct switch_attr *attr,
                                        struct switch_val *val) {
    struct gsw_mt753x *gsw = container_of(dev, struct gsw_mt753x, swdev);
    int port = val->port_vlan;

    if (port >= MT753X_NUM_PORTS)
        return -EINVAL;

    if (val->value.i) {
        gsw->port_entries[port].protected = true;
        gsw->protected_ports |= BIT(port);
    } else {
        gsw->port_entries[port].protected = false;
        gsw->protected_ports &= ~BIT(port);
    }

    return 0;
}

static const struct switch_attr mt753x_global[] = {
    {
        .type = SWITCH_TYPE_INT,
        .name = "enable_vlan",
        .description = "VLAN mode (1:enabled)",
        .max = 1,
        .id = MT753X_ATTR_ENABLE_VLAN,
        .get = mt753x_get_vlan_enable,
        .set = mt753x_set_vlan_enable,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "arl_age_time",
        .description = "ARL age time (secs)",
        .get = mt753x_sw_get_arl_age_time,
        .set = mt753x_sw_set_arl_age_time,
    },
    {
        .type = SWITCH_TYPE_STRING,
        .name = "arl_table",
        .description = "Get ARL table",
        .get = mt753x_sw_get_arl_table,
        .set = NULL,
    },
    {
        .type = SWITCH_TYPE_NOVAL,
        .name = "flush_arl_table",
        .description = "Flush ARL table",
        .get = NULL,
        .set = mt753x_sw_set_flush_arl_table,
    },
    {
        .type = SWITCH_TYPE_STRING,
        .name = "reg",
        .description = "Switch register operations (Apply immediately). Please [set reg \"reg=0x1234 ops=r\"] before get",
        .get = mt753x_sw_get_reg_ops,
        .set = mt753x_sw_set_reg_ops,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "mirror_max_sessions",
        .description = "Get max sessions setting",
        .set = NULL,
        .get = mt753x_sw_get_mirror_max_sessions,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "enable_mirror_rx",
        .description = "Enable mirroring of RX packets",
        .set = mt753x_sw_set_mirror_rx_enable,
        .get = mt753x_sw_get_mirror_rx_enable,
        .max = 1
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "enable_mirror_tx",
        .description = "Enable mirroring of TX packets",
        .set = mt753x_sw_set_mirror_tx_enable,
        .get = mt753x_sw_get_mirror_tx_enable,
        .max = 1
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "mirror_monitor_port",
        .description = "Mirror destination port",
        .set = mt753x_sw_set_mirror_destination_port,
        .get = mt753x_sw_get_mirror_destination_port,
        .max = MT753X_NUM_PHYS - 1
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "mirror_source_port",
        .description = "Mirror source port",
        .set = mt753x_sw_set_mirror_source_port,
        .get = mt753x_sw_get_mirror_source_port,
        .max = MT753X_NUM_PHYS - 1
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "enable_ingress_jumbo_frame",
        .description = "Enable to receive jumbo frame",
        .set = mt753x_sw_set_ingress_jumbo_frame_enable,
        .get = mt753x_sw_get_ingress_jumbo_frame_enable,
        .max = 1
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "max_ingress_jumbo_frame_len",
        .description = "Maximum length of ingress frame including CRC. unit: KBytes",
        .set = mt753x_sw_set_max_ingress_jumbo_frame,
        .get = mt753x_sw_get_max_ingress_jumbo_frame,
        .max = MT753X_MAC_MAX_RX_JUMBO_FRAME_KBYTES
    },
};

static const struct switch_attr mt753x_port[] = {
    {
        .type = SWITCH_TYPE_STRING,
        .name = "mib",
        .description = "Get MIB counters for port",
        .get = mt753x_sw_get_port_mib,
        .set = NULL,
    },
    {
        .type = SWITCH_TYPE_STRING,
        .name = "phyreg",
        .description = "Get PHY register value",
        .get = mt753x_sw_get_port_phyreg,
        .set = NULL,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "disable",
        .description = "Port state (1:disabled)",
        .max = 1,
        .get = mt753x_sw_get_port_disable,
        .set = mt753x_sw_set_port_disable,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "ingress_mode_override",
        .description = "Enable port's ingress override mode",
        .max = 1,
        .get = mt753x_sw_get_port_ingress_mode_override,
        .set = mt753x_sw_set_port_ingress_mode_override,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "ingress_mode_val",
        .description = "Get/Set port's ingress override mode value",
        .max = 3,
        .get = mt753x_sw_get_port_ingress_mode,
        .set = mt753x_sw_set_port_ingress_mode,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "egress_mode_override",
        .description = "Enable port's egress override mode",
        .max = 1,
        .get = mt753x_sw_get_port_egress_mode_override,
        .set = mt753x_sw_set_port_egress_mode_override,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "egress_mode_val",
        .description = "Get/Set port's egress override mode value (0 = unmodified, 1 = untagged, 2 = tagged, 3 = untouched)",
        .max = 3,
        .get = mt753x_sw_get_port_egress_mode,
        .set = mt753x_sw_set_port_egress_mode,
    },
    {
        .type = SWITCH_TYPE_NOVAL,
        .name = "flush_arl_table",
        .description = "Flush port's ARL table entries",
        .get = NULL,
        .set = mt753x_sw_set_port_flush_arl_table,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "security",
        .description = "Port security control (0 = auto, 1 = force-auth, 2 = force-unauth, 3 = mac-based)",
        .get = mt753x_sw_get_port_security,
        .set = mt753x_sw_set_port_security,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "authorize",
        .description = "Authorize/Deauthorize port (when security is auto)",
        .get = mt753x_sw_get_port_auth,
        .set = mt753x_sw_set_port_auth,
    },
    {
        .type = SWITCH_TYPE_INT,
        .name = "protected",
        .description = "Enable port protection (isolation)",
        .max = 1,
        .get = mt753x_sw_get_port_protected,
        .set = mt753x_sw_set_port_protected,
    },
};

static const struct switch_attr mt753x_vlan[] = {
    {
        .type = SWITCH_TYPE_INT,
        .name = "vid",
        .description = "VLAN ID (0-4094)",
        .set = mt753x_set_vid,
        .get = mt753x_get_vid,
        .max = 4094,
    },
};

static const struct switch_dev_ops mt753x_swdev_ops = {
    .attr_global = {
        .attr = mt753x_global,
        .n_attr = ARRAY_SIZE(mt753x_global),
    },
    .attr_port = {
        .attr = mt753x_port,
        .n_attr = ARRAY_SIZE(mt753x_port),
    },
    .attr_vlan = {
        .attr = mt753x_vlan,
        .n_attr = ARRAY_SIZE(mt753x_vlan),
    },
    .get_vlan_ports = mt753x_get_vlan_ports,
    .set_vlan_ports = mt753x_set_vlan_ports,
    .get_port_pvid = mt753x_get_port_pvid,
    .set_port_pvid = mt753x_set_port_pvid,
    .get_port_link = mt753x_get_port_link,
    .set_port_link = mt753x_set_port_link,
    .phy_read16 = mt753x_phy_read16,
    .phy_write16 = mt753x_phy_write16,
    .get_port_stats = mt753x_get_port_stats,
    .apply_config = mt753x_apply_config,
    .reset_switch = mt753x_reset_switch,
};

int mt753x_swconfig_init(struct gsw_mt753x *gsw)
{
    struct device_node *np = gsw->dev->of_node;
    struct switch_dev *swdev;
    struct mt753x_mapping *map;
    const char *proc_dir;
    const __be32 *port_disable;
    int ret;
    int reg_value;

    port_disable = of_get_property(np, "mediatek,portdisable", NULL);
    if (port_disable)
        gsw->port_disable = be32_to_cpu(*port_disable);

    if (of_property_read_u32(np, "mediatek,cpuport", &gsw->cpu_port))
        gsw->cpu_port = MT753X_DFL_CPU_PORT;

    gsw->global_vlan_enable = DEFAULT_VLAN;
    gsw->arl_age_time = MT753X_DEFAULT_ARL_AGE_TIME;
    mutex_init(&gsw->arl_mutex);
    hash_init(gsw->arl_entries);
    INIT_DELAYED_WORK(&gsw->arl_update_work, mt753x_update_arl_table);
    gsw->reg_entry.regnum = MT753X_UINT32_MAX;
    gsw->ingress_jumbo_frame_enable = false;
    gsw->max_ingress_jumbo_frame_kbytes = MT753X_MAC_DEFAULT_RX_JUMBO_FRAME_KBYTES;

    mt753x_init_ports(gsw);

    swdev = &gsw->swdev;

    swdev->name = gsw->name;
    swdev->alias = gsw->name;
    swdev->cpu_port = gsw->cpu_port;
    swdev->ports = MT753X_NUM_PORTS;
    swdev->vlans = MT753X_NUM_VLANS;
    swdev->ops = &mt753x_swdev_ops;

    ret = register_switch(swdev, NULL);
    if (ret) {
        dev_err(gsw->dev, "Failed to register switch %s\n",
                swdev->name);
        return ret;
    }

    map = mt753x_find_mapping(gsw->dev->of_node);
    if (map)
        mt753x_apply_mapping(gsw, map);

    reg_value = mt753x_reg_read(gsw, REG_ESW_WT_MAC_MFC);
    /* Enable CPU port */
    reg_value |= BIT(7);
    /* Set CPU port number */
    reg_value &= ~BITS(6, 4);
    reg_value |= (gsw->cpu_port << 4);
    mt753x_reg_write(gsw, REG_ESW_WT_MAC_MFC, reg_value);

    /* BPDU and PAE control */
    reg_value = mt753x_reg_read(gsw, REG_ESW_BPC);
    /* Forward PAE to CPU port only */
    reg_value &= ~BITS(18, 16);
    reg_value |= (REG_ESW_PORT_FW_CPU_PORT_ONLY << 16);
    /* Untagged Egress PAE */
    reg_value &= ~BITS(24, 22);
    reg_value |= (REG_ESW_EG_TAG_UNTAG << 22);
    /* Enable Leaky VLAN for PAE */
    reg_value |= BIT(21);
    mt753x_reg_write(gsw, REG_ESW_BPC, reg_value);

    mt753x_apply_config(swdev);
    mt753x_flush_arl_table(gsw);

    mtk_gsw = gsw;
    dev_info(gsw->dev, "loaded %s driver\n", swdev->name);

#ifdef CONFIG_PROC_FS
    proc_dir = swdev->devname;
    proc_switch_dir = proc_mkdir(proc_dir, NULL);
    if (proc_switch_dir) {
        proc_arl_table = proc_create(MT753X_ARL_PROC_ENTRY, 0, proc_switch_dir, &proc_arl_table_fops);
        if (!proc_arl_table) {
            remove_proc_entry(proc_dir, NULL);
            proc_switch_dir = NULL;
        }
    }
#endif /* CONFIG_PROC_FS */

    schedule_delayed_work(&gsw->arl_update_work,
                          msecs_to_jiffies(MT753X_ARL_READ_WORK_DELAY));

    return 0;
}

void mt753x_swconfig_destroy(struct gsw_mt753x *gsw)
{
    cancel_delayed_work_sync(&gsw->arl_update_work);
#ifdef CONFIG_PROC_FS
    remove_proc_entry(MT753X_ARL_PROC_ENTRY, proc_switch_dir);
    remove_proc_entry(gsw->swdev.devname, NULL);
#endif /* CONFIG_PROC_FS */
    unregister_switch(&gsw->swdev);
}
