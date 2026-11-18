/*
 * clock.c - RT-Spark clock tree.
 *
 * Evidence: schematic sheet 5 (X2 = 8 MHz crystal on PH0/PH1, X1 = 32.768 kHz on
 * PC14/PC15) and the vendor CubeMX_Config.ioc (PLLM 4, PLLN 168, PLLP 2, PLLQ 7,
 * AHB /1, APB1 /4, APB2 /2). Result: SYSCLK = HCLK = 168 MHz, PCLK1 = 42 MHz
 * (APB1 timers 84 MHz), PCLK2 = 84 MHz (APB2 timers 168 MHz), 48 MHz for SDIO/USB.
 * Flash latency 5 WS at 3.3 V (RM0090 table 10), voltage scale 1.
 *
 * If the HSE fails to start, the same 168 MHz is built from HSI (16 MHz / 8 * 168 / 2)
 * and the condition is reported at boot and in diagnostics instead of hanging.
 */
#include "bsp.h"

sig_clock_info_t g_clock;

static bool config_pll(uint32_t source)
{
    RCC_OscInitTypeDef osc = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    if (source == RCC_PLLSOURCE_HSE) {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
        osc.HSEState = RCC_HSE_ON;
        osc.PLL.PLLM = 4;           /* 8 MHz / 4 = 2 MHz VCO input */
    } else {
        osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
        osc.HSIState = RCC_HSI_ON;
        osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
        osc.PLL.PLLM = 8;           /* 16 MHz / 8 = 2 MHz VCO input */
    }
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = source;
    osc.PLL.PLLN = 168;             /* VCO 336 MHz */
    osc.PLL.PLLP = RCC_PLLP_DIV2;   /* 168 MHz */
    osc.PLL.PLLQ = 7;               /* 48 MHz */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        return false;
    }

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    return HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) == HAL_OK;
}

void sig_clock_init(void)
{
    g_clock.hse_ok = config_pll(RCC_PLLSOURCE_HSE);
    if (!g_clock.hse_ok) {
        /* Return to HSI as SYSCLK, disable the PLL, then rebuild from HSI. */
        HAL_RCC_DeInit();
        if (!config_pll(RCC_PLLSOURCE_HSI)) {
            sig_fatal_early("clock: PLL failed on both HSE and HSI");
        }
    }
    SystemCoreClockUpdate();
    g_clock.sysclk_hz = HAL_RCC_GetSysClockFreq();
    g_clock.hclk_hz = HAL_RCC_GetHCLKFreq();
    g_clock.pclk1_hz = HAL_RCC_GetPCLK1Freq();
    g_clock.pclk2_hz = HAL_RCC_GetPCLK2Freq();
}
