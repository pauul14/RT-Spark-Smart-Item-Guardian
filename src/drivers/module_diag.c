/*
 * module_diag.c - see module_diag.h. Pin assignments: schematic sheets 5-11
 * (docs/hardware/hardware_function_matrix.md lists the evidence per module).
 * All tests run from the health task (priority 2) and bound their own runtime.
 */
#include "module_diag.h"
#include "board_pins.h"
#include "board_io.h"
#include "i2c_buses.h"
#include "spi_flash.h"
#include "sd_card.h"
#include "ir_nec.h"
#include "bsp.h"
#include "mini_printf.h"
#include "FreeRTOS.h"
#include "task.h"

const char *diag_result_name(diag_result_t r)
{
    switch (r) {
    case DIAG_PASS: return "PASS";
    case DIAG_FAIL: return "FAIL";
    case DIAG_WARN: return "WARN";
    default:        return "N/A";
    }
}

/* ============================ microSD ===================================== */
/* Read-only check through the shared SD driver: (re)init, card info, block-0 read.
 * The event log (src/app/sd_logger.c) remounts afterwards. */
static uint8_t s_sd_block[512] __attribute__((aligned(4)));   /* SRAM */

diag_result_t diag_sdcard(char *out, int n)
{
    sd_info_t si;
    if (!sd_card_init(&si)) {
        sig_snprintf(out, (size_t)n, "SD: CD=%d, init failed (err 0x%08lx) - no card or unreadable",
                     si.cd_level, (unsigned long)si.init_error);
        return DIAG_NA;
    }
    bool rd = sd_card_read(0, s_sd_block, 1);
    bool sig = rd && s_sd_block[510] == 0x55 && s_sd_block[511] == 0xAA;
    sig_snprintf(out, (size_t)n, "SD: CD=%d type=%lu capacity=%luMB bus=%s block0_read=%s boot_sig=%s",
                 si.cd_level, (unsigned long)si.card_type, (unsigned long)(si.blocks / 2048u),
                 si.wide_bus ? "4bit" : "1bit", rd ? "ok" : "fail", sig ? "55AA" : "absent");
    return rd ? DIAG_PASS : DIAG_FAIL;
}

/* ============================ Infrared ==================================== */
/* Sends this product's own NEC TEST frame and checks whether the board's receiver
 * DECODED it (needs a reflector, e.g. a white card ~5 cm above both parts). */
diag_result_t diag_ir(char *out, int n)
{
    int idle = ir_rx_level();
    uint32_t e0 = ir_rx_test_echoes(), edges0 = ir_rx_edges();
    bool sent = false;
    for (int attempt = 0; attempt < 3 && ir_rx_test_echoes() == e0; attempt++) {
        uint32_t t0 = HAL_GetTick();
        while (ir_tx_busy() && (HAL_GetTick() - t0) < 200) vTaskDelay(pdMS_TO_TICKS(5));
        sent = ir_tx_send(IR_RELAY_ADDR, IR_CMD_TEST) || sent;
        vTaskDelay(pdMS_TO_TICKS(250));            /* 68 ms frame + UI-task decode */
    }
    uint32_t echoes = ir_rx_test_echoes() - e0;
    sig_snprintf(out, (size_t)n, "IR: rx_idle=%s tx=%s decoded_self_echo=%lu rx_edges=%lu frames_total=%lu "
                 "decode_errors=%lu (echo needs a reflector above the board)", idle ? "HIGH(ok)" : "LOW(!)",
                 sent ? "sent" : "busy", (unsigned long)echoes, (unsigned long)(ir_rx_edges() - edges0),
                 (unsigned long)ir_rx_frames(), (unsigned long)ir_rx_errors());
    if (!idle) return DIAG_FAIL;
    return echoes ? DIAG_PASS : DIAG_WARN;
}

/* ============================ RW007 Wi-Fi ================================= */
diag_result_t diag_rw007(char *out, int n)
{
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = WIFI_INT_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(WIFI_INT_PORT, &g);
    int before = HAL_GPIO_ReadPin(WIFI_INT_PORT, WIFI_INT_PIN) == GPIO_PIN_SET;

    if (!spi2_lock(200)) { sig_snprintf(out, (size_t)n, "RW007: SPI2 busy"); return DIAG_WARN; }
    /* SCK idles low (CPOL 0) and nCS/BOOT1 is held high by spi2_init: the boot
     * straps the vendor rw007 package uses for SPI mode. Lock keeps SCK quiet. */
    g.Pin = WIFI_RST_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
    HAL_GPIO_Init(WIFI_RST_PORT, &g);
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(20));
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
    int last = HAL_GPIO_ReadPin(WIFI_INT_PORT, WIFI_INT_PIN) == GPIO_PIN_SET;
    uint32_t transitions = 0, first_ms = 0;
    for (uint32_t ms = 1; ms <= 1500; ms++) {
        vTaskDelay(1);
        int v = HAL_GPIO_ReadPin(WIFI_INT_PORT, WIFI_INT_PIN) == GPIO_PIN_SET;
        if (v != last) { transitions++; if (!first_ms) first_ms = ms; last = v; }
    }
    spi2_unlock();
    sig_snprintf(out, (size_t)n, "RW007: INT before=%d after=%d transitions_1.5s=%lu first_at=%lums "
                 "(presence/boot signal only; no network joined)", before, last,
                 (unsigned long)transitions, (unsigned long)first_ms);
    return transitions > 0 ? DIAG_PASS : DIAG_WARN;
}

