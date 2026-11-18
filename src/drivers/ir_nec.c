/*
 * ir_nec.c - hardware side of ir_nec.h (EXTI edge capture, TIM8 carrier, TIM7 sequencer).
 */
#include "ir_nec.h"
#include "board_pins.h"
#include "bsp.h"

#define EDGE_RING 256u
#define CARRIER_ARR   4420u          /* 168 MHz / 4421 = 38.0 kHz */
#define CARRIER_ON    2947u          /* CH3N = NOT OC3REF: high for (ARR+1-CCR)/(ARR+1) = 1/3 */
#define CARRIER_OFF   (CARRIER_ARR + 1u)   /* OC3REF always high -> CH3N always low (LED off) */

static volatile uint32_t s_edge_t[EDGE_RING];
static volatile uint8_t s_edge_lvl[EDGE_RING];
static volatile uint32_t s_head, s_tail;
static volatile uint32_t s_edges, s_frames, s_tx_frames, s_test_echoes;
static ir_nec_dec_t s_dec;
static uint32_t s_last_t;
static bool s_have_last;

static TIM_HandleTypeDef s_tim8;
static uint16_t s_seq[70];
static volatile int s_seq_n, s_seq_i;
static volatile bool s_tx_busy;

/* ---------------- RX ---------------- */
void ir_rx_edge_isr(uint32_t t_us)
{
    uint32_t h = s_head;
    s_edge_t[h % EDGE_RING] = t_us;
    s_edge_lvl[h % EDGE_RING] = (IR_RX_PORT->IDR & IR_RX_PIN) ? 1u : 0u;   /* level AFTER the edge */
    s_head = h + 1;
    s_edges++;
}

int ir_rx_level(void) { return (IR_RX_PORT->IDR & IR_RX_PIN) ? 1 : 0; }
uint32_t ir_rx_edges(void) { return s_edges; }
uint32_t ir_rx_frames(void) { return s_frames; }
uint32_t ir_rx_errors(void) { return s_dec.errors; }
uint32_t ir_tx_frames(void) { return s_tx_frames; }
uint32_t ir_rx_test_echoes(void) { return s_test_echoes; }

bool ir_rx_poll(ir_frame_t *out)
{
    while (s_tail != s_head) {
        if (s_head - s_tail > EDGE_RING) s_tail = s_head - EDGE_RING;   /* overrun: drop oldest */
        uint32_t i = s_tail % EDGE_RING;
        uint32_t t = s_edge_t[i];
        uint8_t lvl = s_edge_lvl[i];
        s_tail++;
        if (s_have_last) {
            /* interval since the previous edge, at the level that followed it (= !lvl) */
            int r = ir_nec_feed(&s_dec, t - s_last_t, lvl ? 0 : 1, out);
            if (r) {
                out->t_us = t;
                s_last_t = t;
                if (r == 1) s_frames++;
                if (r == 1 && out->addr == IR_RELAY_ADDR && out->cmd == IR_CMD_TEST) s_test_echoes++;
                return true;
            }
        }
        s_last_t = t;
        s_have_last = true;
    }
    /* An idle receiver after a partial frame: restart cleanly next time. */
    if (s_have_last && (sig_time_us() - s_last_t) > 120000u && s_dec.state) ir_nec_reset(&s_dec);
    return false;
}

/* ---------------- TX ---------------- */
bool ir_tx_busy(void) { return s_tx_busy; }

bool ir_tx_send(uint16_t addr, uint8_t cmd)
{
    if (s_tx_busy) return false;
    s_seq_n = ir_nec_encode(addr, cmd, s_seq, (int)(sizeof s_seq / sizeof s_seq[0]));
    if (s_seq_n <= 0) return false;
    s_seq_i = 0;
    s_tx_busy = true;
    TIM8->CCR3 = CARRIER_ON;                    /* first mark */
    TIM7->CNT = 0;
    TIM7->ARR = s_seq[0] - 1u;
    TIM7->SR = 0;
    TIM7->CR1 |= TIM_CR1_CEN;
    return true;
}

void TIM7_IRQHandler(void)
{
    if (!(TIM7->SR & TIM_SR_UIF)) return;
    TIM7->SR = ~TIM_SR_UIF;
    int i = ++s_seq_i;
    if (i >= s_seq_n || s_seq[i] == 0) {
        TIM8->CCR3 = CARRIER_OFF;
        TIM7->CR1 &= ~TIM_CR1_CEN;
        s_tx_busy = false;
        s_tx_frames++;
        return;
    }
    TIM8->CCR3 = (i & 1) ? CARRIER_OFF : CARRIER_ON;   /* even index = mark */
    TIM7->ARR = s_seq[i] - 1u;                          /* ARPE = 0: applies to this period */
}

void ir_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_TIM7_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();

    /* RX: both edges on PF8 (EXTI9_5 vector shared with KEY_UP, already enabled). */
    GPIO_InitTypeDef g = {0};
    g.Pin = IR_RX_PIN;
    g.Mode = GPIO_MODE_IT_RISING_FALLING;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(IR_RX_PORT, &g);

    /* TX carrier: TIM8 PWM1 on CH3N, OSSR=1 so a disabled output is driven inactive (LED off). */
    s_tim8.Instance = TIM8;
    s_tim8.Init.Prescaler = 0;
    s_tim8.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tim8.Init.Period = CARRIER_ARR;
    s_tim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_tim8.Init.RepetitionCounter = 0;
    s_tim8.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&s_tim8);
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = CARRIER_OFF;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&s_tim8, &oc, TIM_CHANNEL_3);
    TIM8->BDTR |= TIM_BDTR_OSSR;
    g.Pin = IR_TX_PIN;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLDOWN;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF3_TIM8;                /* PB1 = TIM8_CH3N */
    HAL_GPIO_Init(IR_TX_PORT, &g);
    HAL_TIMEx_PWMN_Start(&s_tim8, TIM_CHANNEL_3);   /* runs continuously; CCR3 gates the LED */

    /* Sequencer: TIM7 1 MHz, no ARR preload, update interrupt. */
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    uint32_t tclk = ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) ? pclk1 : 2u * pclk1;
    TIM7->CR1 = TIM_CR1_URS;                    /* only overflow raises UIF */
    TIM7->PSC = (uint16_t)(tclk / 1000000u - 1u);
    TIM7->ARR = 0xFFFF;
    TIM7->EGR = TIM_EGR_UG;
    TIM7->SR = 0;
    TIM7->DIER = TIM_DIER_UIE;
    HAL_NVIC_SetPriority(TIM7_IRQn, 4, 0);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
}
