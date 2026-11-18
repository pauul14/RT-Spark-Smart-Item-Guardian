/*
 * ir_nec.h - infrared remote input and alert relay hardware (schematic sheet 6).
 *   RX: IRM-H638T 38 kHz receiver on PF8 (output LOW during a carrier burst). Both
 *       edges are timestamped in the EXTI ISR; ir_rx_poll() (UI task only) decodes NEC.
 *   TX: IR LED via PB1 = TIM8_CH3N (38 kHz carrier, 1/3 duty, gated through CCR3).
 *       TIM7 (1 MHz) steps through the mark/space sequence in its ISR, so sending
 *       never blocks a task. TIM7 runs at NVIC priority 4 (no FreeRTOS calls), so
 *       short critical sections elsewhere do not stretch the IR timing.
 */
#ifndef SIG_IR_NEC_H
#define SIG_IR_NEC_H

#include "ir_nec_codec.h"

void     ir_init(void);
bool     ir_rx_poll(ir_frame_t *out);
bool     ir_tx_send(uint16_t addr, uint8_t cmd);   /* false if a frame is still being sent */
bool     ir_tx_busy(void);
int      ir_rx_level(void);                        /* 1 = idle (no carrier) */
uint32_t ir_rx_edges(void);
uint32_t ir_rx_frames(void);
uint32_t ir_rx_errors(void);
uint32_t ir_rx_test_echoes(void);                  /* own TEST frames decoded by own receiver */
uint32_t ir_tx_frames(void);

#endif
