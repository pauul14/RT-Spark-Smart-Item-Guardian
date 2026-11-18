/*
 * board_pins.h - RT-Spark (Spark-1) pin map used by Smart Item Guardian.
 *
 * Every entry is taken from the vendor schematic
 *   "星火1号开发板原理图_V1_0.pdf" (KiCad, sheets marked Rev HW1.1, 2023-06-10),
 *   SHA-256 1442c30c...57d3f, see docs/hardware/reference/SHA256SUMS.txt
 * and cross-checked against the vendor BSP (RT-Thread bsp/stm32/stm32f407-rt-spark:
 * board/Kconfig, CubeMX_Config.ioc, ports/lcd/drv_lcd.c, ports/led_matrix) and the
 * RT-Spark SDK examples (02_basic_key, 02_basic_irq_beep).
 * "Sheet n" refers to the schematic page id printed in the title block.
 * Physical presence of each part still has to be confirmed in hardware session 1.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "stm32f4xx_hal.h"

/* ---- Clocks (sheet 5) ---------------------------------------------------- */
/* X2 8 MHz crystal on PH0/PH1 (HSE), X1 32.768 kHz crystal on PC14/PC15 (LSE). */

/* ---- Console: ST-LINK/V2.1 virtual COM port (sheet 12 VCP_TXD/RXD) ------ */
#define CONSOLE_UART            USART1
#define CONSOLE_TX_PORT         GPIOA
#define CONSOLE_TX_PIN          GPIO_PIN_9      /* USART1_TX AF7 */
#define CONSOLE_RX_PORT         GPIOA
#define CONSOLE_RX_PIN          GPIO_PIN_10     /* USART1_RX AF7 */

/* ---- LEDs D4 (sheet 6): anode to +3V3 via 2K, cathode on GPIO => active LOW */
#define LED_R_PORT              GPIOF
#define LED_R_PIN               GPIO_PIN_12     /* GPIO_LED_R */
#define LED_B_PORT              GPIOF
#define LED_B_PIN               GPIO_PIN_11     /* GPIO_LED_B (vendor README calls it green) */

/* ---- Buzzer BUZ1 (sheet 6): active buzzer via SS8050, base R20 1K => HIGH = on */
#define BUZZER_PORT             GPIOB
#define BUZZER_PIN              GPIO_PIN_0      /* GPIO_BUZZER */

/* ---- Arrow keys SW2..SW5 (sheet 6): to GND, no external pull-up => input pull-up, LOW = pressed */
#define KEY_UP_PORT             GPIOC
#define KEY_UP_PIN              GPIO_PIN_5      /* GPIO_BTN_UP   -> EXTI5 (EXTI9_5_IRQn)  */
#define KEY_DOWN_PORT           GPIOC
#define KEY_DOWN_PIN            GPIO_PIN_1      /* GPIO_BTN_DOWN -> EXTI1 */
#define KEY_LEFT_PORT           GPIOC
#define KEY_LEFT_PIN            GPIO_PIN_0      /* GPIO_BTN_LEFT -> EXTI0 */
#define KEY_RIGHT_PORT          GPIOC
#define KEY_RIGHT_PIN           GPIO_PIN_4      /* GPIO_BTN_RIGHT-> EXTI4 */
/* SW6 is the reset key on NRST (not readable by firmware). */

/* ---- Hardware I2C2 bus (sheet 5 R1/R2 5.1K pull-ups) --------------------- */
#define I2C2_SCL_PORT           GPIOF
#define I2C2_SCL_PIN            GPIO_PIN_1      /* I2C2_SCL AF4 */
#define I2C2_SDA_PORT           GPIOF
#define I2C2_SDA_PIN            GPIO_PIN_0      /* I2C2_SDA AF4 */
#define ICM20608_I2C_ADDR       0x68u           /* sheet 6 "I2C ADDR: 0x68" */
#define AP3216C_I2C_ADDR        0x1Eu           /* sheet 6 "I2C ADDR: 0x1E" */
#define ES8388_I2C_ADDR         0x10u           /* sheet 9 "I2C ADDR: 0x10" */
#define ICM_INT_PORT            GPIOG
#define ICM_INT_PIN             GPIO_PIN_12     /* GPIO_ICM_INT -> EXTI12 (EXTI15_10_IRQn) */
#define APS_INT_PORT            GPIOG
#define APS_INT_PIN             GPIO_PIN_13     /* GPIO_APS_INT (read as level only) */

/* ---- AHT21 on software I2C (sheet 5 R6/R7 5.1K pull-ups) ----------------- */
#define AHT_SCL_PORT            GPIOE
#define AHT_SCL_PIN             GPIO_PIN_0      /* GPIO_AHT_SCL */
#define AHT_SDA_PORT            GPIOE
#define AHT_SDA_PIN             GPIO_PIN_1      /* GPIO_AHT_SDA */
#define AHT21_I2C_ADDR          0x38u           /* sheet 6 "I2C ADDR: 0x38" */

