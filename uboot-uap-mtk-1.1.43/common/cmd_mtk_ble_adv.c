#include <common.h>
#include <serial.h>
#include <command.h>
#include <dm.h>
#include <errno.h>
#include <config.h>
#include <asm/arch/typedefs.h>
#include <ubnt_export.h>


#define GPIO61_MODE 			0x10211340
#define GPIO61_DIR_MODE 		0x10211010
#define GPIO61_DOUT_MODE 		0x10211110

#define GPIO83_MODE 			0x10211350
#define GPIO83_DIR_MODE 		0x10211020
#define GPIO83_DOUT_MODE 		0x10211120

#define CMD_RET_SUCCESS 1
#define CMD_RET_FAIL 0
#define TRUE 1
#define FALSE 0
#define BLE_FORMAT_HEADER_LEN 4
#define BLE_PW_ON_IDX 0
#define BLE_HCI_RESET_IDX 1
#define BLE_HCI_SET_ADV_PARAM_IDX 2
#define BLE_HCI_SET_ADV_DATA_IDX 3
#define BLE_HCI_SET_SCAN_RESP_DATA_IDX 4
#define BLE_HCI_SET_ADV_ENABLE_IDX 5
#define BLE_HCI_SET_BT_MAC_IDX 6
#define GPIO_CLEAR 0
#define GPIO_SET 1
// BLE EXPECTED RX Messgae
const char  pw_on_correct_code[] = {0x04, 0xe4, 0x05, 0x02, 0x06, 0x01, 0x00, 0x00};
const char  hic_reset_correct_code[] = {0x04, 0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00};
const char  hic_le_set_adv_param_correct_code[] = {0x04, 0x0E, 0x04, 0x01, 0x06, 0x20, 0x00};
const char  hic_le_set_adv_data_correct_code[] = {0x04, 0x0E, 0x04, 0x01, 0x08, 0x20, 0x00};
const char  hic_le_set_scan_resp_data_correct_code[] = {0x04, 0x0E, 0x04, 0x01, 0x09, 0x20, 0x00};
const char  hic_le_set_adv_en_correct_code[] = {0x04, 0x0E, 0x04, 0x01, 0x0A, 0x20, 0x00};
const char  hic_le_set_bt_mac_correct_code[] = {0x04, 0x0E, 0x04, 0x01, 0x1A, 0xFC, 0x00};

#define LEN_HCI_CORRECT_POWER_ON_CODE        (sizeof(pw_on_correct_code)/sizeof(char))
#define LEN_HCI_CORRECT_RESET_CODE           (sizeof(hic_reset_correct_code)/sizeof(char))
#define LEN_HCI_LE_SET_ADV_PARAM_CODE        (sizeof(hic_le_set_adv_param_correct_code)/sizeof(char))
#define LEN_HCI_LE_SET_ADV_DATA_CODE         (sizeof(hic_le_set_adv_data_correct_code)/sizeof(char))
#define LEN_HCI_LE_SET_SCAN_RSP_DATA_CODE    (sizeof(hic_le_set_scan_resp_data_correct_code)/sizeof(char))
#define LEN_HCI_LE_ADV_EN_CODE               (sizeof(hic_le_set_adv_en_correct_code)/sizeof(char))
#define LEN_HCI_LE_SET_BT_MAC_CODE           (sizeof(hic_le_set_bt_mac_correct_code)/sizeof(char))

//BLE TX Command
const char bt_power_on_tx_cmd[] = {0x01, 0x6f, 0xfc, 0x06, 0x01, 0x06, 0x02, 0x00, 0x00, 0x01};

const char hci_reset_tx_cmd[] = {0x01, 0x03, 0x0C, 0x00};

const char hci_le_set_adv_param_tx_cmd[] =
{
	0x01, 0x06, 0x20, 0x0f,
	0x20, 0x00, //Advertising Interval Min (x0.625ms)
	0x20, 0x00, //Advertising Interval Max (x0.625ms)
	0x02, //Advertising Type
	0x00, //Own Address Type
	0x00, //Peer Address Type
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //Peer Address
	0x07, //Advertising Channel Map
	0x00 //Advertising Filter Policy
};
//Sample1: MT7915_BLE_ADV_TEST
const char hci_le_set_adv_data_tx_cmd[] =
{
	0x01, 0x08, 0x20, 0x20,
	0x18, //Advertising Data Length, Total 31 bytes
	0x02, 0x01, 0x1A, 0x14, 0x09, 0x4d, // Advertising Data Byte0~Byte6
	0x54, 0x37, 0x39, 0x31, 0x35, 0x5f, // Advertising Data Byte7~Byte12
	0x42, 0x4c, 0x45, 0x5f, 0x41, 0x44, // Advertising Data Byte13~Byte18
	0x56, 0x5f, 0x54, 0x45, 0x53, 0x54, // Advertising Data Byte19~Byte24
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Advertising Data Byte25~Byte30
	0x00 // Advertising Data Byte31
};

const char hci_le_set_scan_resp_data_tx_cmd[] =
{
	0x01, 0x09, 0x20, 0x20,
	0x18, //Scan Response Data Length, Total 31 bytes
	0x02, 0x01, 0x1A, 0x14, 0x09, 0x4d, // Scan Response Data Byte0~Byte6
	0x54, 0x37, 0x39, 0x31, 0x35, 0x5f, // Scan Response Data Byte7~Byte12
	0x42, 0x4c, 0x45, 0x5f, 0x41, 0x44, // Scan Response Data Byte13~Byte18
	0x56, 0x5f, 0x54, 0x45, 0x53, 0x54, // Scan Response Data Byte19~Byte24
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Scan Response Data Byte25~Byte30
	0x00 // Scan Response Data Byte31
};

