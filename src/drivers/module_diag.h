/*
 * module_diag.h - service/diagnostic tests for modules that are fitted on the
 * RT-Spark but are not part of the primary product function. Each test is
 * read-only or self-contained and returns a one-line result for the console
 * (and a short status for the LCD). None of them needs a network.
 */
#ifndef SIG_MODULE_DIAG_H
#define SIG_MODULE_DIAG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum { DIAG_PASS = 0, DIAG_FAIL, DIAG_WARN, DIAG_NA } diag_result_t;
const char *diag_result_name(diag_result_t r);

/* microSD over SDIO: card-detect level, init, card info, read-only block 0 check
 * (the product's CSV event log lives in src/app/sd_logger.c). */
diag_result_t diag_sdcard(char *out, int n);
/* IR: sends the product's NEC TEST frame and checks that the board's own receiver
 * decodes it (reflector required); remote control + alert relay live in ir_nec.c / task_ui.c. */
diag_result_t diag_ir(char *out, int n);
/* RW007 Wi-Fi: reset pulse and INT/BUSY activity. No network is joined. */
diag_result_t diag_rw007(char *out, int n);
/* ES8388 codec on I2C2: ACK + read of control registers (no writes). */
diag_result_t diag_es8388(char *out, int n);
/* CAN1 controller: silent loop-back frame (no bus traffic, transceiver idle). */
diag_result_t diag_can_loopback(char *out, int n);
/* MCU internal temperature sensor and VDDA from VREFINT (ADC1). */
diag_result_t diag_mcu_adc(char *out, int n, float *temp_c, float *vdda_v);

#endif
