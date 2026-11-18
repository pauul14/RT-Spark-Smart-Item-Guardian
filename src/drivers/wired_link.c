/*
 * wired_link.c - see wired_link.h. Used only from the health task (priority 2),
 * except the USART6 RX ISR which only fills a byte ring.
 */
#include "wired_link.h"
#include "board_pins.h"
#include "bsp.h"
#include "mini_printf.h"
#include <string.h>

static CAN_HandleTypeDef s_can;
static UART_HandleTypeDef s_u6;
static wired_stats_t s_st;
static uint8_t s_hb_seq;

#define RX_RING 64u
static volatile uint8_t s_rx[RX_RING];
static volatile uint32_t s_rx_head;
static uint32_t s_rx_tail;
static char s_line[24];
static uint32_t s_line_len;
static bool s_query;

uint8_t wired_nmea_checksum(const char *body)
{
    uint8_t c = 0;
    while (*body) c ^= (uint8_t)*body++;
    return c;
}

/* ---------------------------------------------------------------- CAN ---- */
static void can_stop(void)
{
    if (s_can.Instance) {
        HAL_CAN_Stop(&s_can);
        HAL_CAN_DeInit(&s_can);
        GPIO_InitTypeDef g = {0};                 /* idle pins: pulled-up inputs (TJA1042 recessive) */
        g.Pin = GPIO_PIN_8 | GPIO_PIN_9;
        g.Mode = GPIO_MODE_INPUT;
        g.Pull = GPIO_PULLUP;
        HAL_GPIO_Init(GPIOB, &g);
    }
    s_can.Instance = NULL;
}

static bool can_start(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_CAN1_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_8 | GPIO_PIN_9;              /* PB8 CAN1_RX, PB9 CAN1_TX (schematic sheet 5/11) */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF9_CAN1;
    HAL_GPIO_Init(GPIOB, &g);
    memset(&s_can, 0, sizeof s_can);
    s_can.Instance = CAN1;
    s_can.Init.Prescaler = 6;                     /* 42 MHz / 6 / 14 tq = 500 kbit/s, SP 85.7 % */
    s_can.Init.Mode = CAN_MODE_NORMAL;
    s_can.Init.SyncJumpWidth = CAN_SJW_1TQ;
    s_can.Init.TimeSeg1 = CAN_BS1_11TQ;
    s_can.Init.TimeSeg2 = CAN_BS2_2TQ;
    s_can.Init.TimeTriggeredMode = DISABLE;
    s_can.Init.AutoBusOff = ENABLE;
    s_can.Init.AutoWakeUp = DISABLE;
    s_can.Init.AutoRetransmission = DISABLE;      /* never flood a bus with unacknowledged retries */
    s_can.Init.ReceiveFifoLocked = DISABLE;
    s_can.Init.TransmitFifoPriority = ENABLE;
    if (HAL_CAN_Init(&s_can) != HAL_OK) { s_can.Instance = NULL; return false; }
    CAN_FilterTypeDef f = {0};
    f.FilterBank = 0;
    f.FilterMode = CAN_FILTERMODE_IDMASK;
    f.FilterScale = CAN_FILTERSCALE_32BIT;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;
    f.FilterActivation = ENABLE;
    f.SlaveStartFilterBank = 14;
    HAL_CAN_ConfigFilter(&s_can, &f);
    if (HAL_CAN_Start(&s_can) != HAL_OK) { can_stop(); return false; }
    return true;
}

static void can_update_errors(void)
{
    if (!s_can.Instance) return;
    uint32_t esr = CAN1->ESR;
    s_st.can_tec = (uint8_t)((esr & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos);
    s_st.can_rec = (uint8_t)((esr & CAN_ESR_REC) >> CAN_ESR_REC_Pos);
    s_st.can_lec = (uint8_t)((esr & CAN_ESR_LEC) >> CAN_ESR_LEC_Pos);
    s_st.can_error_passive = (esr & CAN_ESR_EPVF) != 0;
    s_st.can_bus_off = (esr & CAN_ESR_BOFF) != 0;
    /* Completed mailboxes: TXOKx = acknowledged by a partner, else failed (no ACK). */
    uint32_t tsr = CAN1->TSR;
    static const uint32_t rq[3] = { CAN_TSR_RQCP0, CAN_TSR_RQCP1, CAN_TSR_RQCP2 };
    static const uint32_t ok[3] = { CAN_TSR_TXOK0, CAN_TSR_TXOK1, CAN_TSR_TXOK2 };
    for (int i = 0; i < 3; i++) {
        if (tsr & rq[i]) {
            if (tsr & ok[i]) s_st.can_tx_ok++; else { s_st.can_tx_fail++; if (s_st.can_lec == 3) s_st.can_ack_err++; }
            CAN1->TSR = rq[i];                    /* write 1 to clear */
        }
    }
}

static void can_send(uint16_t id, const uint8_t d[8])
{
    if (!s_can.Instance) return;
    can_update_errors();
    if (HAL_CAN_GetTxMailboxesFreeLevel(&s_can) == 0) { s_st.can_busy++; return; }
    CAN_TxHeaderTypeDef h = {0};
    h.StdId = id;
    h.IDE = CAN_ID_STD;
    h.RTR = CAN_RTR_DATA;
    h.DLC = 8;
    uint32_t mb;
    if (HAL_CAN_AddTxMessage(&s_can, &h, (uint8_t *)d, &mb) != HAL_OK) s_st.can_busy++;
}

/* -------------------------------------------------------------- RS485 ---- */
void USART6_IRQHandler(void)
{
    uint32_t sr = USART6->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        uint8_t b = (uint8_t)USART6->DR;
        if (sr & USART_SR_RXNE) { s_rx[s_rx_head % RX_RING] = b; s_rx_head++; }
    }
}