#define STP_HEAD_FORMAT_LEN 4
#define STP_TAIL_FORMAT_LEN 2

//STP format
const char stp_head_tx_pw_on_cmd[] = {0x80, 0x00, 0x0A, 0x00};
const char stp_head_tx_hci_reset_cmd[] = {0x80, 0x00, 0x04, 0x00};
const char stp_head_tx_adv_param_cmd[] = {0x80, 0x00, 0x13, 0x00};
const char stp_head_tx_adv_data_cmd[] = {0x80, 0x00, 0x24, 0x00};
const char stp_head_tx_scan_rsp_data_cmd[] = {0x80, 0x00, 0x24, 0x00};
const char stp_head_tx_adv_en_cmd[] = {0x80, 0x00, 0x05, 0x00};
const char stp_head_tx_bt_mac_cmd[] = {0x80, 0x00, 0x0A, 0x00};
const char stp_tail_txrx_cmd[] = {0x00, 0x00};

#ifndef REVERT_TO_CHECK_ORIGIN_PATCH
#define UUID128_SIZE		16
const char u6lr_factory_uuid[UUID128_SIZE] = {0xf2, 0x91, 0x1b, 0xbf, 0x22, 0x9e, 0xa3, 0xa0, 0x56, 0x4b, 0x0a, 0xf6, 0xa6, 0xf5, 0x3f, 0x88};
const char u6lr_config_uuid[UUID128_SIZE] = {0x27, 0x96, 0x5f, 0x9c, 0x40, 0xe3, 0xcd, 0xb6, 0x01, 0x4a, 0x62, 0x65, 0x06, 0x08, 0x6e, 0x3e};
const char u6lr_str[] = {"U6-LR"};

/* ADV */
typedef struct {
    uint8_t len;
    uint8_t ad_type;
    uint8_t ad_data[UUID128_SIZE];
} ad_element_uuid_t;

typedef struct {
    uint8_t len;
    uint8_t ad_type;
    uint8_t ad_service_uuid[2];
    uint8_t ad_data;
} ad_element_mode_t;

typedef struct {
    uint8_t hci_packet_type;
    uint8_t opcode[2];
    uint8_t total_len;
    uint8_t ad_data_len;
    ad_element_uuid_t uuid;
    ad_element_mode_t mode;
    uint8_t reserved[8];
} hci_le_adv_data_cmd_t;

hci_le_adv_data_cmd_t hci_le_adv_data_cmd =
{
    .hci_packet_type = 0x01,
    .opcode = {0x08, 0x20},
    .total_len = 0x20,
    .ad_data_len = 0x17,
    .uuid.len = 0x11,	//128-bits UUID
    .uuid.ad_type = 0x06,
    .uuid.ad_data = {0x00},
    .mode.len = 0x04,
    .mode.ad_type = 0x16,	//service data: 0x0 in uboot, 0x1 in kernel mode
    .mode.ad_service_uuid = {0x21, 0x20},
    .mode.ad_data = 0x00,
    .reserved = {0x00}
};

/* Scan Resposne */
typedef struct {
    uint8_t len;
    uint8_t ad_type;
    uint8_t ad_service_uuid[2];
    uint8_t ad_data[6];
} ad_element_mac_t;

typedef struct {
    uint8_t len;
    uint8_t ad_type;
    uint8_t ad_service_uuid[2];
    uint8_t time;
} ad_element_boottime_t;

typedef struct {
    uint8_t len;
    uint8_t ad_type;
    uint8_t ad_data[9];
} ad_element_name_t;

typedef struct {
    uint8_t hci_packet_type;
    uint8_t opcode[2];
    uint8_t total_len;
    uint8_t ad_data_len;
    ad_element_mac_t mac;
    ad_element_boottime_t boot_time;
    ad_element_name_t name;
    uint8_t reserved[5];
} hci_le_scan_rsp_data_cmd_t;

hci_le_scan_rsp_data_cmd_t hci_le_scan_rsp_data_cmd =
{
    .hci_packet_type = 0x01,
    .opcode = {0x09, 0x20},
    .total_len = 0x20,
    .ad_data_len = 0x18,
    .mac.len = 0x09,
    .mac.ad_type = 0x16,    //service data: mac address
    .mac.ad_service_uuid = {0x2a, 0x25},
    .mac.ad_data = {0x00},
    .boot_time.len = 0x04,
    .boot_time.ad_type = 0x16,
    .boot_time.ad_service_uuid = {0x29, 0x25},
    .boot_time.time = 0x41,
    .name.len = 0x00,
    .name.ad_type = 0x08,	//short local name
    .name.ad_data = {0x00},
    .reserved = {0x00}
};

static char *p_macaddr = NULL;
static char *p_device_model = NULL;
bool is_default = true;
bool is_ble_stp = false;
#endif

const char hci_le_set_adv_enable_tx_cmd[] = {0x01, 0x0A, 0x20, 0x01, 0x01}; //Advertising Enable/Disable
const char hci_le_set_bt_mac_tx_cmd[] = { 0x01, 0x1A, 0xFC, 0x06, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 /*BT MAC Setting, ex:11:22:33:44:55:66*/ };

extern volatile unsigned int g_uart;
extern volatile unsigned int g_2nd_uart;
extern struct serial_device *default_serial_console(void);
extern struct serial_device *mtk_2nd_serial_console(void);

static int get_default_state(void)
{
    char *is_default = NULL;
    is_default = getenv("is_default");
    if (is_default && (strcmp(is_default, "false") == 0)) {
        printf("is_default = false\n");
        return 0;
    }

    printf("is_default %s\n", is_default);
    if (is_default == NULL) {
        setenv("is_default", "true");
        printf("set is_default true\n");
    }
    printf("is_default = true or NULL\n");
    return 1;
}