/* ============================ ES8388 codec ================================ */
diag_result_t diag_es8388(char *out, int n)
{
    uint8_t r[5] = {0};
    if (!i2c2_probe(ES8388_I2C_ADDR)) {
        sig_snprintf(out, (size_t)n, "ES8388: no ACK at 0x10");
        return DIAG_FAIL;
    }
    for (int i = 0; i < 5; i++) {
        if (i2c2_mem_read(ES8388_I2C_ADDR, (uint8_t)i, &r[i], 1) != I2C_OK) {
            sig_snprintf(out, (size_t)n, "ES8388: ACK but register %d read failed", i);
            return DIAG_WARN;
        }
    }
    sig_snprintf(out, (size_t)n, "ES8388: ACK 0x10, reg00..04 = %02x %02x %02x %02x %02x (read-only; audio path unused)",
                 r[0], r[1], r[2], r[3], r[4]);
    return DIAG_PASS;
}

/* ============================ CAN1 loop-back ============================== */
static void can_pins(bool af)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9;          /* PB8 CAN1_RX, PB9 CAN1_TX */
    g.Pull = GPIO_PULLUP;                     /* idle: TJA1042 TXD high = recessive */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Mode = af ? GPIO_MODE_AF_PP : GPIO_MODE_INPUT;
    g.Alternate = af ? GPIO_AF9_CAN1 : 0;
    HAL_GPIO_Init(GPIOB, &g);
}

/* Silent loop-back: the frame never reaches the bus (TX held recessive). Each step
 * is reported, so a failure names where it stopped (hardware finding 2026-11-18:
 * the first version only said "not received"). */
diag_result_t diag_can_loopback(char *out, int n)
{
    CAN_HandleTypeDef h = {0};
    const char *step = "init";
    uint32_t err = 0, esr = 0;
    bool match = false;
    __HAL_RCC_CAN1_CLK_ENABLE();
    can_pins(true);
    h.Instance = CAN1;
    h.Init.Prescaler = 6;                        /* 42 MHz / 6 / 14 tq = 500 kbit/s */
    h.Init.Mode = CAN_MODE_SILENT_LOOPBACK;
    h.Init.SyncJumpWidth = CAN_SJW_1TQ;
    h.Init.TimeSeg1 = CAN_BS1_11TQ;
    h.Init.TimeSeg2 = CAN_BS2_2TQ;
    h.Init.TimeTriggeredMode = DISABLE;
    h.Init.AutoBusOff = DISABLE;
    h.Init.AutoWakeUp = DISABLE;
    h.Init.AutoRetransmission = DISABLE;
    h.Init.ReceiveFifoLocked = DISABLE;
    h.Init.TransmitFifoPriority = DISABLE;
    if (HAL_CAN_Init(&h) == HAL_OK) {
        step = "filter";
        CAN_FilterTypeDef f = {0};
        f.FilterBank = 0;
        f.FilterMode = CAN_FILTERMODE_IDMASK;
        f.FilterScale = CAN_FILTERSCALE_32BIT;
        f.FilterFIFOAssignment = CAN_RX_FIFO0;
        f.FilterActivation = ENABLE;
        f.SlaveStartFilterBank = 14;
        if (HAL_CAN_ConfigFilter(&h, &f) == HAL_OK) {
            step = "start";
            if (HAL_CAN_Start(&h) == HAL_OK) {
                step = "tx";
                CAN_TxHeaderTypeDef th = {0};
                th.StdId = 0x5A1;
                th.IDE = CAN_ID_STD;
                th.RTR = CAN_RTR_DATA;
                th.DLC = 8;
                uint8_t tx[8] = {'S', 'I', 'G', 'C', 'A', 'N', 0x55, 0xAA}, rx[8] = {0};
                uint32_t mb;
                if (HAL_CAN_AddTxMessage(&h, &th, tx, &mb) == HAL_OK) {
                    step = "rx";
                    uint32_t t0 = HAL_GetTick();
                    while (HAL_CAN_GetRxFifoFillLevel(&h, CAN_RX_FIFO0) == 0 && (HAL_GetTick() - t0) < 50) { }
                    CAN_RxHeaderTypeDef rh = {0};
                    if (HAL_CAN_GetRxFifoFillLevel(&h, CAN_RX_FIFO0) > 0 &&
                        HAL_CAN_GetRxMessage(&h, CAN_RX_FIFO0, &rh, rx) == HAL_OK) {
                        step = "compare";
                        match = rh.StdId == th.StdId && rh.DLC == 8;
                        for (int i = 0; match && i < 8; i++) match = rx[i] == tx[i];
                        if (match) step = "done";
                    }
                }
            }
        }
    }
    err = HAL_CAN_GetError(&h);
    esr = CAN1->ESR;
    uint32_t tsr = CAN1->TSR, msr = CAN1->MSR;
    HAL_CAN_Stop(&h);
    HAL_CAN_DeInit(&h);
    can_pins(false);                             /* back to pulled-up inputs (recessive) */
    sig_snprintf(out, (size_t)n, "CAN1: silent loop-back 500k %s (stopped at %s; hal_err=0x%08lx esr=0x%08lx "
                 "tsr=0x%08lx msr=0x%08lx) - MCU controller only, no bus traffic",
                 match ? "frame echoed OK" : "FAILED", step, (unsigned long)err, (unsigned long)esr,
                 (unsigned long)tsr, (unsigned long)msr);
    return match ? DIAG_PASS : DIAG_FAIL;
}