static void rs485_stop(void)
{
    HAL_NVIC_DisableIRQ(USART6_IRQn);
    if (s_u6.Instance) HAL_UART_DeInit(&s_u6);
    s_u6.Instance = NULL;
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);
}

static bool rs485_start(void)
{
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_USART6_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_14 | GPIO_PIN_9;             /* PG14 USART6_TX, PG9 USART6_RX (sheet 5/11) */
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(GPIOG, &g);
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);   /* receive */
    memset(&s_u6, 0, sizeof s_u6);
    s_u6.Instance = USART6;
    s_u6.Init.BaudRate = 115200;
    s_u6.Init.WordLength = UART_WORDLENGTH_8B;
    s_u6.Init.StopBits = UART_STOPBITS_1;
    s_u6.Init.Parity = UART_PARITY_NONE;
    s_u6.Init.Mode = UART_MODE_TX_RX;
    s_u6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_u6.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&s_u6) != HAL_OK) { s_u6.Instance = NULL; return false; }
    USART6->CR1 |= USART_CR1_RXNEIE;
    HAL_NVIC_SetPriority(USART6_IRQn, IRQ_PRIO_UART, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
    return true;
}

static void rs485_send_line(const char *body)
{
    if (!s_u6.Instance) return;
    char line[112];
    int n = sig_snprintf(line, sizeof line, "$%s*%02X\r\n", body, wired_nmea_checksum(body));
    if (n <= 0 || n >= (int)sizeof line) return;
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET);      /* drive the bus */
    HAL_UART_Transmit(&s_u6, (uint8_t *)line, (uint16_t)n, 50);        /* <= ~9 ms at 115200 */
    uint32_t t0 = HAL_GetTick();
    while (!(USART6->SR & USART_SR_TC) && (HAL_GetTick() - t0) < 5) { }
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);    /* back to receive */
    s_st.rs485_tx_lines++;
}

bool wired_rs485_query_pending(void)
{
    while (s_rx_tail != s_rx_head) {
        char c = (char)s_rx[s_rx_tail % RX_RING];
        s_rx_tail++;
        s_st.rs485_rx_bytes++;
        if (c == '\r' || c == '\n') {
            s_line[s_line_len] = 0;
            if (!strcmp(s_line, "?STATUS")) { s_query = true; s_st.rs485_queries++; }
            s_line_len = 0;
        } else if (s_line_len < sizeof s_line - 1) {
            s_line[s_line_len++] = c;
        } else {
            s_line_len = 0;
        }
    }
    bool q = s_query;
    s_query = false;
    return q;
}

/* ------------------------------------------------------------ public ---- */
void wired_init(bool can_on, bool rs485_on)
{
    can_stop();
    rs485_stop();
    s_st.can_on = can_on && can_start();
    s_st.rs485_on = rs485_on && rs485_start();
}

void wired_reinit(void) { wired_init(s_st.can_on, s_st.rs485_on); }

void wired_event(uint8_t type, const char *type_name, uint8_t cls, const char *cls_name, uint16_t value, uint32_t t_ms)
{
    if (s_st.can_on) {
        uint8_t d[8] = { type, cls, (uint8_t)value, (uint8_t)(value >> 8),
                         (uint8_t)t_ms, (uint8_t)(t_ms >> 8), (uint8_t)(t_ms >> 16), (uint8_t)(t_ms >> 24) };
        can_send((uint16_t)(0x5A0u + (type < 0x0Fu ? type : 0x0Eu)), d);   /* 0x5AF is the heartbeat */
    }
    if (s_st.rs485_on) {
        char b[96];
        sig_snprintf(b, sizeof b, "SIGEV,%lu,%s,%s,%u", (unsigned long)t_ms, type_name, cls_name, value);
        rs485_send_line(b);
    }
}

void wired_heartbeat(uint8_t shown, const char *shown_name, uint8_t alert, const char *alert_name, bool acked,
                     uint32_t rough, bool fault, uint32_t t_ms)
{
    if (s_st.can_on) {
        uint8_t d[8] = { shown, alert, acked, 0, (uint8_t)rough, (uint8_t)(rough >> 8), fault, s_hb_seq++ };
        can_send(0x5AFu, d);
    }
    if (s_st.rs485_on) {
        char b[96];
        sig_snprintf(b, sizeof b, "SIGHB,%lu,%s,%s,%d,%lu,%d", (unsigned long)t_ms, shown_name, alert_name, acked,
                     (unsigned long)rough, fault);
        rs485_send_line(b);
    }
}

void wired_stats(wired_stats_t *s)
{
    can_update_errors();
    *s = s_st;
}