static int get_ble_stp_state(void)
{
    char *is_ble_stp = NULL;
    is_ble_stp = getenv("is_ble_stp");
    if (is_ble_stp && (strcmp(is_ble_stp, "true") == 0)) {
        printf("is_ble_stp = true\n");
        return 1;
    }

    printf("is_ble_stp %s\n", is_ble_stp);
    if (is_ble_stp == NULL) {
        setenv("is_ble_stp", "false");
    }
    printf("is_ble_stp = false or NULL\n");
    return 0;
}

static int get_ble_tx_cmd(u8 idx, u8 tx_cmd_buf[])
{
	u8 len = 0;
	int8_t mac_offset = 0;
	char *tmpmac;
#ifndef REVERT_TO_CHECK_ORIGIN_PATCH
	int8_t i = 0;
	char *end;
#endif

	memset(tx_cmd_buf, 0x0, sizeof(tx_cmd_buf));
	switch(idx)
	{
		case BLE_PW_ON_IDX:
			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_pw_on_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], bt_power_on_tx_cmd, sizeof(bt_power_on_tx_cmd));
			len = len + (sizeof(bt_power_on_tx_cmd) / sizeof(u8));
			break;
		case BLE_HCI_RESET_IDX:
			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_hci_reset_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], hci_reset_tx_cmd, sizeof(hci_reset_tx_cmd));
			len = len + (sizeof(hci_reset_tx_cmd) / sizeof(u8));
			break;
		case BLE_HCI_SET_ADV_PARAM_IDX:
			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_adv_param_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], hci_le_set_adv_param_tx_cmd, sizeof(hci_le_set_adv_param_tx_cmd));
			len = len + (sizeof(hci_le_set_adv_param_tx_cmd) / sizeof(u8));
			break;
		case BLE_HCI_SET_ADV_DATA_IDX:
#ifdef REVERT_TO_CHECK_ORIGIN_PATCH
			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_adv_data_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], hci_le_set_adv_data_tx_cmd, sizeof(hci_le_set_adv_data_tx_cmd));
			len = len + (sizeof(hci_le_set_adv_data_tx_cmd) / sizeof(u8));
#else
			hci_le_adv_data_cmd.ad_data_len = 5; //Constant hci_le_adv_data_cmd.mode
			if(p_device_model != NULL) {
				hci_le_adv_data_cmd.ad_data_len += 18; /* 1 byte ad_data len + 1 byte ad_type + 16 bytes uuid */
				if(strcmp(p_device_model, u6lr_str) == 0) {
					if (is_default == true) {
						memcpy((void *)&hci_le_adv_data_cmd.uuid.ad_data[0], (void *)&u6lr_factory_uuid[0], UUID128_SIZE);
					} else {
						memcpy((void *)&hci_le_adv_data_cmd.uuid.ad_data[0], (void *)&u6lr_config_uuid[0], UUID128_SIZE);
					}
				}
			} else {
				printf("\n[ERROR]Empty p_device_model: %s\n", p_device_model);
			}

			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_adv_data_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], (u8 *)&hci_le_adv_data_cmd, sizeof(hci_le_adv_data_cmd));
			len = len + (sizeof(hci_le_adv_data_cmd) / sizeof(u8));
#endif
			break;
		case BLE_HCI_SET_SCAN_RESP_DATA_IDX:
#ifdef REVERT_TO_CHECK_ORIGIN_PATCH
			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_scan_rsp_data_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], hci_le_set_scan_resp_data_tx_cmd, sizeof(hci_le_set_scan_resp_data_tx_cmd));
			len = len + (sizeof(hci_le_set_scan_resp_data_tx_cmd) / sizeof(u8));
#else
			hci_le_scan_rsp_data_cmd.ad_data_len = 5;
			if(p_device_model != NULL) {
				if(strcmp(p_device_model, u6lr_str) == 0) {
					hci_le_scan_rsp_data_cmd.ad_data_len += sizeof(u6lr_str)+1;
					hci_le_scan_rsp_data_cmd.name.len = sizeof(u6lr_str);
					memcpy((void *)&hci_le_scan_rsp_data_cmd.name.ad_data, (void *)&u6lr_str, sizeof(u6lr_str));
				}
			} else {
				printf("\n[ERROR]Empty p_device_model: %s\n", p_device_model);
			}

			tmpmac = p_macaddr;
			if(tmpmac != NULL) {
				hci_le_scan_rsp_data_cmd.ad_data_len += 10; /* 1 byte ad_data len + 1 byte ad_type + 2 bytes service_uuid + 6 byte mac */
				for (i = 0; i < 6; ++i) {
					hci_le_scan_rsp_data_cmd.mac.ad_data[i] = tmpmac ? simple_strtoul(tmpmac, &end, 16) : 0;
					if (tmpmac) {
						tmpmac = (*end) ? end + 1 : end;
					}
				}
			}

			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_scan_rsp_data_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], (u8 *)&hci_le_scan_rsp_data_cmd, sizeof(hci_le_scan_rsp_data_cmd));
			len = len + (sizeof(hci_le_scan_rsp_data_cmd) / sizeof(u8));
