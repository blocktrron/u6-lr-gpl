#ifndef __CUST_GPIO_USAGE_H__
#define __CUST_GPIO_USAGE_H__


#define GPIO_NFC_EINT_PIN         (GPIO2 | 0x80000000)
#define GPIO_NFC_EINT_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_NFC_EINT_PIN_M_EINT  GPIO_NFC_EINT_PIN_M_GPIO

#define GPIO_OTG_IDDIG_EINT_PIN         (GPIO38 | 0x80000000)
#define GPIO_OTG_IDDIG_EINT_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_OTG_IDDIG_EINT_PIN_M_KCOL  GPIO_MODE_01
#define GPIO_OTG_IDDIG_EINT_PIN_M_IDDIG   GPIO_MODE_02
#define GPIO_OTG_IDDIG_EINT_PIN_M_EXT_FRAME_SYNC   GPIO_MODE_03
#define GPIO_OTG_IDDIG_EINT_PIN_M_DBG_MON_B   GPIO_MODE_07

#define GPIO_MSDC1_INSI         (GPIO50 | 0x80000000)
#define GPIO_MSDC1_INSI_M_GPIO  GPIO_MODE_00
#define GPIO_MSDC1_INSI_M_CLK  GPIO_MODE_02
#define GPIO_MSDC1_INSI_M_EINT  GPIO_MSDC1_INSI_M_GPIO
#define GPIO_MSDC1_INSI_CLK     CLK_OUT3
#define GPIO_MSDC1_INSI_FREQ    GPIO_CLKSRC_NONE

#define GPIO_MSDC2_INSI         (GPIO51 | 0x80000000)
#define GPIO_MSDC2_INSI_M_GPIO  GPIO_MODE_00
#define GPIO_MSDC2_INSI_M_CLK  GPIO_MODE_04
#define GPIO_MSDC2_INSI_M_EINT  GPIO_MSDC2_INSI_M_GPIO
#define GPIO_MSDC2_INSI_CLK     CLK_OUT1
#define GPIO_MSDC2_INSI_FREQ    GPIO_CLKSRC_NONE

#define GPIO_I2C1_SDA_PIN         (GPIO57 | 0x80000000)
#define GPIO_I2C1_SDA_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_I2C1_SDA_PIN_M_SDA   GPIO_MODE_01

#define GPIO_I2C1_SCA_PIN         (GPIO58 | 0x80000000)
#define GPIO_I2C1_SCA_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_I2C1_SCA_PIN_M_SCL   GPIO_MODE_01

#define GPIO_I2C0_SDA_PIN         (GPIO75 | 0x80000000)
#define GPIO_I2C0_SDA_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_I2C0_SDA_PIN_M_SDA   GPIO_MODE_01

#define GPIO_I2C0_SCA_PIN         (GPIO76 | 0x80000000)
#define GPIO_I2C0_SCA_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_I2C0_SCA_PIN_M_SCL   GPIO_MODE_01

#define GPIO_UART_URXD1_PIN         (GPIO79 | 0x80000000)
#define GPIO_UART_URXD1_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_UART_URXD1_PIN_M_URXD   GPIO_MODE_01
#define GPIO_UART_URXD1_PIN_M_UTXD   GPIO_MODE_02

#define GPIO_UART_UTXD1_PIN         (GPIO80 | 0x80000000)
#define GPIO_UART_UTXD1_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_UART_UTXD1_PIN_M_UTXD   GPIO_MODE_01
#define GPIO_UART_UTXD1_PIN_M_URXD   GPIO_MODE_02


/*Output for default variable names*/
/*@XXX_XX_PIN in gpio.cmp          */



#define GPIO_ACCDET_EINT_PIN         (GPIOEXT14 | 0x80000000)
#define GPIO_ACCDET_EINT_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_ACCDET_EINT_PIN_M_EINT  GPIO_MODE_02

#define GPIO_SWCHARGER_EN_PIN         (GPIOEXT19 | 0x80000000)
#define GPIO_SWCHARGER_EN_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_SWCHARGER_EN_PIN_M_EINT  GPIO_MODE_02
#define GPIO_SWCHARGER_EN_PIN_M_PWM  GPIO_MODE_03

#define GPIO_CHR_CE_PIN         (GPIOEXT19 | 0x80000000)
#define GPIO_CHR_CE_PIN_M_GPIO  GPIO_MODE_00
#define GPIO_CHR_CE_PIN_M_EINT  GPIO_MODE_02
#define GPIO_CHR_CE_PIN_M_PWM  GPIO_MODE_03


/*Output for default variable names*/
/*@XXX_XX_PIN in gpio.cmp          */



#endif /* __CUST_GPIO_USAGE_H__ */