/* ---- LCD ST7789V3 1.3" 240x240 on FSMC (sheet 8) ------------------------- */
/* 8-bit 8080 bus: D0..D7 = PD14 PD15 PD0 PD1 PE7 PE8 PE9 PE10, NOE PD4, NWE PD5,
 * CS = FSMC_NE3 (PG10), DC = FSMC_A18 (PD13). Bank1 sub-bank 3 base 0x68000000.
 * With an 8-bit bus HADDR[25:0] maps straight to A[25:0], so A18 = 0 selects command. */
#define LCD_CMD_ADDR            (0x6803FFFEu)   /* A18 = 0 */
#define LCD_DATA_ADDR           (0x68040000u)   /* A18 = 1 */
#define LCD_RST_PORT            GPIOD
#define LCD_RST_PIN             GPIO_PIN_3      /* GPIO_LCD_RESET */
#define LCD_BL_PORT             GPIOF
#define LCD_BL_PIN              GPIO_PIN_9      /* GPIO_LCD_BL -> Q3 SS8050 => HIGH = backlight on */
#define LCD_TE_PORT             GPIOD
#define LCD_TE_PIN              GPIO_PIN_6      /* GPIO_LCD_TE (unused) */

/* ---- LED matrix: 19 x SK6805 (sheet 6) ----------------------------------- */
/* Data PA7 (TIM3_CH2 AF2, DMA1 Stream5 Ch5) through 74LVC1G125 U6.
 * U6 ~OE = GPIO_MATRIX_EN (PF2) with R14 10K pull-up => drive LOW to enable. */
#define MATRIX_DATA_PORT        GPIOA
#define MATRIX_DATA_PIN         GPIO_PIN_7
#define MATRIX_EN_PORT          GPIOF
#define MATRIX_EN_PIN           GPIO_PIN_2
#define MATRIX_LED_COUNT        19

/* ---- Infrared (sheet 6) -------------------------------------------------- */
#define IR_TX_PORT              GPIOB
#define IR_TX_PIN               GPIO_PIN_1      /* GPIO_IR_TRANSMEET -> Q2 -> IR LED D6; TIM8_CH3N AF3 */
#define IR_RX_PORT              GPIOF
#define IR_RX_PIN               GPIO_PIN_8      /* GPIO_IR_RECV <- IRM-H638T (38 kHz), LOW = carrier */

/* ---- SPI2 bus (sheet 5/7/10) --------------------------------------------- */
#define SPI2_SCK_PORT           GPIOB
#define SPI2_SCK_PIN            GPIO_PIN_13     /* AF5 */
#define SPI2_MISO_PORT          GPIOC
#define SPI2_MISO_PIN           GPIO_PIN_2      /* AF5 */
#define SPI2_MOSI_PORT          GPIOC
#define SPI2_MOSI_PIN           GPIO_PIN_3      /* AF5 */
#define FLASH_CS_PORT           GPIOB
#define FLASH_CS_PIN            GPIO_PIN_12     /* SPI2_CS0 -> W25Q128 U10 /CE (R31 10K pull-up) */
#define WIFI_CS_PORT            GPIOF
#define WIFI_CS_PIN             GPIO_PIN_10     /* GPIO_SPI2_CS1 -> RW007 nCS/BOOT1: keep HIGH */
#define WIFI_INT_PORT           GPIOG
#define WIFI_INT_PIN            GPIO_PIN_11     /* GPIO_WIFI_INT -> RW007 INT/BUSY */
#define WIFI_RST_PORT           GPIOG
#define WIFI_RST_PIN            GPIO_PIN_15     /* GPIO_WIFI_RST -> RW007 nRST */

/* ---- microSD (sheet 7): SDIO 4-bit, D0..D3 PC8..PC11, CK PC12, CMD PD2 --- */
#define SD_CD_PORT              GPIOF
#define SD_CD_PIN               GPIO_PIN_3      /* GPIO_CARD_DETECT: 47K pull-up (R24..R29 group),
                                                   CD_SW to shell/GND => LOW expected when a card is
                                                   inserted (polarity to be confirmed in session 1) */

/* ---- CAN1 (sheet 11): TJA1042, CAN1_RX PB8, CAN1_TX PB9 (AF9) ------------ */
/* ---- RS485 (sheet 11): SP3485, USART6 TX PG14 / RX PG9, RE/DE PG8 -------- */
#define RS485_DE_PORT           GPIOG
#define RS485_DE_PIN            GPIO_PIN_8      /* held LOW (receive) - no bus attached */

#endif /* BOARD_PINS_H */