#endif
			break;
		case BLE_HCI_SET_ADV_ENABLE_IDX:
			if (is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_adv_en_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], hci_le_set_adv_enable_tx_cmd, sizeof(hci_le_set_adv_enable_tx_cmd));
			len = len + (sizeof(hci_le_set_adv_enable_tx_cmd) / sizeof(u8));

			break;
		case BLE_HCI_SET_BT_MAC_IDX:
			if(is_ble_stp)
			{
				memcpy(tx_cmd_buf, stp_head_tx_bt_mac_cmd, STP_HEAD_FORMAT_LEN);
				len = STP_HEAD_FORMAT_LEN;
				mac_offset = STP_HEAD_FORMAT_LEN;
			}
			memcpy(&tx_cmd_buf[len], hci_le_set_bt_mac_tx_cmd, sizeof(hci_le_set_bt_mac_tx_cmd));
			len = len + (sizeof(hci_le_set_bt_mac_tx_cmd) / sizeof(u8));

			tmpmac = p_macaddr;
			if(tmpmac != NULL) {
				for (i = 5; i >= 0; --i) {
					tx_cmd_buf[mac_offset+4+i] = tmpmac ? simple_strtoul(tmpmac, &end, 16) : 0;
					if(i == 0) {
						//bt mac address generation rule is to add 3 at the last byte of mac address
						tx_cmd_buf[mac_offset+4+i] += 3;
					}
					if (tmpmac) {
						tmpmac = (*end) ? end + 1 : end;
					}
				}
			}
		default:
			break;
	}

	if (is_ble_stp)
	{
		memcpy(&tx_cmd_buf[len], stp_tail_txrx_cmd, sizeof(stp_tail_txrx_cmd));
		len = len + STP_TAIL_FORMAT_LEN;
	}

	return len;
}
static int judge_rx_msg(u8 msg_idx, u8 rx_msg_buf[])
{
	u8 ret = 0;
	u8 num = 0;

	switch(msg_idx)
	{
		case BLE_PW_ON_IDX:
			num = sizeof(pw_on_correct_code);
			if (is_ble_stp) {
				ret = memcmp( &rx_msg_buf[STP_HEAD_FORMAT_LEN], pw_on_correct_code, num);
			} else {
				ret = memcmp( rx_msg_buf, pw_on_correct_code, num);
			}
			//printf("[judge_rx_msg]: num=%d, ret=%d\n", num, ret);
			break;
		case BLE_HCI_RESET_IDX:
			num = sizeof(hic_reset_correct_code);
			if (is_ble_stp) {
				ret = memcmp( &rx_msg_buf[STP_HEAD_FORMAT_LEN], hic_reset_correct_code, num);
			} else {
				ret = memcmp( rx_msg_buf, hic_reset_correct_code, num);
			}
			//printf("[judge_rx_msg]: num=%d, ret=%d\n", num, ret);
			break;
		case BLE_HCI_SET_ADV_PARAM_IDX:
			num = sizeof(hic_le_set_adv_param_correct_code);
			if (is_ble_stp) {
				ret = memcmp( &rx_msg_buf[STP_HEAD_FORMAT_LEN], hic_le_set_adv_param_correct_code, num);
			} else {
				ret = memcmp( rx_msg_buf, hic_le_set_adv_param_correct_code, num);
			}
			//printf("[judge_rx_msg]: num=%d, ret=%d\n", num, ret);
			break;
		case BLE_HCI_SET_ADV_DATA_IDX:
			num = sizeof(hic_le_set_adv_data_correct_code);
			if (is_ble_stp) {
				ret = memcmp( &rx_msg_buf[STP_HEAD_FORMAT_LEN], hic_le_set_adv_data_correct_code, num);
			} else {
				ret = memcmp( rx_msg_buf, hic_le_set_adv_data_correct_code, num);
			}
			//printf("[judge_rx_msg]: num=%d, ret=%d\n", num, ret);
			break;
		case BLE_HCI_SET_SCAN_RESP_DATA_IDX:
			num = sizeof(hic_le_set_scan_resp_data_correct_code);
			if (is_ble_stp) {
				ret = memcmp( &rx_msg_buf[STP_HEAD_FORMAT_LEN], hic_le_set_scan_resp_data_correct_code, num);
			} else {
				ret = memcmp( rx_msg_buf, hic_le_set_scan_resp_data_correct_code, num);
			}
			//printf("[judge_rx_msg]: num=%d, ret=%d\n", num, ret);
			break;
		case BLE_HCI_SET_ADV_ENABLE_IDX:
			num = sizeof(hic_le_set_adv_en_correct_code);
			if (is_ble_stp) {
				ret = memcmp( &rx_msg_buf[STP_HEAD_FORMAT_LEN], hic_le_set_adv_en_correct_code, num);
			} else {
				ret = memcmp( rx_msg_buf, hic_le_set_adv_en_correct_code, num);
			}
			//printf("[judge_rx_msg]: num=%d, ret=%d\n", num, ret);
			break;
		case BLE_HCI_SET_BT_MAC_IDX:
			num = sizeof(hic_le_set_bt_mac_correct_code);
			if (is_ble_stp) {
				ret = memcmp( &rx_msg_buf[STP_HEAD_FORMAT_LEN], hic_le_set_bt_mac_correct_code, num);
			} else {
				ret = memcmp( rx_msg_buf, hic_le_set_bt_mac_correct_code, num);
			}
			//printf("[judge_rx_msg]: num=%d, ret=%d\n", num, ret);
			break;
		default:
			break;
	}
	if(ret == 0 )
		return CMD_RET_SUCCESS;
	else
		return CMD_RET_FAIL;
}

