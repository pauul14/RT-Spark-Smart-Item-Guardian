/*
 * uart_console.h - USART1 console on the ST-LINK/V2.1 virtual COM port.
 * RX bytes go from the ISR into a FreeRTOS stream buffer; TX is interrupt
 * driven from a caller-owned buffer and completion notifies the console task.
 */
#ifndef SIG_UART_CONSOLE_H
#define SIG_UART_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

#define CONSOLE_BAUD 115200u

void uart_console_init(uint32_t baud);
void uart_console_attach(StreamBufferHandle_t rx_sb, TaskHandle_t tx_task);
/* Starts an interrupt-driven transmit; data must stay valid until completion. */
bool uart_console_tx_start(const uint8_t *data, uint16_t len);
bool uart_console_tx_busy(void);

extern volatile uint32_t g_uart_rx_overrun;
extern volatile uint32_t g_uart_rx_dropped;

#endif
