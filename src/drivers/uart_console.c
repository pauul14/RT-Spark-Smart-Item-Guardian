#include "uart_console.h"
#include "board_pins.h"
#include "bsp.h"

volatile uint32_t g_uart_rx_overrun;
volatile uint32_t g_uart_rx_dropped;

static UART_HandleTypeDef s_huart;
static StreamBufferHandle_t s_rx_sb;
static TaskHandle_t s_tx_task;
static const uint8_t *volatile s_tx_buf;
static volatile uint16_t s_tx_len, s_tx_pos;
static volatile bool s_tx_busy;

void uart_console_init(uint32_t baud)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = CONSOLE_TX_PIN | CONSOLE_RX_PIN;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_PULLUP;                 /* as in the vendor .ioc */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &g);

    s_huart.Instance = CONSOLE_UART;
    s_huart.Init.BaudRate = baud;
    s_huart.Init.WordLength = UART_WORDLENGTH_8B;
    s_huart.Init.StopBits = UART_STOPBITS_1;
    s_huart.Init.Parity = UART_PARITY_NONE;
    s_huart.Init.Mode = UART_MODE_TX_RX;
    s_huart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_huart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&s_huart) != HAL_OK) {
        sig_fatal_early("uart1 init");
    }
}

void uart_console_attach(StreamBufferHandle_t rx_sb, TaskHandle_t tx_task)
{
    s_rx_sb = rx_sb;
    s_tx_task = tx_task;
    HAL_NVIC_SetPriority(USART1_IRQn, IRQ_PRIO_UART, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    USART1->CR1 |= USART_CR1_RXNEIE;
}

bool uart_console_tx_busy(void) { return s_tx_busy; }

bool uart_console_tx_start(const uint8_t *data, uint16_t len)
{
    if (s_tx_busy || len == 0) return false;
    s_tx_buf = data;
    s_tx_len = len;
    s_tx_pos = 0;
    s_tx_busy = true;
    USART1->CR1 |= USART_CR1_TXEIE;
    return true;
}

void USART1_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t sr = USART1->SR;
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        uint8_t b = (uint8_t)USART1->DR;          /* SR then DR read clears ORE/FE/NE */
        if (sr & USART_SR_ORE) g_uart_rx_overrun++;
        if ((sr & USART_SR_RXNE) && s_rx_sb) {
            if (xStreamBufferSendFromISR(s_rx_sb, &b, 1, &woken) != 1) g_uart_rx_dropped++;
        }
    }
    if ((USART1->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        if (s_tx_pos < s_tx_len) {
            USART1->DR = s_tx_buf[s_tx_pos++];
        } else {
            USART1->CR1 &= ~USART_CR1_TXEIE;
            s_tx_busy = false;
            if (s_tx_task) vTaskNotifyGiveFromISR(s_tx_task, &woken);
        }
    }
    portYIELD_FROM_ISR(woken);
}