static int do_mtk_ble_adv(cmd_tbl_t *cmdtp, int flag, int argc,	char *const argv[])
{
	// START BT PROCESS
	//struct udevice *uart;
	u8 bt_tx_cmd[40]={0x0};
	u8 bt_rx_msg[40]={0x0};
	u8 num = 0;
	u8 len = 0;
	u8 i = 0;
	u8 ch = 0;
	u8 curr_step_pass = 0;
	int ret;
	u32 start_timep;
	u32 curr_timep;
	u32 value = 0;

	printf("=========================GPIO INIT=====================\n");
	printf("GPIO61 initial to push GPIO28 on MT7915\n");
	// init gpio61 to gpio mode
	value = DRV_Reg32(GPIO61_MODE);
	value = value | (0x1 << 12);
	DRV_WriteReg32(GPIO61_MODE, value);
	value = DRV_Reg32(GPIO61_MODE);
	printf("GPIO61_MODE value: %x\n", value);

	// init gpio61 to output mode
	value = DRV_Reg32(GPIO61_DIR_MODE);
	value = value | (0x1 << 29);
	DRV_WriteReg32(GPIO61_DIR_MODE, value);
	value = DRV_Reg32(GPIO61_DIR_MODE);
	printf("GPIO61_DIR_MODE value: %x\n", value);

	// init gpio61 data to high
	value = DRV_Reg32(GPIO61_DOUT_MODE);
	value = value | (0x1 << 29);
	DRV_WriteReg32(GPIO61_DOUT_MODE, value);
	value = DRV_Reg32(GPIO61_DOUT_MODE);
	printf("GPIO61_DOUT_MODE value: %x\n", value);

	printf("GPIO83(PERST0) initial to signal trigger PCIE on MT7915\n");
	// init gpio83(PERST0) to gpio mode
	value = DRV_Reg32(GPIO83_MODE);
	value = value | (0x1 << 28);
	DRV_WriteReg32(GPIO83_MODE, value);
	value = DRV_Reg32(GPIO83_MODE);
	printf("GPIO83_MODE value: %x\n", value);

	// init gpio83(PERST0)  to output mode
	value = DRV_Reg32(GPIO83_DIR_MODE);
	value = value | (0x1 << 19);
	DRV_WriteReg32(GPIO83_DIR_MODE, value);
	value = DRV_Reg32(GPIO83_DIR_MODE);
	printf("GPIO83_DIR_MODE value: %x\n", value);

	// trigger gpio83(PERST0) outpur data to high
	value = DRV_Reg32(GPIO83_DOUT_MODE);
	value = value | (0x1 << 19);
	DRV_WriteReg32(GPIO83_DOUT_MODE, value);
	value = DRV_Reg32(GPIO83_DOUT_MODE);
	printf("GPIO83_DOUT_MODE value: %x\n", value);
	mdelay(100);

	printf("=========================UART_3 INIT=====================\n");
	mtk_2nd_serial_console()->start();
	mtk_2nd_serial_console()->setbrg();
	//FLOW 1: BT Power On
	printf("=========================FLOW 1=====================\n");
	memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
	len = sizeof(pw_on_correct_code)/sizeof(u8);
	ret = get_ble_tx_cmd(BLE_PW_ON_IDX, bt_tx_cmd);
	if(ret > 0)
	{
		num = ret;
		printf("[BT Power On]: Tx Cmd=");
		for (i = 0; i < num; i++)
		{
			mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
			printf("%02x ",bt_tx_cmd[i]);
		}
		printf("\n");
	}
	printf("len= %d, num= %d\n", len, num);

	i = 0;
	memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
	printf("[BT Power On]: Rx Msg=");
	start_timep = get_timer(0);
	while (i < len) {
		if (mtk_2nd_serial_console()->tstc()){
			ch = mtk_2nd_serial_console()->getc();
			memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
			printf("%02x ",bt_rx_msg[i]);
			i++;
		}

		curr_timep = get_timer(0);
		if(curr_timep - start_timep > 1500)
			break;
	}
	printf("\n");
	ret = judge_rx_msg(BLE_PW_ON_IDX, bt_rx_msg);
	if(ret == CMD_RET_SUCCESS){
		curr_step_pass = TRUE;
		printf("[BT Power On Result] Success\n");
	}else
		printf("[BT Power On Result] Fail\n");

	//FLOW 2: HCI_RESET
	printf("\n=========================FLOW 2=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		len = sizeof(hic_reset_correct_code)/sizeof(u8);
		ret = get_ble_tx_cmd(BLE_HCI_RESET_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			printf("[HCI RESET]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n");
		}
		printf("len= %d, num= %d\n", len, num);

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		printf("[HCI RESET]: Rx Msg=");
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				printf("%02x ",bt_rx_msg[i]);
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}
		printf("\n");

		ret = judge_rx_msg(BLE_HCI_RESET_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HIC RESET Result] Success\n");
		}
		else{
			curr_step_pass = FALSE;
			printf("[HIC RESET Result] Fail\n");
		}
	}

	//FLOW 3: HCI_LE_Set_Advertising_Parameters
	printf("\n=========================FLOW 3=====================\n");
	if(curr_step_pass == TRUE)
	{	memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		len = sizeof(hic_le_set_adv_param_correct_code)/sizeof(u8);
		ret = get_ble_tx_cmd(BLE_HCI_SET_ADV_PARAM_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			printf("[HCI LE_Set Advertising Parameters]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n");
		}
		printf("len= %d, num= %d\n", len, num);

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		printf("[HCI LE_Set Advertising Parameters]: Rx Msg=");
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				printf("%02x ",bt_rx_msg[i]);
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}
		printf("\n");

		ret = judge_rx_msg(BLE_HCI_SET_ADV_PARAM_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HIC LE  SET ADVERTISING PARAMETER Result] Success\n");
		}
		else{
			curr_step_pass = FALSE;
			printf("[HIC LE  SET ADVERTISING PARAMETER Result]  Fail\n");
		}
	}

	//FLOW 4: HCI_LE_Set_Advertising_Data
	printf("\n=========================FLOW 4=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		len = sizeof(hic_le_set_adv_data_correct_code)/sizeof(u8);
		ret = get_ble_tx_cmd(BLE_HCI_SET_ADV_DATA_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			printf("[HCI LE_Set Advertising Data]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n");
		}
		printf("len= %d, num= %d\n", len, num);

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		printf("[HCI LE_Set Advertising Data]: Rx Msg=");
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				printf("%02x ",bt_rx_msg[i]);
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}
		printf("\n");

		ret = judge_rx_msg(BLE_HCI_SET_ADV_DATA_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HIC LE  SET ADVERTISING DATA Result] Success\n");
		}
		else{
			curr_step_pass = FALSE;
			printf("[HIC LE  SET ADVERTISING DATA Result] Fail\n");
		}
	}

	//FLOW 5: HCI_LE_Set_Scan_Response_Data
	printf("\n=========================FLOW 5=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		len = sizeof(hic_le_set_scan_resp_data_correct_code)/sizeof(u8);
		ret = get_ble_tx_cmd(BLE_HCI_SET_SCAN_RESP_DATA_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			printf("[HCI LE_Set Scan Response Data]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n");
		}
		printf("len= %d, num= %d\n", len, num);

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		printf("[HCI LE_Set Scan Response Data ]: Rx Msg=");
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				printf("%02x ",bt_rx_msg[i]);
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}
		printf("\n");

		ret = judge_rx_msg(BLE_HCI_SET_SCAN_RESP_DATA_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HIC LE  SET SCAN RESPONSE Result] Success\n");
		}
		else{
			curr_step_pass = FALSE;
			printf("[HIC LE  SET SCAN RESPONSE Result] Fail\n");
		}

	}
	//FLOW 6: HCI_LE_Set_Advertising_Enable
	printf("\n=========================FLOW 6=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		len = sizeof(hic_le_set_adv_en_correct_code)/sizeof(u8);
		ret = get_ble_tx_cmd(BLE_HCI_SET_ADV_ENABLE_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			printf("[HCI LE_Set Advertising Enable]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n");
		}
		printf("len= %d, num= %d\n", len, num);
		i = 0;

		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		printf("[HCI LE_Set Advertising Enable]: Rx Msg=");
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()) {
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				printf("%02x ",bt_rx_msg[i]);
				i++;
			}
			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}
		printf("\n");

		ret = judge_rx_msg(BLE_HCI_SET_ADV_ENABLE_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HIC LE  SET ADVERISTING ENABLE Result] Success\n");
		}
		else{
			curr_step_pass = FALSE;
			printf("[HIC LE  SET ADVERISTING ENABLE Result] Fail\n");
		}
	}

	return CMD_RET_SUCCESS;
}