/* ============================ MCU ADC ===================================== */
#define VREFINT_CAL (*(const uint16_t *)0x1FFF7A2Au)   /* DS8626 6.3.22/6.3.23 */
#define TS_CAL1     (*(const uint16_t *)0x1FFF7A2Cu)   /* 30 C  @ 3.3 V */
#define TS_CAL2     (*(const uint16_t *)0x1FFF7A2Eu)   /* 110 C @ 3.3 V */

static uint32_t adc_read(ADC_HandleTypeDef *h, uint32_t ch)
{
    ADC_ChannelConfTypeDef c = {0};
    c.Channel = ch;
    c.Rank = 1;
    c.SamplingTime = ADC_SAMPLETIME_480CYCLES;   /* >= 10 us for the temperature sensor */
    HAL_ADC_ConfigChannel(h, &c);
    uint32_t acc = 0;
    for (int i = 0; i < 8; i++) {
        HAL_ADC_Start(h);
        HAL_ADC_PollForConversion(h, 5);
        acc += HAL_ADC_GetValue(h);
    }
    HAL_ADC_Stop(h);
    return acc / 8;
}

diag_result_t diag_mcu_adc(char *out, int n, float *temp_c, float *vdda_v)
{
    static ADC_HandleTypeDef h;
    static bool inited;
    if (!inited) {
        __HAL_RCC_ADC1_CLK_ENABLE();
        h.Instance = ADC1;
        h.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;   /* 84 / 4 = 21 MHz */
        h.Init.Resolution = ADC_RESOLUTION_12B;
        h.Init.ScanConvMode = DISABLE;
        h.Init.ContinuousConvMode = DISABLE;
        h.Init.DiscontinuousConvMode = DISABLE;
        h.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
        h.Init.ExternalTrigConv = ADC_SOFTWARE_START;
        h.Init.DataAlign = ADC_DATAALIGN_RIGHT;
        h.Init.NbrOfConversion = 1;
        h.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
        if (HAL_ADC_Init(&h) != HAL_OK) {
            sig_snprintf(out, (size_t)n, "MCU ADC init failed");
            return DIAG_FAIL;
        }
        inited = true;
    }
    uint32_t vref = adc_read(&h, ADC_CHANNEL_VREFINT);
    uint32_t ts = adc_read(&h, ADC_CHANNEL_TEMPSENSOR);
    bool cal_ok = VREFINT_CAL > 1300 && VREFINT_CAL < 1700 && TS_CAL1 > 700 && TS_CAL2 > TS_CAL1 && TS_CAL2 < 1400;
    float vdda = vref ? 3.3f * (float)(cal_ok ? VREFINT_CAL : 1489u) / (float)vref : 0.0f;
    float t;
    if (cal_ok) {
        float ts33 = (float)ts * vdda / 3.3f;           /* normalise to the 3.3 V calibration */
        t = 30.0f + (ts33 - (float)TS_CAL1) * 80.0f / (float)(TS_CAL2 - TS_CAL1);
    } else {
        float v = (float)ts * vdda / 4095.0f;
        t = (v - 0.76f) / 0.0025f + 25.0f;              /* DS8626 typical V25 / Avg_Slope */
    }
    if (temp_c) *temp_c = t;
    if (vdda_v) *vdda_v = vdda;
    sig_snprintf(out, (size_t)n, "MCU: die_temp=%.1fC vdda=%.3fV (%s calibration)", (double)t, (double)vdda,
                 cal_ok ? "factory" : "typical");
    return (vdda > 2.9f && vdda < 3.6f) ? DIAG_PASS : DIAG_WARN;
}
