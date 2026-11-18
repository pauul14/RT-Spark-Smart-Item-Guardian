/*
 * task_console.c - priority 1 (lowest application priority).
 * Drains the log stream buffer to USART1 (interrupt-driven TX) and reads
 * command lines from the RX stream buffer. Because it runs below every other
 * task, console traffic can never delay sampling, inference or feedback.
 */
#include "app.h"
#include "board_pins.h"
#include "uart_console.h"
#include <string.h>

#define TX_CHUNK 256
static uint8_t s_tx[TX_CHUNK];          /* SRAM (.bss): read by the USART ISR */

void task_console(void *arg)
{
    (void)arg;
    uart_console_attach(g_rx_sb, xTaskGetCurrentTaskHandle());
    char line[128];
    size_t len = 0;

    for (;;) {
        xEventGroupSetBits(g_alive, ALIVE_CONSOLE);
        size_t n = xStreamBufferReceive(g_log_sb, s_tx, sizeof s_tx, pdMS_TO_TICKS(10));
        if (n) {
            ulTaskNotifyTake(pdTRUE, 0);                       /* clear a stale completion */
            if (uart_console_tx_start(s_tx, (uint16_t)n)) {
                /* 256 bytes at 115200 Bd ~= 22 ms */
                if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200))) {
                    USART1->CR1 &= ~USART_CR1_TXEIE;           /* should never happen; recover */
                }
            }
        }
        uint8_t c;
        while (xStreamBufferReceive(g_rx_sb, &c, 1, 0) == 1) {
            if (c == '\r' || c == '\n') {
                if (len) {
                    line[len] = 0;
                    cli_execute(line);
                    len = 0;
                }
            } else if (c == 0x08 || c == 0x7F) {
                if (len) len--;
            } else if (len < sizeof line - 1 && c >= 0x20 && c < 0x7F) {
                line[len++] = (char)c;
            }
        }
    }
}