static int do_ble_adveristing_start(void)
{
	// START BT PROCESS
	//struct udevice *uart;
	u8 bt_tx_cmd[50]={0x0};
	u8 bt_rx_msg[40]={0x0};
	u8 num = 0;
	u8 len = 0;
	u8 i = 0;
	u8 ch = 0;
	u8 curr_step_pass = 0;
	int ret;
	u32 start_timep;
	u32 curr_timep;
	u32 value = 0;

	p_device_model = getenv("device_model");
	is_default = get_default_state();
	is_ble_stp = get_ble_stp_state();

	printf("\n~~~ p_device_model:%s\n", p_device_model);
	printf("~~~ is_default:%d ~~~\n", is_default);
	p_macaddr = getenv("macaddr");
	printf("~~~ p_macaddr:%s ~~~\n", p_macaddr);
	printf("~~~ is_ble_stp:%d ~~~\n", is_ble_stp);

	printf("=========================GPIO INIT=====================\n");
	printf("GPIO61 initial to push GPIO28 on MT7915\n");
	// init gpio61 to gpio mode
	value = DRV_Reg32(GPIO61_MODE);
	value = value | (0x1 << 12);
	DRV_WriteReg32(GPIO61_MODE, value);
	value = DRV_Reg32(GPIO61_MODE);
	printf("GPIO61_MODE value: %x\n", value);

	// init gpio61 to output mode
	value = DRV_Reg32(GPIO61_DIR_MODE);
	value = value | (0x1 << 29);
	DRV_WriteReg32(GPIO61_DIR_MODE, value);
	value = DRV_Reg32(GPIO61_DIR_MODE);
	printf("GPIO61_DIR_MODE value: %x\n", value);

	// init gpio61 data to high
	value = DRV_Reg32(GPIO61_DOUT_MODE);
	value = value | (0x1 << 29);
	DRV_WriteReg32(GPIO61_DOUT_MODE, value);
	value = DRV_Reg32(GPIO61_DOUT_MODE);
	printf("GPIO61_DOUT_MODE value: %x\n", value);

	printf("GPIO83(PERST0) initial to signal trigger PCIE on MT7915\n");
	// init gpio83(PERST0) to gpio mode
	value = DRV_Reg32(GPIO83_MODE);
	value = value | (0x1 << 28);
	DRV_WriteReg32(GPIO83_MODE, value);
	value = DRV_Reg32(GPIO83_MODE);
	printf("GPIO83_MODE value: %x\n", value);

	// init gpio83(PERST0)  to output mode
	value = DRV_Reg32(GPIO83_DIR_MODE);
	value = value | (0x1 << 19);
	DRV_WriteReg32(GPIO83_DIR_MODE, value);
	value = DRV_Reg32(GPIO83_DIR_MODE);
	printf("GPIO83_DIR_MODE value: %x\n", value);

	// trigger gpio83(PERST0) outpur data to high
	value = DRV_Reg32(GPIO83_DOUT_MODE);
	value = value | (0x1 << 19);
	DRV_WriteReg32(GPIO83_DOUT_MODE, value);
	value = DRV_Reg32(GPIO83_DOUT_MODE);
	printf("GPIO83_DOUT_MODE value: %x\n", value);
	mdelay(100);

	printf("=========================UART_3 INIT=====================\n");
	mtk_2nd_serial_console()->start();
	mtk_2nd_serial_console()->setbrg();
	//FLOW 1: BT Power On
	printf("=========================FLOW 1=====================\n");
	memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
	if (is_ble_stp) {
		len = (sizeof(pw_on_correct_code)/sizeof(u8)) + STP_HEAD_FORMAT_LEN + STP_TAIL_FORMAT_LEN;
	}
	else {
		len = sizeof(pw_on_correct_code)/sizeof(u8);
	}
	ret = get_ble_tx_cmd(BLE_PW_ON_IDX, bt_tx_cmd);
	if(ret > 0)
	{
		num = ret;
		for (i = 0; i < num; i++)
		{
			mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
		}
	}

	i = 0;
	memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
	start_timep = get_timer(0);
	while (i < len) {
		if (mtk_2nd_serial_console()->tstc()){
			ch = mtk_2nd_serial_console()->getc();
			memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
			i++;
		}

		curr_timep = get_timer(0);
		if(curr_timep - start_timep > 1500)
			break;
	}
	ret = judge_rx_msg(BLE_PW_ON_IDX, bt_rx_msg);
	if(ret == CMD_RET_SUCCESS){
		curr_step_pass = TRUE;
		printf("[BT Power On Result] Success\n");
	}else {
		printf("len = %d, num = %d\n", LEN_HCI_CORRECT_POWER_ON_CODE, num);
		printf("[BT Power On]: Tx Cmd=");
		for (i = 0; i < num; i++)
		{
			printf("%02x ",bt_tx_cmd[i]);
		}
		printf("\n[BT Power On]: Rx Msg=");
		for (i = 0; i < LEN_HCI_CORRECT_POWER_ON_CODE; i++)
		{
			printf("%02x ",bt_rx_msg[i]);
		}
		printf("\n[BT Power On Result] Fail\n");
		curr_step_pass = FALSE;
	}
	//FLOW 2: HCI_RESET
	printf("\n=========================FLOW 2=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		if (is_ble_stp) {
			len = (sizeof(hic_reset_correct_code)/sizeof(u8))+ STP_HEAD_FORMAT_LEN + STP_TAIL_FORMAT_LEN;
		}
		else {
			len = sizeof(hic_reset_correct_code)/sizeof(u8);
		}
		ret = get_ble_tx_cmd(BLE_HCI_RESET_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
			}
		}

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				i++;
			}
			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}

		ret = judge_rx_msg(BLE_HCI_RESET_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HCI RESET Result] Success\n");
		}
		else{
			printf("len= %d, num= %d\n", LEN_HCI_CORRECT_RESET_CODE, num);
			printf("[HCI RESET]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n[HCI RESET]: Rx Msg=");
			for (i = 0; i < LEN_HCI_CORRECT_RESET_CODE; i++)
			{
				printf("%02x ",bt_rx_msg[i]);
			}
			printf("\n[HCI RESET Result] Fail\n");
			curr_step_pass = FALSE;
		}
	}
#if 1
	//Extend FLOW: HCI_LE_Set_BT_MAC_ADDR
	printf("\n=========================Extend FLOW=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		if (is_ble_stp) {
			len = (sizeof(hic_le_set_bt_mac_correct_code)/sizeof(u8))+ STP_HEAD_FORMAT_LEN + STP_TAIL_FORMAT_LEN;
		}
		else {
			len = sizeof(hic_le_set_bt_mac_correct_code)/sizeof(u8);
		}
		ret = get_ble_tx_cmd(BLE_HCI_SET_BT_MAC_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			for (i = 0; i < num; i++)
			{
				//putc(bt_tx_cmd[i]);
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
			}
		}

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		start_timep = get_timer(0);
		while (i < len)
		{
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}

		ret = judge_rx_msg(BLE_HCI_SET_BT_MAC_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS)
		{
			curr_step_pass = TRUE;
			printf("[HCI LE BT MAC ADDR Result] Success\n");
		}
		else
		{
			printf("len= %d, num= %d\n", LEN_HCI_LE_SET_BT_MAC_CODE, num);
			printf("[HCI LE_Set BT MAC ADDR]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n[HCI LE_Set BT MAC ADDR]: Rx Msg=");
			for (i = 0; i < LEN_HCI_LE_SET_BT_MAC_CODE; i++)
			{
				printf("%02x ",bt_rx_msg[i]);
			}
			printf("\n[HCI LE BT MAC ADDR Result] Fail\n");
			curr_step_pass = FALSE;

		}
	}
#endif
	//FLOW 3: HCI_LE_Set_Advertising_Parameters
	printf("\n=========================FLOW 3=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		if (is_ble_stp) {
			len = (sizeof(hic_le_set_adv_param_correct_code)/sizeof(u8))+ STP_HEAD_FORMAT_LEN + STP_TAIL_FORMAT_LEN;
		}
		else {
			len = sizeof(hic_le_set_adv_param_correct_code)/sizeof(u8);
		}
		ret = get_ble_tx_cmd(BLE_HCI_SET_ADV_PARAM_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
			}
		}

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}

		ret = judge_rx_msg(BLE_HCI_SET_ADV_PARAM_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HCI LE SET ADVERTISING PARAMETER Result] Success\n");
		}
		else{
			printf("len= %d, num= %d\n", LEN_HCI_LE_SET_ADV_PARAM_CODE, num);
			printf("[HCI LE_Set Advertising Parameters]: Tx Cmd =");
			for (i = 0; i < num; i++)
			{
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n[HCI LE_Set Advertising Parameters]: Rx Msg =");
			for (i = 0; i < LEN_HCI_LE_SET_ADV_PARAM_CODE; i++)
			{
				printf("%02x ",bt_rx_msg[i]);
			}
			printf("\n[HCI LE  SET ADVERTISING PARAMETER Result]  Fail\n");
			curr_step_pass = FALSE;
		}
	}

	//FLOW 4: HCI_LE_Set_Advertising_Data
	printf("\n=========================FLOW 4=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		if(is_ble_stp) {
			len = (sizeof(hic_le_set_adv_data_correct_code)/sizeof(u8))+ STP_HEAD_FORMAT_LEN + STP_TAIL_FORMAT_LEN;
		}
		else {
			len = sizeof(hic_le_set_adv_data_correct_code)/sizeof(u8);
		}
		ret = get_ble_tx_cmd(BLE_HCI_SET_ADV_DATA_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
			}
		}

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}

		ret = judge_rx_msg(BLE_HCI_SET_ADV_DATA_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HCI LE SET ADVERTISING DATA Result] Success\n");
		}
		else{
			printf("len= %d, num= %d\n", LEN_HCI_LE_SET_ADV_DATA_CODE, num);
			printf("[HCI LE_Set Advertising Data]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n[HCI LE_Set Advertising Data]: Rx Msg=");

			for (i = 0; i < LEN_HCI_LE_SET_ADV_DATA_CODE; i++)
			{
				printf("%02x ",bt_rx_msg[i]);
			}
			printf("\n[HCI LE  SET ADVERTISING DATA Result] Fail\n");
			curr_step_pass = FALSE;
		}
	}

	//FLOW 5: HCI_LE_Set_Scan_Response_Data
	printf("\n=========================FLOW 5=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		if (is_ble_stp) {
			len = (sizeof(hic_le_set_scan_resp_data_correct_code)/sizeof(u8))+ STP_HEAD_FORMAT_LEN + STP_TAIL_FORMAT_LEN;
		}
		else {
			len = sizeof(hic_le_set_scan_resp_data_correct_code)/sizeof(u8);
		}
		ret = get_ble_tx_cmd(BLE_HCI_SET_SCAN_RESP_DATA_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
			}
		}

		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		start_timep = get_timer(0);
		while (i < len) {
			if (mtk_2nd_serial_console()->tstc()){
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				i++;
			}

			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}

		ret = judge_rx_msg(BLE_HCI_SET_SCAN_RESP_DATA_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HCI LE SET SCAN RESPONSE Result] Success\n");
		}
		else{
			printf("len= %d, num= %d\n", LEN_HCI_LE_SET_SCAN_RSP_DATA_CODE, num);
			printf("[HCI LE_Set Scan Response Data]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n[HCI LE_Set Scan Response Data ]: Rx Msg=");
			for (i = 0; i < LEN_HCI_LE_SET_SCAN_RSP_DATA_CODE; i++)
			{
				printf("%02x ",bt_rx_msg[i]);
			}
			printf("\n[HCI LE  SET SCAN RESPONSE Result] Fail\n");
			curr_step_pass = FALSE;
		}

	}
	//FLOW 6: HCI_LE_Set_Advertising_Enable
	printf("\n=========================FLOW 6=====================\n");
	if(curr_step_pass == TRUE)
	{
		memset(bt_tx_cmd, 0x0, sizeof(bt_tx_cmd));
		if (is_ble_stp) {
			len = (sizeof(hic_le_set_adv_en_correct_code)/sizeof(u8))+ STP_HEAD_FORMAT_LEN + STP_TAIL_FORMAT_LEN;
		}
		else {
			len = sizeof(hic_le_set_adv_en_correct_code)/sizeof(u8);
		}
		ret = get_ble_tx_cmd(BLE_HCI_SET_ADV_ENABLE_IDX, bt_tx_cmd);
		if(ret > 0)
		{
			num = ret;
			for (i = 0; i < num; i++)
			{
				mtk_2nd_serial_console()->putc(bt_tx_cmd[i]);
			}
		}
		i = 0;
		memset(bt_rx_msg, 0x0, sizeof(bt_rx_msg));
		start_timep = get_timer(0);
		while (i <len) {
			if (mtk_2nd_serial_console()->tstc()) {
				ch = mtk_2nd_serial_console()->getc();
				memcpy(&bt_rx_msg[i], &ch, sizeof(ch));
				i++;
			}
			curr_timep = get_timer(0);
			if(curr_timep - start_timep > 1500)
				break;
		}

		ret = judge_rx_msg(BLE_HCI_SET_ADV_ENABLE_IDX, bt_rx_msg);
		if(ret == CMD_RET_SUCCESS){
			curr_step_pass = TRUE;
			printf("[HCI LE SET ADVERISTING ENABLE Result] Success\n");
		}
		else{
			printf("len= %d, num= %d\n", LEN_HCI_LE_ADV_EN_CODE, num);
			printf("[HCI LE_Set Advertising Enable]: Tx Cmd=");
			for (i = 0; i < num; i++)
			{
				printf("%02x ",bt_tx_cmd[i]);
			}
			printf("\n[HCI LE_Set Advertising Enable]: Rx Msg=");
			for (i = 0; i < LEN_HCI_LE_ADV_EN_CODE; i++)
			{
				printf("%02x ",bt_rx_msg[i]);
			}
			printf("\n[HCI LE  SET ADVERISTING ENABLE Result] Fail\n");
			curr_step_pass = FALSE;
		}
	}
	if(curr_step_pass == TRUE) {
		return CMD_RET_SUCCESS;
	}
	return CMD_RET_FAILURE;
}

void start_ble(void)
{
    do_ble_adveristing_start();
}


U_BOOT_CMD(mtk_ble_adv, 1, 0, do_mtk_ble_adv,
			"Test BLE Adv",
			"this feature is based on uart3 function enable\n"
);
